/**
 * @file mxvk_sprite.cpp
 * @brief Implementation of mxvk::VK_Sprite Vulkan 2-D sprite renderer.
 */
#include "mxvk/mxvk_sprite.hpp"
#include "mxvk/mxvk_opencv_compat.hpp"
#include "mxvk/mxvk_png.hpp"
#include "mxvk/mxvk_shader_module.hpp"
#include <algorithm>
#include <filesystem>
#include <limits>
#ifdef MXVK_CUDA
#include <unistd.h>
#endif

namespace mxvk {

    VK_Sprite::VK_Sprite(VkDevice dev, VkPhysicalDevice physDev, VkQueue gQueue, VkCommandPool cmdPool)
        : device(dev), physicalDevice(physDev), graphicsQueue(gQueue), commandPool(cmdPool) {
        std::cout << "mxvk: Created Sprite\n";
    }

    void VK_Sprite::releaseUploadResources() {
        destroyStagingResources();
    }

    void VK_Sprite::setCommandPool(VkCommandPool pool) {
        if (pool == commandPool) {
            return;
        }

        // uploadCmdBuffer is allocated from commandPool, so it must be released
        // before switching to another pool.
        destroyStagingResources();
        commandPool = pool;
    }

    VK_Sprite::~VK_Sprite() {
        vkDeviceWaitIdle(device);
        drawQueue.clear();

        destroyStagingResources();
#ifdef MXVK_CUDA
        destroyCudaInterop();
#endif
        if (quadVertexBuffer != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite quad vertex buffer\n";
            vkDestroyBuffer(device, quadVertexBuffer, nullptr);
            vkFreeMemory(device, quadVertexBufferMemory, nullptr);
        }
        if (quadIndexBuffer != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite quad index buffer\n";
            vkDestroyBuffer(device, quadIndexBuffer, nullptr);
            vkFreeMemory(device, quadIndexBufferMemory, nullptr);
        }

        destroyDescriptorPools();

        if (spriteSampler != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite sampler\n";
            vkDestroySampler(device, spriteSampler, nullptr);
        }

        if (!externalTexture && spriteImageView != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite image view\n";
            vkDestroyImageView(device, spriteImageView, nullptr);
        }

        if (!externalTexture && spriteImage != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite image\n";
            vkDestroyImage(device, spriteImage, nullptr);
            vkFreeMemory(device, spriteImageMemory, nullptr);
        }

        if (fragmentShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
        }

        if (customPipeline != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite custom pipeline\n";
            vkDestroyPipeline(device, customPipeline, nullptr);
        }

        if (customPipelineLayout != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite custom pipeline layout\n";
            vkDestroyPipelineLayout(device, customPipelineLayout, nullptr);
        }

        destroyExtendedUBO();
        destroyInstanceResources();
    }

    void VK_Sprite::destroySpriteResources() {
        destroyStagingResources();
#ifdef MXVK_CUDA
        destroyCudaInterop();
#endif

        destroyDescriptorPools();

        // Extended descriptor set references spriteImageView/spriteSampler,
        // so it must be invalidated when those are destroyed.
        if (extendedDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, extendedDescriptorPool, nullptr);
            extendedDescriptorPool = VK_NULL_HANDLE;
            extendedDescriptorSet = VK_NULL_HANDLE;
        }

        if (spriteSampler != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite sampler\n";
            vkDestroySampler(device, spriteSampler, nullptr);
            spriteSampler = VK_NULL_HANDLE;
        }

        if (!externalTexture && spriteImageView != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite image view\n";
            vkDestroyImageView(device, spriteImageView, nullptr);
            spriteImageView = VK_NULL_HANDLE;
        }

