/**
 * @file mxvk_abstract_model.hpp
 * @brief High-level model wrapper integrated with MXVK dynamic rendering.
 */
#ifndef _MXVK_ABSTRACT_MODEL_H_
#define _MXVK_ABSTRACT_MODEL_H_

#include <volk/volk.h>

#include "mxvk.hpp"
#include "mxvk_model.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>
#ifdef MXVK_CUDA
#include <cuda_runtime_api.h>
#include <opencv2/core/cuda.hpp>
#endif

namespace mxvk {

    /**
     * @struct UniformBufferObject
     * @brief Default transform UBO payload for model shaders.
     */
    struct UniformBufferObject {
        glm::mat4 model{1.0f};
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        glm::vec4 fx{0.0f, 0.0f, 0.0f, 0.0f};
    };

    /**
     * @class VKAbstractModel
     * @brief Convenience wrapper that owns mesh, textures, descriptors, and pipeline state.
     *
     * This class is intended to be recorded from inside
     * `VK_Window::onRecordCustomRendering()` so it participates in the same
     * dynamic-rendering pass as sprites/text.
     */
    class VKAbstractModel {
      public:
        VKAbstractModel() = default;
        ~VKAbstractModel() = default;

        VKAbstractModel(const VKAbstractModel &) = delete;
        VKAbstractModel &operator=(const VKAbstractModel &) = delete;
        VKAbstractModel(VKAbstractModel &&) = delete;
        VKAbstractModel &operator=(VKAbstractModel &&) = delete;

        /**
         * @brief Load mesh/texture resources and build Vulkan state.
         * @param window Active MXVK window.
         * @param modelPath Path to .obj or .mxmod mesh file.
         * @param textureManifestPath Optional texture manifest path (.tex or .mtl-like text).
         * @param textureBasePath Optional base path for texture files in the manifest.
         * @param scale Uniform mesh scale.
         */
        void load(VK_Window *window,
                  const std::string &modelPath,
                  const std::string &textureManifestPath,
                  const std::string &textureBasePath,
                  float scale = 1.0f);

        /**
         * @brief Consume pre-parsed mesh data and build Vulkan state.
         * @param window Active MXVK window.
         * @param model Pre-parsed CPU-side model data.
         * @param textureManifestPath Optional texture manifest path (.tex or .mtl-like text).
         * @param textureBasePath Optional base path for texture files in the manifest.
         * @param scale Uniform mesh scale. Kept for API compatibility.
         */
        void load(VK_Window *window,
                  MXModel &&model,
                  const std::string &textureManifestPath,
                  const std::string &textureBasePath,
                  [[maybe_unused]] float scale = 1.0f);

        /**
         * @brief Configure custom shader paths and rebuild pipelines.
         * @param window Active MXVK window.
         * @param vertSpv Vertex shader SPIR-V path.
         * @param fragSpv Fragment shader SPIR-V path.
         */
        void setShaders(VK_Window *window, const std::string &vertSpv, const std::string &fragSpv);

        /**
         * @brief Update one per-frame UBO payload.
         * @param imageIndex Swapchain image index.
         * @param ubo New transform values.
         */
        void updateUBO(uint32_t imageIndex, const UniformBufferObject &ubo);

        /**
         * @brief Upload raw RGBA pixels into the primary model texture.
         * @param pixels Pointer to RGBA8 pixel data.
         * @param width Texture width in pixels.
         * @param height Texture height in pixels.
         * @param pitch Bytes per input row. When 0, defaults to width * 4.
         * @return True when the upload succeeds, false for invalid inputs or unavailable resources.
         */
        [[nodiscard]] bool updatePrimaryTexture(const void *pixels, int width, int height, int pitch = 0);

#ifdef MXVK_CUDA
        /**
         * @brief Upload RGBA8 pixels from CUDA device memory into the primary model texture.
         *
         * The Vulkan texture is imported into CUDA as a mipmapped array because
         * sampled Vulkan images use opaque optimal tiling, not a linear pitched layout.
         */
        [[nodiscard]] bool updatePrimaryTextureCuda(const cv::cuda::GpuMat &rgba, cv::cuda::Stream &stream);
#endif

        /**
         * @brief Record draw commands for this model.
         * @param cmd Active command buffer, inside a dynamic rendering scope.
         * @param imageIndex Current swapchain image index.
         * @param wireframe Render using the optional wireframe pipeline when available.
         */
        void render(VkCommandBuffer cmd, uint32_t imageIndex, bool wireframe = false) const;

        /**
         * @brief Record one draw using push constants for per-draw transforms and an explicit texture slot.
         * @param cmd Active command buffer, inside a dynamic rendering scope.
         * @param imageIndex Current swapchain image index.
         * @param textureIndex Texture slot to bind for all submeshes in this draw.
         * @param ubo Transform/effect payload copied into vertex-stage push constants.
         * @param wireframe Render using the optional wireframe pipeline when available.
         */
        void renderWithPushConstants(VkCommandBuffer cmd,
                                     uint32_t imageIndex,
                                     size_t textureIndex,
                                     const UniformBufferObject &ubo,
                                     bool wireframe = false);

