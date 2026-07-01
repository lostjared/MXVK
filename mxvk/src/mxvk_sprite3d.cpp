#include "mxvk/mxvk_sprite3d.hpp"

#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

#ifndef VK_CHECK_RESULT
#define VK_CHECK_RESULT(f)                                                                                                                \
    {                                                                                                                                     \
        VkResult res = (f);                                                                                                               \
        if (res != VK_SUCCESS) {                                                                                                          \
            throw mxvk::Exception(std::format("Fatal : VkResult is \"{}\" in {} at line {}", static_cast<int>(res), __FILE__, __LINE__)); \
        }                                                                                                                                 \
    }
#endif

namespace mxvk {

    VK_Sprite3D::~VK_Sprite3D() {
        cleanup();
    }

    void VK_Sprite3D::load(VK_Window *window,
                           const std::string &pngPath,
                           const std::string &vertexPath,
                           const std::string &fragmentPath) {
        SDL_Surface *surface = mxvk::LoadPNG(pngPath.c_str());
        if (surface == nullptr) {
            throw mxvk::Exception("Failed to load 3D sprite image: " + pngPath);
        }
        load(window, surface, vertexPath, fragmentPath);
        SDL_DestroySurface(surface);
        std::cout << std::format("mxvk: Loaded 3D sprite PNG: {}\n", pngPath);
    }

    void VK_Sprite3D::load(VK_Window *window,
                           SDL_Surface *surface,
                           const std::string &vertexPath,
                           const std::string &fragmentPath) {
        if (window == nullptr) {
            throw mxvk::Exception("VK_Sprite3D::load called with null window");
        }
        if (surface == nullptr) {
            throw mxvk::Exception("VK_Sprite3D::load called with null surface");
        }

        cleanup();

        device = window->getDevice();
        physicalDevice = window->getPhysicalDevice();
        graphicsQueue = window->getGraphicsQueue();
        commandPool = window->getCommandPool();
        pipelineCache = window->getPipelineCache();
        colorAttachmentFormat = window->getSwapchainFormat();
        depthAttachmentFormat = window->getDepthFormat();
        imageCount = window->getSwapchainImageCount();
        vertexShaderPath = vertexPath.empty() ? (std::filesystem::path(MXVK_SPRITE3D_SHADER_DIR) / "sprite3d.vert.spv").string() : vertexPath;
        fragmentShaderPath = fragmentPath.empty() ? (std::filesystem::path(MXVK_SPRITE3D_SHADER_DIR) / "sprite3d.frag.spv").string() : fragmentPath;

        if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE || commandPool == VK_NULL_HANDLE) {
            throw mxvk::Exception("Cannot create 3D sprite before Vulkan render resources are available");
        }
        if (colorAttachmentFormat == VK_FORMAT_UNDEFINED || imageCount == 0) {
            throw mxvk::Exception("Cannot create 3D sprite before swapchain resources are available");
        }