        if (!externalTexture && spriteImage != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite image\n";
            vkDestroyImage(device, spriteImage, nullptr);
            spriteImage = VK_NULL_HANDLE;
        }
        if (!externalTexture && spriteImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, spriteImageMemory, nullptr);
            spriteImageMemory = VK_NULL_HANDLE;
        }
        externalTexture = false;

        if (fragmentShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
            fragmentShaderModule = VK_NULL_HANDLE;
        }

        if (customPipeline != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite custom pipeline\n";
            vkDestroyPipeline(device, customPipeline, nullptr);
            customPipeline = VK_NULL_HANDLE;
        }

        if (customPipelineLayout != VK_NULL_HANDLE) {
            std::cout << "vk: destroying sprite custom pipeline layout\n";
            vkDestroyPipelineLayout(device, customPipelineLayout, nullptr);
            customPipelineLayout = VK_NULL_HANDLE;
        }

        hasCustomShader = false;
        spriteLoaded = false;
    }

    void VK_Sprite::enableExtendedUBO() {
        if (extendedUBOEnabled)
            return;
        extendedUBOEnabled = true;
        createExtendedUBO();
        createExtendedDescriptorSetLayout();
        rebuildPipeline();
    }

    void VK_Sprite::setMouseState(float mx, float my, float pressed, float reserved) {
        extendedUBOData.mouse = glm::vec4(mx, my, pressed, reserved);
    }

    void VK_Sprite::setUniform0(float x, float y, float z, float w) {
        extendedUBOData.u0 = glm::vec4(x, y, z, w);
    }

    void VK_Sprite::setUniform1(float x, float y, float z, float w) {
        extendedUBOData.u1 = glm::vec4(x, y, z, w);
    }

    void VK_Sprite::setUniform2(float x, float y, float z, float w) {
        extendedUBOData.u2 = glm::vec4(x, y, z, w);
    }

    void VK_Sprite::setUniform3(float x, float y, float z, float w) {
        extendedUBOData.u3 = glm::vec4(x, y, z, w);
    }

    void VK_Sprite::createExtendedUBO() {
        if (extendedUBOBuffer != VK_NULL_HANDLE)
            return;
        createBuffer(sizeof(SpriteExtendedUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     extendedUBOBuffer, extendedUBOMemory);
        VK_CHECK_RESULT(vkMapMemory(device, extendedUBOMemory, 0, sizeof(SpriteExtendedUBO), 0, &extendedUBOMapped));
        memset(extendedUBOMapped, 0, sizeof(SpriteExtendedUBO));
    }

    void VK_Sprite::updateExtendedUBO() {
        if (!extendedUBOEnabled || !extendedUBOMapped)
            return;
        memcpy(extendedUBOMapped, &extendedUBOData, sizeof(SpriteExtendedUBO));
    }

    void VK_Sprite::createExtendedDescriptorSetLayout() {
        if (extendedDescriptorSetLayout != VK_NULL_HANDLE)
            return;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        // binding 0: combined image sampler (same as original)
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].pImmutableSamplers = nullptr;
        // binding 1: uniform buffer for extended data
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &extendedDescriptorSetLayout));
        ownExtendedDescriptorSetLayout = true;
    }

    void VK_Sprite::createExtendedDescriptorSet() {
        if (extendedDescriptorSetLayout == VK_NULL_HANDLE || spriteImageView == VK_NULL_HANDLE ||
            spriteSampler == VK_NULL_HANDLE || extendedUBOBuffer == VK_NULL_HANDLE)
            return;

        if (extendedDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, extendedDescriptorPool, nullptr);
            extendedDescriptorPool = VK_NULL_HANDLE;
            extendedDescriptorSet = VK_NULL_HANDLE;
        }

        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &extendedDescriptorPool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = extendedDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &extendedDescriptorSetLayout;

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &extendedDescriptorSet));

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = spriteImageView;
        imageInfo.sampler = spriteSampler;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = extendedUBOBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(SpriteExtendedUBO);

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = extendedDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = extendedDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void VK_Sprite::destroyExtendedUBO() {
        if (extendedDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, extendedDescriptorPool, nullptr);
            extendedDescriptorPool = VK_NULL_HANDLE;
            extendedDescriptorSet = VK_NULL_HANDLE;
        }
        if (ownExtendedDescriptorSetLayout && extendedDescriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, extendedDescriptorSetLayout, nullptr);
            extendedDescriptorSetLayout = VK_NULL_HANDLE;
            ownExtendedDescriptorSetLayout = false;
        }
        if (extendedUBOBuffer != VK_NULL_HANDLE) {
            if (extendedUBOMapped) {
                vkUnmapMemory(device, extendedUBOMemory);
                extendedUBOMapped = nullptr;
            }
            vkDestroyBuffer(device, extendedUBOBuffer, nullptr);
            vkFreeMemory(device, extendedUBOMemory, nullptr);
            extendedUBOBuffer = VK_NULL_HANDLE;
            extendedUBOMemory = VK_NULL_HANDLE;
        }
        extendedUBOEnabled = false;
    }

    VkDeviceSize VK_Sprite::stagingAllocationSize(VkDeviceSize requiredSize) const {
        VkDeviceSize allocationSize = 1;
        while (allocationSize < requiredSize && allocationSize <= (std::numeric_limits<VkDeviceSize>::max() / 2)) {
            allocationSize *= 2;
        }
        return std::max(allocationSize, requiredSize);
    }

    void VK_Sprite::createStagingResources(VkDeviceSize size) {
        const VkDeviceSize allocationSize = stagingAllocationSize(size);
        if (stagingResourcesCreated && persistentStagingSize >= size) {
            return;
        }
        destroyStagingResources();

        createBuffer(allocationSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     persistentStagingBuffer, persistentStagingMemory);

        try {
            VK_CHECK_RESULT(vkMapMemory(device, persistentStagingMemory, 0, allocationSize, 0, &persistentStagingMapped));
            persistentStagingSize = allocationSize;
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = commandPool;
            allocInfo.commandBufferCount = 1;
            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &uploadCmdBuffer));
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &uploadFence));
            stagingResourcesCreated = true;
        } catch (...) {
            if (uploadCmdBuffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device, commandPool, 1, &uploadCmdBuffer);
                uploadCmdBuffer = VK_NULL_HANDLE;
            }
            if (persistentStagingMapped) {
                vkUnmapMemory(device, persistentStagingMemory);
                persistentStagingMapped = nullptr;
            }
            if (persistentStagingBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, persistentStagingBuffer, nullptr);
                persistentStagingBuffer = VK_NULL_HANDLE;
            }
            if (persistentStagingMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, persistentStagingMemory, nullptr);
                persistentStagingMemory = VK_NULL_HANDLE;
            }
            persistentStagingSize = 0;
            throw;
        }
    }

    void VK_Sprite::destroyStagingResources() {
        if (!stagingResourcesCreated)
            return;

        if (uploadFence != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device, uploadFence, nullptr);
            uploadFence = VK_NULL_HANDLE;
        }
        if (uploadCmdBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, commandPool, 1, &uploadCmdBuffer);
            uploadCmdBuffer = VK_NULL_HANDLE;
        }
        if (persistentStagingBuffer != VK_NULL_HANDLE) {
            vkUnmapMemory(device, persistentStagingMemory);
            vkDestroyBuffer(device, persistentStagingBuffer, nullptr);
            vkFreeMemory(device, persistentStagingMemory, nullptr);
            persistentStagingBuffer = VK_NULL_HANDLE;
            persistentStagingMemory = VK_NULL_HANDLE;
            persistentStagingMapped = nullptr;
            persistentStagingSize = 0;
        }
        stagingResourcesCreated = false;
    }

    void VK_Sprite::destroyInstanceResources() {
        if (instanceBuffer != VK_NULL_HANDLE) {
            if (instanceBufferMapped) {
                vkUnmapMemory(device, instanceBufferMemory);
                instanceBufferMapped = nullptr;
            }
            vkDestroyBuffer(device, instanceBuffer, nullptr);
            vkFreeMemory(device, instanceBufferMemory, nullptr);
            instanceBuffer = VK_NULL_HANDLE;
            instanceBufferMemory = VK_NULL_HANDLE;
            instanceBufferCapacity = 0;
        }
        if (instancedPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, instancedPipeline, nullptr);
            instancedPipeline = VK_NULL_HANDLE;
        }
        if (instancedPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, instancedPipelineLayout, nullptr);
            instancedPipelineLayout = VK_NULL_HANDLE;
        }
        instancingEnabled = false;
    }

    void VK_Sprite::ensureInstanceBuffer(uint32_t count) {
        if (instanceBufferCapacity >= count && instanceBuffer != VK_NULL_HANDLE)
            return;

        if (instanceBuffer != VK_NULL_HANDLE) {
            if (instanceBufferMapped) {
                vkUnmapMemory(device, instanceBufferMemory);
                instanceBufferMapped = nullptr;
            }
            vkDestroyBuffer(device, instanceBuffer, nullptr);
            vkFreeMemory(device, instanceBufferMemory, nullptr);
            instanceBuffer = VK_NULL_HANDLE;
            instanceBufferMemory = VK_NULL_HANDLE;
        }

        VkDeviceSize size = sizeof(SpriteInstanceData) * count;
        createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     instanceBuffer, instanceBufferMemory);

        VK_CHECK_RESULT(vkMapMemory(device, instanceBufferMemory, 0, size, 0, &instanceBufferMapped));
        instanceBufferCapacity = count;
    }

    void VK_Sprite::enableInstancing(uint32_t maxInstances,
                                     const std::string &instanceVertShaderPath,
                                     const std::string &instanceFragShaderPath) {
        if (colorAttachmentFormat == VK_FORMAT_UNDEFINED || descriptorSetLayout == VK_NULL_HANDLE) {
            throw mxvk::Exception("VKSprite::enableInstancing called before color format/descriptorSetLayout set");
        }
        ensureInstanceBuffer(maxInstances);
        createQuadBuffer();
        instanceVertPath = instanceVertShaderPath;
        instanceFragPath = instanceFragShaderPath;
        createInstancedPipeline(instanceVertShaderPath, instanceFragShaderPath);
        instancingEnabled = true;
        std::cout << std::format("mxvk: Instancing enabled (max {} instances)\n", maxInstances);
    }

    void VK_Sprite::createInstancedPipeline(const std::string &vertPath, const std::string &fragPath) {
        if (instancedPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, instancedPipeline, nullptr);
            instancedPipeline = VK_NULL_HANDLE;
        }
        if (instancedPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, instancedPipelineLayout, nullptr);
            instancedPipelineLayout = VK_NULL_HANDLE;
        }

        auto vertShaderCode = readShaderFile(vertPath);
        auto fragShaderCode = readShaderFile(fragPath);
        VkShaderModule vertModule = mxvk::create_shader_module(device, vertShaderCode);
        VkShaderModule fragModule = mxvk::create_shader_module(device, fragShaderCode);

        VkPipelineShaderStageCreateInfo vertStageInfo{};
        vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module = vertModule;
        vertStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragStageInfo{};
        fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfo.module = fragModule;
        fragStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

        std::array<VkVertexInputBindingDescription, 2> bindingDescs{};
        bindingDescs[0].binding = 0;
        bindingDescs[0].stride = sizeof(float) * 4;
        bindingDescs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescs[1].binding = 1;
        bindingDescs[1].stride = sizeof(SpriteInstanceData);
        bindingDescs[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        std::array<VkVertexInputAttributeDescription, 4> attrDescs{};

        attrDescs[0].binding = 0;
        attrDescs[0].location = 0;
        attrDescs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrDescs[0].offset = 0;

        attrDescs[1].binding = 0;
        attrDescs[1].location = 1;
        attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrDescs[1].offset = sizeof(float) * 2;

        attrDescs[2].binding = 1;
        attrDescs[2].location = 2;
        attrDescs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrDescs[2].offset = 0;

        attrDescs[3].binding = 1;
        attrDescs[3].location = 3;
        attrDescs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrDescs[3].offset = sizeof(float) * 4;

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescs.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 2;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &instancedPipelineLayout));

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.viewMask = 0;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorAttachmentFormat;
        if (depthAttachmentFormat != VK_FORMAT_UNDEFINED) {
            renderingInfo.depthAttachmentFormat = depthAttachmentFormat;
        }

        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = instancedPipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &instancedPipeline));

        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
    }

    void VK_Sprite::createCustomPipeline() {
        if (!hasCustomShader || fragmentShaderModule == VK_NULL_HANDLE)
            return;
        if (colorAttachmentFormat == VK_FORMAT_UNDEFINED || descriptorSetLayout == VK_NULL_HANDLE)
            return;

        if (customPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, customPipeline, nullptr);
            customPipeline = VK_NULL_HANDLE;
        }
        if (customPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, customPipelineLayout, nullptr);
            customPipelineLayout = VK_NULL_HANDLE;
        }

        std::string vertPath = vertexShaderPath.empty() ? "sprite.vert.spv" : vertexShaderPath;
        auto vertShaderCode = readShaderFile(vertPath);
        VkShaderModule vertShaderModule = mxvk::create_shader_module(device, vertShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragmentShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(float) * 4;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = 0;
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = sizeof(float) * 2;

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float) * 12;

        VkDescriptorSetLayout layoutToUseForPipeline = extendedUBOEnabled ? extendedDescriptorSetLayout : descriptorSetLayout;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &layoutToUseForPipeline;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &customPipelineLayout));

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.viewMask = 0;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorAttachmentFormat;
        if (depthAttachmentFormat != VK_FORMAT_UNDEFINED) {
            renderingInfo.depthAttachmentFormat = depthAttachmentFormat;
        }

        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = customPipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &customPipeline));

        vkDestroyShaderModule(device, vertShaderModule, nullptr);
    }

    void VK_Sprite::rebuildPipeline() {
        if (!hasCustomShader || fragmentShaderModule == VK_NULL_HANDLE)
            return;
        createCustomPipeline();
        std::cout << "mxvk: Pipeline rebuilt\n";
    }

    void VK_Sprite::setFragmentShaderPath(const std::string &path) {
        if (path == fragmentShaderPath && fragmentShaderModule != VK_NULL_HANDLE) {
            return;
        }

        if (customPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, customPipeline, nullptr);
            customPipeline = VK_NULL_HANDLE;
        }
        if (customPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, customPipelineLayout, nullptr);
            customPipelineLayout = VK_NULL_HANDLE;
        }
        if (fragmentShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
            fragmentShaderModule = VK_NULL_HANDLE;
        }

        fragmentShaderPath = path;
        hasCustomShader = false;

        if (fragmentShaderPath.empty()) {
            return;
        }

        const auto shaderCode = readShaderFile(fragmentShaderPath);
        fragmentShaderModule = mxvk::create_shader_module(device, shaderCode);
        hasCustomShader = true;

        if (colorAttachmentFormat != VK_FORMAT_UNDEFINED && descriptorSetLayout != VK_NULL_HANDLE) {
            createCustomPipeline();
        }
    }

    void VK_Sprite::rebuildInstancedPipeline() {
        if (!instancingEnabled || instanceVertPath.empty() || instanceFragPath.empty())
            return;
        createInstancedPipeline(instanceVertPath, instanceFragPath);
    }

    void VK_Sprite::createQuadBuffer() {
        if (quadBufferCreated)
            return;

        SpriteVertex vertices[] = {
            {{0.0f, 0.0f}, {0.0f, 0.0f}},
            {{1.0f, 0.0f}, {1.0f, 0.0f}},
            {{1.0f, 1.0f}, {1.0f, 1.0f}},
            {{0.0f, 1.0f}, {0.0f, 1.0f}}};
        uint16_t indices[] = {0, 1, 2, 0, 2, 3};

        VkDeviceSize vertexSize = sizeof(vertices);
        createBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     quadVertexBuffer, quadVertexBufferMemory);

        void *data;
        VK_CHECK_RESULT(vkMapMemory(device, quadVertexBufferMemory, 0, vertexSize, 0, &data));
        memcpy(data, vertices, vertexSize);
        vkUnmapMemory(device, quadVertexBufferMemory);

        VkDeviceSize indexSize = sizeof(indices);
        createBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     quadIndexBuffer, quadIndexBufferMemory);

        VK_CHECK_RESULT(vkMapMemory(device, quadIndexBufferMemory, 0, indexSize, 0, &data));
        memcpy(data, indices, indexSize);
        vkUnmapMemory(device, quadIndexBufferMemory);

        quadBufferCreated = true;
    }

    void VK_Sprite::loadSprite(const std::string &pngPath, const std::string &fragmentShaderPath) {
        SDL_Surface *surface = mxvk::LoadPNG(pngPath.c_str());
        if (!surface) {
            throw mxvk::Exception("Failed to load sprite image: " + pngPath);
        }
        loadSprite(surface, fragmentShaderPath);
        SDL_DestroySurface(surface);
        std::cout << std::format("mxvk: Loaded PNG: {}\n", pngPath);
    }

    void VK_Sprite::loadSprite(SDL_Surface *surface, const std::string &fragmentShaderPath) {
        if (!surface) {
            throw mxvk::Exception("VKSprite::loadSprite called with null surface");
        }
        if (spriteLoaded || spriteImage != VK_NULL_HANDLE || fragmentShaderModule != VK_NULL_HANDLE) {
            destroySpriteResources();
        }
        SDL_Surface *rgbaSurface = convertToRGBA(surface);
        if (!rgbaSurface) {
            throw mxvk::Exception("Failed to convert sprite surface to RGBA");
        }
        spriteWidth = rgbaSurface->w;
        spriteHeight = rgbaSurface->h;
        createSpriteTexture(rgbaSurface);
        SDL_DestroySurface(rgbaSurface);
        createSampler();
        createQuadBuffer();
        if (!fragmentShaderPath.empty()) {
            auto shaderCode = readShaderFile(fragmentShaderPath);
            fragmentShaderModule = mxvk::create_shader_module(device, shaderCode);
            hasCustomShader = true;
            this->fragmentShaderPath = fragmentShaderPath;

            if (colorAttachmentFormat != VK_FORMAT_UNDEFINED && descriptorSetLayout != VK_NULL_HANDLE) {
                createCustomPipeline();
            }
        }
        spriteLoaded = true;
        std::cout << std::format("mxvk: Loaded surface texture: {}x{}\n", spriteWidth, spriteHeight);
    }

    void VK_Sprite::createEmptySprite(int width, int height, const std::string &vertexShaderPath, const std::string &fragmentShaderPath) {
        if (width <= 0 || height <= 0) {
            throw mxvk::Exception("VKSprite::createEmptySprite invalid dimensions");
        }
        if (spriteLoaded || spriteImage != VK_NULL_HANDLE || fragmentShaderModule != VK_NULL_HANDLE) {
            destroySpriteResources();
        }
        spriteWidth = width;
        spriteHeight = height;

        if (!vertexShaderPath.empty()) {
            setVertexShaderPath(vertexShaderPath);
        }

#ifdef MXVK_CUDA
        try {
            createCudaExportableImage(width, height, spriteImage, spriteImageMemory);
        } catch (const std::exception &ex) {
            std::cout << std::format("mxvk: CUDA exportable sprite image unavailable: {}; using standard Vulkan image\n", ex.what());
            createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, spriteImage, spriteImageMemory);
        }