        /**
         * @brief Rebuild swapchain-dependent resources after resize.
         * @param window Active MXVK window.
         */
        void resize(VK_Window *window);

        /**
         * @brief Destroy all owned Vulkan resources.
         * @param window Active MXVK window.
         */
        void cleanup(VK_Window *window);

        /** @brief Access the underlying mesh object. */
        [[nodiscard]] const MXModel &model() const { return obj; }
        /** @brief Access the computed center offset used for normalization. */
        [[nodiscard]] glm::vec3 modelCenterOffset() const { return modelCenterOffsetValue; }
        /** @brief Access the computed render scale used for normalization. */
        [[nodiscard]] float modelRenderScale() const { return modelRenderScaleValue; }
        /** @brief Per-axis extent (max - min) of the source mesh's bounding box. */
        [[nodiscard]] glm::vec3 modelAxisExtent() const { return modelAxisExtentValue; }
        /** @brief True once the model has been uploaded to GPU buffers. */
        [[nodiscard]] bool isLoaded() const { return obj.indexCount() > 0 && vertexBufferReady(); }

        /**
         * @brief Enable or disable backface culling for this model pipeline.
         * @param enabled True to cull backfaces, false to disable culling.
         */
        void setBackfaceCulling(bool enabled);

        /**
         * @brief Enable or disable alpha blending for this model pipeline.
         * @param enabled True to blend fragment alpha and avoid depth writes.
         */
        void setAlphaBlending(bool enabled);

      private:
        struct TextureEntry {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            uint32_t width = 0;
            uint32_t height = 0;
#ifdef MXVK_CUDA
            VkDeviceSize cudaExportMemorySize = 0;
            cudaExternalMemory_t cudaExternalMemory = nullptr;
            cudaMipmappedArray_t cudaMipmappedArray = nullptr;
            cudaArray_t cudaArray = nullptr;
            bool cudaInteropEnabled = false;
            bool cudaInteropUnavailableLogged = false;
            bool cudaUploadLogged = false;
            bool cudaWriteTransitionLogged = false;
            bool cudaShaderTransitionLogged = false;
            VkImageLayout cudaImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
#endif
        };

        MXModel obj{};
        std::vector<TextureEntry> textures{};

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets{};

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipelineFill = VK_NULL_HANDLE;
        VkPipeline pipelineWireframe = VK_NULL_HANDLE;
        VkSampler textureSampler = VK_NULL_HANDLE;

        std::vector<VkBuffer> uniformBuffers{};
        std::vector<VkDeviceMemory> uniformBufferMemory{};
        std::vector<void *> uniformBuffersMapped{};

        glm::vec3 modelCenterOffsetValue{0.0f, 0.0f, 0.0f};
        float modelRenderScaleValue = 1.0f;
        glm::vec3 modelAxisExtentValue{1.0f, 1.0f, 1.0f};

        std::string vertexShaderPath{};
        std::string fragmentShaderPath{};
        bool backfaceCullingEnabled = false;
        bool alphaBlendingEnabled = false;

        VK_Window *windowPtr = nullptr;

        [[nodiscard]] bool vertexBufferReady() const { return obj.vertexBuffer() != VK_NULL_HANDLE && obj.indexBuffer() != VK_NULL_HANDLE; }

        void computeBoundsAndScale();
        void loadTextures(const std::string &textureManifestPath, const std::string &textureBasePath);
        void loadTexturesFromMTL(const std::string &textureBasePath);
        void createFallbackTexture();

        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const;
        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
        void createImage(uint32_t width, uint32_t height, VkFormat format,
                         VkImageTiling tiling, VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties, VkImage &image,
                         VkDeviceMemory &memory) const;
        void createTextureImage(uint32_t width, uint32_t height, TextureEntry &texture) const;
#ifdef MXVK_CUDA
        void createCudaExportableImage(uint32_t width, uint32_t height, TextureEntry &texture) const;
        void destroyTextureCudaInterop(TextureEntry &texture) const;
        [[nodiscard]] bool ensureTextureCudaInterop(TextureEntry &texture) const;
        [[nodiscard]] bool transitionTextureForCudaWrite(TextureEntry &texture) const;
        [[nodiscard]] bool transitionTextureForShaderRead(TextureEntry &texture) const;
        [[nodiscard]] bool updatePrimaryTextureCudaHost(TextureEntry &texture, const void *pixels,
                                                        uint32_t width, uint32_t height, uint32_t pitch) const;
        void recreatePrimaryTextureForCuda(TextureEntry &texture, uint32_t width, uint32_t height);
#endif
        [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const;
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) const;
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;

        void createTextureSampler();
        void createDescriptorSetLayout();
        void createUniformBuffers();
        void destroyUniformBuffers();
        void createDescriptorPool();
        void createDescriptorSets();
        void createPipelines();

        void destroyPipelines();
        void destroyDescriptors();
        void destroyTextures();
    };

    using VK_AbstractModel = VKAbstractModel;

} // namespace mxvk

#endif
