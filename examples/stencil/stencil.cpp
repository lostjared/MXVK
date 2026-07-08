#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_stencil.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <string>

namespace example {
    class StencilWindow : public mxvk::VK_Window {
      public:
        StencilWindow(const std::string &path, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("MXVK Stencil", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              shader_root((path.empty() ? std::string(STENCIL_ASSET_DIR) : path) + "/data") {
            setClearColor(0.015f, 0.018f, 0.025f, 1.0f);
        }

        ~StencilWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            stencil.reset();
        }

        void onSwapchainAboutToRecreate() override {
            stencil.reset();
        }

        void onPrepareFrameRendering(VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) override {
            ensure_stencil();
            if (stencil) {
                stencil->prepare_for_rendering(cmd);
            }
        }

        void onConfigureDepthStencilAttachments(VkRenderingAttachmentInfo &depth_attachment,
                                                VkRenderingAttachmentInfo &stencil_attachment,
                                                [[maybe_unused]] uint32_t image_index) override {
            depth_attachment.imageView = VK_NULL_HANDLE;
            if (stencil) {
                stencil->configure_attachment(stencil_attachment);
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, [[maybe_unused]] uint32_t image_index) override {
            if (!stencil || !stencil->valid()) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - start_time).count();
            const VkExtent2D current_extent = getSwapchainExtent();
            const float aspect = current_extent.height > 0U
                                     ? static_cast<float>(current_extent.width) / static_cast<float>(current_extent.height)
                                     : 1.0f;

            const mxvk::VK_Stencil::PushConstants push{
                .time = elapsed,
                .aspect = aspect,
                .phase = elapsed * 0.35f,
                .scale = 1.0f,
            };
            stencil->draw_mask(cmd, push);
            stencil->draw_content(cmd, push);
        }

      private:
        void ensure_stencil() {
            const VkExtent2D current_extent = getSwapchainExtent();
            if (current_extent.width == 0U || current_extent.height == 0U) {
                return;
            }

            if (!stencil) {
                stencil = std::make_unique<mxvk::VK_Stencil>();
                stencil->initialize(mxvk::VulkanContext{
                                        .device = getDevice(),
                                        .physical_device = getPhysicalDevice(),
                                        .graphics_queue = getGraphicsQueue(),
                                        .command_pool = getCommandPool(),
                                    },
                                    current_extent, getSwapchainFormat(), VK_FORMAT_UNDEFINED, getPipelineCache(), shader_root + "/fullscreen.vert.spv", shader_root + "/mandala_mask.frag.spv", shader_root + "/content_fullscreen.vert.spv", shader_root + "/stencil_fill.frag.spv");
            } else {
                stencil->resize(current_extent);
            }
        }

        std::string shader_root;
        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
        std::unique_ptr<mxvk::VK_Stencil> stencil{};
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::StencilWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