#else
        createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, spriteImage, spriteImageMemory);
#endif

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);

        void *data;
        VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data));
        memset(data, 0, imageSize);
        vkUnmapMemory(device, stagingMemory);

        transitionImageLayout(spriteImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, spriteImage, width, height);
        transitionImageLayout(spriteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#ifdef MXVK_CUDA
        cudaImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#endif

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        spriteImageView = createImageView(spriteImage, VK_FORMAT_R8G8B8A8_UNORM);
        createSampler();
        createQuadBuffer();
        createDescriptorPool();

        createStagingResources(imageSize);

        if (!fragmentShaderPath.empty()) {
            auto shaderCode = readShaderFile(fragmentShaderPath);
            fragmentShaderModule = mxvk::create_shader_module(device, shaderCode);
            hasCustomShader = true;
            this->fragmentShaderPath = fragmentShaderPath;

            if (colorAttachmentFormat != VK_FORMAT_UNDEFINED && descriptorSetLayout != VK_NULL_HANDLE) {
                createCustomPipeline();
            }
        }

        spriteLoaded = true;
        std::cout << std::format("mxvk: Created empty sprite: {}x{}\n", spriteWidth, spriteHeight);
    }

    void VK_Sprite::updateTexture(SDL_Surface *surface) {
        if (!surface) {
            throw mxvk::Exception("VKSprite::updateTexture called with null surface");
        }
        if (!spriteLoaded) {
            throw mxvk::Exception("VKSprite::updateTexture called before sprite was loaded");
        }
        SDL_Surface *rgbaSurface = convertToRGBA(surface);
        if (!rgbaSurface) {
            throw mxvk::Exception("Failed to convert surface to RGBA in updateTexture");
        }
        if (rgbaSurface->w == spriteWidth && rgbaSurface->h == spriteHeight) {
#ifdef MXVK_CUDA
            if (updateTextureCudaHost(rgbaSurface->pixels, static_cast<uint32_t>(rgbaSurface->w), static_cast<uint32_t>(rgbaSurface->h),
                                      static_cast<uint32_t>(rgbaSurface->pitch))) {
                SDL_DestroySurface(rgbaSurface);
                return;
            }
#endif
            updateSpriteTexture(rgbaSurface->pixels, rgbaSurface->w, rgbaSurface->h);
        } else {
            if (stagingResourcesCreated && uploadFence != VK_NULL_HANDLE) {
                vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
            }
#ifdef MXVK_CUDA
            destroyCudaInterop();
#endif
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
            spriteWidth = rgbaSurface->w;
            spriteHeight = rgbaSurface->h;
            createSpriteTexture(rgbaSurface);
            destroyDescriptorPools();
            if (extendedDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, extendedDescriptorPool, nullptr);
                extendedDescriptorPool = VK_NULL_HANDLE;
                extendedDescriptorSet = VK_NULL_HANDLE;
            }
            createDescriptorPool();
        }
        SDL_DestroySurface(rgbaSurface);
    }

    void VK_Sprite::updateTexture(const void *pixels, int width, int height, int pitch) {
        if (!pixels) {
            throw mxvk::Exception("VKSprite::updateTexture called with null pixel data");
        }
        if (!spriteLoaded) {
            throw mxvk::Exception("VKSprite::updateTexture called before sprite was loaded");
        }
        if (width <= 0 || height <= 0) {
            throw mxvk::Exception("VKSprite::updateTexture invalid dimensions");
        }
        int srcPitch = (pitch > 0) ? pitch : width * 4;
        if (width == spriteWidth && height == spriteHeight && srcPitch == width * 4) {
#ifdef MXVK_CUDA
            if (updateTextureCudaHost(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(srcPitch))) {
                return;
            }
#endif
            updateSpriteTexture(pixels, width, height);
        } else if (width == spriteWidth && height == spriteHeight) {
#ifdef MXVK_CUDA
            if (updateTextureCudaHost(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(srcPitch))) {
                return;
            }
#endif
            std::vector<uint8_t> packed(width * height * 4);
            const uint8_t *src = static_cast<const uint8_t *>(pixels);
            for (int row = 0; row < height; ++row) {
                memcpy(packed.data() + row * width * 4, src + row * srcPitch, width * 4);
            }
            updateSpriteTexture(packed.data(), width, height);
        } else {
            if (stagingResourcesCreated && uploadFence != VK_NULL_HANDLE) {
                vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
            }
#ifdef MXVK_CUDA
            destroyCudaInterop();
#endif
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
            spriteWidth = width;
            spriteHeight = height;
            std::vector<uint8_t> packed;
            const void *texData = pixels;
            if (srcPitch != width * 4) {
                packed.resize(width * height * 4);
                const uint8_t *src = static_cast<const uint8_t *>(pixels);
                for (int row = 0; row < height; ++row) {
                    memcpy(packed.data() + row * width * 4, src + row * srcPitch, width * 4);
                }
                texData = packed.data();
            }
            // Resize path: wrap raw pixels in a temporary SDL3 surface (no copy)
            SDL_Surface *tmpSurface = SDL_CreateSurfaceFrom(
                width, height, SDL_PIXELFORMAT_RGBA32,
                const_cast<void *>(texData), width * 4);
            if (!tmpSurface) {
                throw mxvk::Exception("VKSprite::updateTexture failed to create temp surface");
            }
            createSpriteTexture(tmpSurface);
            SDL_DestroySurface(tmpSurface);
            destroyDescriptorPools();
            if (extendedDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, extendedDescriptorPool, nullptr);
                extendedDescriptorPool = VK_NULL_HANDLE;
                extendedDescriptorSet = VK_NULL_HANDLE;
            }
            createDescriptorPool();
        }
    }

    void VK_Sprite::updateSpriteTexture(const void *pixels, uint32_t width, uint32_t height) {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        createStagingResources(imageSize);
        VK_CHECK_RESULT(vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX));
        VK_CHECK_RESULT(vkResetFences(device, 1, &uploadFence));
        memcpy(persistentStagingMapped, pixels, imageSize);
        VK_CHECK_RESULT(vkResetCommandBuffer(uploadCmdBuffer, 0));
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_RESULT(vkBeginCommandBuffer(uploadCmdBuffer, &beginInfo));
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#ifdef MXVK_CUDA
        if (cudaImageLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
            oldLayout = cudaImageLayout;
        }
