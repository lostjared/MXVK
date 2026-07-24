/**
 * @file mxvk_model.hpp
 * @brief Vulkan mesh loader and GPU buffer manager for MXVK.
 */
#pragma once

#include <volk/volk.h>

#include "mxvk_exception.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace mxvk {

    /**
     * @struct VKVertex
     * @brief Vertex payload consumed by MXVK model shaders.
     */
    struct VKVertex {
        float pos[3]{};
        float texCoord[2]{};
        float normal[3]{};

        /** @brief Byte-wise equality used for index compression. */
        bool operator==(const VKVertex &other) const {
            return std::memcmp(this, &other, sizeof(VKVertex)) == 0;
        }
    };

    /** @brief Hash functor for VKVertex. */
    struct VKVertexHash {
        std::size_t operator()(const VKVertex &v) const;
    };

    /**
     * @struct SubMesh
     * @brief One indexed sub-range that can reference a dedicated texture slot.
     */
    struct SubMesh {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t textureIndex = 0;
        std::string materialName;
    };

    /**
     * @struct MXMaterial
     * @brief Parsed subset of Wavefront MTL material fields.
     */
    struct MXMaterial {
        std::string name;
        float ka[3] = {0.2f, 0.2f, 0.2f};
        float kd[3] = {0.8f, 0.8f, 0.8f};
        float ks[3] = {0.0f, 0.0f, 0.0f};
        float ns = 30.0f;
        float d = 1.0f;
        int illum = 2;
        std::string map_kd;
    };

    /**
     * @class MXModel
     * @brief Loads OBJ/MXMOD meshes and uploads them to Vulkan buffers.
     */
    class MXModel {
      public:
        MXModel() = default;
        ~MXModel() = default;

        MXModel(const MXModel &) = delete;
        MXModel &operator=(const MXModel &) = delete;
        MXModel(MXModel &&other) noexcept;
        MXModel &operator=(MXModel &&other) noexcept;

        /**
         * @brief Parse model data from disk into CPU-side arrays.
         * @param path Path to .obj, .mxmod, or .mxmod.z model.
         * @param positionScale Uniform scale applied to positions.
         */
        void load(const std::string &path, float positionScale = 1.0f);

        /**
         * @brief Parse model data and an optional texture manifest from disk.
         * @param path Path to .obj, .mxmod, or .mxmod.z model.
         * @param textureManifestPath Optional .txt/.tex or MTL-like texture manifest.
         * @param textureBasePath Base path used to resolve relative texture paths.
         * @param positionScale Uniform scale applied to positions.
         */
        void load(const std::string &path,
                  const std::string &textureManifestPath,
                  const std::string &textureBasePath,
                  float positionScale = 1.0f);

        /**
         * @brief Export the loaded model as Wavefront OBJ plus MTL.
         * @param objPath Output .obj path.
         * @param mtlPath Optional output .mtl path. Defaults to objPath with .mtl extension.
         */
        void exportOBJ(const std::string &objPath, const std::string &mtlPath = "") const;

        /**
         * @brief Load a model/manifest pair and export it as Wavefront OBJ plus MTL.
         */
        static void exportOBJ(const std::string &modelPath,
                              const std::string &textureManifestPath,
                              const std::string &textureBasePath,
                              const std::string &objPath,
                              float positionScale = 1.0f);

        /**
         * @brief Upload parsed geometry to device-local GPU buffers.
         * @param device Logical Vulkan device.
         * @param physicalDevice Physical Vulkan device.
         * @param commandPool Command pool used for transfer command buffer allocation.
         * @param graphicsQueue Queue used to submit transfer commands.
         */
        void upload(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool, VkQueue graphicsQueue);

        /**
         * @brief Release owned GPU buffers.
         * @param device Logical Vulkan device that owns these resources.
         */
        void cleanup(VkDevice device);

        /**
         * @brief Record one indexed draw for the full mesh.
         * @param cmd Active command buffer.
         */
        void draw(VkCommandBuffer cmd) const;

        /**
         * @brief Record one indexed draw for a sub-mesh.
         * @param cmd Active command buffer.
         * @param index Sub-mesh index.
         */
        void drawSubMesh(VkCommandBuffer cmd, size_t index) const;

        /** @brief Remove duplicate vertices and remap indices. */
        void compressIndices();

        [[nodiscard]] const std::vector<VKVertex> &vertices() const { return verticesData; }
        [[nodiscard]] const std::vector<uint32_t> &indices() const { return indicesData; }
        [[nodiscard]] uint32_t indexCount() const { return static_cast<uint32_t>(indicesData.size()); }

        [[nodiscard]] VkBuffer vertexBuffer() const { return vertexBufferHandle; }
        [[nodiscard]] VkBuffer indexBuffer() const { return indexBufferHandle; }

        [[nodiscard]] size_t subMeshCount() const { return subMeshList.size(); }
        [[nodiscard]] const SubMesh &subMesh(size_t i) const { return subMeshList[i]; }
        [[nodiscard]] const std::vector<SubMesh> &subMeshes() const { return subMeshList; }

        [[nodiscard]] const std::vector<MXMaterial> &materials() const { return materialList; }
        [[nodiscard]] const std::string &mtlLibPath() const { return mtlLibraryPath; }

      private:
        std::vector<VKVertex> verticesData{};
        std::vector<uint32_t> indicesData{};
        std::vector<SubMesh> subMeshList{};
        std::vector<MXMaterial> materialList{};
        std::string mtlLibraryPath{};

        VkBuffer vertexBufferHandle = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBufferHandle = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;

        static void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                                 VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer &buffer, VkDeviceMemory &bufferMemory);

        static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                       uint32_t typeFilter, VkMemoryPropertyFlags properties);

        static void copyBuffer(VkDevice device, VkCommandPool commandPool,
                               VkQueue graphicsQueue,
                               VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

        void loadOBJ(const std::string &path, float positionScale);
        void loadMXMOD(const std::string &path, float positionScale);
        void loadMXMODZ(const std::string &path, float positionScale);
        void loadMTL(const std::string &path);
        void loadTextureManifest(const std::string &path, const std::string &textureBasePath);
    };

} // namespace mxvk
