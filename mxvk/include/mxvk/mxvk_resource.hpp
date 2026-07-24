/**
 * @file mxvk_resource.hpp
 * @brief Reusable Vulkan buffer, image, upload, and one-shot command helpers.
 */
#pragma once

#include "mxvk_context.hpp"

#include <volk/volk.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>

namespace mxvk {

    /**
     * @brief Owned Vulkan buffer allocation with optional persistent host mapping.
     *
     * The helper functions in this file treat this as a small RAII-like aggregate:
     * create_buffer() initializes the handles, map_buffer() stores the mapped
     * pointer, and destroy_buffer() unmaps and releases any live resources.
     */
    struct BufferResource {
        /** @brief Vulkan buffer handle. */
        VkBuffer buffer = VK_NULL_HANDLE;
        /** @brief Device memory bound to @ref buffer. */
        VkDeviceMemory memory = VK_NULL_HANDLE;
        /** @brief Requested buffer size in bytes. */
        VkDeviceSize size = 0;
        /** @brief Host pointer returned by vkMapMemory, or nullptr when unmapped. */
        void *mapped = nullptr;
    };

    /**
     * @brief Owned sampled 2D texture resources.
     *
     * Texture upload helpers create the image, memory, image view, and sampler.
     * destroy_texture() releases every live member and resets the dimensions.
     */
    struct TextureResource {
        /** @brief Optimal-tiled image containing the texture pixels. */
        VkImage image = VK_NULL_HANDLE;
        /** @brief Device-local memory bound to @ref image. */
        VkDeviceMemory memory = VK_NULL_HANDLE;
        /** @brief 2D color image view used by descriptors. */
        VkImageView view = VK_NULL_HANDLE;
        /** @brief Sampler configured for linear filtering and clamp-to-edge addressing. */
        VkSampler sampler = VK_NULL_HANDLE;
        /** @brief Texture width in pixels. */
        uint32_t width = 0;
        /** @brief Texture height in pixels. */
        uint32_t height = 0;
    };