#endif
        barrier.oldLayout = oldLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = spriteImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_GENERAL) ? VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        const VkPipelineStageFlags srcStage = (oldLayout == VK_IMAGE_LAYOUT_GENERAL)
                                                  ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
                                                  : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        vkCmdPipelineBarrier(uploadCmdBuffer, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

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
        vkCmdCopyBufferToImage(uploadCmdBuffer, persistentStagingBuffer, spriteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(uploadCmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VK_CHECK_RESULT(vkEndCommandBuffer(uploadCmdBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadCmdBuffer;
        VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, uploadFence));
#ifdef MXVK_CUDA
        cudaImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cudaImageNeedsShaderBarrier = false;
#endif
    }

#ifdef MXVK_CUDA
    void VK_Sprite::destroyCudaInterop() {
        if (cudaInteropEnabled || cudaExternalMemory != nullptr || cudaMipmappedArray != nullptr) {
            std::cout << "mxvk: CUDA interop: destroying imported Vulkan texture resources\n";
        }
        if (cudaMipmappedArray != nullptr) {
            cudaFreeMipmappedArray(cudaMipmappedArray);
            cudaMipmappedArray = nullptr;
            cudaArray = nullptr;
        }
        if (cudaExternalMemory != nullptr) {
            cudaDestroyExternalMemory(cudaExternalMemory);
            cudaExternalMemory = nullptr;
        }
        cudaInteropEnabled = false;
        cudaImageNeedsShaderBarrier = false;
        cudaImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cudaExportMemorySize = 0;
        cudaUploadLogged = false;
        cudaWriteTransitionLogged = false;
        cudaSampleBarrierLogged = false;
    }

    void VK_Sprite::createCudaExportableImage(uint32_t width, uint32_t height, VkImage &image, VkDeviceMemory &imageMemory) {
        std::cout << std::format("mxvk: CUDA interop init: requesting exportable Vulkan image {}x{} RGBA8 OPAQUE_FD\n", width, height);
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
        }
        if (imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, imageMemory, nullptr);
            imageMemory = VK_NULL_HANDLE;
        }

        VkExternalMemoryImageCreateInfo externalImageInfo{};
        externalImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        externalImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = &externalImageInfo;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &image));

        VkMemoryRequirements memRequirements{};
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkExportMemoryAllocateInfo exportMemoryInfo{};
        exportMemoryInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &exportMemoryInfo;
        allocInfo.allocationSize = memRequirements.size;

        try {
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device, image, imageMemory, 0));
            cudaExportMemorySize = memRequirements.size;
            cudaInteropUnavailableLogged = false;
            std::cout << std::format(
                "mxvk: CUDA interop init: exportable Vulkan image allocated (memorySize={} bytes, memoryType={})\n",
                static_cast<unsigned long long>(cudaExportMemorySize), allocInfo.memoryTypeIndex);
        } catch (...) {
            if (imageMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, imageMemory, nullptr);
                imageMemory = VK_NULL_HANDLE;
            }
            if (image != VK_NULL_HANDLE) {
                vkDestroyImage(device, image, nullptr);
                image = VK_NULL_HANDLE;
            }
            cudaExportMemorySize = 0;
            throw;
        }
    }

    bool VK_Sprite::ensureCudaInterop() {
        if (cudaInteropEnabled) {
            return true;
        }
        if (spriteImage == VK_NULL_HANDLE || spriteImageMemory == VK_NULL_HANDLE || cudaExportMemorySize == 0) {
            if (!cudaInteropUnavailableLogged) {
                std::cout << "mxvk: CUDA interop init: sprite image is not exportable; using CPU/pinned fallback\n";
                cudaInteropUnavailableLogged = true;
            }
            return false;
        }
        if (vkGetMemoryFdKHR == nullptr) {
            if (!cudaInteropUnavailableLogged) {
                std::cout << "mxvk: CUDA interop init: vkGetMemoryFdKHR was not loaded; using CPU/pinned fallback\n";
                cudaInteropUnavailableLogged = true;
            }
            return false;
        }

        VkMemoryGetFdInfoKHR fdInfo{};
        fdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        fdInfo.memory = spriteImageMemory;
        fdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        int memoryFd = -1;
        const VkResult fdResult = vkGetMemoryFdKHR(device, &fdInfo, &memoryFd);
        if (fdResult != VK_SUCCESS) {
            if (!cudaInteropUnavailableLogged) {
                std::cout << std::format("mxvk: CUDA interop init: vkGetMemoryFdKHR failed ({})\n", static_cast<int>(fdResult));
                cudaInteropUnavailableLogged = true;
            }
            return false;
        }
        std::cout << std::format("mxvk: CUDA interop init: exported Vulkan image memory fd={}\n", memoryFd);

        cudaExternalMemoryHandleDesc externalMemoryDesc{};
        externalMemoryDesc.type = cudaExternalMemoryHandleTypeOpaqueFd;
        externalMemoryDesc.handle.fd = memoryFd;
        externalMemoryDesc.size = cudaExportMemorySize;

        cudaError_t cudaResult = cudaImportExternalMemory(&cudaExternalMemory, &externalMemoryDesc);
        if (cudaResult != cudaSuccess) {
            close(memoryFd);
            if (!cudaInteropUnavailableLogged) {
                std::cout << std::format("mxvk: CUDA interop init: cudaImportExternalMemory failed: {}\n",
                                         cudaGetErrorString(cudaResult));
                cudaInteropUnavailableLogged = true;
            }
            cudaExternalMemory = nullptr;
            return false;
        }
        std::cout << std::format("mxvk: CUDA interop init: imported external memory into CUDA ({} bytes)\n",
                                 static_cast<unsigned long long>(cudaExportMemorySize));

        cudaExternalMemoryMipmappedArrayDesc arrayDesc{};
        arrayDesc.offset = 0;
        arrayDesc.formatDesc = cudaCreateChannelDesc<uchar4>();
        arrayDesc.extent = make_cudaExtent(static_cast<size_t>(spriteWidth), static_cast<size_t>(spriteHeight), 0);
        arrayDesc.flags = cudaArrayColorAttachment;
        arrayDesc.numLevels = 1;

        cudaResult = cudaExternalMemoryGetMappedMipmappedArray(&cudaMipmappedArray, cudaExternalMemory, &arrayDesc);
        if (cudaResult != cudaSuccess) {
            if (!cudaInteropUnavailableLogged) {
                std::cout << std::format("mxvk: CUDA interop init: cudaExternalMemoryGetMappedMipmappedArray failed: {}\n",
                                         cudaGetErrorString(cudaResult));
                cudaInteropUnavailableLogged = true;
            }
            destroyCudaInterop();
            return false;
        }
        std::cout << std::format("mxvk: CUDA interop init: mapped CUDA mipmapped array {}x{} uchar4\n", spriteWidth, spriteHeight);

        cudaResult = cudaGetMipmappedArrayLevel(&cudaArray, cudaMipmappedArray, 0);
        if (cudaResult != cudaSuccess) {
            if (!cudaInteropUnavailableLogged) {
                std::cout << std::format("mxvk: CUDA interop init: cudaGetMipmappedArrayLevel failed: {}\n",
                                         cudaGetErrorString(cudaResult));
                cudaInteropUnavailableLogged = true;
            }
            destroyCudaInterop();
            return false;
        }

        cudaInteropEnabled = true;
        std::cout << "mxvk: CUDA interop init: direct CUDA-to-Vulkan texture upload is ready\n";
        return true;
    }

    bool VK_Sprite::transitionCudaImageForWrite() {
        if (cudaImageLayout == VK_IMAGE_LAYOUT_GENERAL) {
            return true;
        }

        const VkImageLayout oldLayout = (cudaImageLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                            ? VK_IMAGE_LAYOUT_UNDEFINED
                                            : cudaImageLayout;
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = spriteImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        const VkPipelineStageFlags srcStage = (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                                  ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                  : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        vkCmdPipelineBarrier(commandBuffer, srcStage, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);

        cudaImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        if (!cudaWriteTransitionLogged) {
            std::cout << "mxvk: CUDA interop sync: Vulkan image transitions to GENERAL before CUDA writes\n";
            cudaWriteTransitionLogged = true;
        }
        return true;
    }

    bool VK_Sprite::transitionCudaImageForShaderRead() {
        if (cudaImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && !cudaImageNeedsShaderBarrier) {
            return true;
        }

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = cudaImageLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = spriteImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);

        cudaImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cudaImageNeedsShaderBarrier = false;
        if (!cudaSampleBarrierLogged) {
            std::cout << "mxvk: CUDA interop sync: Vulkan transitions GENERAL -> SHADER_READ_ONLY before sampling\n";
            cudaSampleBarrierLogged = true;
        }
        return true;
    }

    void VK_Sprite::recordCudaReadyBarrier(VkCommandBuffer cmdBuffer) {
        if (!cudaImageNeedsShaderBarrier) {
            return;
        }

        (void)cmdBuffer;
        transitionCudaImageForShaderRead();
    }

    bool VK_Sprite::updateTextureCuda(const cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream) {
        if (!spriteLoaded) {
            return false;
        }
        if (rgba.empty() || rgba.type() != CV_8UC4 || rgba.cols <= 0 || rgba.rows <= 0) {
            return false;
        }
        if (rgba.cols != spriteWidth || rgba.rows != spriteHeight || spriteImage == VK_NULL_HANDLE ||
            spriteImageMemory == VK_NULL_HANDLE || spriteImageView == VK_NULL_HANDLE || cudaExportMemorySize == 0) {
            if (stagingResourcesCreated && uploadFence != VK_NULL_HANDLE) {
                vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
            }
            vkDeviceWaitIdle(device);
            destroyCudaInterop();
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

            spriteWidth = rgba.cols;
            spriteHeight = rgba.rows;
            try {
                createCudaExportableImage(static_cast<uint32_t>(spriteWidth), static_cast<uint32_t>(spriteHeight),
                                          spriteImage, spriteImageMemory);
                spriteImageView = createImageView(spriteImage, VK_FORMAT_R8G8B8A8_UNORM);
            } catch (const std::exception &ex) {
                if (!cudaInteropUnavailableLogged) {
                    std::cout << std::format("mxvk: CUDA exportable sprite resize unavailable: {}; using CPU/pinned fallback\n", ex.what());
                    cudaInteropUnavailableLogged = true;
                }
                return false;
            }

            destroyDescriptorPools();
            if (extendedDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, extendedDescriptorPool, nullptr);
                extendedDescriptorPool = VK_NULL_HANDLE;
                extendedDescriptorSet = VK_NULL_HANDLE;
            }
            createDescriptorPool();
            if (spriteSampler == VK_NULL_HANDLE) {
                createSampler();
            }
            createQuadBuffer();
        }
        if (!ensureCudaInterop() || !transitionCudaImageForWrite()) {
            return false;
        }

        cudaStream_t cudaStream = cuda_stream_handle(stream);
        if (!cudaUploadLogged) {
            std::cout << std::format("mxvk: CUDA interop upload: copying {}x{} RGBA GpuMat to Vulkan image array (pitch={} bytes)\n",
                                     rgba.cols, rgba.rows, static_cast<unsigned long long>(rgba.step));
            cudaUploadLogged = true;
        }
        cudaError_t cudaResult = cudaMemcpy2DToArrayAsync(
            cudaArray, 0, 0, rgba.ptr(), rgba.step,
            static_cast<size_t>(rgba.cols) * 4, static_cast<size_t>(rgba.rows),
            cudaMemcpyDeviceToDevice, cudaStream);
        if (cudaResult != cudaSuccess) {
            std::cout << std::format("mxvk: CUDA interop texture copy failed: {}\n", cudaGetErrorString(cudaResult));
            return false;
        }

        cudaResult = cudaStreamSynchronize(cudaStream);
        if (cudaResult != cudaSuccess) {
            std::cout << std::format("mxvk: CUDA interop texture sync failed: {}\n", cudaGetErrorString(cudaResult));
            return false;
        }

        cudaImageNeedsShaderBarrier = true;
        return transitionCudaImageForShaderRead();
    }

    bool VK_Sprite::updateTextureCudaHost(const void *pixels, uint32_t width, uint32_t height, uint32_t pitch) {
        if (pixels == nullptr || width == 0 || height == 0) {
            return false;
        }
        const uint32_t rowBytes = width * 4U;
        if (pitch < rowBytes || static_cast<int>(width) != spriteWidth || static_cast<int>(height) != spriteHeight) {
            return false;
        }
        if (!ensureCudaInterop() || !transitionCudaImageForWrite()) {
            return false;
        }

        if (!cudaUploadLogged) {
            std::cout << std::format(
                "mxvk: CUDA interop upload: copying {}x{} host RGBA pixels to Vulkan image array (pitch={} bytes)\n",
                width, height, pitch);
            cudaUploadLogged = true;
        }

        const cudaError_t copyResult = cudaMemcpy2DToArray(
            cudaArray, 0, 0, pixels, pitch,
            static_cast<size_t>(rowBytes), static_cast<size_t>(height),
            cudaMemcpyHostToDevice);
        if (copyResult != cudaSuccess) {
            std::cout << std::format("mxvk: CUDA interop host texture copy failed: {}\n", cudaGetErrorString(copyResult));
            return false;
        }

        cudaImageNeedsShaderBarrier = true;
        return transitionCudaImageForShaderRead();
    }
