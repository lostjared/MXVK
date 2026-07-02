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

    class VK_Window;

    struct PointSpriteVertex {
        float position[3]{};
        float size = 0.0f;
        float color[4]{};
    };

    class VK_PointSpriteBatch {
      public:
        VK_PointSpriteBatch() = default;
        ~VK_PointSpriteBatch();

        VK_PointSpriteBatch(const VK_PointSpriteBatch &) = delete;
        VK_PointSpriteBatch &operator=(const VK_PointSpriteBatch &) = delete;
        VK_PointSpriteBatch(VK_PointSpriteBatch &&) = delete;
        VK_PointSpriteBatch &operator=(VK_PointSpriteBatch &&) = delete;

        void load(VK_Window *window,
                  const std::string &texture_path,
                  const std::string &vertex_shader_path,
                  const std::string &fragment_shader_path,
                  size_t max_vertices);

        void resize(VK_Window *window);
        void cleanup();

        void upload_vertices(const PointSpriteVertex *vertices, size_t count);
        void update_mvp(uint32_t image_index, const glm::mat4 &mvp);
        void render(VkCommandBuffer cmd, uint32_t image_index);

        void set_additive_blending(bool enabled);
        void set_depth_test_enabled(bool enabled);
        void set_depth_write_enabled(bool enabled);

        [[nodiscard]] bool loaded() const { return batch_loaded; }
        [[nodiscard]] size_t capacity() const { return max_vertices; }
        [[nodiscard]] size_t vertex_count() const { return active_vertices; }

      private:
        struct UniformBufferObject {
            alignas(16) glm::mat4 mvp{1.0f};
        };

        void create_vertex_buffer();
        void destroy_vertex_buffer();
        void create_swapchain_resources();
        void cleanup_swapchain_resources();
        void create_descriptor_set_layout();
        void create_uniform_buffers();
        void destroy_uniform_buffers();
        void create_descriptor_pool();
        void create_descriptor_sets();
        void create_pipeline();
        void destroy_pipeline();
        [[nodiscard]] std::vector<char> read_shader_file(const std::string &path) const;
        [[nodiscard]] VkShaderModule create_shader_module(const std::vector<char> &code) const;

        VulkanContext context{};
        VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
        VkFormat color_attachment_format = VK_FORMAT_UNDEFINED;
        VkFormat depth_attachment_format = VK_FORMAT_UNDEFINED;
        size_t image_count = 0;

        std::string texture_path{};
        std::string vertex_shader_path{};
        std::string fragment_shader_path{};

        TextureResource texture{};
        BufferResource vertex_buffer{};
        size_t max_vertices = 0;
        size_t active_vertices = 0;

        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptor_sets{};
        std::vector<BufferResource> uniform_buffers{};

        VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        bool additive_blending = true;
        bool depth_test_enabled = false;
        bool depth_write_enabled = false;
        bool batch_loaded = false;
    };

} // namespace mxvk

#endif
