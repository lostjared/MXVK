#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace example {
    class ExampleWindow : public mxvk::VK_Window {
        struct PushConstants {
            float time;
            float aspect;
        };

      public:
        ExampleWindow(const std::string path, const std::string &text, int width, int height, bool fullscreen)
            : mxvk::VK_Window(text, width, height, fullscreen, MXVK_VALIDATION),
              shader_root(path.empty() ? std::string(HELLO_WORLD_SHADER_DIR) : path + "/shaders") {
            setClearColor(0.02f, 0.03f, 0.06f, 1.0f);
        }

        ~ExampleWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            destroyGraphicsPipeline();
        }

        void onSwapchainAboutToRecreate() override {
            destroyGraphicsPipeline();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) override {
            if (!ensureGraphicsPipeline()) {
                return;
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

            const auto now = std::chrono::steady_clock::now();
            const float elapsed_seconds = std::chrono::duration<float>(now - start_time).count();
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect =
                (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

            const PushConstants push_constants{elapsed_seconds, aspect};
            vkCmdPushConstants(
                cmd,
                pipeline_layout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(PushConstants),
                &push_constants);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

      private:
        std::string shader_root;
        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};

        void destroyGraphicsPipeline() {
            if (device == VK_NULL_HANDLE) {
                pipeline_layout = VK_NULL_HANDLE;
                graphics_pipeline = VK_NULL_HANDLE;
                return;
            }

            if (graphics_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, graphics_pipeline, nullptr);
                graphics_pipeline = VK_NULL_HANDLE;
            }
            if (pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                pipeline_layout = VK_NULL_HANDLE;
            }
        }

        bool ensureGraphicsPipeline() {
            if (graphics_pipeline != VK_NULL_HANDLE && pipeline_layout != VK_NULL_HANDLE) {
                return true;
            }

            createGraphicsPipeline();
            return graphics_pipeline != VK_NULL_HANDLE && pipeline_layout != VK_NULL_HANDLE;
        }

        void createGraphicsPipeline() {
            if (device == VK_NULL_HANDLE || swapchain_format == VK_FORMAT_UNDEFINED) {
                return;
            }

            const std::string vert_path = shader_root + "/triangle.vert.spv";
            const std::string frag_path = shader_root + "/triangle.frag.spv";
            const std::vector<char> vert_bytes = loadSpv(vert_path);
            const std::vector<char> frag_bytes = loadSpv(frag_path);

            const VkShaderModule vert_module = createShaderModule(device, vert_bytes);
            VkShaderModule frag_module = VK_NULL_HANDLE;
            try {
                frag_module = createShaderModule(device, frag_bytes);

                std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{};
                shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                shader_stages[0].module = vert_module;
                shader_stages[0].pName = "main";
                shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                shader_stages[1].module = frag_module;
                shader_stages[1].pName = "main";

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
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_NONE;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                rasterizer.depthBiasEnable = VK_FALSE;

                VkPipelineMultisampleStateCreateInfo multisampling{};
                multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampling.sampleShadingEnable = VK_FALSE;
                multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineDepthStencilStateCreateInfo depth_stencil{};
                depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depth_stencil.depthTestEnable = VK_FALSE;
                depth_stencil.depthWriteEnable = VK_FALSE;
                depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                depth_stencil.depthBoundsTestEnable = VK_FALSE;
                depth_stencil.stencilTestEnable = VK_FALSE;

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

                VkPushConstantRange push_constant_range{};
                push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                push_constant_range.offset = 0;
                push_constant_range.size = sizeof(PushConstants);

                VkPipelineLayoutCreateInfo pipeline_layout_info{};
                pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipeline_layout_info.pushConstantRangeCount = 1;
                pipeline_layout_info.pPushConstantRanges = &push_constant_range;
                if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create pipeline layout");
                }

                VkPipelineRenderingCreateInfo pipeline_rendering_info{};
                pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                pipeline_rendering_info.viewMask = 0;
                pipeline_rendering_info.colorAttachmentCount = 1;
                pipeline_rendering_info.pColorAttachmentFormats = &swapchain_format;
                const VkFormat depth_format = getDepthFormat();
                if (depth_format != VK_FORMAT_UNDEFINED) {
                    pipeline_rendering_info.depthAttachmentFormat = depth_format;
                }

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.pNext = &pipeline_rendering_info;
                pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
                pipeline_info.pStages = shader_stages.data();
                pipeline_info.pVertexInputState = &vertex_input;
                pipeline_info.pInputAssemblyState = &input_assembly;
                pipeline_info.pViewportState = &viewport_state;
                pipeline_info.pRasterizationState = &rasterizer;
                pipeline_info.pMultisampleState = &multisampling;
                pipeline_info.pDepthStencilState = &depth_stencil;
                pipeline_info.pColorBlendState = &color_blending;
                pipeline_info.pDynamicState = &dynamic_state;
                pipeline_info.layout = pipeline_layout;
                pipeline_info.renderPass = VK_NULL_HANDLE;
                pipeline_info.subpass = 0;
                pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
                pipeline_info.basePipelineIndex = -1;

                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create graphics pipeline");
                }
            } catch (...) {
                if (graphics_pipeline != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, graphics_pipeline, nullptr);
                    graphics_pipeline = VK_NULL_HANDLE;
                }
                if (pipeline_layout != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                    pipeline_layout = VK_NULL_HANDLE;
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

        VkPipeline graphics_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::ExampleWindow ex_window(args.path, "VK_Example", args.width, args.height, args.fullscreen);
        ex_window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
    }
    return EXIT_SUCCESS;
}