#endif

    void VK_Sprite::createSpriteTexture(SDL_Surface *surface) {
#ifdef MXVK_CUDA
        try {
            createCudaExportableImage(surface->w, surface->h, spriteImage, spriteImageMemory);
        } catch (const std::exception &ex) {
            std::cout << std::format("mxvk: CUDA exportable sprite image unavailable: {}; using standard Vulkan image\n", ex.what());
            createImage(surface->w, surface->h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, spriteImage, spriteImageMemory);
        }
#else
        createImage(surface->w, surface->h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, spriteImage, spriteImageMemory);
#endif

#ifdef MXVK_CUDA
        if (updateTextureCudaHost(surface->pixels, static_cast<uint32_t>(surface->w), static_cast<uint32_t>(surface->h),
                                  static_cast<uint32_t>(surface->pitch))) {
            spriteImageView = createImageView(spriteImage, VK_FORMAT_R8G8B8A8_UNORM);
            return;
        }
#endif

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(surface->w) * surface->h * 4;

        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);

        void *data;
        VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data));
        const int rowBytes = surface->w * 4;
        if (surface->pitch == rowBytes) {
            memcpy(data, surface->pixels, imageSize);
        } else {
            const auto *src = static_cast<const uint8_t *>(surface->pixels);
            auto *dst = static_cast<uint8_t *>(data);
            for (int y = 0; y < surface->h; ++y)
                memcpy(dst + y * rowBytes, src + y * surface->pitch, rowBytes);
        }
        vkUnmapMemory(device, stagingMemory);

