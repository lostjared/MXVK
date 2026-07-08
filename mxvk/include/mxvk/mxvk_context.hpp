/**
 * @file mxvk_context.hpp
 * @brief Minimal Vulkan handles shared across MXVK helpers.
 */
#ifndef MXVK_CONTEXT_HPP
#define MXVK_CONTEXT_HPP

#include <volk/volk.h>

namespace mxvk {

    /**
     * @brief Minimal Vulkan handles required by MXVK resource helpers.
     *
     * Buffer and image allocation require @ref device and @ref physical_device.
     * Upload helpers also require @ref graphics_queue and @ref command_pool.
     */
    struct VulkanContext {
        /** @brief Logical device used to create and destroy resources. */
        VkDevice device = VK_NULL_HANDLE;
        /** @brief Physical device used for memory type queries. */
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        /** @brief Queue used for one-shot upload command submission. */
        VkQueue graphics_queue = VK_NULL_HANDLE;
        /** @brief Command pool used for transient upload command buffers. */
        VkCommandPool command_pool = VK_NULL_HANDLE;
    };

}

#endif