    /**
     * @brief Find a memory type satisfying a Vulkan memory type mask and property set.
     * @param physical_device Physical device to query.
     * @param type_filter Memory type bitmask from VkMemoryRequirements.
     * @param properties Required VkMemoryPropertyFlagBits.
     * @return Matching memory type index.
     * @throws mxvk::Exception if no matching memory type is available.
     */
    [[nodiscard]] uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                            uint32_t type_filter,
                                            VkMemoryPropertyFlags properties);

    /**
     * @brief Create and bind a Vulkan buffer allocation.
     * @param context Valid Vulkan device and physical device handles.
     * @param size Buffer size in bytes.
     * @param usage VkBufferUsageFlagBits describing intended buffer use.
     * @param properties Required memory properties.
     * @param buffer Output buffer resource; any existing contents are destroyed first.
     * @throws mxvk::Exception on invalid input or Vulkan allocation failure.
     */
    void create_buffer(const VulkanContext &context,
                       VkDeviceSize size,
                       VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       BufferResource &buffer);

    /**
     * @brief Unmap and destroy a BufferResource.
     * @param device Logical device that owns the resource.
     * @param buffer Resource to release and reset.
     */
    void destroy_buffer(VkDevice device, BufferResource &buffer);

    /**
     * @brief Persistently map a host-visible buffer allocation.
     * @param device Logical device that owns the buffer memory.
     * @param buffer Buffer resource to map.
     * @throws mxvk::Exception if the buffer is invalid or mapping fails.
     */
    void map_buffer(VkDevice device, BufferResource &buffer);

    /**
     * @brief Unmap a BufferResource if it is currently mapped.
     * @param device Logical device that owns the buffer memory.
     * @param buffer Buffer resource to unmap.
     */
    void unmap_buffer(VkDevice device, BufferResource &buffer);

    /**
     * @brief Create a 2D VkImage and allocate/bind its memory.
     * @param context Valid Vulkan device and physical device handles.
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     * @param format Image format.
     * @param tiling Image tiling mode.
     * @param usage VkImageUsageFlagBits describing intended image use.
     * @param properties Required memory properties.
     * @param image Output image handle.
     * @param memory Output memory handle bound to @p image.
     * @throws mxvk::Exception on invalid dimensions or Vulkan allocation failure.
     */
    void create_image(const VulkanContext &context,
                      uint32_t width,
                      uint32_t height,
                      VkFormat format,
                      VkImageTiling tiling,
                      VkImageUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkImage &image,
                      VkDeviceMemory &memory);

    /**
     * @brief Create a 2D image view for an image.
     * @param device Logical device.
     * @param image Image to view.
     * @param format View format.
     * @param aspect Image aspect mask, usually VK_IMAGE_ASPECT_COLOR_BIT.
     * @return Created image view handle.
     * @throws mxvk::Exception if view creation fails.
     */
    [[nodiscard]] VkImageView create_image_view(VkDevice device,
                                                VkImage image,
                                                VkFormat format,
                                                VkImageAspectFlags aspect);

    /**
     * @brief Allocate and begin a primary one-time command buffer.
     * @param context Valid upload context with device, queue, and command pool.
     * @return Recording command buffer.
     * @throws mxvk::Exception if allocation or begin fails.
     */
    [[nodiscard]] VkCommandBuffer begin_one_time_commands(const VulkanContext &context);

    /**
     * @brief End, submit with vkQueueSubmit2, wait idle, and free a one-time command buffer.
     * @param context Valid upload context with device, queue, and command pool.
     * @param command_buffer Command buffer returned by begin_one_time_commands().
     * @throws mxvk::Exception if end, submit, or queue wait fails.
     */
    void end_one_time_commands(const VulkanContext &context, VkCommandBuffer command_buffer);

    /**
     * @brief Copy one buffer into another with a one-shot Vulkan 1.3 copy command.
     * @param context Valid upload context.
     * @param source Source buffer.
     * @param destination Destination buffer.
     * @param size Bytes to copy.
     * @throws mxvk::Exception if command allocation, submission, or completion fails.
     */
    void copy_buffer(const VulkanContext &context,
                     VkBuffer source,
                     VkBuffer destination,
                     VkDeviceSize size);

    /**
     * @brief Record a Vulkan 1.3 synchronization2 image layout transition.
     * @param command_buffer Command buffer in recording state.
     * @param image Image to transition.
     * @param old_layout Current image layout.
     * @param new_layout Desired image layout.
     * @param aspect Image aspect mask.
     *
     * Supported transitions are:
     * - VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
     * - VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     *
     * @throws mxvk::Exception for unsupported transitions.
     */
    void transition_image_layout(VkCommandBuffer command_buffer,
                                 VkImage image,
                                 VkImageLayout old_layout,
                                 VkImageLayout new_layout,
                                 VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

    /**
     * @brief Record a Vulkan 1.3 vkCmdCopyBufferToImage2 copy for a full 2D image.
     * @param command_buffer Command buffer in recording state.
     * @param buffer Source buffer containing tightly packed RGBA pixels.
     * @param image Destination image in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
     * @param width Copy width in pixels.
     * @param height Copy height in pixels.
     */
    void copy_buffer_to_image(VkCommandBuffer command_buffer,
                              VkBuffer buffer,
                              VkImage image,
                              uint32_t width,
                              uint32_t height);

    /**
     * @brief Upload an SDL surface into a sampled 2D texture.
     * @param context Valid upload context.
     * @param surface Source surface. Pixels are copied immediately.
     * @param texture Output texture resource; any existing contents are destroyed first.
     * @param format Destination image format. Defaults to VK_FORMAT_R8G8B8A8_UNORM.
     *
     * The source surface is copied row-by-row into a tight staging buffer to
     * tolerate SDL pitch padding. The surface is expected to contain 4 bytes per pixel,
     * as produced by MXVK's PNG loader.
     *
     * @throws mxvk::Exception on invalid input, allocation failure, or upload failure.
     */
    void create_texture_from_surface(const VulkanContext &context,
                                     SDL_Surface *surface,
                                     TextureResource &texture,
                                     VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    /**
     * @brief Load a PNG and upload it into a sampled 2D texture.
     * @param context Valid upload context.
     * @param path PNG path.
     * @param texture Output texture resource; any existing contents are destroyed first.
     * @param format Destination image format. Defaults to VK_FORMAT_R8G8B8A8_UNORM.
     * @throws mxvk::Exception if loading or upload fails.
     */
    void create_texture_from_png(const VulkanContext &context,
                                 const std::string &path,
                                 TextureResource &texture,
                                 VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    /**
     * @brief Destroy every Vulkan handle owned by a TextureResource.
     * @param device Logical device that owns the texture.
     * @param texture Texture resource to release and reset.
     */
    void destroy_texture(VkDevice device, TextureResource &texture);

} // namespace mxvk