        createTexture(surface);
        createSampler();
        createQuadBuffers();
        createDescriptorSetLayout();
        createCameraBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createPipeline();
        spriteLoaded = true;
        std::cout << std::format("mxvk: Created 3D sprite: {}x{}\n", spriteWidth, spriteHeight);
    }

    void VK_Sprite3D::updateCamera(uint32_t imageIndex, const glm::mat4 &view, const glm::mat4 &proj) {
        if (imageIndex >= cameraBuffersMapped.size() || cameraBuffersMapped[imageIndex] == nullptr) {
            return;
        }

        CameraUBO camera{};
        camera.view = view;
        camera.proj = proj;
        std::memcpy(cameraBuffersMapped[imageIndex], &camera, sizeof(camera));
    }

    void VK_Sprite3D::drawSprite(const glm::vec3 &position,
                                 const glm::vec2 &size,
                                 const glm::vec4 &color,
                                 float rotationRadians) {
        if (!spriteLoaded) {
            throw mxvk::Exception("VK_Sprite3D::drawSprite called before sprite was loaded");
        }
        if (size.x <= 0.0f || size.y <= 0.0f) {
            return;
        }
        drawQueue.push_back({position, size, color, rotationRadians});
    }

    void VK_Sprite3D::render(VkCommandBuffer cmd, uint32_t imageIndex) {
        if (!spriteLoaded || drawQueue.empty() || imageIndex >= descriptorSets.size()) {
            return;
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                0, 1, &descriptorSets[imageIndex], 0, nullptr);

        VkBuffer vertexBuffers[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

        struct PushConstants {
            glm::vec4 positionSizeX;
            glm::vec4 color;
            glm::vec4 sizeYRotationAlpha;
        };

        for (const DrawCmd &draw : drawQueue) {
            PushConstants pc{};
            pc.positionSizeX = glm::vec4(draw.position, draw.size.x);
            pc.color = draw.color;
            pc.sizeYRotationAlpha = glm::vec4(draw.size.y, draw.rotationRadians, alphaDiscardThreshold, 0.0f);
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstants), &pc);
            vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
        }
    }

    void VK_Sprite3D::clearQueue() {
        drawQueue.clear();
    }

    void VK_Sprite3D::setDepthTestEnabled(bool enabled) {
        if (depthTestEnabled == enabled) {
            return;
        }
        depthTestEnabled = enabled;
        if (spriteLoaded) {
            createPipeline();
        }
    }

    void VK_Sprite3D::setDepthWriteEnabled(bool enabled) {
        if (depthWriteEnabled == enabled) {
            return;
        }
        depthWriteEnabled = enabled;
        if (spriteLoaded) {
            createPipeline();
        }
    }

    void VK_Sprite3D::resize(VK_Window *window) {
        if (window == nullptr || !spriteLoaded) {
            return;
        }

        colorAttachmentFormat = window->getSwapchainFormat();
        depthAttachmentFormat = window->getDepthFormat();
        const size_t newImageCount = window->getSwapchainImageCount();

        if (newImageCount != imageCount) {
            imageCount = newImageCount;
            destroyDescriptors();
            destroyCameraBuffers();
            createDescriptorSetLayout();
            createCameraBuffers();
            createDescriptorPool();
            createDescriptorSets();
        }

        createPipeline();
    }

    void VK_Sprite3D::cleanup() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
        drawQueue.clear();
        destroyPipeline();
        destroyDescriptors();
        destroyCameraBuffers();
        destroyBuffers();
        destroyTexture();

        device = VK_NULL_HANDLE;
        physicalDevice = VK_NULL_HANDLE;
        graphicsQueue = VK_NULL_HANDLE;
        commandPool = VK_NULL_HANDLE;
        colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        imageCount = 0;
        spriteLoaded = false;
    }

    void VK_Sprite3D::createQuadBuffers() {
        const std::array<Vertex, 4> vertices{{
            {{-0.5f, -0.5f}, {0.0f, 1.0f}},
            {{0.5f, -0.5f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f}, {1.0f, 0.0f}},
            {{-0.5f, 0.5f}, {0.0f, 0.0f}},
        }};
        const std::array<uint16_t, 6> indices{{0, 1, 2, 0, 2, 3}};

        createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     vertexBuffer, vertexBufferMemory);

        void *data = nullptr;
        VK_CHECK_RESULT(vkMapMemory(device, vertexBufferMemory, 0, sizeof(vertices), 0, &data));
        std::memcpy(data, vertices.data(), sizeof(vertices));
        vkUnmapMemory(device, vertexBufferMemory);

        createBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     indexBuffer, indexBufferMemory);

        VK_CHECK_RESULT(vkMapMemory(device, indexBufferMemory, 0, sizeof(indices), 0, &data));
        std::memcpy(data, indices.data(), sizeof(indices));
        vkUnmapMemory(device, indexBufferMemory);
    }

    void VK_Sprite3D::createTexture(SDL_Surface *surface) {
        SDL_Surface *rgbaSurface = convertToRGBA(surface);
        if (rgbaSurface == nullptr) {
            throw mxvk::Exception("Failed to convert 3D sprite surface to RGBA");
        }

        spriteWidth = rgbaSurface->w;
        spriteHeight = rgbaSurface->h;

        createImage(static_cast<uint32_t>(spriteWidth), static_cast<uint32_t>(spriteHeight),
                    VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, spriteImage, spriteImageMemory);

        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(spriteWidth) * static_cast<VkDeviceSize>(spriteHeight) * 4;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);

        void *data = nullptr;
        VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data));
        const int rowBytes = spriteWidth * 4;
        if (rgbaSurface->pitch == rowBytes) {
            std::memcpy(data, rgbaSurface->pixels, static_cast<size_t>(imageSize));
        } else {
            const auto *src = static_cast<const uint8_t *>(rgbaSurface->pixels);
            auto *dst = static_cast<uint8_t *>(data);
            for (int y = 0; y < spriteHeight; ++y) {
                std::memcpy(dst + y * rowBytes, src + y * rgbaSurface->pitch, static_cast<size_t>(rowBytes));
            }
        }
        vkUnmapMemory(device, stagingMemory);

        transitionImageLayout(spriteImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, spriteImage, static_cast<uint32_t>(spriteWidth), static_cast<uint32_t>(spriteHeight));
        transitionImageLayout(spriteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
        SDL_DestroySurface(rgbaSurface);

        spriteImageView = createImageView(spriteImage, VK_FORMAT_R8G8B8A8_UNORM);
    }

    void VK_Sprite3D::createSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &spriteSampler));
    }

    void VK_Sprite3D::createDescriptorSetLayout() {
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            return;
        }

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));
    }

    void VK_Sprite3D::createCameraBuffers() {
        cameraBuffers.resize(imageCount, VK_NULL_HANDLE);
        cameraBufferMemory.resize(imageCount, VK_NULL_HANDLE);
        cameraBuffersMapped.resize(imageCount, nullptr);

        for (size_t i = 0; i < imageCount; ++i) {
            createBuffer(sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         cameraBuffers[i], cameraBufferMemory[i]);
            VK_CHECK_RESULT(vkMapMemory(device, cameraBufferMemory[i], 0, sizeof(CameraUBO), 0, &cameraBuffersMapped[i]));
            CameraUBO camera{};
            std::memcpy(cameraBuffersMapped[i], &camera, sizeof(camera));
        }
    }

    void VK_Sprite3D::destroyCameraBuffers() {
        for (size_t i = 0; i < cameraBuffers.size(); ++i) {
            if (cameraBuffersMapped[i] != nullptr) {
                vkUnmapMemory(device, cameraBufferMemory[i]);
            }
            if (cameraBuffers[i] != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, cameraBuffers[i], nullptr);
            }
            if (cameraBufferMemory[i] != VK_NULL_HANDLE) {
                vkFreeMemory(device, cameraBufferMemory[i], nullptr);
            }
        }
        cameraBuffers.clear();
        cameraBufferMemory.clear();
        cameraBuffersMapped.clear();
    }

    void VK_Sprite3D::createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = static_cast<uint32_t>(imageCount);
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = static_cast<uint32_t>(imageCount);

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(imageCount);
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
    }

    void VK_Sprite3D::createDescriptorSets() {
        descriptorSets.resize(imageCount, VK_NULL_HANDLE);
        std::vector<VkDescriptorSetLayout> layouts(imageCount, descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
        allocInfo.pSetLayouts = layouts.data();
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()));

        for (size_t i = 0; i < imageCount; ++i) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = spriteImageView;
            imageInfo.sampler = spriteSampler;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = cameraBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CameraUBO);

            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &imageInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void VK_Sprite3D::createPipeline() {
        destroyPipeline();

        auto vertCode = readShaderFile(vertexShaderPath);
        auto fragCode = readShaderFile(fragmentShaderPath);
        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

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

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, pos);
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, uv);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        std::array<VkDynamicState, 2> dynamicStates{{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = depthTestEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = depthWriteEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(glm::vec4) * 3;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout));

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorAttachmentFormat;
        if (depthAttachmentFormat != VK_FORMAT_UNDEFINED) {
            renderingInfo.depthAttachmentFormat = depthAttachmentFormat;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));

        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
    }

    void VK_Sprite3D::destroyPipeline() {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }
    }

    void VK_Sprite3D::destroyTexture() {
        if (spriteSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, spriteSampler, nullptr);
            spriteSampler = VK_NULL_HANDLE;
        }
        if (spriteImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, spriteImageView, nullptr);
            spriteImageView = VK_NULL_HANDLE;
        }
        if (spriteImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, spriteImage, nullptr);
            spriteImage = VK_NULL_HANDLE;
        }
        if (spriteImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, spriteImageMemory, nullptr);
            spriteImageMemory = VK_NULL_HANDLE;
        }
        spriteWidth = 0;
        spriteHeight = 0;
    }

    void VK_Sprite3D::destroyBuffers() {
        if (vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertexBuffer, nullptr);
            vertexBuffer = VK_NULL_HANDLE;
        }
        if (vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertexBufferMemory, nullptr);
            vertexBufferMemory = VK_NULL_HANDLE;
        }
        if (indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, indexBuffer, nullptr);
            indexBuffer = VK_NULL_HANDLE;
        }
        if (indexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, indexBufferMemory, nullptr);
            indexBufferMemory = VK_NULL_HANDLE;
        }
    }

    void VK_Sprite3D::destroyDescriptors() {
        descriptorSets.clear();
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void VK_Sprite3D::createBuffer(VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags properties,
                                   VkBuffer &buffer,
                                   VkDeviceMemory &bufferMemory) const {
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

    uint32_t VK_Sprite3D::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1U << i)) != 0U &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw mxvk::Exception("Failed to find suitable memory type for 3D sprite");
    }

    VkCommandBuffer VK_Sprite3D::beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        return commandBuffer;
    }

    void VK_Sprite3D::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(graphicsQueue));
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void VK_Sprite3D::createImage(uint32_t width,
                                  uint32_t height,
                                  VkFormat format,
                                  VkImageTiling tiling,
                                  VkImageUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkImage &image,
                                  VkDeviceMemory &imageMemory) const {
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

    VkImageView VK_Sprite3D::createImageView(VkImage image, VkFormat format) const {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    void VK_Sprite3D::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw mxvk::Exception("Unsupported 3D sprite image layout transition");
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);
    }

    void VK_Sprite3D::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endSingleTimeCommands(commandBuffer);
    }

    SDL_Surface *VK_Sprite3D::convertToRGBA(SDL_Surface *surface) const {
        if (surface == nullptr) {
            return nullptr;
        }
        return SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    }

    std::vector<char> VK_Sprite3D::readShaderFile(const std::string &path) const {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw mxvk::Exception("Failed to open 3D sprite shader: " + path);
        }

        const std::streamsize fileSize = file.tellg();
        if (fileSize <= 0 || (fileSize % 4) != 0) {
            throw mxvk::Exception("Invalid 3D sprite shader size: " + path);
        }

        std::vector<char> buffer(static_cast<size_t>(fileSize));
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        return buffer;
    }

    VkShaderModule VK_Sprite3D::createShaderModule(const std::vector<char> &code) const {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
        return shaderModule;
    }

} // namespace mxvk
