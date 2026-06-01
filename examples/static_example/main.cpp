#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace example {
    class StaticWindow : public mxvk::VK_Window {
        std::string current_path = ".";

        struct PushConstants {
            float time;
            float width;
            float height;
            float frame;
        };

      public:
        StaticWindow(const std::string path, const std::string &text, int width, int height, bool fullscreen) : mxvk::VK_Window(text, width, height, fullscreen, MXVK_VALIDATION) {
            current_path = path;
            createGraphicsPipeline();
        }

        ~StaticWindow() {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            destroyGraphicsPipeline();
        }

        void event([[maybe_unused]] mxvk::VK_Window *window, SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void onSwapchainAboutToRecreate() override {
            destroyGraphicsPipeline();
        }

        void onSwapchainRecreated() override {
            createGraphicsPipeline();
        }

        void render([[maybe_unused]] mxvk::VK_Window *window) override {
            if (device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE || command_buffers.empty()) {
                return;
            }

            vkWaitForFences(device, 1, &in_flight, VK_TRUE, UINT64_MAX);

            uint32_t image_index = 0;
            const VkResult acquire_result =
                vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available, VK_NULL_HANDLE, &image_index);
            if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
                framebuffer_resized_ = true;
                return;
            }
            if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
                throw mxvk::Exception("Failed to acquire swapchain image");
            }
            if (
                image_index >= command_buffers.size() ||
                image_index >= swapchain_images.size() ||
                image_index >= swapchain_image_views.size() ||
                image_index >= render_finished.size()) {
                throw mxvk::Exception("Swapchain image index out of bounds");
            }

            const VkCommandBuffer cmd = command_buffers[image_index];
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to begin command buffer");
            }

            VkClearValue clear_value{};
            clear_value.color = {{0.02F, 0.03F, 0.06F, 1.0F}};

            VkImageMemoryBarrier2 to_color_barrier{};
            to_color_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            to_color_barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            to_color_barrier.srcAccessMask = VK_ACCESS_2_NONE;
            to_color_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            to_color_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            to_color_barrier.oldLayout =
                swapchain_image_initialized[image_index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
            to_color_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            to_color_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_color_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_color_barrier.image = swapchain_images[image_index];
            to_color_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_color_barrier.subresourceRange.baseMipLevel = 0;
            to_color_barrier.subresourceRange.levelCount = 1;
            to_color_barrier.subresourceRange.baseArrayLayer = 0;
            to_color_barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo pre_render_dependency{};
            pre_render_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            pre_render_dependency.imageMemoryBarrierCount = 1;
            pre_render_dependency.pImageMemoryBarriers = &to_color_barrier;
            vkCmdPipelineBarrier2(cmd, &pre_render_dependency);

            VkRenderingAttachmentInfo color_attachment{};
            color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_attachment.imageView = swapchain_image_views[image_index];
            color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
            color_attachment.resolveImageView = VK_NULL_HANDLE;
            color_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.clearValue = clear_value;

            VkRenderingInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.renderArea.offset = {0, 0};
            rendering_info.renderArea.extent = swapchain_extent;
            rendering_info.layerCount = 1;
            rendering_info.viewMask = 0;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            rendering_info.pDepthAttachment = nullptr;
            rendering_info.pStencilAttachment = nullptr;

            vkCmdBeginRendering(cmd, &rendering_info);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);

            VkViewport viewport{};
            viewport.x = 0.0F;
            viewport.y = 0.0F;
            viewport.width = static_cast<float>(swapchain_extent.width);
            viewport.height = static_cast<float>(swapchain_extent.height);
            viewport.minDepth = 0.0F;
            viewport.maxDepth = 1.0F;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = swapchain_extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            const auto now = std::chrono::steady_clock::now();
            const float elapsed_seconds = std::chrono::duration<float>(now - start_time_).count();
            const float width = static_cast<float>(swapchain_extent.width);
            const float height = static_cast<float>(swapchain_extent.height);
            const PushConstants push_constants{elapsed_seconds, width, height, static_cast<float>(frame_index_++)};
            vkCmdPushConstants(
                cmd,
                pipeline_layout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(PushConstants),
                &push_constants);

            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRendering(cmd);

            VkImageMemoryBarrier2 to_present_barrier{};
            to_present_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            to_present_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            to_present_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            to_present_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            to_present_barrier.dstAccessMask = VK_ACCESS_2_NONE;
            to_present_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            to_present_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            to_present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_present_barrier.image = swapchain_images[image_index];
            to_present_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_present_barrier.subresourceRange.baseMipLevel = 0;
            to_present_barrier.subresourceRange.levelCount = 1;
            to_present_barrier.subresourceRange.baseArrayLayer = 0;
            to_present_barrier.subresourceRange.layerCount = 1;

            VkDependencyInfo post_render_dependency{};
            post_render_dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            post_render_dependency.imageMemoryBarrierCount = 1;
            post_render_dependency.pImageMemoryBarriers = &to_present_barrier;
            vkCmdPipelineBarrier2(cmd, &post_render_dependency);

            if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to end command buffer");
            }

            const VkSemaphore signal_semaphore = render_finished[image_index];

            VkSemaphoreSubmitInfo wait_semaphore_info{};
            wait_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            wait_semaphore_info.semaphore = image_available;
            wait_semaphore_info.value = 0;
            wait_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            wait_semaphore_info.deviceIndex = 0;

            VkCommandBufferSubmitInfo command_buffer_info{};
            command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            command_buffer_info.commandBuffer = cmd;
            command_buffer_info.deviceMask = 0;

            VkSemaphoreSubmitInfo signal_semaphore_info{};
            signal_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signal_semaphore_info.semaphore = signal_semaphore;
            signal_semaphore_info.value = 0;
            signal_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            signal_semaphore_info.deviceIndex = 0;

            VkSubmitInfo2 submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submit_info.waitSemaphoreInfoCount = 1;
            submit_info.pWaitSemaphoreInfos = &wait_semaphore_info;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &command_buffer_info;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = &signal_semaphore_info;

            vkResetFences(device, 1, &in_flight);
            if (vkQueueSubmit2(graphics_queue, 1, &submit_info, in_flight) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to submit draw command");
            }
            swapchain_image_initialized[image_index] = true;

            VkSwapchainKHR present_swapchain = swapchain;
            VkPresentInfoKHR present_info{};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &signal_semaphore;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &present_swapchain;
            present_info.pImageIndices = &image_index;

            const VkResult present_result = vkQueuePresentKHR(present_queue, &present_info);
            if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
                framebuffer_resized_ = true;
            } else if (present_result != VK_SUCCESS) {
                throw mxvk::Exception("Failed to present swapchain image");
            }
        }

      private:
        void destroyGraphicsPipeline() {
            if (device == VK_NULL_HANDLE) {
                pipeline_layout_ = VK_NULL_HANDLE;
                graphics_pipeline_ = VK_NULL_HANDLE;
                return;
            }

            if (graphics_pipeline_ != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, graphics_pipeline_, nullptr);
                graphics_pipeline_ = VK_NULL_HANDLE;
            }
            if (pipeline_layout_ != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
                pipeline_layout_ = VK_NULL_HANDLE;
            }
        }

        void createGraphicsPipeline() {
            const std::string vert_path = current_path + "/shaders/triangle.vert.spv";
            const std::string frag_path = current_path + "/shaders/triangle.frag.spv";
            const std::vector<char> vert_bytes = loadSpv(vert_path);
            const std::vector<char> frag_bytes = loadSpv(frag_path);

            const VkShaderModule vert_module = createShaderModule(device, vert_bytes);
            VkShaderModule frag_module = VK_NULL_HANDLE;
            try {
                frag_module = createShaderModule(device, frag_bytes);

                VkPipelineShaderStageCreateInfo vert_stage{};
                vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
                vert_stage.module = vert_module;
                vert_stage.pName = "main";

                VkPipelineShaderStageCreateInfo frag_stage{};
                frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                frag_stage.module = frag_module;
                frag_stage.pName = "main";
                const VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};
                VkPipelineVertexInputStateCreateInfo vertex_input{};
                vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                VkPipelineInputAssemblyStateCreateInfo input_assembly{};
                input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                input_assembly.primitiveRestartEnable = VK_FALSE;
                VkPipelineViewportStateCreateInfo viewport_state{};
                viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewport_state.viewportCount = 1;
                viewport_state.scissorCount = 1;

                const VkDynamicState dynamic_states[] = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamic_state{};
                dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic_state.dynamicStateCount = 2;
                dynamic_state.pDynamicStates = dynamic_states;

                VkPipelineRasterizationStateCreateInfo rasterizer{};
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.depthClampEnable = VK_FALSE;
                rasterizer.rasterizerDiscardEnable = VK_FALSE;
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.lineWidth = 1.0F;
                rasterizer.cullMode = VK_CULL_MODE_NONE;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                rasterizer.depthBiasEnable = VK_FALSE;

                VkPipelineMultisampleStateCreateInfo multisampling{};
                multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampling.sampleShadingEnable = VK_FALSE;
                multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineColorBlendAttachmentState color_blend_attachment{};
                color_blend_attachment.colorWriteMask =
                    VK_COLOR_COMPONENT_R_BIT |
                    VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT |
                    VK_COLOR_COMPONENT_A_BIT;
                color_blend_attachment.blendEnable = VK_FALSE;

                VkPipelineColorBlendStateCreateInfo color_blending{};
                color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                color_blending.logicOpEnable = VK_FALSE;
                color_blending.attachmentCount = 1;
                color_blending.pAttachments = &color_blend_attachment;

                VkPipelineLayoutCreateInfo pipeline_layout_info{};
                pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                VkPushConstantRange push_constant_range{};
                push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                push_constant_range.offset = 0;
                push_constant_range.size = sizeof(PushConstants);
                pipeline_layout_info.pushConstantRangeCount = 1;
                pipeline_layout_info.pPushConstantRanges = &push_constant_range;
                if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create pipeline layout");
                }

                VkGraphicsPipelineCreateInfo pipeline_info{};
                VkPipelineRenderingCreateInfo pipeline_rendering_info{};
                pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                pipeline_rendering_info.viewMask = 0;
                pipeline_rendering_info.colorAttachmentCount = 1;
                pipeline_rendering_info.pColorAttachmentFormats = &swapchain_format;

                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.pNext = &pipeline_rendering_info;
                pipeline_info.stageCount = 2;
                pipeline_info.pStages = shader_stages;
                pipeline_info.pVertexInputState = &vertex_input;
                pipeline_info.pInputAssemblyState = &input_assembly;
                pipeline_info.pViewportState = &viewport_state;
                pipeline_info.pRasterizationState = &rasterizer;
                pipeline_info.pMultisampleState = &multisampling;
                pipeline_info.pDepthStencilState = nullptr;
                pipeline_info.pColorBlendState = &color_blending;
                pipeline_info.pDynamicState = &dynamic_state;
                pipeline_info.layout = pipeline_layout_;
                pipeline_info.renderPass = VK_NULL_HANDLE;
                pipeline_info.subpass = 0;
                pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
                pipeline_info.basePipelineIndex = -1;

                if (vkCreateGraphicsPipelines(
                        device,
                        VK_NULL_HANDLE,
                        1,
                        &pipeline_info,
                        nullptr,
                        &graphics_pipeline_) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create graphics pipeline");
                }
            } catch (...) {
                if (graphics_pipeline_ != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, graphics_pipeline_, nullptr);
                    graphics_pipeline_ = VK_NULL_HANDLE;
                }
                if (pipeline_layout_ != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
                    pipeline_layout_ = VK_NULL_HANDLE;
                }
                if (frag_module != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, frag_module, nullptr);
                }
                vkDestroyShaderModule(device, vert_module, nullptr);
                throw;
            }

            vkDestroyShaderModule(device, frag_module, nullptr);
            vkDestroyShaderModule(device, vert_module, nullptr);
        }

        VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
        VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;
        std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
        uint32_t frame_index_ = 0;
    };
} 

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::StaticWindow ex_window(args.path, "Static Noise Example", args.width, args.height, args.fullscreen);
        ex_window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
    }
    return EXIT_SUCCESS;
}