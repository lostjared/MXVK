/**
 * @file mxvk_text.cpp
 * @brief Implementation of mxvk::VK_Text Vulkan SDL_ttf text renderer.
 */
#include "mxvk/mxvk_text.hpp"
#include <algorithm>
#include <iostream>

namespace mxvk {
    Font::Font(const std::string &fontPath, int fontSize) {
        reset(fontPath, fontSize);
    }

    Font::~Font() {
        reset();
    }

    Font::Font(Font &&other) noexcept
        : font(std::exchange(other.font, nullptr)),
          ownsTtfInit(std::exchange(other.ownsTtfInit, false)) {}

    Font &Font::operator=(Font &&other) noexcept {
        if (this != &other) {
            reset();
            font = std::exchange(other.font, nullptr);
            ownsTtfInit = std::exchange(other.ownsTtfInit, false);
        }
        return *this;
    }

    void Font::reset() {
        if (font != nullptr) {
            TTF_CloseFont(font);
            font = nullptr;
        }
        if (ownsTtfInit) {
            TTF_Quit();
            ownsTtfInit = false;
        }
    }

    void Font::reset(const std::string &fontPath, int fontSize) {
        if (fontPath.empty() || fontSize <= 0) {
            throw mxvk::Exception("Font requires a non-empty path and positive font size");
        }

        reset();

        if (!TTF_Init()) {
            throw mxvk::Exception("Failed to initialize SDL_ttf: " + std::string(SDL_GetError()));
        }

        TTF_Font *newFont = TTF_OpenFont(fontPath.c_str(), fontSize);
        if (newFont == nullptr) {
            TTF_Quit();
            throw mxvk::Exception("Failed to load font: " + std::string(SDL_GetError()));
        }

        font = newFont;
        ownsTtfInit = true;
    }

    VK_Text::VK_Text(VkDevice dev, VkPhysicalDevice physDev, VkQueue gQueue,
                     VkCommandPool cmdPool, const std::string &fontPath, int fontSize)
        : device(dev), physicalDevice(physDev), graphicsQueue(gQueue), commandPool(cmdPool) {

        if (!TTF_Init()) {
            throw mxvk::Exception("Failed to initialize SDL_ttf: " + std::string(SDL_GetError()));
        }

        initFont(fontPath, fontSize);
    }

    VK_Text::~VK_Text() {
        vkDeviceWaitIdle(device);
        textQuads.clear();
        clearCache();

        if (descriptorPool != VK_NULL_HANDLE) {
            std::cout << "vk: destroying text descriptor pool\n";
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        }

        if (fontSampler != VK_NULL_HANDLE) {
            std::cout << "vk: destroying text font sampler\n";
            vkDestroySampler(device, fontSampler, nullptr);
        }

        if (font) {
            std::cout << "mxvk: closing text font\n";
            TTF_CloseFont(font);
        }
        TTF_Quit();
    }

    void VK_Text::clearCache() {
        for (auto &[key, cached] : textureCache) {
            destroyCachedTexture(cached);
        }
        textureCache.clear();
    }

