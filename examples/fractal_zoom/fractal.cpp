#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#ifndef fractal_zoom_SHADER_DIR
#define fractal_zoom_SHADER_DIR "."
#endif

namespace example {

    class FractalWindow : public mxvk::VK_Window {
      public:
        FractalWindow([[maybe_unused]] const std::string &path, int width, int height, bool fullscreen)
            : mxvk::VK_Window("-[ Fractal Zoom - MXVK ]-", width, height, fullscreen, MXVK_VALIDATION) {
        }

        ~FractalWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            destroyFractalPipeline();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                handleKey(e.key.key);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                dragging_ = true;
                drag_start_mouse_x_ = e.button.x;
                drag_start_mouse_y_ = e.button.y;
                drag_start_center_x_ = center_x_;
                drag_start_center_y_ = center_y_;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                dragging_ = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && dragging_) {
                const VkExtent2D extent = getSwapchainExtent();
                if (extent.width == 0U || extent.height == 0U) {
                    return;
                }
                const int delta_x = e.motion.x - drag_start_mouse_x_;
                const int delta_y = e.motion.y - drag_start_mouse_y_;
                const double scale = 2.0 / (zoom_ * static_cast<double>(std::min(extent.width, extent.height)));
                center_x_ = drag_start_center_x_ - static_cast<double>(delta_x) * scale;
                center_y_ = drag_start_center_y_ + static_cast<double>(delta_y) * scale;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                applyWheelZoom(e.wheel.y);
            }
        }

        void proc() override {
            updateKeyboardNavigation();
        }

        void onSwapchainAboutToRecreate() override {
            destroyFractalPipeline();
        }

        void onSwapchainRecreated() override {
            createFractalPipeline();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) override {
            if (fractal_pipeline_ == VK_NULL_HANDLE || fractal_pipeline_layout_ == VK_NULL_HANDLE) {
                createFractalPipeline();
            }

            if (fractal_pipeline_ == VK_NULL_HANDLE || fractal_pipeline_layout_ == VK_NULL_HANDLE) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                return;
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fractal_pipeline_);

            const FractalPushConstants push_constants{
                center_x_,
                center_y_,
                zoom_,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count(),
                static_cast<double>(extent.width),
                static_cast<double>(extent.height),
                max_iterations_,
                palette_index_,
                1,
                0};

            vkCmdPushConstants(
                cmd,
                fractal_pipeline_layout_,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(push_constants),
                &push_constants);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

      private:
        void handleKey(SDL_Keycode key) {
            switch (key) {
            case SDLK_ESCAPE:
                exit();
                break;
            case SDLK_R:
                resetView();
                break;
            case SDLK_1:
                center_x_ = -0.5;
                center_y_ = 0.0;
                zoom_ = 1.0;
                max_iterations_ = 256;
                break;
            case SDLK_2:
                center_x_ = -0.745;
                center_y_ = 0.113;
                zoom_ = 50.0;
                max_iterations_ = 512;
                break;
            case SDLK_3:
                center_x_ = -0.761574;
                center_y_ = -0.0847596;
                zoom_ = 220.0;
                max_iterations_ = 900;
                break;
            case SDLK_EQUALS:
            case SDLK_PLUS:
                max_iterations_ = std::min(max_iterations_ + 64, 4096);
                break;
            case SDLK_MINUS:
                max_iterations_ = std::max(max_iterations_ - 64, 64);
                break;
            case SDLK_LEFTBRACKET:
                palette_index_ = (palette_index_ + 2) % 3;
                break;
            case SDLK_RIGHTBRACKET:
                palette_index_ = (palette_index_ + 1) % 3;
                break;
            default:
                break;
            }
        }

        void applyWheelZoom(float wheel_y) {
            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U || window == nullptr) {
                return;
            }

            float mouse_x = 0.0f;
            float mouse_y = 0.0f;
            SDL_GetMouseState(&mouse_x, &mouse_y);

            const double base_scale = 2.0 / (zoom_ * static_cast<double>(std::min(extent.width, extent.height)));
            const double before_x = (static_cast<double>(mouse_x) - static_cast<double>(extent.width) * 0.5) * base_scale + center_x_;
            const double before_y = (static_cast<double>(extent.height) * 0.5 - static_cast<double>(mouse_y)) * base_scale + center_y_;

            const double zoom_factor = (wheel_y > 0.0f) ? 1.2 : (1.0 / 1.2);
            zoom_ = std::clamp(zoom_ * zoom_factor, 0.5, 1.0e16);

            const double new_scale = 2.0 / (zoom_ * static_cast<double>(std::min(extent.width, extent.height)));
            const double after_x = (static_cast<double>(mouse_x) - static_cast<double>(extent.width) * 0.5) * new_scale + center_x_;
            const double after_y = (static_cast<double>(extent.height) * 0.5 - static_cast<double>(mouse_y)) * new_scale + center_y_;

            center_x_ += before_x - after_x;
            center_y_ += before_y - after_y;

            if (wheel_y > 0.0f && zoom_ > 10.0) {
                max_iterations_ = std::min(max_iterations_ + 12, 4096);
            }
        }

        void updateKeyboardNavigation() {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - last_tick_).count();
            last_tick_ = now;

            const double move_speed = 0.85 * std::min(dt, 0.1) / zoom_;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
                center_x_ -= move_speed;
            }
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
                center_x_ += move_speed;
            }
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
                center_y_ += move_speed;
            }
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
                center_y_ -= move_speed;
            }
            if (keys[SDL_SCANCODE_Z]) {
                zoom_ = std::min(zoom_ * (1.0 + 1.9 * dt), 1.0e16);
            }
            if (keys[SDL_SCANCODE_X]) {
                zoom_ = std::max(zoom_ * (1.0 - 1.9 * dt), 0.5);
            }
        }

        void resetView() {
            center_x_ = -0.5;
            center_y_ = 0.0;
            zoom_ = 1.0;
            max_iterations_ = 256;
        }

        void destroyFractalPipeline() {
            if (device == VK_NULL_HANDLE) {
                fractal_pipeline_ = VK_NULL_HANDLE;
                fractal_pipeline_layout_ = VK_NULL_HANDLE;
                return;
            }
            if (fractal_pipeline_ != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, fractal_pipeline_, nullptr);
                fractal_pipeline_ = VK_NULL_HANDLE;
            }
            if (fractal_pipeline_layout_ != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, fractal_pipeline_layout_, nullptr);
                fractal_pipeline_layout_ = VK_NULL_HANDLE;
            }
        }

        void createFractalPipeline() {
            if (device == VK_NULL_HANDLE) {
                return;
            }

            const VkFormat color_format = getSwapchainFormat();
            const VkFormat depth_attachment_format = getDepthFormat();
            if (color_format == VK_FORMAT_UNDEFINED || depth_attachment_format == VK_FORMAT_UNDEFINED) {
                return;
            }

            destroyFractalPipeline();

            const std::string vert_path = std::string(fractal_zoom_SHADER_DIR) + "/fractal.vert.spv";
            const std::string frag_path = std::string(fractal_zoom_SHADER_DIR) + "/fractal.frag.spv";
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
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_NONE;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
                push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                push_constant_range.offset = 0;
                push_constant_range.size = static_cast<uint32_t>(sizeof(FractalPushConstants));

                VkPipelineLayoutCreateInfo pipeline_layout_info{};
                pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipeline_layout_info.pushConstantRangeCount = 1;
                pipeline_layout_info.pPushConstantRanges = &push_constant_range;

                if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &fractal_pipeline_layout_) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create fractal pipeline layout");
                }

                VkPipelineRenderingCreateInfo pipeline_rendering_info{};
                pipeline_rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                pipeline_rendering_info.viewMask = 0;
                pipeline_rendering_info.colorAttachmentCount = 1;
                pipeline_rendering_info.pColorAttachmentFormats = &color_format;
                pipeline_rendering_info.depthAttachmentFormat = depth_attachment_format;
                pipeline_rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.pNext = &pipeline_rendering_info;
                pipeline_info.stageCount = 2;
                pipeline_info.pStages = shader_stages;
                pipeline_info.pVertexInputState = &vertex_input;
                pipeline_info.pInputAssemblyState = &input_assembly;
                pipeline_info.pViewportState = &viewport_state;
                pipeline_info.pRasterizationState = &rasterizer;
                pipeline_info.pMultisampleState = &multisampling;
                pipeline_info.pDepthStencilState = &depth_stencil;
                pipeline_info.pColorBlendState = &color_blending;
                pipeline_info.pDynamicState = &dynamic_state;
                pipeline_info.layout = fractal_pipeline_layout_;
                pipeline_info.renderPass = VK_NULL_HANDLE;
                pipeline_info.subpass = 0;

                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &fractal_pipeline_) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create fractal graphics pipeline");
                }
            } catch (...) {
                if (fractal_pipeline_ != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device, fractal_pipeline_, nullptr);
                    fractal_pipeline_ = VK_NULL_HANDLE;
                }
                if (fractal_pipeline_layout_ != VK_NULL_HANDLE) {
                    vkDestroyPipelineLayout(device, fractal_pipeline_layout_, nullptr);
                    fractal_pipeline_layout_ = VK_NULL_HANDLE;
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

        struct FractalPushConstants {
            double center_x;
            double center_y;
            double zoom;
            double time;
            double resolution_x;
            double resolution_y;
            int max_iterations;
            int palette;
            int aa_samples;
            int reserved;
        };

        VkPipeline fractal_pipeline_ = VK_NULL_HANDLE;
        VkPipelineLayout fractal_pipeline_layout_ = VK_NULL_HANDLE;

        double center_x_ = -0.5;
        double center_y_ = 0.0;
        double zoom_ = 1.0;
        int max_iterations_ = 256;
        int palette_index_ = 0;

        bool dragging_ = false;
        int drag_start_mouse_x_ = 0;
        int drag_start_mouse_y_ = 0;
        double drag_start_center_x_ = 0.0;
        double drag_start_center_y_ = 0.0;

        std::chrono::steady_clock::time_point start_time_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_tick_{std::chrono::steady_clock::now()};
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::FractalWindow window(args.path, args.width, args.height, args.fullscreen);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("Argument exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
