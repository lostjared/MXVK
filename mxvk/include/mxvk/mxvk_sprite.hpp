/**
 * @file mxvk_sprite.hpp
 * @brief Vulkan 2-D sprite renderer with optional custom shaders and instancing.
 *
 * VKSprite manages a Vulkan texture, a screen-space quad, and an optional
 * custom graphics pipeline.  It supports:
 * - Loading images from PNG files or SDL_Surface objects.
 * - Drawing at arbitrary positions, scales, and rotations.
 * - GPU instancing for large batches of identical sprites.
 * - Extended UBO with mouse state and four custom vec4 uniforms.
 */
#pragma once

#include <volk/volk.h>

#include <SDL3/SDL.h>

#include "mxvk_exception.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef MXVK_CUDA
#include <cuda_runtime_api.h>
#include <opencv2/core/cuda.hpp>
#endif

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

    /**
     * @class VKSprite
     * @brief Vulkan 2-D sprite with texture, custom shader, and instancing support.
     *
     * Allocates and manages all Vulkan resources required to render one sprite
     * type: image, sampler, descriptor set, vertex/index buffers, and an
     * optional custom pipeline.  Multiple draw commands are batched into a
     * queue and submitted together in renderSprites().
     */
    class VK_Sprite {
      public:
        static constexpr std::size_t MAX_CUSTOM_UNIFORMS = 64;

        /**
         * @brief Construct and record Vulkan context handles.
         * @param device         Logical device.
         * @param physicalDevice Physical device.
         * @param graphicsQueue  Graphics queue.
         * @param commandPool    Command pool for staging operations.
         */
        VK_Sprite(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue,
                  VkCommandPool commandPool);

        /** @brief Destructor — frees all Vulkan resources. */
        ~VK_Sprite();

        VK_Sprite(const VK_Sprite &) = delete;
        VK_Sprite &operator=(const VK_Sprite &) = delete;
        VK_Sprite(VK_Sprite &&) = delete;
        VK_Sprite &operator=(VK_Sprite &&) = delete;

        /**
         * @brief Load sprite texture from a PNG file.
         * @param pngPath            Path to the PNG file.
         * @param fragmentShaderPath Optional custom fragment shader (SPIR-V .spv).
         */
        void loadSprite(const std::string &pngPath, const std::string &fragmentShaderPath = "");

        /**
         * @brief Load sprite texture from an SDL_Surface.
         * @param surface            Source surface (not consumed).
         * @param fragmentShaderPath Optional custom fragment shader.
         */
        void loadSprite(SDL_Surface *surface, const std::string &fragmentShaderPath = "");

        /**
         * @brief Create a blank (un-initialised) sprite texture.
         * @param width              Pixel width.
         * @param height             Pixel height.
         * @param vertexShaderPath   Optional vertex shader.
         * @param fragmentShaderPath Optional fragment shader.
         */
        void createEmptySprite(int width, int height, const std::string &vertexShaderPath = "", const std::string &fragmentShaderPath = "");

        /**
         * @brief Queue a draw at the given pixel position.
         * @param x Destination X.
         * @param y Destination Y.
         */
        void drawSprite(int x, int y);

        /**
         * @brief Queue a scaled draw.
         * @param x      Destination X.
         * @param y      Destination Y.
         * @param scaleX Horizontal scale factor.
         * @param scaleY Vertical scale factor.
         */
        void drawSprite(int x, int y, float scaleX, float scaleY);

        /**
         * @brief Queue a scaled and rotated draw.
         * @param x        Destination X.
         * @param y        Destination Y.
         * @param scaleX   Horizontal scale.
         * @param scaleY   Vertical scale.
         * @param rotation Clockwise rotation in degrees.
         */
        void drawSprite(int x, int y, float scaleX, float scaleY, float rotation);

        /**
         * @brief Queue a draw into an explicit destination rectangle.
         * @param x,y,w,h Destination rectangle.
         */
        void drawSpriteRect(int x, int y, int w, int h);

        /**
         * @brief Replace the sprite texture from an SDL_Surface.
         * @param surface New surface (not consumed).
         */
        void updateTexture(SDL_Surface *surface);

        /**
         * @brief Replace the sprite texture from a raw pixel buffer.
         * @param pixels Pointer to RGBA data.
         * @param width  Buffer width.
         * @param height Buffer height.
         * @param pitch  Row stride in bytes (0 = auto).
         */
        void updateTexture(const void *pixels, int width, int height, int pitch = 0);

        void setExternalTexture(VkImageView image_view, int width, int height);
        void clearExternalTextureDescriptors();

#ifdef MXVK_CUDA
        /** @brief Replace the sprite texture directly from CUDA device memory. */
        bool updateTextureCuda(const cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream);
#endif

        /**
         * @brief Set up to four custom shader float parameters.
         * @param p1,p2,p3,p4 Parameter values packed into a vec4.
         */
        void setShaderParams(float p1 = 0.0f, float p2 = 0.0f, float p3 = 0.0f, float p4 = 0.0f);

        /**
         * @brief Enable or disable the custom fragment shader effects.
         * @param enabled @c true to enable effects.
         */
        void setEffectsEnabled(bool enabled) { effectsEnabled = enabled; }

        /** @return @c true if shader effects are enabled. */
        bool getEffectsEnabled() const { return effectsEnabled; }

        /**
         * @brief Record all queued draw commands into the given command buffer.
         * @param cmdBuffer    Active command buffer.
         * @param pipelineLayout Pipeline layout for push constants/descriptors.
         * @param screenWidth  Current viewport width.
         * @param screenHeight Current viewport height.
         */
        void renderSprites(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout,
                           uint32_t screenWidth, uint32_t screenHeight);

        /**
         * @brief Record texture barriers that must happen before dynamic rendering begins.
         * @param cmdBuffer Command buffer currently being recorded outside a rendering instance.
         */
        void prepareForRendering(VkCommandBuffer cmdBuffer);

        /** @brief Discard all pending draw commands without rendering. */
        void clearQueue();

        /** @return Sprite texture width in pixels. */
        int getWidth() const { return spriteWidth; }
        /** @return Sprite texture height in pixels. */
        int getHeight() const { return spriteHeight; }

        /**
         * @brief Select the hardware filter used when scaling this sprite.
         * @param filter VK_FILTER_NEAREST for sharp pixels or VK_FILTER_LINEAR for smoothing.
         */
        void setTextureFilter(VkFilter filter);

        /** @brief Assign an external descriptor-set layout. */
        void setDescriptorSetLayout(VkDescriptorSetLayout layout) { descriptorSetLayout = layout; }
        /** @brief Assign the render pass used to build the custom pipeline. */
        void setRenderPass(VkRenderPass rp) { renderPass = rp; }
        /** @brief Assign dynamic-rendering color attachment format used to build pipelines. */
        void setColorAttachmentFormat(VkFormat format) { colorAttachmentFormat = format; }
        /** @brief Assign dynamic-rendering depth attachment format used to build pipelines. */
        void setDepthAttachmentFormat(VkFormat format) { depthAttachmentFormat = format; }
        /**
         * @brief Rebind the command pool used for upload/staging operations.
         *
         * Any in-flight upload resources tied to the previous pool are released first.
         */
        void setCommandPool(VkCommandPool pool);
        /** @brief Use the shared pipeline cache for custom/instanced pipeline creation. */
        void setPipelineCache(VkPipelineCache cache) { pipelineCache = cache; }
        /**
         * @brief Release upload/staging resources tied to the current command pool.
         *
         * Call this before destroying or recreating the command pool.
         */
        void releaseUploadResources();
        /** @brief Override the vertex shader path (used when rebuilding the pipeline). */
        void setVertexShaderPath(const std::string &path) { vertexShaderPath = path; }
        /** @brief Replace the fragment shader path and rebuild the custom pipeline. */
        void setFragmentShaderPath(const std::string &path);

        /** @return @c true if a custom pipeline has been built. */
        bool hasOwnPipeline() const { return customPipeline != VK_NULL_HANDLE; }
        /** @return The custom VkPipeline handle (may be VK_NULL_HANDLE). */
        VkPipeline getPipeline() const { return customPipeline; }
        /** @return The custom pipeline layout handle. */
        VkPipelineLayout getPipelineLayout() const { return customPipelineLayout; }

        /** @brief Destroy and recreate the custom graphics pipeline. */
        void rebuildPipeline();
        /** @brief Destroy and recreate the instanced graphics pipeline. */
        void rebuildInstancedPipeline();

        VkSampler spriteSampler = VK_NULL_HANDLE; ///< Texture sampler.

        /**
         * @brief Enable GPU instancing for this sprite type.
         * @param maxInstances          Maximum simultaneous instances.
         * @param instanceVertShaderPath Vertex shader supporting instancing.
         * @param instanceFragShaderPath Fragment shader.
         */
        void enableInstancing(uint32_t maxInstances,
                              const std::string &instanceVertShaderPath,
                              const std::string &instanceFragShaderPath);

        /** @return @c true if GPU instancing is active. */
        bool isInstancingEnabled() const { return instancingEnabled; }

        /** @brief Allocate and initialise the extended uniform buffer object. */
        void enableExtendedUBO();

        /** @return @c true if the extended UBO is active. */
        bool isExtendedUBOEnabled() const { return extendedUBOEnabled; }

        /**
         * @brief Upload mouse state to the extended UBO.
         * @param mx       Mouse X (normalised or pixels).
         * @param my       Mouse Y.
         * @param pressed  Mouse button state.
         * @param reserved Reserved channel.
         */
        void setMouseState(float mx, float my, float pressed, float reserved = 0.0f);

        /** @brief Upload user uniform 0 to the extended UBO. @param x,y,z,w Components. */
        void setUniform0(float x, float y, float z, float w);
        /** @brief Upload user uniform 1 to the extended UBO. @param x,y,z,w Components. */
        void setUniform1(float x, float y, float z, float w);
        /** @brief Upload user uniform 2 to the extended UBO. @param x,y,z,w Components. */
        void setUniform2(float x, float y, float z, float w);
        /** @brief Upload user uniform 3 to the extended UBO. @param x,y,z,w Components. */
        void setUniform3(float x, float y, float z, float w);

        /**
         * @brief Upload audio frequency-band energy to the extended UBO.
         *
         * The values are appended after the custom-uniform array to preserve
         * the existing SpriteExtended prefix and custom-uniform offsets.
         *
         * @param low Energy below 300 Hz.
         * @param mid Energy from 300 through 3000 Hz.
         * @param high Energy above 3000 Hz.
         * @param reserved Reserved channel.
         */
        void setAudioBands(float low, float mid, float high, float reserved = 0.0f);

        /**
         * @brief Upload ordered custom float values to the extended UBO.
         *
         * Custom shaders can append @c vec4 custom_uniforms[16] after @c u3 in
         * their binding-1 SpriteExtended block. Value N is available at
         * @c custom_uniforms[N/4][N%4]. Existing shaders that use only the
         * original SpriteExtended prefix remain compatible.
         *
         * @param values Up to MAX_CUSTOM_UNIFORMS values. Unused slots are zeroed.
         * @throws mxvk::Exception when too many values are supplied.
         */
        void setCustomUniforms(const std::vector<float> &values);

        /**
         * @brief Allocate a shader-readable RGBA history texture array.
         *
         * Enables extended descriptors and exposes the array as a combined image
         * sampler at set 0, binding 2. Replaces any previously allocated history
         * texture. The texture is initialized to transparent black.
         *
         * @param width  Width of every history layer in pixels.
         * @param height Height of every history layer in pixels.
         * @param layers Number of layers in the circular history buffer.
         * @throws mxvk::Exception when any dimension is zero.
         */
        void enableHistoryTexture(uint32_t width, uint32_t height, uint32_t layers);

        /**
         * @brief Upload one RGBA frame into the next history layer.
         *
         * The write head advances after a successful upload. Input dimensions
         * must match those passed to enableHistoryTexture().
         *
         * @param pixels RGBA8 source pixels.
         * @param width  Source width in pixels.
         * @param height Source height in pixels.
         * @param pitch  Source row stride in bytes, or zero for tightly packed data.
         * @throws mxvk::Exception for null data, invalid dimensions, or an inactive cache.
         */
        void updateHistoryTexture(const void *pixels, int width, int height, int pitch = 0);

        /** @return The logical oldest-layer index for a circular history sampler. */
        [[nodiscard]] uint32_t getHistoryHead() const { return historyHead; }

        /** @return Number of allocated texture-history layers. */
        [[nodiscard]] uint32_t getHistoryLayerCount() const { return historyLayers; }

        /**
         * @brief Allocate a shader-readable 1-D floating-point spectrum texture.
         *
         * Enables extended descriptors and exposes the texture as a combined
         * image sampler at set 0, binding 3. The texture is initialized to zero.
         *
         * @param bins Number of R32_SFLOAT frequency bins.
         * @throws mxvk::Exception when @p bins is zero.
         */
        void enableSpectrumTexture(uint32_t bins);

        /**
         * @brief Replace the current floating-point spectrum data.
         * @param magnitudes Pointer to @p bins frequency magnitudes.
         * @param bins Number of values; must match enableSpectrumTexture().
         */
        void updateSpectrumTexture(const float *magnitudes, uint32_t bins);

        /** @return Number of allocated spectrum bins. */
        [[nodiscard]] uint32_t getSpectrumBinCount() const { return spectrumBins; }

        /**
         * @brief Allocate a shader-readable FFT spectrum-history array.
         *
         * Enables extended descriptors and exposes the history as a combined
         * image sampler at set 0, binding 4. The R32_SFLOAT 1-D array is
         * initialized to zero and uses a circular write head.
         *
         * @param bins Number of frequency bins in every history layer.
         * @param layers Requested number of history layers. The active GPU's
         * maximum image-array-layer limit is applied automatically.
         * @return Number of history layers actually allocated.
         * @throws mxvk::Exception when either requested dimension is zero.
         */
        uint32_t enableSpectrumHistoryTexture(uint32_t bins, uint32_t layers);

        /**
         * @brief Upload one FFT spectrum into the next history layer.
         *
         * @param magnitudes Pointer to @p bins frequency magnitudes.
         * @param bins Number of values; must match the configured history.
         */
        void updateSpectrumHistoryTexture(const float *magnitudes, uint32_t bins);

        /** @return Physical array layer containing the newest FFT spectrum. */
        [[nodiscard]] uint32_t getSpectrumHistoryHead() const { return spectrumHistoryHead; }

        /** @return Number of allocated FFT spectrum-history layers. */
        [[nodiscard]] uint32_t getSpectrumHistoryLayerCount() const { return spectrumHistoryLayers; }

      private:
        struct SpriteVertex {
            float pos[2];
            float texCoord[2];
        };

        struct SpriteDrawCmd {
            float x, y, w, h;
            float rotation;
            glm::vec4 params;
        };

        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkPipelineCache pipelineCache = VK_NULL_HANDLE;
        VkImage spriteImage = VK_NULL_HANDLE;
        VkDeviceMemory spriteImageMemory = VK_NULL_HANDLE;
        VkImageView spriteImageView = VK_NULL_HANDLE;
        int spriteWidth = 0;
        int spriteHeight = 0;
        bool spriteLoaded = false;
        bool externalTexture = false;
        VkShaderModule fragmentShaderModule = VK_NULL_HANDLE;
        bool hasCustomShader = false;
        glm::vec4 shaderParams = glm::vec4(0.0f);
        bool effectsEnabled = true;
        std::vector<SpriteDrawCmd> drawQueue;

        VkPipeline customPipeline = VK_NULL_HANDLE;
        VkPipelineLayout customPipelineLayout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        std::string vertexShaderPath;
        std::string fragmentShaderPath;
        void createCustomPipeline();

        VkBuffer quadVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory quadVertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer quadIndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory quadIndexBufferMemory = VK_NULL_HANDLE;
        bool quadBufferCreated = false;
        void createQuadBuffer();

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> descriptorPools{};
        VkDescriptorPool descriptorSetPool = VK_NULL_HANDLE;
        uint32_t nextDescriptorPoolSets = 16;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::unordered_map<VkImageView, VkDescriptorSet> externalDescriptorSets{};
        VkFilter textureFilter = VK_FILTER_LINEAR;
        void createDescriptorPool();
        void destroyDescriptorPools();
        void destroyTextureDescriptorPools();
        VkDescriptorSet createDescriptorSet(VkImageView imageView);
        void destroySpriteResources();

        VkBuffer persistentStagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory persistentStagingMemory = VK_NULL_HANDLE;
        void *persistentStagingMapped = nullptr;
        VkDeviceSize persistentStagingSize = 0;
        VkFence uploadFence = VK_NULL_HANDLE;
        VkCommandBuffer uploadCmdBuffer = VK_NULL_HANDLE;
        bool stagingResourcesCreated = false;
        [[nodiscard]] VkDeviceSize stagingAllocationSize(VkDeviceSize requiredSize) const;
        void createStagingResources(VkDeviceSize size);
        void destroyStagingResources();
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory);
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                               uint32_t baseArrayLayer = 0, uint32_t layerCount = 1);
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                   uint32_t baseArrayLayer = 0, uint32_t layerCount = 1);
        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                         VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                         VkImage &image, VkDeviceMemory &imageMemory,
                         uint32_t arrayLayers = 1,
                         VkImageType imageType = VK_IMAGE_TYPE_2D);
        VkImageView createImageView(VkImage image, VkFormat format,
                                    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
                                    uint32_t layerCount = 1);
        void createSampler();
        SDL_Surface *convertToRGBA(SDL_Surface *surface);
        void createSpriteTexture(SDL_Surface *surface);
        void updateSpriteTexture(const void *pixels, uint32_t width, uint32_t height);