    void VK_Text::destroyCachedTexture(CachedTexture &cached) {
        if (cached.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, cached.imageView, nullptr);
            cached.imageView = VK_NULL_HANDLE;
        }
        if (cached.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, cached.image, nullptr);
            cached.image = VK_NULL_HANDLE;
        }
        if (cached.imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, cached.imageMemory, nullptr);
            cached.imageMemory = VK_NULL_HANDLE;
        }
    }

    void VK_Text::pruneCache() {
        if (textureCache.size() <= MAX_CACHED_TEXTURES) {
            return;
        }

        std::vector<std::pair<uint64_t, CacheKey>> entries;
        entries.reserve(textureCache.size());
        for (const auto &[key, cached] : textureCache) {
            entries.emplace_back(cached.lastUsedSerial, key);
        }

        const size_t removeCount = textureCache.size() - MAX_CACHED_TEXTURES;
        std::nth_element(
            entries.begin(),
            entries.begin() + static_cast<std::ptrdiff_t>(removeCount),
            entries.end(),
            [](const auto &lhs, const auto &rhs) {
                return lhs.first < rhs.first;
            });

        for (size_t i = 0; i < removeCount; ++i) {
            auto it = textureCache.find(entries[i].second);
            if (it == textureCache.end()) {
                continue;
            }
            destroyCachedTexture(it->second);
            textureCache.erase(it);
        }
    }

    void VK_Text::createDescriptorPool() {
        createDescriptorPool(maxPoolSets);
    }

    void VK_Text::createDescriptorPool(uint32_t maxSets) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = maxSets;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = maxSets;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
        maxPoolSets = maxSets;
    }

    void VK_Text::growDescriptorPool() {
        vkDeviceWaitIdle(device);
        textQuads.clear();
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }
        maxPoolSets *= 2;
        createDescriptorPool(maxPoolSets);
    }

    VkDescriptorSet VK_Text::createDescriptorSet(VkImageView imageView) {
        if (descriptorSetLayout == VK_NULL_HANDLE) {
            throw mxvk::Exception("VKText::createDescriptorSet called before setDescriptorSetLayout");
        }

        if (descriptorPool == VK_NULL_HANDLE) {
            createDescriptorPool();
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        VkDescriptorSet descriptorSet;
        VkResult res = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
        if (res == VK_ERROR_OUT_OF_POOL_MEMORY || res == VK_ERROR_FRAGMENTED_POOL) {
            growDescriptorPool();
            allocInfo.descriptorPool = descriptorPool;
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
        } else if (res != VK_SUCCESS) {
            throw mxvk::Exception(std::format("Fatal : VkResult is \"{}\" in {} at line {}", static_cast<int>(res), __FILE__, __LINE__));
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = fontSampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

        return descriptorSet;
    }

    void VK_Text::initSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &fontSampler));
    }

    void VK_Text::initFont(const std::string &fontPath, int fontSize) {
        font = TTF_OpenFont(fontPath.c_str(), fontSize);
        if (!font) {
            throw mxvk::Exception("Failed to load font: " + std::string(SDL_GetError()));
        }
        if (fontSampler == VK_NULL_HANDLE) {
            initSampler();
        }
        std::cout << std::format("mxvk: Font loaded: {} @ {}pt\n", fontPath, fontSize);
    }

    void VK_Text::setFont(const std::string &fontPath, int fontSize) {
        vkDeviceWaitIdle(device);
        clearQueue();
        clearCache();
        if (font) {
            TTF_CloseFont(font);
            font = nullptr;
        }
        if (fontSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, fontSampler, nullptr);
            fontSampler = VK_NULL_HANDLE;
        }
        initFont(fontPath, fontSize);
    }

    SDL_Surface *VK_Text::convertToRGBA(SDL_Surface *surface) {
        // SDL3: SDL_ConvertSurface handles RGBA conversion directly
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        return converted;
    }

    VkImageView VK_Text::createImageView(VkImage image, VkFormat format) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    void VK_Text::printTextG_Solid(const std::string &text, int x, int y, const SDL_Color &col) {
        printTextG_SolidWithFont(text, x, y, col, font);
    }

    void VK_Text::printTextG_Solid(const std::string &text, int x, int y, const SDL_Color &col, TTF_Font *textFont) {
        printTextG_SolidWithFont(text, x, y, col, textFont);
    }

    void VK_Text::printTextG_Solid(const std::string &text, int x, int y, const SDL_Color &col, const Font &textFont) {
        printTextG_SolidWithFont(text, x, y, col, textFont.get());
    }

    void VK_Text::printTextG_SolidWithFont(const std::string &text, int x, int y, const SDL_Color &col, TTF_Font *textFont) {
        if (text.empty() || !textFont)
            return;

        if (fontSampler == VK_NULL_HANDLE) {
            initSampler();
        }

        TextQuad quad;
        quad.device = device;
        quad.text = text;
        quad.x = x;
        quad.y = y;
        quad.color = col;
        quad.ownsTexture = false; // cache owns all textures

        CacheKey key{text, textFont, col.r, col.g, col.b, col.a};
        auto it = textureCache.find(key);

        if (it != textureCache.end()) {
            // Cache hit -- reuse the existing GPU texture
            auto &cached = it->second;
            cached.lastUsedSerial = ++cacheUseSerial;
            quad.textImage = cached.image;
            quad.textImageMemory = cached.imageMemory;
            quad.textImageView = cached.imageView;
            quad.width = cached.width;
            quad.height = cached.height;
        } else {
            // Cache miss -- render with SDL_ttf and upload to GPU
            // SDL3_ttf: TTF_RenderText_Blended requires an explicit length (0 = null-terminated)
            SDL_Surface *textSurface = TTF_RenderText_Blended(textFont, text.c_str(), 0, col);
            if (!textSurface) {
                return;
            }

            SDL_Surface *rgbaSurface = convertToRGBA(textSurface);
            SDL_DestroySurface(textSurface);
            if (!rgbaSurface) {
                return;
            }

            quad.width = rgbaSurface->w;
            quad.height = rgbaSurface->h;

            VkImage uploadedImage = VK_NULL_HANDLE;
            VkDeviceMemory uploadedImageMemory = VK_NULL_HANDLE;
            VkImageView uploadedImageView = VK_NULL_HANDLE;
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            const VkDeviceSize imageSize = static_cast<VkDeviceSize>(rgbaSurface->w) * static_cast<VkDeviceSize>(rgbaSurface->h) * 4u;

            auto cleanupUploadResources = [&]() {
                if (stagingBuffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, stagingBuffer, nullptr);
                    stagingBuffer = VK_NULL_HANDLE;
                }
                if (stagingMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, stagingMemory, nullptr);
                    stagingMemory = VK_NULL_HANDLE;
                }
                if (uploadedImageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, uploadedImageView, nullptr);
                    uploadedImageView = VK_NULL_HANDLE;
                }
                if (uploadedImage != VK_NULL_HANDLE) {
                    vkDestroyImage(device, uploadedImage, nullptr);
                    uploadedImage = VK_NULL_HANDLE;
                }
                if (uploadedImageMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, uploadedImageMemory, nullptr);
                    uploadedImageMemory = VK_NULL_HANDLE;
                }
            };

            bool uploadSucceeded = false;
            try {
                createImage(rgbaSurface->w, rgbaSurface->h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, uploadedImage, uploadedImageMemory);

                createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             stagingBuffer, stagingMemory);

                void *data = nullptr;
                VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data));
                memcpy(data, rgbaSurface->pixels, static_cast<size_t>(imageSize));
                vkUnmapMemory(device, stagingMemory);

                if (!transitionImageLayout(uploadedImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
                    SDL_DestroySurface(rgbaSurface);
                    cleanupUploadResources();
                    return;
                }
                if (!copyBufferToImage(stagingBuffer, uploadedImage, static_cast<uint32_t>(rgbaSurface->w), static_cast<uint32_t>(rgbaSurface->h))) {
                    SDL_DestroySurface(rgbaSurface);
                    cleanupUploadResources();
                    return;
                }
                if (!transitionImageLayout(uploadedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
                    SDL_DestroySurface(rgbaSurface);
                    cleanupUploadResources();
                    return;
                }

                uploadedImageView = createImageView(uploadedImage, VK_FORMAT_R8G8B8A8_UNORM);
                uploadSucceeded = true;
            } catch (const mxvk::Exception &ex) {
                SDL_DestroySurface(rgbaSurface);
                cleanupUploadResources();
                static bool textTextureWarningLogged = false;
                if (!textTextureWarningLogged) {
                    std::cerr << "mxvk: dropping text texture after Vulkan allocation failure: " << ex.text() << "\n";
                    textTextureWarningLogged = true;
                }
                return;
            }

            SDL_DestroySurface(rgbaSurface);

            if (!uploadSucceeded) {
                cleanupUploadResources();
                return;
            }

            if (stagingBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, stagingBuffer, nullptr);
                stagingBuffer = VK_NULL_HANDLE;
            }
            if (stagingMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, stagingMemory, nullptr);
                stagingMemory = VK_NULL_HANDLE;
            }

            quad.textImage = uploadedImage;
            quad.textImageMemory = uploadedImageMemory;
            quad.textImageView = uploadedImageView;

            // Store in cache
            textureCache[key] = {quad.textImage, quad.textImageMemory, quad.textImageView,
                                 quad.width, quad.height, ++cacheUseSerial};
        }

        // Build the screen-space quad (position-dependent, not cached)
        float x0 = (float)x;
        float y0 = (float)y;
        float x1 = x0 + quad.width;
        float y1 = y0 + quad.height;

        TextVertex v0 = {{x0, y0}, {0.0f, 0.0f}};
        TextVertex v1 = {{x1, y0}, {1.0f, 0.0f}};
        TextVertex v2 = {{x1, y1}, {1.0f, 1.0f}};
        TextVertex v3 = {{x0, y1}, {0.0f, 1.0f}};

        quad.vertices = {v0, v1, v2, v3};
        quad.indices = {0, 1, 2, 0, 2, 3};
        quad.indexCount = 6;

        try {
            void *data;
            VkDeviceSize vertexSize = quad.vertices.size() * sizeof(TextVertex);
            createBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         quad.vertexBuffer, quad.vertexBufferMemory);

            VK_CHECK_RESULT(vkMapMemory(device, quad.vertexBufferMemory, 0, vertexSize, 0, &data));
            memcpy(data, quad.vertices.data(), vertexSize);
            vkUnmapMemory(device, quad.vertexBufferMemory);

            VkDeviceSize indexSize = quad.indices.size() * sizeof(uint16_t);
            createBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         quad.indexBuffer, quad.indexBufferMemory);

            VK_CHECK_RESULT(vkMapMemory(device, quad.indexBufferMemory, 0, indexSize, 0, &data));
            memcpy(data, quad.indices.data(), indexSize);
            vkUnmapMemory(device, quad.indexBufferMemory);

            quad.descriptorSet = createDescriptorSet(quad.textImageView);
        } catch (const mxvk::Exception &ex) {
            static bool textAllocationWarningLogged = false;
            if (!textAllocationWarningLogged) {
                std::cerr << "mxvk: dropping text draw after Vulkan allocation failure: " << ex.text() << "\n";
                textAllocationWarningLogged = true;
            }
            return;
        }

        textQuads.emplace_back(std::move(quad));
    }

    void VK_Text::renderText(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout,
                             uint32_t screenWidth, uint32_t screenHeight) {

        struct TextPushConstants {
            float screenWidth;
            float screenHeight;
        } pc{static_cast<float>(screenWidth), static_cast<float>(screenHeight)};

        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TextPushConstants), &pc);

        for (auto &quad : textQuads) {
            VkBuffer vertexBuffers[] = {quad.vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmdBuffer, quad.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &quad.descriptorSet, 0, nullptr);

            vkCmdDrawIndexed(cmdBuffer, quad.indexCount, 1, 0, 0, 0);
        }
    }

    void VK_Text::clearQueue() {
        if (device == VK_NULL_HANDLE) {
            textQuads.clear();
            return;
        }

        // Text quads own vertex/index buffers referenced by submitted command buffers.
        // Wait for graphics work to finish before destroying these resources.
        const VkResult queue_idle_result = vkQueueWaitIdle(graphicsQueue);
        if (queue_idle_result == VK_ERROR_DEVICE_LOST) {
            std::cerr << "mxvk: Device lost while clearing queue; skipping text resource reset\n";
            textQuads.clear();
            return;
        }
        if (queue_idle_result != VK_SUCCESS) {
            throw mxvk::Exception(std::format(
                "Fatal : VkResult is \"{}\" in {} at line {}",
                static_cast<int>(queue_idle_result),
                __FILE__,
                __LINE__));
        }

        textQuads.clear();
        if (descriptorPool != VK_NULL_HANDLE) {
            VK_CHECK_RESULT(vkResetDescriptorPool(device, descriptorPool, 0));
        }
        pruneCache();
    }

    void VK_Text::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags properties, VkBuffer &buffer,
                               VkDeviceMemory &bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, buffer, bufferMemory, 0));
    }

    bool VK_Text::getTextDimensions(const std::string &text, int &width, int &height) {
        return getTextDimensions(text, width, height, font);
    }

    bool VK_Text::getTextDimensions(const std::string &text, int &width, int &height, TTF_Font *textFont) {
        if (text.empty() || !textFont) {
            width = 0;
            height = 0;
            return false;
        }

        // SDL3_ttf: TTF_GetStringSize replaces TTF_SizeText; length=0 for null-terminated
        return TTF_GetStringSize(textFont, text.c_str(), 0, &width, &height);
    }

    bool VK_Text::getTextDimensions(const std::string &text, int &width, int &height, const Font &textFont) {
        return getTextDimensions(text, width, height, textFont.get());
    }

    uint32_t VK_Text::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw mxvk::Exception("Failed to find suitable memory type!");
    }

    bool VK_Text::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        return endSingleTimeCommands(commandBuffer);
    }

    bool VK_Text::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        return endSingleTimeCommands(commandBuffer);
    }

    VkCommandBuffer VK_Text::beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        return commandBuffer;
    }

    bool VK_Text::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        const VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            std::cerr << "mxvk: Device lost during text command submit; skipping upload\n";
            return false;
        }
        VK_CHECK_RESULT(submitResult);

        const VkResult waitResult = vkQueueWaitIdle(graphicsQueue);
        if (waitResult == VK_ERROR_DEVICE_LOST) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            std::cerr << "mxvk: Device lost while waiting text queue idle; skipping upload\n";
            return false;
        }
        VK_CHECK_RESULT(waitResult);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        return true;
    }

    void VK_Text::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                              VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                              VkImage &image, VkDeviceMemory &imageMemory) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &image));

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, image, imageMemory, 0));
    }

} // namespace mxvk