#ifdef MXVK_CUDA
        const VkImageLayout uploadOldLayout = (cudaImageLayout == VK_IMAGE_LAYOUT_GENERAL)
                                                  ? VK_IMAGE_LAYOUT_GENERAL
                                                  : VK_IMAGE_LAYOUT_UNDEFINED;
#else
        const VkImageLayout uploadOldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
#endif
        transitionImageLayout(spriteImage, uploadOldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, spriteImage, surface->w, surface->h);
        transitionImageLayout(spriteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#ifdef MXVK_CUDA
        cudaImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#endif

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        spriteImageView = createImageView(spriteImage, VK_FORMAT_R8G8B8A8_UNORM);
    }

    void VK_Sprite::createSampler() {
        if (spriteSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, spriteSampler, nullptr);
            spriteSampler = VK_NULL_HANDLE;
        }
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
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.mipLodBias = 0.0f;

        VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &spriteSampler));
    }

    void VK_Sprite::drawSprite(int x, int y) {
        drawSpriteRect(x, y, spriteWidth, spriteHeight);
    }

    void VK_Sprite::drawSprite(int x, int y, float scaleX, float scaleY) {
        drawSpriteRect(x, y, static_cast<int>(spriteWidth * scaleX), static_cast<int>(spriteHeight * scaleY));
    }

    void VK_Sprite::drawSprite(int x, int y, float scaleX, float scaleY, float rotation) {
        drawSpriteRect(x, y, static_cast<int>(spriteWidth * scaleX), static_cast<int>(spriteHeight * scaleY));
    }

    void VK_Sprite::drawSpriteRect(int x, int y, int w, int h) {
        if (!spriteLoaded) {
            throw mxvk::Exception("VKSprite::drawSpriteRect called before sprite was loaded");
        }

        drawQueue.push_back({static_cast<float>(x), static_cast<float>(y),
                             static_cast<float>(w), static_cast<float>(h), shaderParams});
    }

    void VK_Sprite::setShaderParams(float p1, float p2, float p3, float p4) {
        shaderParams = glm::vec4(p1, p2, p3, p4);
    }

    void VK_Sprite::setExternalTexture(VkImageView image_view, int width, int height) {
        if (image_view == VK_NULL_HANDLE || width <= 0 || height <= 0) {
            throw mxvk::Exception("VKSprite::setExternalTexture received an invalid image view");
        }
        if (externalTexture && spriteImageView == image_view) {
            spriteWidth = width;
            spriteHeight = height;
            spriteLoaded = true;
            return;
        }
        auto cached_descriptor = externalDescriptorSets.find(image_view);
        descriptorSet = (cached_descriptor != externalDescriptorSets.end()) ? cached_descriptor->second : VK_NULL_HANDLE;
        descriptorSetPool = VK_NULL_HANDLE;
        if (extendedDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, extendedDescriptorPool, nullptr);
            extendedDescriptorPool = VK_NULL_HANDLE;
            extendedDescriptorSet = VK_NULL_HANDLE;
        }
        if (!externalTexture && spriteImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, spriteImageView, nullptr);
        }
        if (!externalTexture && spriteImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, spriteImage, nullptr);
        }
        if (!externalTexture && spriteImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, spriteImageMemory, nullptr);
        }
        spriteImageView = image_view;
        spriteImage = VK_NULL_HANDLE;
        spriteImageMemory = VK_NULL_HANDLE;
        externalTexture = true;
        spriteWidth = width;
        spriteHeight = height;
        spriteLoaded = true;
    }

    void VK_Sprite::prepareForRendering([[maybe_unused]] VkCommandBuffer cmdBuffer) {
#ifdef MXVK_CUDA
        recordCudaReadyBarrier(cmdBuffer);
#endif
    }

    void VK_Sprite::renderSprites(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout,
                                  uint32_t screenWidth, uint32_t screenHeight) {
        if (drawQueue.empty() || !spriteLoaded || !quadBufferCreated) {
            return;
        }
        if (descriptorSet == VK_NULL_HANDLE) {
            descriptorSet = createDescriptorSet(spriteImageView);
            if (externalTexture) {
                externalDescriptorSets[spriteImageView] = descriptorSet;
            }
        }

        if (instancingEnabled && instancedPipeline != VK_NULL_HANDLE && instanceBuffer != VK_NULL_HANDLE) {
            uint32_t instanceCount = static_cast<uint32_t>(drawQueue.size());

            if (instanceCount > instanceBufferCapacity) {
                ensureInstanceBuffer(instanceCount * 2);
            }

            SpriteInstanceData *dst = static_cast<SpriteInstanceData *>(instanceBufferMapped);
            for (uint32_t i = 0; i < instanceCount; ++i) {
                const auto &cmd = drawQueue[i];
                dst[i].posX = cmd.x;
                dst[i].posY = cmd.y;
                dst[i].sizeW = cmd.w;
                dst[i].sizeH = cmd.h;
                dst[i].params[0] = cmd.params.x;
                dst[i].params[1] = cmd.params.y;
                dst[i].params[2] = cmd.params.z;
                dst[i].params[3] = cmd.params.w;
            }

            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline);
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipelineLayout,
                                    0, 1, &descriptorSet, 0, nullptr);

            VkBuffer buffers[] = {quadVertexBuffer, instanceBuffer};
            VkDeviceSize bufOffsets[] = {0, 0};
            vkCmdBindVertexBuffers(cmdBuffer, 0, 2, buffers, bufOffsets);
            vkCmdBindIndexBuffer(cmdBuffer, quadIndexBuffer, 0, VK_INDEX_TYPE_UINT16);

            float screenSize[2] = {(float)screenWidth, (float)screenHeight};
            vkCmdPushConstants(cmdBuffer, instancedPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(screenSize), screenSize);

            vkCmdDrawIndexed(cmdBuffer, 6, instanceCount, 0, 0, 0);
            return;
        }

        VkPipelineLayout layoutToUse = (customPipeline != VK_NULL_HANDLE) ? customPipelineLayout : pipelineLayout;
        if (customPipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, customPipeline);
        }

        // When extended UBO is enabled, update UBO and bind extended descriptor set
        if (extendedUBOEnabled && customPipeline != VK_NULL_HANDLE) {
            updateExtendedUBO();
            if (extendedDescriptorSet == VK_NULL_HANDLE) {
                createExtendedDescriptorSet();
            }
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layoutToUse,
                                    0, 1, &extendedDescriptorSet, 0, nullptr);
        } else {
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layoutToUse,
                                    0, 1, &descriptorSet, 0, nullptr);
        }

        VkBuffer vertexBuffers[] = {quadVertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmdBuffer, quadIndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        for (const auto &cmd : drawQueue) {
            struct SpritePushConstants {
                float screenWidth;
                float screenHeight;
                float spritePosX;
                float spritePosY;
                float spriteSizeW;
                float spriteSizeH;
                float effectsOn;
                float padding2;
                float params[4];
            } pc{
                (float)screenWidth, (float)screenHeight, cmd.x, cmd.y, cmd.w, cmd.h, effectsEnabled ? 1.0f : 0.0f, 0.0f, {cmd.params.x, cmd.params.y, cmd.params.z, cmd.params.w}};

            vkCmdPushConstants(cmdBuffer, layoutToUse, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(SpritePushConstants), &pc);

            vkCmdDrawIndexed(cmdBuffer, 6, 1, 0, 0, 0);
        }
    }

    void VK_Sprite::clearQueue() {
        drawQueue.clear();
    }

    void VK_Sprite::createDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = nextDescriptorPoolSets;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = nextDescriptorPoolSets;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
        descriptorPools.push_back(descriptorPool);
        if (nextDescriptorPoolSets <= (std::numeric_limits<uint32_t>::max() / 2U)) {
            nextDescriptorPoolSets *= 2U;
        }
    }

    void VK_Sprite::destroyDescriptorPools() {
        for (VkDescriptorPool pool : descriptorPools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, pool, nullptr);
            }
        }
        descriptorPools.clear();
        descriptorPool = VK_NULL_HANDLE;
        descriptorSetPool = VK_NULL_HANDLE;
        descriptorSet = VK_NULL_HANDLE;
        externalDescriptorSets.clear();
        nextDescriptorPoolSets = 16;
    }

    VkDescriptorSet VK_Sprite::createDescriptorSet(VkImageView imageView) {
        if (descriptorSetLayout == VK_NULL_HANDLE) {
            throw mxvk::Exception("VKSprite::createDescriptorSet called before setDescriptorSetLayout");
        }

        if (descriptorPool == VK_NULL_HANDLE) {
            createDescriptorPool();
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        VkDescriptorSet descSet = VK_NULL_HANDLE;
        VkResult allocateResult = vkAllocateDescriptorSets(device, &allocInfo, &descSet);
        if (allocateResult == VK_ERROR_OUT_OF_POOL_MEMORY || allocateResult == VK_ERROR_FRAGMENTED_POOL) {
            createDescriptorPool();
            allocInfo.descriptorPool = descriptorPool;
            allocateResult = vkAllocateDescriptorSets(device, &allocInfo, &descSet);
        }
        if (allocateResult != VK_SUCCESS) {
            throw mxvk::Exception(std::format("Fatal : VkResult is \"{}\" in {} at line {}", static_cast<int>(allocateResult), __FILE__, __LINE__));
        }
        descriptorSetPool = allocInfo.descriptorPool;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = spriteSampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

        return descSet;
    }

    void VK_Sprite::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties, VkBuffer &buffer,
                                 VkDeviceMemory &bufferMemory) {
        VkBuffer newBuffer = VK_NULL_HANDLE;
        VkDeviceMemory newMemory = VK_NULL_HANDLE;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        try {
            VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &newBuffer));

            VkMemoryRequirements memRequirements;
            vkGetBufferMemoryRequirements(device, newBuffer, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
            VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &newMemory));
            VK_CHECK_RESULT(vkBindBufferMemory(device, newBuffer, newMemory, 0));
        } catch (...) {
            if (newBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, newBuffer, nullptr);
            }
            if (newMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, newMemory, nullptr);
            }
            throw;
        }

        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
        }
        if (bufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, bufferMemory, nullptr);
        }
        buffer = newBuffer;
        bufferMemory = newMemory;
    }

    uint32_t VK_Sprite::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw mxvk::Exception("Failed to find suitable memory type!");
    }

    void VK_Sprite::transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
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
        } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);
    }

    void VK_Sprite::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
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

        endSingleTimeCommands(commandBuffer);
    }

    VkCommandBuffer VK_Sprite::beginSingleTimeCommands() {
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

    void VK_Sprite::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RESULT(vkQueueWaitIdle(graphicsQueue));

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void VK_Sprite::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                                VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                                VkImage &image, VkDeviceMemory &imageMemory) {
        VkImage newImage = VK_NULL_HANDLE;
        VkDeviceMemory newMemory = VK_NULL_HANDLE;

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

        try {
            VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &newImage));

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, newImage, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
            VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &newMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device, newImage, newMemory, 0));
        } catch (...) {
            if (newImage != VK_NULL_HANDLE) {
                vkDestroyImage(device, newImage, nullptr);
            }
            if (newMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, newMemory, nullptr);
            }
            throw;
        }

        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
        }
        if (imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, imageMemory, nullptr);
        }
        image = newImage;
        imageMemory = newMemory;
    }

    VkImageView VK_Sprite::createImageView(VkImage image, VkFormat format) {
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

    SDL_Surface *VK_Sprite::convertToRGBA(SDL_Surface *surface) {
        // SDL3: SDL_ConvertSurface handles format conversion including adding alpha
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        return converted;
    }

    std::vector<char> VK_Sprite::readShaderFile(const std::string &filename) {
        std::vector<std::filesystem::path> candidates{};
        const std::filesystem::path requested(filename);

        if (requested.is_absolute() || requested.has_parent_path()) {
            candidates.push_back(requested);
        } else {
            if (const char *basePath = SDL_GetBasePath(); basePath != nullptr) {
                const std::filesystem::path executableDir(basePath);
                candidates.push_back(executableDir / "data" / requested);
                candidates.push_back(executableDir / requested);
            }
            candidates.push_back(std::filesystem::path("data") / requested);
            candidates.push_back(requested);
        }

        std::ifstream file;
        for (const std::filesystem::path &candidate : candidates) {
            file.open(candidate, std::ios::ate | std::ios::binary);
            if (file.is_open()) {
                break;
            }
            file.clear();
        }

        if (!file.is_open()) {
            throw mxvk::Exception("Failed to open shader file: " + filename);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
    }

} // namespace mxvk
