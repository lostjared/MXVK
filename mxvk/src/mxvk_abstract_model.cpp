#include "mxvk/mxvk_abstract_model.hpp"

#include <cstring>

#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace mxvk {

    namespace {
        void logVKAbstractModelStep(const std::string &message) {
            std::cout << "mxvk_abstract_model: " << message << '\n';
        }

        [[nodiscard]] std::vector<char> readBinaryFile(const std::string &path) {
            if (path.empty()) {
                throw mxvk::Exception("Shader path is empty");
            }

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                throw mxvk::Exception("Failed to open shader file: " + path);
            }

            std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (bytes.empty()) {
                throw mxvk::Exception("Shader file is empty: " + path);
            }
            if ((bytes.size() % 4U) != 0U) {
                throw mxvk::Exception("Shader file is not 4-byte aligned: " + path);
            }
            return bytes;
        }

        [[nodiscard]] VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &spvBytes) {
            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = spvBytes.size();
            createInfo.pCode = reinterpret_cast<const uint32_t *>(spvBytes.data());

            VkShaderModule module = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create shader module");
            }
            return module;
        }
    } // namespace

    void VKAbstractModel::load(VK_Window *window,
                               const std::string &modelPath,
                               const std::string &textureManifestPath,
                               const std::string &textureBasePath,
                               float scale) {
        if (window == nullptr) {
            throw mxvk::Exception("VKAbstractModel::load requires a valid window");
        }
        if (modelPath.empty()) {
            throw mxvk::Exception("VKAbstractModel::load modelPath is empty");
        }

        logVKAbstractModelStep("creation begin: " + modelPath);

        window_ = window;
        if (!window_->ensureRenderResources()) {
            throw mxvk::Exception("VKAbstractModel::load failed because render resources are not ready");
        }

        obj_.load(modelPath, scale);
        obj_.upload(window_->getDevice(), window_->getPhysicalDevice(), window_->getCommandPool(), window_->getGraphicsQueue());
        computeBoundsAndScale();
        logVKAbstractModelStep("mesh upload complete");

        textures_.clear();
        if (!textureManifestPath.empty()) {
            loadTextures(textureManifestPath, textureBasePath);
        } else {
            loadTexturesFromMTL(textureBasePath.empty() ? std::filesystem::path(modelPath).parent_path().string() : textureBasePath);
        }
        if (textures_.empty()) {
            createFallbackTexture();
            logVKAbstractModelStep("using fallback texture");
        }
        logVKAbstractModelStep("textures ready: " + std::to_string(textures_.size()));

        createTextureSampler();
        createDescriptorSetLayout();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createPipelines();
        logVKAbstractModelStep("creation complete");
    }

    void VKAbstractModel::setShaders(VK_Window *window, const std::string &vertSpv, const std::string &fragSpv) {
        if (window == nullptr) {
            throw mxvk::Exception("VKAbstractModel::setShaders requires a valid window");
        }

        window_ = window;
        vertexShaderPath_ = vertSpv;
        fragmentShaderPath_ = fragSpv;
        logVKAbstractModelStep("setShaders: vert=" + vertSpv + ", frag=" + fragSpv);
        createPipelines();
    }

    void VKAbstractModel::updateUBO(uint32_t imageIndex, const UniformBufferObject &ubo) {
        if (imageIndex >= uniformBuffersMapped_.size()) {
            return;
        }
        if (uniformBuffersMapped_[imageIndex] == nullptr) {
            return;
        }

        std::memcpy(uniformBuffersMapped_[imageIndex], &ubo, sizeof(UniformBufferObject));
    }

    void VKAbstractModel::render(VkCommandBuffer cmd, uint32_t imageIndex, bool wireframe) const {
        if (cmd == VK_NULL_HANDLE || imageIndex >= uniformBuffers_.size() || descriptorSets_.empty()) {
            return;
        }

        const VkPipeline pipeline = (wireframe && pipelineWireframe_ != VK_NULL_HANDLE) ? pipelineWireframe_ : pipelineFill_;
        if (pipeline == VK_NULL_HANDLE || pipelineLayout_ == VK_NULL_HANDLE) {
            return;
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        const size_t textureCount = std::max<size_t>(1, textures_.size());
        for (size_t i = 0; i < obj_.subMeshCount(); ++i) {
            const SubMesh &submesh = obj_.subMesh(i);
            const size_t textureIndex = std::min<size_t>(submesh.textureIndex, textureCount - 1U);
            const size_t setIndex = static_cast<size_t>(imageIndex) * textureCount + textureIndex;
            if (setIndex >= descriptorSets_.size()) {
                continue;
            }

            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout_,
                                    0,
                                    1,
                                    &descriptorSets_[setIndex],
                                    0,
                                    nullptr);

            obj_.drawSubMesh(cmd, i);
        }
    }

    void VKAbstractModel::resize(VK_Window *window) {
        if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
            return;
        }

        logVKAbstractModelStep("resize begin");
        window_ = window;
        destroyPipelines();
        destroyDescriptors();

        createDescriptorSetLayout();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createPipelines();
        logVKAbstractModelStep("resize complete");
    }

    void VKAbstractModel::cleanup(VK_Window *window) {
        if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
            return;
        }

        logVKAbstractModelStep("teardown begin");
        window_ = window;
        destroyPipelines();
        destroyDescriptors();
        destroyTextures();
        obj_.cleanup(window_->getDevice());
        window_ = nullptr;
        logVKAbstractModelStep("teardown complete");
    }

    void VKAbstractModel::computeBoundsAndScale() {
        const auto &vertices = obj_.vertices();
        if (vertices.empty()) {
            modelCenterOffset_ = glm::vec3(0.0f);
            modelRenderScale_ = 1.0f;
            return;
        }

        float minX = vertices.front().pos[0];
        float maxX = minX;
        float minY = vertices.front().pos[1];
        float maxY = minY;
        float minZ = vertices.front().pos[2];
        float maxZ = minZ;

        for (const VKVertex &v : vertices) {
            minX = std::min(minX, v.pos[0]);
            maxX = std::max(maxX, v.pos[0]);
            minY = std::min(minY, v.pos[1]);
            maxY = std::max(maxY, v.pos[1]);
            minZ = std::min(minZ, v.pos[2]);
            maxZ = std::max(maxZ, v.pos[2]);
        }

        modelCenterOffset_ = glm::vec3(
            -0.5f * (minX + maxX),
            -0.5f * (minY + maxY),
            -0.5f * (minZ + maxZ));

        const float maxExtent = std::max(maxX - minX, std::max(maxY - minY, maxZ - minZ));
        modelRenderScale_ = (maxExtent > 1e-6f) ? (2.5f / maxExtent) : 1.0f;
    }

    void VKAbstractModel::loadTextures(const std::string &textureManifestPath, const std::string &textureBasePath) {
        std::ifstream file(textureManifestPath);
        if (!file.is_open()) {
            throw mxvk::Exception("Failed to open texture manifest: " + textureManifestPath);
        }

        std::vector<std::string> lines{};
        std::string line;
        while (std::getline(file, line)) {
            const size_t begin = line.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                continue;
            }
            const size_t end = line.find_last_not_of(" \t\r\n");
            line = line.substr(begin, end - begin + 1);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            lines.push_back(line);
        }

        bool isStructured = false;
        bool isMtlLike = false;
        for (const std::string &ln : lines) {
            std::istringstream stream(ln);
            std::string keyword;
            stream >> keyword;
            if (keyword == "submesh" || keyword == "texture_dir" || keyword == "material_lib" || keyword == "model") {
                isStructured = true;
                break;
            }
            if (keyword == "newmtl") {
                isMtlLike = true;
                break;
            }
        }

        std::vector<std::string> imagePaths{};
        const std::string prefix = textureBasePath;

        if (isMtlLike) {
            for (const std::string &ln : lines) {
                std::istringstream stream(ln);
                std::string keyword;
                stream >> keyword;
                if (keyword == "map_Kd") {
                    std::string image;
                    if (stream >> image) {
                        imagePaths.push_back(prefix.empty() ? image : (prefix + "/" + image));
                    }
                }
            }
        } else if (isStructured) {
            for (const std::string &ln : lines) {
                std::istringstream stream(ln);
                std::string keyword;
                stream >> keyword;
                if (keyword == "texture") {
                    std::string image;
                    if (stream >> image) {
                        imagePaths.push_back(prefix.empty() ? image : (prefix + "/" + image));
                    }
                }
            }
        } else {
            for (const std::string &ln : lines) {
                imagePaths.push_back(prefix.empty() ? ln : (prefix + "/" + ln));
            }
        }

        for (const std::string &path : imagePaths) {
            SDL_Surface *surface = mxvk::LoadPNG(path.c_str());
            if (surface == nullptr) {
                continue;
            }

            const uint32_t width = static_cast<uint32_t>(surface->w);
            const uint32_t height = static_cast<uint32_t>(surface->h);
            const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4U;

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingMemory);

            void *mapped = nullptr;
            vkMapMemory(window_->getDevice(), stagingMemory, 0, imageSize, 0, &mapped);
            std::memcpy(mapped, surface->pixels, static_cast<size_t>(imageSize));
            vkUnmapMemory(window_->getDevice(), stagingMemory);

            TextureEntry tex{};
            tex.width = width;
            tex.height = height;
            createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        tex.image, tex.memory);

            transitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copyBufferToImage(stagingBuffer, tex.image, width, height);
            transitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            tex.view = createImageView(tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
            textures_.push_back(tex);

            vkDestroyBuffer(window_->getDevice(), stagingBuffer, nullptr);
            vkFreeMemory(window_->getDevice(), stagingMemory, nullptr);
            SDL_DestroySurface(surface);
        }
    }

    void VKAbstractModel::loadTexturesFromMTL(const std::string &textureBasePath) {
        for (const MXMaterial &material : obj_.materials()) {
            if (material.map_kd.empty()) {
                continue;
            }

            const std::string path = textureBasePath.empty() ? material.map_kd : (textureBasePath + "/" + material.map_kd);
            SDL_Surface *surface = mxvk::LoadPNG(path.c_str());
            if (surface == nullptr) {
                continue;
            }

            const uint32_t width = static_cast<uint32_t>(surface->w);
            const uint32_t height = static_cast<uint32_t>(surface->h);
            const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4U;

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer, stagingMemory);

            void *mapped = nullptr;
            vkMapMemory(window_->getDevice(), stagingMemory, 0, imageSize, 0, &mapped);
            std::memcpy(mapped, surface->pixels, static_cast<size_t>(imageSize));
            vkUnmapMemory(window_->getDevice(), stagingMemory);

            TextureEntry tex{};
            tex.width = width;
            tex.height = height;
            createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        tex.image, tex.memory);

            transitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copyBufferToImage(stagingBuffer, tex.image, width, height);
            transitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            tex.view = createImageView(tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
            textures_.push_back(tex);

            vkDestroyBuffer(window_->getDevice(), stagingBuffer, nullptr);
            vkFreeMemory(window_->getDevice(), stagingMemory, nullptr);
            SDL_DestroySurface(surface);
        }
    }

    void VKAbstractModel::createFallbackTexture() {
        SDL_Surface *surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            throw mxvk::Exception("VKAbstractModel failed to allocate fallback texture surface");
        }

        auto *pixel = static_cast<uint32_t *>(surface->pixels);
        *pixel = 0xFFFFFFFFu;

        const VkDeviceSize imageSize = 4;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);

        void *mapped = nullptr;
        vkMapMemory(window_->getDevice(), stagingMemory, 0, imageSize, 0, &mapped);
        std::memcpy(mapped, surface->pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(window_->getDevice(), stagingMemory);

        TextureEntry tex{};
        tex.width = 1;
        tex.height = 1;
        createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    tex.image, tex.memory);

        transitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, tex.image, 1, 1);
        transitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        tex.view = createImageView(tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
        textures_.push_back(tex);

        vkDestroyBuffer(window_->getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(window_->getDevice(), stagingMemory, nullptr);
        SDL_DestroySurface(surface);
    }

    void VKAbstractModel::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkMemoryPropertyFlags properties, VkBuffer &buffer,
                                       VkDeviceMemory &bufferMemory) const {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(window_->getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to create buffer");
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(window_->getDevice(), buffer, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);

        if (vkAllocateMemory(window_->getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            vkDestroyBuffer(window_->getDevice(), buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            throw mxvk::Exception("VKAbstractModel failed to allocate buffer memory");
        }

        if (vkBindBufferMemory(window_->getDevice(), buffer, bufferMemory, 0) != VK_SUCCESS) {
            vkDestroyBuffer(window_->getDevice(), buffer, nullptr);
            vkFreeMemory(window_->getDevice(), bufferMemory, nullptr);
            buffer = VK_NULL_HANDLE;
            bufferMemory = VK_NULL_HANDLE;
            throw mxvk::Exception("VKAbstractModel failed to bind buffer memory");
        }
    }

    uint32_t VKAbstractModel::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProperties{};
        vkGetPhysicalDeviceMemoryProperties(window_->getPhysicalDevice(), &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            const bool typeSupported = (typeFilter & (1u << i)) != 0u;
            const bool propsSupported =
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties;
            if (typeSupported && propsSupported) {
                return i;
            }
        }

        throw mxvk::Exception("VKAbstractModel failed to find suitable memory type");
    }

    VkCommandBuffer VKAbstractModel::beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = window_->getCommandPool();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(window_->getDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to allocate command buffer");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            vkFreeCommandBuffers(window_->getDevice(), window_->getCommandPool(), 1, &commandBuffer);
            throw mxvk::Exception("VKAbstractModel failed to begin command buffer");
        }

        return commandBuffer;
    }

    void VKAbstractModel::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            vkFreeCommandBuffers(window_->getDevice(), window_->getCommandPool(), 1, &commandBuffer);
            throw mxvk::Exception("VKAbstractModel failed to end command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(window_->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(window_->getDevice(), window_->getCommandPool(), 1, &commandBuffer);
            throw mxvk::Exception("VKAbstractModel failed to submit command buffer");
        }
        if (vkQueueWaitIdle(window_->getGraphicsQueue()) != VK_SUCCESS) {
            vkFreeCommandBuffers(window_->getDevice(), window_->getCommandPool(), 1, &commandBuffer);
            throw mxvk::Exception("VKAbstractModel failed to wait for queue idle");
        }

        vkFreeCommandBuffers(window_->getDevice(), window_->getCommandPool(), 1, &commandBuffer);
    }

    void VKAbstractModel::createImage(uint32_t width, uint32_t height, VkFormat format,
                                      VkImageTiling tiling, VkImageUsageFlags usage,
                                      VkMemoryPropertyFlags properties, VkImage &image,
                                      VkDeviceMemory &memory) const {
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
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(window_->getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to create image");
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(window_->getDevice(), image, &requirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);

        if (vkAllocateMemory(window_->getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyImage(window_->getDevice(), image, nullptr);
            image = VK_NULL_HANDLE;
            throw mxvk::Exception("VKAbstractModel failed to allocate image memory");
        }

        if (vkBindImageMemory(window_->getDevice(), image, memory, 0) != VK_SUCCESS) {
            vkDestroyImage(window_->getDevice(), image, nullptr);
            vkFreeMemory(window_->getDevice(), memory, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            throw mxvk::Exception("VKAbstractModel failed to bind image memory");
        }
    }

    VkImageView VKAbstractModel::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        if (vkCreateImageView(window_->getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to create image view");
        }
        return imageView;
    }

    void VKAbstractModel::transitionImageLayout(VkImage image, VkFormat, VkImageLayout oldLayout, VkImageLayout newLayout) const {
        VkCommandBuffer cmd = beginSingleTimeCommands();

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

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

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
        }

        vkCmdPipelineBarrier(cmd,
                             sourceStage,
                             destinationStage,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);

        endSingleTimeCommands(cmd);
    }

    void VKAbstractModel::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
        VkCommandBuffer cmd = beginSingleTimeCommands();

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

        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(cmd);
    }

    void VKAbstractModel::createTextureSampler() {
        if (textureSampler_ != VK_NULL_HANDLE) {
            return;
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(window_->getDevice(), &samplerInfo, nullptr, &textureSampler_) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to create texture sampler");
        }
    }

    void VKAbstractModel::createDescriptorSetLayout() {
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            return;
        }

        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = 0;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 1;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {samplerBinding, uboBinding};

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(window_->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to create descriptor set layout");
        }
    }

    void VKAbstractModel::createUniformBuffers() {
        destroyUniformBuffers();

        const size_t frameCount = window_->getSwapchainImageCount();
        if (frameCount == 0) {
            return;
        }

        uniformBuffers_.resize(frameCount, VK_NULL_HANDLE);
        uniformBufferMemory_.resize(frameCount, VK_NULL_HANDLE);
        uniformBuffersMapped_.resize(frameCount, nullptr);

        for (size_t i = 0; i < frameCount; ++i) {
            createBuffer(sizeof(UniformBufferObject),
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         uniformBuffers_[i], uniformBufferMemory_[i]);
            vkMapMemory(window_->getDevice(), uniformBufferMemory_[i], 0, sizeof(UniformBufferObject), 0, &uniformBuffersMapped_[i]);
        }
    }

    void VKAbstractModel::destroyUniformBuffers() {
        if (window_ == nullptr || window_->getDevice() == VK_NULL_HANDLE) {
            uniformBuffers_.clear();
            uniformBufferMemory_.clear();
            uniformBuffersMapped_.clear();
            return;
        }

        for (size_t i = 0; i < uniformBuffers_.size(); ++i) {
            if (uniformBuffersMapped_[i] != nullptr) {
                vkUnmapMemory(window_->getDevice(), uniformBufferMemory_[i]);
                uniformBuffersMapped_[i] = nullptr;
            }
            if (uniformBuffers_[i] != VK_NULL_HANDLE) {
                vkDestroyBuffer(window_->getDevice(), uniformBuffers_[i], nullptr);
            }
            if (uniformBufferMemory_[i] != VK_NULL_HANDLE) {
                vkFreeMemory(window_->getDevice(), uniformBufferMemory_[i], nullptr);
            }
        }

        uniformBuffers_.clear();
        uniformBufferMemory_.clear();
        uniformBuffersMapped_.clear();
    }

    void VKAbstractModel::createDescriptorPool() {
        const uint32_t textureCount = std::max<uint32_t>(1U, static_cast<uint32_t>(textures_.size()));
        const uint32_t frameCount = static_cast<uint32_t>(window_->getSwapchainImageCount());
        const uint32_t setCount = textureCount * frameCount;

        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = setCount;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = setCount;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = setCount;

        if (vkCreateDescriptorPool(window_->getDevice(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to create descriptor pool");
        }
    }

    void VKAbstractModel::createDescriptorSets() {
        const size_t textureCount = std::max<size_t>(1, textures_.size());
        const size_t frameCount = window_->getSwapchainImageCount();
        const size_t setCount = textureCount * frameCount;

        std::vector<VkDescriptorSetLayout> layouts(setCount, descriptorSetLayout_);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(setCount);
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets_.resize(setCount, VK_NULL_HANDLE);
        if (vkAllocateDescriptorSets(window_->getDevice(), &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
            throw mxvk::Exception("VKAbstractModel failed to allocate descriptor sets");
        }

        for (size_t frame = 0; frame < frameCount; ++frame) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[frame];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            for (size_t tex = 0; tex < textureCount; ++tex) {
                const size_t setIndex = frame * textureCount + tex;
                const TextureEntry &entry = textures_[tex];

                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = entry.view;
                imageInfo.sampler = textureSampler_;

                std::array<VkWriteDescriptorSet, 2> writes{};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = descriptorSets_[setIndex];
                writes[0].dstBinding = 0;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[0].descriptorCount = 1;
                writes[0].pImageInfo = &imageInfo;

                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet = descriptorSets_[setIndex];
                writes[1].dstBinding = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[1].descriptorCount = 1;
                writes[1].pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(window_->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }
    }

    void VKAbstractModel::createPipelines() {
        destroyPipelines();

        if (window_ == nullptr || window_->getDevice() == VK_NULL_HANDLE) {
            return;
        }
        if (descriptorSetLayout_ == VK_NULL_HANDLE) {
            return;
        }
        if (vertexShaderPath_.empty() || fragmentShaderPath_.empty()) {
            return;
        }
        if (window_->getSwapchainFormat() == VK_FORMAT_UNDEFINED) {
            return;
        }

        const std::vector<char> vertBytes = readBinaryFile(vertexShaderPath_);
        const std::vector<char> fragBytes = readBinaryFile(fragmentShaderPath_);

        const VkShaderModule vertModule = createShaderModule(window_->getDevice(), vertBytes);
        VkShaderModule fragModule = VK_NULL_HANDLE;

        try {
            fragModule = createShaderModule(window_->getDevice(), fragBytes);

            VkPipelineShaderStageCreateInfo vertStage{};
            vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertStage.module = vertModule;
            vertStage.pName = "main";

            VkPipelineShaderStageCreateInfo fragStage{};
            fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragStage.module = fragModule;
            fragStage.pName = "main";

            const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(VKVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 3> attrs{};
            attrs[0].binding = 0;
            attrs[0].location = 0;
            attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrs[0].offset = offsetof(VKVertex, pos);
            attrs[1].binding = 0;
            attrs[1].location = 1;
            attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
            attrs[1].offset = offsetof(VKVertex, texCoord);
            attrs[2].binding = 0;
            attrs[2].location = 2;
            attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrs[2].offset = offsetof(VKVertex, normal);

            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
            vertexInput.pVertexAttributeDescriptions = attrs.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            const std::array<VkDynamicState, 2> dynamicStates = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
            VkPipelineDynamicStateCreateInfo dynamicInfo{};
            dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
            dynamicInfo.pDynamicStates = dynamicStates.data();

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisample.sampleShadingEnable = VK_FALSE;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;
            blendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.logicOpEnable = VK_FALSE;
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments = &blendAttachment;

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &descriptorSetLayout_;

            if (vkCreatePipelineLayout(window_->getDevice(), &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
                throw mxvk::Exception("VKAbstractModel failed to create pipeline layout");
            }

            const VkFormat colorFormat = window_->getSwapchainFormat();
            const VkFormat depthFormat = window_->getDepthFormat();
            VkPipelineRenderingCreateInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats = &colorFormat;
            if (depthFormat != VK_FORMAT_UNDEFINED) {
                renderingInfo.depthAttachmentFormat = depthFormat;
            }

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.pNext = &renderingInfo;
            pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisample;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlend;
            pipelineInfo.pDynamicState = &dynamicInfo;
            pipelineInfo.layout = pipelineLayout_;
            pipelineInfo.renderPass = VK_NULL_HANDLE;
            pipelineInfo.subpass = 0;

            if (vkCreateGraphicsPipelines(window_->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelineFill_) != VK_SUCCESS) {
                throw mxvk::Exception("VKAbstractModel failed to create fill pipeline");
            }

            VkPhysicalDeviceFeatures features{};
            vkGetPhysicalDeviceFeatures(window_->getPhysicalDevice(), &features);
            if (features.fillModeNonSolid == VK_TRUE) {
                rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
                if (vkCreateGraphicsPipelines(window_->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipelineWireframe_) != VK_SUCCESS) {
                    pipelineWireframe_ = VK_NULL_HANDLE;
                }
            }
        } catch (...) {
            if (fragModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(window_->getDevice(), fragModule, nullptr);
            }
            vkDestroyShaderModule(window_->getDevice(), vertModule, nullptr);
            throw;
        }

        vkDestroyShaderModule(window_->getDevice(), fragModule, nullptr);
        vkDestroyShaderModule(window_->getDevice(), vertModule, nullptr);
    }

    void VKAbstractModel::destroyPipelines() {
        if (window_ == nullptr || window_->getDevice() == VK_NULL_HANDLE) {
            pipelineFill_ = VK_NULL_HANDLE;
            pipelineWireframe_ = VK_NULL_HANDLE;
            pipelineLayout_ = VK_NULL_HANDLE;
            return;
        }

        if (pipelineFill_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(window_->getDevice(), pipelineFill_, nullptr);
            pipelineFill_ = VK_NULL_HANDLE;
        }
        if (pipelineWireframe_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(window_->getDevice(), pipelineWireframe_, nullptr);
            pipelineWireframe_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(window_->getDevice(), pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    void VKAbstractModel::destroyDescriptors() {
        if (window_ == nullptr || window_->getDevice() == VK_NULL_HANDLE) {
            descriptorSets_.clear();
            descriptorPool_ = VK_NULL_HANDLE;
            descriptorSetLayout_ = VK_NULL_HANDLE;
            destroyUniformBuffers();
            return;
        }

        descriptorSets_.clear();
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(window_->getDevice(), descriptorPool_, nullptr);
            descriptorPool_ = VK_NULL_HANDLE;
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(window_->getDevice(), descriptorSetLayout_, nullptr);
            descriptorSetLayout_ = VK_NULL_HANDLE;
        }

        destroyUniformBuffers();
    }

    void VKAbstractModel::destroyTextures() {
        if (window_ == nullptr || window_->getDevice() == VK_NULL_HANDLE) {
            textures_.clear();
            textureSampler_ = VK_NULL_HANDLE;
            return;
        }

        for (const TextureEntry &tex : textures_) {
            if (tex.view != VK_NULL_HANDLE) {
                vkDestroyImageView(window_->getDevice(), tex.view, nullptr);
            }
            if (tex.image != VK_NULL_HANDLE) {
                vkDestroyImage(window_->getDevice(), tex.image, nullptr);
            }
            if (tex.memory != VK_NULL_HANDLE) {
                vkFreeMemory(window_->getDevice(), tex.memory, nullptr);
            }
        }
        textures_.clear();

        if (textureSampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(window_->getDevice(), textureSampler_, nullptr);
            textureSampler_ = VK_NULL_HANDLE;
        }
    }

} // namespace mxvk
