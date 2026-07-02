#include "mxvk/mxvk_resource.hpp"

#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <vector>

namespace mxvk {

    namespace {
        void validate_context(const VulkanContext &context) {
            if (context.device == VK_NULL_HANDLE || context.physical_device == VK_NULL_HANDLE) {
                throw mxvk::Exception("mxvk resource helper requires a valid Vulkan device");
            }
        }

        void validate_upload_context(const VulkanContext &context) {
            validate_context(context);
            if (context.graphics_queue == VK_NULL_HANDLE || context.command_pool == VK_NULL_HANDLE) {
                throw mxvk::Exception("mxvk upload helper requires a valid graphics queue and command pool");
            }
        }

        void create_sampler(VkDevice device, TextureResource &texture) {
            VkSamplerCreateInfo sampler_info{};
            sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_info.magFilter = VK_FILTER_LINEAR;
            sampler_info.minFilter = VK_FILTER_LINEAR;
            sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler_info.anisotropyEnable = VK_FALSE;
            sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            sampler_info.unnormalizedCoordinates = VK_FALSE;
            sampler_info.compareEnable = VK_FALSE;
            sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
            sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            if (vkCreateSampler(device, &sampler_info, nullptr, &texture.sampler) != VK_SUCCESS) {
                throw mxvk::Exception("mxvk resource helper failed to create texture sampler");
            }
        }
    } // namespace

