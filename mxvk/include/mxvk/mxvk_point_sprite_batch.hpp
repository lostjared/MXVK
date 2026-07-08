/**
 * @file mxvk_point_sprite_batch.hpp
 * @brief Reusable point-sprite renderer for particle and starfield effects.
 */
#ifndef MXVK_POINT_SPRITE_BATCH_HPP
#define MXVK_POINT_SPRITE_BATCH_HPP

#include "mxvk/mxvk_resource.hpp"

#include <volk/volk.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace mxvk {

    /**
     * @brief Forward declaration of the MXVK window wrapper.
     */
    class VK_Window;

    /**
     * @brief Vertex layout consumed by VK_PointSpriteBatch.
     *
     * The vertex shader receives world or clip-space effect coordinates at
     * location 0, point size at location 1, and tint color at location 2.
     */
    struct PointSpriteVertex {
        /** @brief Vertex position passed to shader location 0. */
        float position[3]{};
        /** @brief Rasterized point size in pixels, passed to shader location 1. */
        float size = 0.0f;
        /** @brief RGBA tint color passed to shader location 2. */
        float color[4]{};
    };

    /**
     * @class VK_PointSpriteBatch
     * @brief Reusable Vulkan point-list renderer for textured particle effects.
     *
     * VK_PointSpriteBatch owns a host-visible vertex buffer, a sampled point
     * texture, per-swapchain MVP uniform buffers, descriptors, and a point-list
     * graphics pipeline using MXVK's dynamic rendering path. It is intended for
     * starfields, sparks, dust, and similar effects where each vertex becomes
     * one shader-expanded point sprite via gl_PointSize/gl_PointCoord.
     *
     * The caller owns simulation and fills PointSpriteVertex data each frame via
     * upload_vertices(). The batch must be loaded only after VK_Window has created
     * deferred render resources, usually from onRecordCustomRendering().
     */
    class VK_PointSpriteBatch {
      public:
        /**
         * @brief Construct an empty point-sprite batch.
         */
        VK_PointSpriteBatch() = default;

        /**
         * @brief Destroy all owned Vulkan resources.
         */
        ~VK_PointSpriteBatch();

        VK_PointSpriteBatch(const VK_PointSpriteBatch &) = delete;
        VK_PointSpriteBatch &operator=(const VK_PointSpriteBatch &) = delete;
        VK_PointSpriteBatch(VK_PointSpriteBatch &&) = delete;
        VK_PointSpriteBatch &operator=(VK_PointSpriteBatch &&) = delete;

        /**
         * @brief Create texture, vertex buffer, descriptors, and point-list pipeline.
         * @param window Active MXVK window with ready swapchain/render resources.
         * @param texture_path PNG texture sampled by the fragment shader.
         * @param vertex_shader_path SPIR-V vertex shader path.
         * @param fragment_shader_path SPIR-V fragment shader path.
         * @param max_vertices Maximum number of point vertices the batch can draw.
         * @throws mxvk::Exception on invalid input or Vulkan resource failure.
         */
        void load(VK_Window *window,
                  const std::string &texture_path,
                  const std::string &vertex_shader_path,
                  const std::string &fragment_shader_path,
                  size_t max_vertices);

        /**
         * @brief Recreate swapchain-dependent resources after a window resize.
         * @param window Active MXVK window with recreated swapchain resources.
         */
        void resize(VK_Window *window);

        /**
         * @brief Destroy all owned resources and reset the batch to an unloaded state.
         */
        void cleanup();

        /**
         * @brief Copy point vertices into the persistent mapped vertex buffer.
         * @param vertices Pointer to @p count vertices. May be nullptr only when count is zero.
         * @param count Number of vertices to upload and draw.
         * @throws mxvk::Exception if @p count exceeds capacity().
         */
        void upload_vertices(const PointSpriteVertex *vertices, size_t count);

        /**
         * @brief Update the MVP uniform for one swapchain image.
         * @param image_index Current swapchain image index.
         * @param mvp Model-view-projection transform consumed by the vertex shader.
         */
        void update_mvp(uint32_t image_index, const glm::mat4 &mvp);

        /**
         * @brief Record point-sprite draw commands into an active rendering scope.
         * @param cmd Command buffer in recording state inside MXVK dynamic rendering.
         * @param image_index Current swapchain image index.
         */
        void render(VkCommandBuffer cmd, uint32_t image_index);

        /**
         * @brief Select additive or alpha-over color blending.
         * @param enabled @c true for additive blending, @c false for alpha blending.
         */
        void set_additive_blending(bool enabled);

        /**
         * @brief Enable or disable depth testing in the point-sprite pipeline.
         * @param enabled @c true to test against the depth attachment.
         */
        void set_depth_test_enabled(bool enabled);

        /**
         * @brief Enable or disable depth writes in the point-sprite pipeline.
         * @param enabled @c true to write point depth values.
         */
        void set_depth_write_enabled(bool enabled);

        /** @return @c true when load() has completed successfully. */
        [[nodiscard]] bool loaded() const { return batch_loaded; }
        /** @return Maximum number of vertices accepted by upload_vertices(). */
        [[nodiscard]] size_t capacity() const { return max_vertices; }
        /** @return Number of vertices that will be drawn by render(). */
        [[nodiscard]] size_t vertex_count() const { return active_vertices; }

      private:
        /** @brief Per-frame uniform payload. */
        struct UniformBufferObject {
            /** @brief Model-view-projection transform. */
            alignas(16) glm::mat4 mvp{1.0f};
        };

        /** @brief Allocate and persistently map the vertex buffer. */
        void create_vertex_buffer();
        /** @brief Destroy the persistent vertex buffer. */
        void destroy_vertex_buffer();
        /** @brief Create resources tied to current swapchain image count/formats. */
        void create_swapchain_resources();
        /** @brief Destroy resources tied to current swapchain image count/formats. */
        void cleanup_swapchain_resources();
        /** @brief Create texture and MVP descriptor set layout. */
        void create_descriptor_set_layout();
        /** @brief Allocate one mapped MVP uniform buffer per swapchain image. */
        void create_uniform_buffers();
        /** @brief Destroy all mapped MVP uniform buffers. */
        void destroy_uniform_buffers();
        /** @brief Create descriptor pool for all swapchain images. */
        void create_descriptor_pool();
        /** @brief Allocate and update descriptor sets for texture and MVP UBOs. */
        void create_descriptor_sets();
        /** @brief Create the dynamic-rendering point-list graphics pipeline. */
        void create_pipeline();
        /** @brief Destroy the graphics pipeline and layout. */
        void destroy_pipeline();
        /** @brief Read a SPIR-V shader file into memory. */
        [[nodiscard]] std::vector<char> read_shader_file(const std::string &path) const;
        /** @brief Vulkan handles required for allocation, upload, and draw resource creation. */
        VulkanContext context{};
        /** @brief Persistent pipeline cache borrowed from VK_Window. */
        VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
        /** @brief Current swapchain color attachment format. */
        VkFormat color_attachment_format = VK_FORMAT_UNDEFINED;
        /** @brief Current depth attachment format. */
        VkFormat depth_attachment_format = VK_FORMAT_UNDEFINED;
        /** @brief Current swapchain image count. */
        size_t image_count = 0;

        /** @brief Source texture path used during load(). */
        std::string texture_path{};
        /** @brief Vertex shader SPIR-V path. */
        std::string vertex_shader_path{};
        /** @brief Fragment shader SPIR-V path. */
        std::string fragment_shader_path{};

        /** @brief Sampled point texture. */
        TextureResource texture{};
        /** @brief Host-visible vertex buffer storing PointSpriteVertex data. */
        BufferResource vertex_buffer{};
        /** @brief Maximum supported vertex count. */
        size_t max_vertices = 0;
        /** @brief Currently uploaded vertex count. */
        size_t active_vertices = 0;

        /** @brief Descriptor layout for sampled texture and MVP UBO. */
        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
        /** @brief Descriptor pool for all per-image descriptor sets. */
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        /** @brief One descriptor set per swapchain image. */
        std::vector<VkDescriptorSet> descriptor_sets{};
        /** @brief One mapped MVP uniform buffer per swapchain image. */
        std::vector<BufferResource> uniform_buffers{};

        /** @brief Graphics pipeline layout. */
        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        /** @brief Point-list graphics pipeline. */
        VkPipeline pipeline = VK_NULL_HANDLE;
        /** @brief True for additive color blending, false for alpha-over blending. */
        bool additive_blending = true;
        /** @brief Whether the pipeline enables depth testing. */
        bool depth_test_enabled = false;
        /** @brief Whether the pipeline writes depth values. */
        bool depth_write_enabled = false;
        /** @brief True after successful load(). */
        bool batch_loaded = false;
    };

} // namespace mxvk

#endif
