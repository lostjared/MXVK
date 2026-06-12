#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#ifndef compute_shader_ASSET_DIR
#define compute_shader_ASSET_DIR "."
#endif

static constexpr int HISTORY_SIZE = 8;

struct ComputePC {
    int32_t mode;
    int32_t historyCount;
    int32_t historyIdx;
    int32_t square_size;
    int32_t history_dir;
    float alpha;
    int32_t do_swap;
    int32_t do_invert;
};

class ComputeWindow : public mxvk::VK_Window {
  public:
    explicit ComputeWindow(const Arguments &args)
        : mxvk::VK_Window("-[ VK Compute CV ]-", args.width, args.height, args.fullscreen, MXVK_VALIDATION),
          assetRoot_(args.path.empty() ? std::string(compute_shader_ASSET_DIR) : args.path),
          cameraIndex_(args.camera_index) {
        initComputeResources();
    }

    ~ComputeWindow() override {
        capture_.close();
        destroyComputeResources();
    }

    void proc() override {
        cv::Mat frame;
        if (capture_.readRgba(frame) && !frame.empty()) {
            if (frame.cols == texWidth_ && frame.rows == texHeight_) {
                uploadToImage(frame.ptr(), workImg_[0]);
                tickAnimState();
                runComputeFrame();
            }
        }

        if (displaySprite_ != nullptr) {
            const int width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : texWidth_;
            const int height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : texHeight_;
            displaySprite_->drawSpriteRect(0, 0, width, height);
        }
    }

    void event(SDL_Event &e) override {
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) {
            exit();
            return;
        }