    uint32_t find_memory_type(VkPhysicalDevice physical_device,
                              uint32_t type_filter,
                              VkMemoryPropertyFlags properties) {
        if (physical_device == VK_NULL_HANDLE) {
            throw mxvk::Exception("find_memory_type requires a valid physical device");
        }

        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
            if ((type_filter & (1U << i)) &&
                (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw mxvk::Exception("failed to find suitable Vulkan memory type");
    }

    void create_buffer(const VulkanContext &context,
                       VkDeviceSize size,
                       VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       BufferResource &buffer) {
        validate_context(context);
        if (size == 0) {
            throw mxvk::Exception("create_buffer requires a non-zero size");
        }
        destroy_buffer(context.device, buffer);

        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(context.device, &buffer_info, nullptr, &buffer.buffer) != VK_SUCCESS) {
            throw mxvk::Exception("failed to create Vulkan buffer");
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(context.device, buffer.buffer, &requirements);

        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = find_memory_type(context.physical_device, requirements.memoryTypeBits, properties);
        if (vkAllocateMemory(context.device, &allocation, nullptr, &buffer.memory) != VK_SUCCESS) {
            destroy_buffer(context.device, buffer);
            throw mxvk::Exception("failed to allocate Vulkan buffer memory");
        }
        if (vkBindBufferMemory(context.device, buffer.buffer, buffer.memory, 0) != VK_SUCCESS) {
            destroy_buffer(context.device, buffer);
            throw mxvk::Exception("failed to bind Vulkan buffer memory");
        }
        buffer.size = size;
    }

    void destroy_buffer(VkDevice device, BufferResource &buffer) {
        if (device == VK_NULL_HANDLE) {
            buffer = {};
            return;
        }
        unmap_buffer(device, buffer);
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer.buffer, nullptr);
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, buffer.memory, nullptr);
        }
        buffer = {};
    }

    void map_buffer(VkDevice device, BufferResource &buffer) {
        if (buffer.mapped != nullptr) {
            return;
        }
        if (device == VK_NULL_HANDLE || buffer.memory == VK_NULL_HANDLE || buffer.size == 0) {
            throw mxvk::Exception("map_buffer requires a valid buffer allocation");
        }
        if (vkMapMemory(device, buffer.memory, 0, buffer.size, 0, &buffer.mapped) != VK_SUCCESS || buffer.mapped == nullptr) {
            throw mxvk::Exception("failed to map Vulkan buffer memory");
        }
    }

    void unmap_buffer(VkDevice device, BufferResource &buffer) {
        if (device != VK_NULL_HANDLE && buffer.memory != VK_NULL_HANDLE && buffer.mapped != nullptr) {
            vkUnmapMemory(device, buffer.memory);
        }
        buffer.mapped = nullptr;
    }

    void create_image(const VulkanContext &context,
                      uint32_t width,
                      uint32_t height,
                      VkFormat format,
                      VkImageTiling tiling,
                      VkImageUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkImage &image,
                      VkDeviceMemory &memory) {
        validate_context(context);
        if (width == 0 || height == 0) {
            throw mxvk::Exception("create_image requires non-zero dimensions");
        }

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent = {width, height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = format;
        image_info.tiling = tiling;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = usage;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(context.device, &image_info, nullptr, &image) != VK_SUCCESS) {
            throw mxvk::Exception("failed to create Vulkan image");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(context.device, image, &requirements);

        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = find_memory_type(context.physical_device, requirements.memoryTypeBits, properties);
        if (vkAllocateMemory(context.device, &allocation, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyImage(context.device, image, nullptr);
            image = VK_NULL_HANDLE;
            throw mxvk::Exception("failed to allocate Vulkan image memory");
        }
        if (vkBindImageMemory(context.device, image, memory, 0) != VK_SUCCESS) {
            vkFreeMemory(context.device, memory, nullptr);
            vkDestroyImage(context.device, image, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            throw mxvk::Exception("failed to bind Vulkan image memory");
        }
    }

    VkImageView create_image_view(VkDevice device,
                                  VkImage image,
                                  VkFormat format,
                                  VkImageAspectFlags aspect) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange.aspectMask = aspect;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        VkImageView image_view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS) {
            throw mxvk::Exception("failed to create Vulkan image view");
        }
        return image_view;
    }

    VkCommandBuffer begin_one_time_commands(const VulkanContext &context) {
        validate_upload_context(context);
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool = context.command_pool;
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(context.device, &alloc_info, &command_buffer) != VK_SUCCESS) {
            throw mxvk::Exception("failed to allocate one-time command buffer");
        }

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
            vkFreeCommandBuffers(context.device, context.command_pool, 1, &command_buffer);
            throw mxvk::Exception("failed to begin one-time command buffer");
        }

        return command_buffer;
    }

    void end_one_time_commands(const VulkanContext &context, VkCommandBuffer command_buffer) {
        validate_upload_context(context);
        if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
            vkFreeCommandBuffers(context.device, context.command_pool, 1, &command_buffer);
            throw mxvk::Exception("failed to end one-time command buffer");
        }

        VkCommandBufferSubmitInfo command_buffer_info{};
        command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        command_buffer_info.commandBuffer = command_buffer;

        VkSubmitInfo2 submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &command_buffer_info;

        if (vkQueueSubmit2(context.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(context.device, context.command_pool, 1, &command_buffer);
            throw mxvk::Exception("failed to submit one-time command buffer");
        }
        if (vkQueueWaitIdle(context.graphics_queue) != VK_SUCCESS) {
            vkFreeCommandBuffers(context.device, context.command_pool, 1, &command_buffer);
            throw mxvk::Exception("failed to wait for one-time command completion");
        }

        // Upload command buffers are transient; callers only observe completion.
        vkFreeCommandBuffers(context.device, context.command_pool, 1, &command_buffer);
    }

    void transition_image_layout(VkCommandBuffer command_buffer,
                                 VkImage image,
                                 VkImageLayout old_layout,
                                 VkImageLayout new_layout,
                                 VkImageAspectFlags aspect) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        // MXVK requires Vulkan 1.3 synchronization2, so uploads use stage/access2 masks.
        if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            barrier.srcAccessMask = VK_ACCESS_2_NONE;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        } else {
            throw mxvk::Exception("unsupported image layout transition");
        }

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command_buffer, &dependency);
    }

    void copy_buffer_to_image(VkCommandBuffer command_buffer,
                              VkBuffer buffer,
                              VkImage image,
                              uint32_t width,
                              uint32_t height) {
        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        VkCopyBufferToImageInfo2 copy_info{};
        copy_info.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        copy_info.srcBuffer = buffer;
        copy_info.dstImage = image;
        copy_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy_info.regionCount = 1;
        copy_info.pRegions = &region;
        vkCmdCopyBufferToImage2(command_buffer, &copy_info);
    }

    void create_texture_from_surface(const VulkanContext &context,
                                     SDL_Surface *surface,
                                     TextureResource &texture,
                                     VkFormat format) {
        validate_upload_context(context);
        if (surface == nullptr || surface->pixels == nullptr || surface->w <= 0 || surface->h <= 0) {
            throw mxvk::Exception("create_texture_from_surface requires a valid SDL surface");
        }
        destroy_texture(context.device, texture);

        const uint32_t width = static_cast<uint32_t>(surface->w);
        const uint32_t height = static_cast<uint32_t>(surface->h);
        const VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4U;
        std::vector<std::byte> tight_pixels(static_cast<size_t>(image_size));

        // SDL surfaces may have padded rows; Vulkan buffer-image copies use tight rows here.
        const auto *src = static_cast<const std::byte *>(surface->pixels);
        auto *dst = tight_pixels.data();
        const size_t tight_row_bytes = static_cast<size_t>(width) * 4U;
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(dst + y * tight_row_bytes, src + static_cast<size_t>(y) * static_cast<size_t>(surface->pitch), tight_row_bytes);
        }

        BufferResource staging{};
        create_buffer(context,
                      image_size,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging);
        map_buffer(context.device, staging);
        std::memcpy(staging.mapped, tight_pixels.data(), tight_pixels.size());
        unmap_buffer(context.device, staging);

        create_image(context,
                     width,
                     height,
                     format,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     texture.image,
                     texture.memory);

        const VkCommandBuffer cmd = begin_one_time_commands(context);
        transition_image_layout(cmd, texture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(cmd, staging.buffer, texture.image, width, height);
        transition_image_layout(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        end_one_time_commands(context, cmd);

        destroy_buffer(context.device, staging);

        texture.view = create_image_view(context.device, texture.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
        create_sampler(context.device, texture);
        texture.width = width;
        texture.height = height;
    }

    void create_texture_from_png(const VulkanContext &context,
                                 const std::string &path,
                                 TextureResource &texture,
                                 VkFormat format) {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(mxvk::LoadPNG(path.c_str()), SDL_DestroySurface);
        if (surface == nullptr) {
            throw mxvk::Exception(std::format("failed to load texture PNG: {}", path));
        }
        create_texture_from_surface(context, surface.get(), texture, format);
    }

    void destroy_texture(VkDevice device, TextureResource &texture) {
        if (device == VK_NULL_HANDLE) {
            texture = {};
            return;
        }
        if (texture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, texture.sampler, nullptr);
        }
        if (texture.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, texture.view, nullptr);
        }
        if (texture.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, texture.image, nullptr);
        }
        if (texture.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, texture.memory, nullptr);
        }
        texture = {};
    }

} // namespace mxvk
