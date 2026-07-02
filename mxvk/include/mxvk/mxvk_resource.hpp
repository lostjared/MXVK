/**
 * @file mxvk_resource.hpp
 * @brief Reusable Vulkan buffer, image, upload, and one-shot command helpers.
 */
#ifndef MXVK_RESOURCE_HPP
#define MXVK_RESOURCE_HPP

#include <volk/volk.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>

namespace mxvk {

    struct BufferResource {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        void *mapped = nullptr;
    };

    struct TextureResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct VulkanContext {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkCommandPool command_pool = VK_NULL_HANDLE;
    };

    [[nodiscard]] uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                            uint32_t type_filter,
                                            VkMemoryPropertyFlags properties);

    void create_buffer(const VulkanContext &context,
                       VkDeviceSize size,
                       VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       BufferResource &buffer);

    void destroy_buffer(VkDevice device, BufferResource &buffer);
    void map_buffer(VkDevice device, BufferResource &buffer);
    void unmap_buffer(VkDevice device, BufferResource &buffer);

    void create_image(const VulkanContext &context,
                      uint32_t width,
                      uint32_t height,
                      VkFormat format,
                      VkImageTiling tiling,
                      VkImageUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkImage &image,
                      VkDeviceMemory &memory);

    [[nodiscard]] VkImageView create_image_view(VkDevice device,
                                                VkImage image,
                                                VkFormat format,
                                                VkImageAspectFlags aspect);

    [[nodiscard]] VkCommandBuffer begin_one_time_commands(const VulkanContext &context);
    void end_one_time_commands(const VulkanContext &context, VkCommandBuffer command_buffer);

    void transition_image_layout(VkCommandBuffer command_buffer,
                                 VkImage image,
                                 VkImageLayout old_layout,
                                 VkImageLayout new_layout,
                                 VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    void copy_buffer_to_image(VkCommandBuffer command_buffer,
                              VkBuffer buffer,
                              VkImage image,
                              uint32_t width,
                              uint32_t height);

    void create_texture_from_surface(const VulkanContext &context,
                                     SDL_Surface *surface,
                                     TextureResource &texture,
                                     VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    void create_texture_from_png(const VulkanContext &context,
                                 const std::string &path,
                                 TextureResource &texture,
                                 VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    void destroy_texture(VkDevice device, TextureResource &texture);

} // namespace mxvk

#endif