        if (e.type == SDL_EVENT_KEY_DOWN && !spvFiles_.empty()) {
            if (e.key.key == SDLK_UP) {
                currentSpvIndex_ =
                    (currentSpvIndex_ - 1 + static_cast<int>(spvFiles_.size())) % static_cast<int>(spvFiles_.size());
                std::cout << "Current index: " << spvFiles_[currentSpvIndex_] << "\n";
                reloadPipeline();
            } else if (e.key.key == SDLK_DOWN) {
                currentSpvIndex_ = (currentSpvIndex_ + 1) % static_cast<int>(spvFiles_.size());
                std::cout << "Current index: " << spvFiles_[currentSpvIndex_] << "\n";
                reloadPipeline();
            }
        }
    }

  private:
    struct ComputeImage {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    std::string assetRoot_;
    mxvk::VK_Capture capture_{};
    int cameraIndex_ = 0;
    int texWidth_ = 1920;
    int texHeight_ = 1080;

    std::array<ComputeImage, 2> workImg_{};
    std::array<ComputeImage, HISTORY_SIZE> histImg_{};
    ComputeImage outImg_{};

    VkSampler computeSampler_ = VK_NULL_HANDLE;

    VkBuffer stagingBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem_ = VK_NULL_HANDLE;
    VkBuffer readbackBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem_ = VK_NULL_HANDLE;
    void *readbackMap_ = nullptr;

    VkDescriptorSetLayout compDSLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout compPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline compPipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool compDSPool_ = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, 2> blurDS_{};
    std::array<VkDescriptorSet, 2> blendDS_{};

    mxvk::VK_Sprite *displaySprite_ = nullptr;

    int historyIndex_ = 0;
    int historyCount_ = 0;
    int currentSquare_ = 4;
    int squareDir_ = 1;
    int currentHistIdx_ = 0;
    int currentDir_ = 1;
    float alpha_ = 1.0F;

    std::vector<std::string> spvFiles_{};
    int currentSpvIndex_ = 0;

    void loadSPV() {
        std::ifstream file(assetRoot_ + "/index.txt");
        if (!file.is_open()) {
            throw mxvk::Exception("Cannot open: " + assetRoot_ + "/index.txt");
        }

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                spvFiles_.push_back(line);
            }
        }

        if (spvFiles_.empty()) {
            throw mxvk::Exception("index.txt contains no entries");
        }
    }

    void initComputeResources() {
        try {
            if (device == VK_NULL_HANDLE) {
                throw mxvk::Exception("Compute resources require an initialized Vulkan device");
            }
            if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
                createDevice();
            }

            if (!capture_.open(cameraIndex_)) {
                throw mxvk::Exception("Failed to open camera " + std::to_string(cameraIndex_));
            }

            capture_.set(cv::CAP_PROP_FRAME_WIDTH, 1920.0);
            capture_.set(cv::CAP_PROP_FRAME_HEIGHT, 1080.0);
            capture_.set(cv::CAP_PROP_FPS, 60.0);

            cv::Mat frame;
            if (!capture_.read(frame) || frame.empty()) {
                throw mxvk::Exception("Failed to read initial camera frame");
            }

            texWidth_ = frame.cols;
            texHeight_ = frame.rows;

            const VkDeviceSize imgBytes = static_cast<VkDeviceSize>(texWidth_) * texHeight_ * 4;

            createBuffer(
                imgBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuf_,
                stagingMem_);

            createBuffer(
                imgBytes,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                readbackBuf_,
                readbackMem_);
            VK_CHECK_RESULT(vkMapMemory(device, readbackMem_, 0, imgBytes, 0, &readbackMap_));

            {
                const VkCommandBuffer cmd = beginSingleTimeCommands();
                allocCImg(workImg_[0], cmd);
                allocCImg(workImg_[1], cmd);
                for (ComputeImage &img : histImg_) {
                    allocCImg(img, cmd);
                }
                allocCImg(outImg_, cmd);
                endSingleTimeCommands(cmd);
            }

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.maxAnisotropy = 1.0F;
            VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &computeSampler_));

            loadSPV();
            buildDescriptorSetLayout();
            buildComputePipeline();
            buildDescriptorSets();

            displaySprite_ = createSprite(texWidth_, texHeight_);
        } catch (...) {
            // Constructor failure bypasses ~ComputeWindow; cleanup partial Vulkan state here.
            capture_.close();
            destroyComputeResources();
            throw;
        }
    }

    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProperties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);

        for (uint32_t index = 0; index < memProperties.memoryTypeCount; ++index) {
            const bool typeMatches = (typeFilter & (1U << index)) != 0U;
            const bool propertyMatches =
                (memProperties.memoryTypes[index].propertyFlags & properties) == properties;
            if (typeMatches && propertyMatches) {
                return index;
            }
        }

        throw mxvk::Exception("Failed to find suitable memory type");
    }

    void createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer &buffer,
                      VkDeviceMemory &bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, buffer, bufferMemory, 0));
    }

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = command_pool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkCommandBufferSubmitInfo commandBufferInfo{};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferInfo.commandBuffer = commandBuffer;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;

        VK_CHECK_RESULT(vkQueueSubmit2(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));
        vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
    }

    void createImage(uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage &image,
                     VkDeviceMemory &imageMemory) {
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

        VkMemoryRequirements memRequirements{};
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, image, imageMemory, 0));
    }

    [[nodiscard]] VkImageView createImageView(VkImage image,
                                              VkFormat format,
                                              VkImageAspectFlags aspectFlags) const {
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
        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    void allocCImg(ComputeImage &img, VkCommandBuffer cmd) {
        createImage(
            static_cast<uint32_t>(texWidth_),
            static_cast<uint32_t>(texHeight_),
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            img.image,
            img.memory);
        img.view = createImageView(img.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

        transitionImageLayout(
            cmd,
            img.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_NONE,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }

    void reloadPipeline() {
        vkDeviceWaitIdle(device);
        if (compPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, compPipeline_, nullptr);
            compPipeline_ = VK_NULL_HANDLE;
        }
        if (compPipeLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, compPipeLayout_, nullptr);
            compPipeLayout_ = VK_NULL_HANDLE;
        }
        buildComputePipeline();
    }

    void buildDescriptorSetLayout() {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = HISTORY_SIZE;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &compDSLayout_));
    }

    void buildComputePipeline() {
        const std::string spvPath = assetRoot_ + "/" + spvFiles_[currentSpvIndex_];
        std::ifstream spvFile(spvPath, std::ios::binary | std::ios::ate);
        if (!spvFile.is_open()) {
            throw mxvk::Exception("Cannot open compute SPIR-V: " + spvPath);
        }

        const size_t size = static_cast<size_t>(spvFile.tellg());
        std::vector<char> spv(size);
        spvFile.seekg(0);
        spvFile.read(spv.data(), static_cast<std::streamsize>(size));

        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = spv.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t *>(spv.data());
        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(ComputePC);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &compDSLayout_;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &compPipeLayout_));

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = compPipeLayout_;
        VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compPipeline_));

        vkDestroyShaderModule(device, module, nullptr);
    }

    void buildDescriptorSets() {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4};
        poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * (1 + HISTORY_SIZE)};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 4;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &compDSPool_));

        std::array<VkDescriptorSetLayout, 4> layouts{};
        layouts.fill(compDSLayout_);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = compDSPool_;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        std::array<VkDescriptorSet, 4> raw{};
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, raw.data()));
        blurDS_[0] = raw[0];
        blurDS_[1] = raw[1];
        blendDS_[0] = raw[2];
        blendDS_[1] = raw[3];

        writeBlurDS(blurDS_[0], workImg_[0].view, workImg_[1].view);
        writeBlurDS(blurDS_[1], workImg_[1].view, workImg_[0].view);
        writeBlendDS(blendDS_[0], workImg_[0].view);
        writeBlendDS(blendDS_[1], workImg_[1].view);
    }

    void writeBlurDS(VkDescriptorSet descriptorSet, VkImageView destView, VkImageView srcView) {
        VkDescriptorImageInfo destInfo{VK_NULL_HANDLE, destView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo srcInfo{computeSampler_, srcView, VK_IMAGE_LAYOUT_GENERAL};
        std::vector<VkDescriptorImageInfo> historyInfos(
            HISTORY_SIZE,
            VkDescriptorImageInfo{computeSampler_, srcView, VK_IMAGE_LAYOUT_GENERAL});

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &srcInfo, nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, HISTORY_SIZE,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, historyInfos.data(), nullptr, nullptr};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void writeBlendDS(VkDescriptorSet descriptorSet, VkImageView srcView) {
        VkDescriptorImageInfo destInfo{VK_NULL_HANDLE, outImg_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo srcInfo{computeSampler_, srcView, VK_IMAGE_LAYOUT_GENERAL};

        std::vector<VkDescriptorImageInfo> historyInfos(HISTORY_SIZE);
        for (int index = 0; index < HISTORY_SIZE; ++index) {
            historyInfos[index] = {computeSampler_, histImg_[index].view, VK_IMAGE_LAYOUT_GENERAL};
        }

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &srcInfo, nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, HISTORY_SIZE,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, historyInfos.data(), nullptr, nullptr};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void uploadToImage(const void *data, ComputeImage &img) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(texWidth_) * texHeight_ * 4;
        void *mapped = nullptr;
        VK_CHECK_RESULT(vkMapMemory(device, stagingMem_, 0, bytes, 0, &mapped));
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        vkUnmapMemory(device, stagingMem_);

        const VkCommandBuffer cmd = beginSingleTimeCommands();

        transitionImageLayout(
            cmd,
            img.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(texWidth_), static_cast<uint32_t>(texHeight_), 1};

        VkCopyBufferToImageInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
        copyInfo.srcBuffer = stagingBuf_;
        copyInfo.dstImage = img.image;
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        vkCmdCopyBufferToImage2(cmd, &copyInfo);

        transitionImageLayout(
            cmd,
            img.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

        endSingleTimeCommands(cmd);
    }

    void dispatchOne(VkCommandBuffer cmd, VkDescriptorSet descriptorSet, int mode) {
        ComputePC pc{};
        pc.mode = mode;
        pc.historyCount = historyCount_;
        pc.historyIdx = currentHistIdx_;
        pc.square_size = currentSquare_;
        pc.history_dir = currentDir_;
        pc.alpha = alpha_;
        pc.do_invert = 0;
        pc.do_swap = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipeline_);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            compPipeLayout_,
            0,
            1,
            &descriptorSet,
            0,
            nullptr);
        vkCmdPushConstants(cmd, compPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        const uint32_t groupX = (static_cast<uint32_t>(texWidth_) + 15) / 16;
        const uint32_t groupY = (static_cast<uint32_t>(texHeight_) + 15) / 16;
        vkCmdDispatch(cmd, groupX, groupY, 1);
    }

    void computeBarrier(VkCommandBuffer cmd, VkImage img) const {
        transitionImageLayout(
            cmd,
            img,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }

    void runComputeFrame() {
        const VkCommandBuffer cmd = beginSingleTimeCommands();
        int srcIdx = 0;
        int dstIdx = 1;
        const int passes = 3 + (std::rand() % 7);
        for (int pass = 0; pass < passes; ++pass) {
            dispatchOne(cmd, blurDS_[dstIdx], 0);
            computeBarrier(cmd, workImg_[dstIdx].image);
            std::swap(srcIdx, dstIdx);
        }

        {
            std::array<VkImageMemoryBarrier2, 2> barriers{};
            barriers[0] = makeImageBarrier(
                workImg_[srcIdx].image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);

            barriers[1] = makeImageBarrier(
                histImg_[historyIndex_].image,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);

            VkDependencyInfo dependencyInfo{};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dependencyInfo.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);

            VkImageCopy2 copy{};
            copy.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.extent = {static_cast<uint32_t>(texWidth_), static_cast<uint32_t>(texHeight_), 1};

            VkCopyImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
            copyInfo.srcImage = workImg_[srcIdx].image;
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstImage = histImg_[historyIndex_].image;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &copy;
            vkCmdCopyImage2(cmd, &copyInfo);

            barriers[0] = makeImageBarrier(
                workImg_[srcIdx].image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

            barriers[1] = makeImageBarrier(
                histImg_[historyIndex_].image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT);

            dependencyInfo = {};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dependencyInfo.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &dependencyInfo);
        }

        if (historyCount_ < HISTORY_SIZE) {
            ++historyCount_;
        }
        historyIndex_ = (historyIndex_ + 1) % HISTORY_SIZE;

        const bool isMetalMedian =
            !spvFiles_.empty() && spvFiles_[currentSpvIndex_].find("metalmedianblend") != std::string::npos;
        dispatchOne(cmd, blendDS_[srcIdx], isMetalMedian ? 2 : 1);

        transitionImageLayout(
            cmd,
            outImg_.image,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);

        VkBufferImageCopy2 region{};
        region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(texWidth_), static_cast<uint32_t>(texHeight_), 1};

        VkCopyImageToBufferInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2;
        copyInfo.srcImage = outImg_.image;
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstBuffer = readbackBuf_;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;
        vkCmdCopyImageToBuffer2(cmd, &copyInfo);

        transitionImageLayout(
            cmd,
            outImg_.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT);

        endSingleTimeCommands(cmd);

        if (displaySprite_ != nullptr && readbackMap_ != nullptr) {
            displaySprite_->updateTexture(readbackMap_, texWidth_, texHeight_, texWidth_ * 4);
        }
    }

    void tickAnimState() {
        if (currentDir_ == 1) {
            if (++currentHistIdx_ >= HISTORY_SIZE - 1) {
                currentHistIdx_ = HISTORY_SIZE - 1;
                currentDir_ = -1;
            }
        } else if (--currentHistIdx_ <= 0) {
            currentHistIdx_ = 0;
            currentDir_ = 1;
        }

        if (squareDir_ == 1) {
            currentSquare_ += 2;
            if (currentSquare_ >= 64) {
                currentSquare_ = 64;
                squareDir_ = 0;
            }
        } else {
            currentSquare_ -= 2;
            if (currentSquare_ <= 2) {
                currentSquare_ = 2;
                squareDir_ = 1;
            }
        }

        static int alphaDir = 1;
        if (alphaDir == 1) {
            alpha_ += 0.005F;
            if (alpha_ >= (255.0F / 32.0F)) {
                alpha_ = 255.0F / 32.0F;
                alphaDir = -1;
            }
        } else {
            alpha_ -= 0.005F;
            if (alpha_ <= 1.0F) {
                alpha_ = 1.0F;
                alphaDir = 1;
            }
        }
    }

    static VkImageMemoryBarrier2 makeImageBarrier(VkImage img,
                                                  VkImageLayout oldLayout,
                                                  VkImageLayout newLayout,
                                                  VkPipelineStageFlags2 srcStage,
                                                  VkAccessFlags2 srcAccess,
                                                  VkPipelineStageFlags2 dstStage,
                                                  VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = img;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        return barrier;
    }

    static void transitionImageLayout(VkCommandBuffer cmd,
                                      VkImage img,
                                      VkImageLayout oldLayout,
                                      VkImageLayout newLayout,
                                      VkPipelineStageFlags2 srcStage,
                                      VkAccessFlags2 srcAccess,
                                      VkPipelineStageFlags2 dstStage,
                                      VkAccessFlags2 dstAccess) {
        const VkImageMemoryBarrier2 barrier = makeImageBarrier(
            img,
            oldLayout,
            newLayout,
            srcStage,
            srcAccess,
            dstStage,
            dstAccess);

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    void destroyComputeResources() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        vkDeviceWaitIdle(device);

        if (readbackMap_ != nullptr) {
            vkUnmapMemory(device, readbackMem_);
            readbackMap_ = nullptr;
        }

        auto destroyImage = [&](ComputeImage &img) {
            if (img.view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, img.view, nullptr);
            }
            if (img.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, img.image, nullptr);
            }
            if (img.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, img.memory, nullptr);
            }
            img = {};
        };

        destroyImage(workImg_[0]);
        destroyImage(workImg_[1]);
        for (ComputeImage &img : histImg_) {
            destroyImage(img);
        }
        destroyImage(outImg_);

        if (computeSampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device, computeSampler_, nullptr);
            computeSampler_ = VK_NULL_HANDLE;
        }
        if (compDSPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, compDSPool_, nullptr);
            compDSPool_ = VK_NULL_HANDLE;
        }
        if (compPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, compPipeline_, nullptr);
            compPipeline_ = VK_NULL_HANDLE;
        }
        if (compPipeLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, compPipeLayout_, nullptr);
            compPipeLayout_ = VK_NULL_HANDLE;
        }
        if (compDSLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, compDSLayout_, nullptr);
            compDSLayout_ = VK_NULL_HANDLE;
        }
        if (stagingBuf_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, stagingBuf_, nullptr);
            stagingBuf_ = VK_NULL_HANDLE;
        }
        if (stagingMem_ != VK_NULL_HANDLE) {
            vkFreeMemory(device, stagingMem_, nullptr);
            stagingMem_ = VK_NULL_HANDLE;
        }
        if (readbackBuf_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, readbackBuf_, nullptr);
            readbackBuf_ = VK_NULL_HANDLE;
        }
        if (readbackMem_ != VK_NULL_HANDLE) {
            vkFreeMemory(device, readbackMem_, nullptr);
            readbackMem_ = VK_NULL_HANDLE;
        }
    }
};

int main(int argc, char **argv) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    try {
        Arguments args = proc_args(argc, argv);
        if (args.path == ".") {
            args.path = compute_shader_ASSET_DIR;
        }

        ComputeWindow window(args);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << "mxvk: Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