#ifdef MXVK_CUDA
        void destroyCudaInterop();
        bool ensureCudaInterop();
        bool transitionCudaImageForWrite();
        bool transitionCudaImageForShaderRead();
        bool updateTextureCudaHost(const void *pixels, uint32_t width, uint32_t height, uint32_t pitch);
        void recordCudaReadyBarrier(VkCommandBuffer cmdBuffer);
        void createCudaExportableImage(uint32_t width, uint32_t height, VkImage &image, VkDeviceMemory &imageMemory);
        VkDeviceSize cudaExportMemorySize = 0;
        cudaExternalMemory_t cudaExternalMemory = nullptr;
        cudaMipmappedArray_t cudaMipmappedArray = nullptr;
        cudaArray_t cudaArray = nullptr;
        bool cudaInteropEnabled = false;
        bool cudaInteropUnavailableLogged = false;
        bool cudaUploadLogged = false;
        bool cudaWriteTransitionLogged = false;
        bool cudaSampleBarrierLogged = false;
        bool cudaImageNeedsShaderBarrier = false;
        VkImageLayout cudaImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
#endif
        std::vector<char> readShaderFile(const std::string &filename);

        struct SpriteExtendedUBO {
            glm::vec4 mouse;
            glm::vec4 u0;
            glm::vec4 u1;
            glm::vec4 u2;
            glm::vec4 u3;
            std::array<glm::vec4, MAX_CUSTOM_UNIFORMS / 4> custom_uniforms;
            glm::vec4 audio_bands;
            glm::vec4 audio_history;
        };
        bool extendedUBOEnabled = false;
        SpriteExtendedUBO extendedUBOData{};
        VkBuffer extendedUBOBuffer = VK_NULL_HANDLE;
        VkDeviceMemory extendedUBOMemory = VK_NULL_HANDLE;
        void *extendedUBOMapped = nullptr;
        VkDescriptorSetLayout extendedDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool extendedDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet extendedDescriptorSet = VK_NULL_HANDLE;
        bool ownExtendedDescriptorSetLayout = false;
        void createExtendedUBO();
        void updateExtendedUBO();
        void createExtendedDescriptorSetLayout();
        void createExtendedDescriptorSet();
        void destroyExtendedUBO();

        bool historyTextureEnabled = false;
        VkImage historyImage = VK_NULL_HANDLE;
        VkDeviceMemory historyImageMemory = VK_NULL_HANDLE;
        VkImageView historyImageView = VK_NULL_HANDLE;
        uint32_t historyWidth = 0;
        uint32_t historyHeight = 0;
        uint32_t historyLayers = 0;
        uint32_t historyHead = 0;
        void destroyHistoryTexture();

        bool spectrumTextureEnabled = false;
        VkImage spectrumImage = VK_NULL_HANDLE;
        VkDeviceMemory spectrumImageMemory = VK_NULL_HANDLE;
        VkImageView spectrumImageView = VK_NULL_HANDLE;
        uint32_t spectrumBins = 0;
        void destroySpectrumTexture();

        bool spectrumHistoryTextureEnabled = false;
        VkImage spectrumHistoryImage = VK_NULL_HANDLE;
        VkDeviceMemory spectrumHistoryImageMemory = VK_NULL_HANDLE;
        VkImageView spectrumHistoryImageView = VK_NULL_HANDLE;
        uint32_t spectrumHistoryBins = 0;
        uint32_t spectrumHistoryLayers = 0;
        uint32_t spectrumHistoryHead = 0;
        uint32_t spectrumHistoryWriteIndex = 0;
        void destroySpectrumHistoryTexture();
        void recreateExtendedDescriptorLayout();

        struct SpriteInstanceData {
            float posX, posY, sizeW, sizeH;
            float params[4];
        };
        VkBuffer instanceBuffer = VK_NULL_HANDLE;
        VkDeviceMemory instanceBufferMemory = VK_NULL_HANDLE;
        void *instanceBufferMapped = nullptr;
        uint32_t instanceBufferCapacity = 0;
        bool instancingEnabled = false;
        VkPipeline instancedPipeline = VK_NULL_HANDLE;
        VkPipelineLayout instancedPipelineLayout = VK_NULL_HANDLE;
        std::string instanceVertPath;
        std::string instanceFragPath;
        void createInstancedPipeline(const std::string &vertPath, const std::string &fragPath);
        void ensureInstanceBuffer(uint32_t count);
        void destroyInstanceResources();
    };

} // namespace mxvk
