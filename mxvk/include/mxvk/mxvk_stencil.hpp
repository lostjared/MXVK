/**
 * @file mxvk_stencil.hpp
 * @brief Reusable dynamic-rendering stencil image and fullscreen stencil pipelines.
 */
#pragma once

#include "mxvk_context.hpp"
#include "mxvk_resource.hpp"

#include <volk/volk.h>

#include <cstdint>
#include <string>

namespace mxvk {

    /**
     * @brief Owns a stencil attachment and two fullscreen pipelines for stencil-masked rendering.
     *
     * VK_Stencil is intended for use from VK_Window subclasses. Call initialize() once the
     * window has a valid device and swapchain extent, call prepare_for_rendering() before
     * VK_Window begins dynamic rendering, pass configure_attachment() through
     * VK_Window::onConfigureDepthStencilAttachments(), then call draw_mask() before draw_content()
     * inside VK_Window::onRecordCustomRendering().
     */
    class VK_Stencil {
      public:
        /**
         * @brief Push-constant payload shared by the mask and content shaders.
         *
         * The helper does not assign semantic meaning beyond the field names; examples use
         * the values for animation time, aspect-ratio correction, phase offsets, and scale.
         */
        struct PushConstants {
            /** @brief Elapsed animation time in seconds. */
            float time = 0.0f;
            /** @brief Render target width divided by height. */
            float aspect = 1.0f;
            /** @brief User-defined phase value for animated stencil/content shaders. */
            float phase = 0.0f;
            /** @brief User-defined scale value for stencil/content shaders. */
            float scale = 1.0f;
        };

        /** @brief Construct an empty stencil helper. */
        VK_Stencil() = default;

        /** @brief Destroy any live Vulkan resources owned by the helper. */
        ~VK_Stencil();

        VK_Stencil(const VK_Stencil &) = delete;
        VK_Stencil(VK_Stencil &&) = delete;
        VK_Stencil &operator=(const VK_Stencil &) = delete;
        VK_Stencil &operator=(VK_Stencil &&) = delete;

        /**
         * @brief Create the stencil attachment and fullscreen mask/content pipelines.
         * @param context Vulkan device, physical device, queue, and command pool handles.
         * @param extent Size of the stencil attachment in pixels.
         * @param color_format Color attachment format used by the active dynamic rendering pass.
         * @param depth_format Depth attachment format used by the active dynamic rendering pass.
         * @param pipeline_cache Optional pipeline cache used when creating graphics pipelines.
         * @param mask_vertex_shader SPIR-V vertex shader path for the stencil-writing pass.
         * @param mask_fragment_shader SPIR-V fragment shader path for the stencil-writing pass.
         * @param content_vertex_shader SPIR-V vertex shader path for the stencil-tested content pass.
         * @param content_fragment_shader SPIR-V fragment shader path for the stencil-tested content pass.
         *
         * Existing resources are destroyed before the new resources are created.
         */
        void initialize(const VulkanContext &context,
                        VkExtent2D extent,
                        VkFormat color_format,
                        VkFormat depth_format,
                        VkPipelineCache pipeline_cache,
                        const std::string &mask_vertex_shader,
                        const std::string &mask_fragment_shader,
                        const std::string &content_vertex_shader,
                        const std::string &content_fragment_shader);

        /** @brief Release all Vulkan resources and reset the helper to an empty state. */
        void destroy();

        /**
         * @brief Recreate the stencil image for a new render extent.
         * @param extent New attachment size in pixels.
         *
         * Pipelines are retained because they depend on formats, not image dimensions.
         */
        void resize(VkExtent2D extent);

        /**
         * @brief Transition the stencil image for use as a dynamic-rendering stencil attachment.
         * @param cmd Command buffer currently recording outside a rendering scope.
         */
        void prepare_for_rendering(VkCommandBuffer cmd);

        /**
         * @brief Fill a VkRenderingAttachmentInfo for VK_Window dynamic rendering.
         * @param attachment Attachment info to populate when the helper is valid.
         */
        void configure_attachment(VkRenderingAttachmentInfo &attachment) const;

        /**
         * @brief Draw the fullscreen stencil-writing mask pass.
         * @param cmd Command buffer recording inside the dynamic-rendering scope.
         * @param push_constants Values passed to the mask fragment shader.
         */
        void draw_mask(VkCommandBuffer cmd, const PushConstants &push_constants) const;

        /**
         * @brief Draw the fullscreen content pass clipped by stencil reference value 1.
         * @param cmd Command buffer recording inside the dynamic-rendering scope.
         * @param push_constants Values passed to the content fragment shader.
         */
        void draw_content(VkCommandBuffer cmd, const PushConstants &push_constants) const;

        /** @brief Return the selected stencil-capable image format. */
        [[nodiscard]] VkFormat stencil_format() const noexcept { return stencil_image_format; }

        /** @brief Return the current stencil attachment extent. */
        [[nodiscard]] VkExtent2D extent() const noexcept { return stencil_extent; }

        /** @brief Check whether image resources are ready for rendering. */
        [[nodiscard]] bool valid() const noexcept;

      private:
        /** @brief Allocate the stencil image, memory, and image view. */
        void create_resources();

        /** @brief Create pipeline layouts and mask/content graphics pipelines. */
        void create_pipelines();

        /** @brief Destroy mask/content graphics pipelines and pipeline layouts. */
        void destroy_pipelines();

        /** @brief Destroy stencil image resources. */
        void destroy_image();

        /** @brief Select the first supported format that can be used as a stencil attachment. */
        [[nodiscard]] VkFormat choose_stencil_format() const;

        /**
         * @brief Create one fullscreen graphics pipeline.
         * @param vertex_shader SPIR-V vertex shader path.
         * @param fragment_shader SPIR-V fragment shader path.
         * @param layout Pipeline layout with the PushConstants range.
         * @param writes_stencil True for the mask pass, false for the stencil-tested content pass.
         * @return Created Vulkan graphics pipeline.
         */
        [[nodiscard]] VkPipeline create_pipeline(const std::string &vertex_shader,
                                                 const std::string &fragment_shader,
                                                 VkPipelineLayout layout,
                                                 bool writes_stencil) const;

        VulkanContext vk_context{};
        VkExtent2D stencil_extent{};
        VkFormat render_color_format = VK_FORMAT_UNDEFINED;
        VkFormat render_depth_format = VK_FORMAT_UNDEFINED;
        VkFormat stencil_image_format = VK_FORMAT_UNDEFINED;
        VkPipelineCache cache = VK_NULL_HANDLE;
        std::string mask_vertex_path{};
        std::string mask_fragment_path{};
        std::string content_vertex_path{};
        std::string content_fragment_path{};

        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        bool image_initialized = false;

        VkPipelineLayout mask_layout = VK_NULL_HANDLE;
        VkPipelineLayout content_layout = VK_NULL_HANDLE;
        VkPipeline mask_pipeline = VK_NULL_HANDLE;
        VkPipeline content_pipeline = VK_NULL_HANDLE;
    };

} // namespace mxvk
