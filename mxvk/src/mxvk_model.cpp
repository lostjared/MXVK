#include "mxvk/mxvk_model.hpp"

#include <cmath>
#include <cstdio>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace mxvk {

    namespace {
        struct Vec2 {
            float x{};
            float y{};
        };

        struct Vec3 {
            float x{};
            float y{};
            float z{};
        };
    } // namespace

    std::size_t VKVertexHash::operator()(const VKVertex &v) const {
        std::size_t seed = 0;
        auto combine = [&seed](float value) {
            std::hash<float> hasher;
            seed ^= hasher(value) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };

        for (int i = 0; i < 3; ++i) {
            combine(v.pos[i]);
        }
        for (int i = 0; i < 2; ++i) {
            combine(v.texCoord[i]);
        }
        for (int i = 0; i < 3; ++i) {
            combine(v.normal[i]);
        }

        return seed;
    }

    void MXModel::compressIndices() {
        if (vertices_.empty() || indices_.empty()) {
            return;
        }

        std::vector<VKVertex> uniqueVertices{};
        uniqueVertices.reserve(vertices_.size());

        std::unordered_map<VKVertex, uint32_t, VKVertexHash> vertexMap{};
        std::vector<uint32_t> remap(vertices_.size(), 0);

        for (size_t i = 0; i < vertices_.size(); ++i) {
            const auto it = vertexMap.find(vertices_[i]);
            if (it != vertexMap.end()) {
                remap[i] = it->second;
                continue;
            }

            const uint32_t nextIndex = static_cast<uint32_t>(uniqueVertices.size());
            uniqueVertices.push_back(vertices_[i]);
            vertexMap.emplace(vertices_[i], nextIndex);
            remap[i] = nextIndex;
        }

        for (uint32_t &idx : indices_) {
            if (idx < static_cast<uint32_t>(remap.size())) {
                idx = remap[idx];
            }
        }

        vertices_ = std::move(uniqueVertices);
    }

    void MXModel::load(const std::string &path, float positionScale) {
        if (path.empty()) {
            throw mxvk::Exception("MXModel::load path is empty");
        }

        if (path.ends_with(".obj")) {
            loadOBJ(path, positionScale);
            return;
        }

        if (path.ends_with(".mxmod")) {
            loadMXMOD(path, positionScale);
            return;
        }

        if (path.ends_with(".mxmod.z")) {
            throw mxvk::Exception("MXModel::load does not support compressed .mxmod.z in this MXVK build");
        }

        throw mxvk::Exception("MXModel::load unsupported file format: " + path);
    }

    void MXModel::loadOBJ(const std::string &path, float positionScale) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw mxvk::Exception("MXModel::loadOBJ failed to open file: " + path);
        }

        std::vector<Vec3> positions{};
        std::vector<Vec2> texcoords{};
        std::vector<Vec3> normals{};

        vertices_.clear();
        indices_.clear();
        subMeshes_.clear();
        materials_.clear();
        mtlLibPath_.clear();

        std::vector<VKVertex> currentVerts{};
        std::string currentMaterialName{};

        auto finalizeGroup = [&]() {
            if (currentVerts.empty()) {
                return;
            }

            SubMesh sm{};
            sm.firstIndex = static_cast<uint32_t>(indices_.size());
            sm.indexCount = static_cast<uint32_t>(currentVerts.size());
            sm.materialName = currentMaterialName;

            const uint32_t baseVertex = static_cast<uint32_t>(vertices_.size());
            vertices_.insert(vertices_.end(), currentVerts.begin(), currentVerts.end());

            for (uint32_t i = 0; i < sm.indexCount; ++i) {
                indices_.push_back(baseVertex + i);
            }

            subMeshes_.push_back(sm);
            currentVerts.clear();
        };

        std::string line{};
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream stream(line);
            std::string tag{};
            stream >> tag;

            if (tag == "v") {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                if (stream >> x >> y >> z) {
                    positions.push_back({x * positionScale, y * positionScale, z * positionScale});
                }
                continue;
            }

            if (tag == "vt") {
                float u = 0.0f;
                float v = 0.0f;
                if (stream >> u >> v) {
                    texcoords.push_back({u, v});
                }
                continue;
            }

            if (tag == "vn") {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                if (stream >> x >> y >> z) {
                    normals.push_back({x, y, z});
                }
                continue;
            }

            if (tag == "mtllib") {
                std::string mtlFile{};
                if (stream >> mtlFile) {
                    const std::filesystem::path objPath(path);
                    mtlLibPath_ = (objPath.parent_path() / mtlFile).string();
                }
                continue;
            }

            if (tag == "g" || tag == "o") {
                finalizeGroup();
                continue;
            }

            if (tag == "usemtl") {
                std::string materialName{};
                stream >> materialName;
                if (!materialName.empty() && materialName != currentMaterialName) {
                    finalizeGroup();
                    currentMaterialName = materialName;
                }
                continue;
            }

            if (tag != "f") {
                continue;
            }

            std::vector<VKVertex> faceVerts{};
            std::string token{};
            while (stream >> token) {
                int vi = 0;
                int ti = 0;
                int ni = 0;

                if (std::sscanf(token.c_str(), "%d/%d/%d", &vi, &ti, &ni) != 3) {
                    if (std::sscanf(token.c_str(), "%d//%d", &vi, &ni) == 2) {
                        ti = 0;
                    } else if (std::sscanf(token.c_str(), "%d/%d", &vi, &ti) == 2) {
                        ni = 0;
                    } else {
                        std::sscanf(token.c_str(), "%d", &vi);
                        ti = 0;
                        ni = 0;
                    }
                }

                VKVertex vertex{};
                if (vi != 0) {
                    const int idx = vi > 0 ? vi - 1 : static_cast<int>(positions.size()) + vi;
                    if (idx >= 0 && idx < static_cast<int>(positions.size())) {
                        vertex.pos[0] = positions[static_cast<size_t>(idx)].x;
                        vertex.pos[1] = positions[static_cast<size_t>(idx)].y;
                        vertex.pos[2] = positions[static_cast<size_t>(idx)].z;
                    }
                }
                if (ti != 0) {
                    const int idx = ti > 0 ? ti - 1 : static_cast<int>(texcoords.size()) + ti;
                    if (idx >= 0 && idx < static_cast<int>(texcoords.size())) {
                        vertex.texCoord[0] = texcoords[static_cast<size_t>(idx)].x;
                        vertex.texCoord[1] = texcoords[static_cast<size_t>(idx)].y;
                    }
                }
                if (ni != 0) {
                    const int idx = ni > 0 ? ni - 1 : static_cast<int>(normals.size()) + ni;
                    if (idx >= 0 && idx < static_cast<int>(normals.size())) {
                        vertex.normal[0] = normals[static_cast<size_t>(idx)].x;
                        vertex.normal[1] = normals[static_cast<size_t>(idx)].y;
                        vertex.normal[2] = normals[static_cast<size_t>(idx)].z;
                    }
                }

                faceVerts.push_back(vertex);
            }

            for (size_t i = 2; i < faceVerts.size(); ++i) {
                currentVerts.push_back(faceVerts[0]);
                currentVerts.push_back(faceVerts[i - 1]);
                currentVerts.push_back(faceVerts[i]);
            }
        }

        finalizeGroup();

        if (!mtlLibPath_.empty()) {
            loadMTL(mtlLibPath_);
            std::unordered_map<std::string, uint32_t> materialIndices{};
            for (uint32_t i = 0; i < static_cast<uint32_t>(materials_.size()); ++i) {
                materialIndices.emplace(materials_[i].name, i);
            }

            for (SubMesh &sm : subMeshes_) {
                const auto it = materialIndices.find(sm.materialName);
                if (it != materialIndices.end()) {
                    sm.textureIndex = it->second;
                }
            }
        }

        if (vertices_.empty() || indices_.empty()) {
            throw mxvk::Exception("MXModel::loadOBJ no geometry found in " + path);
        }

        const bool hasNormals = !normals.empty();
        if (!hasNormals) {
            for (size_t i = 0; i + 2 < vertices_.size(); i += 3) {
                const float ax = vertices_[i + 1].pos[0] - vertices_[i].pos[0];
                const float ay = vertices_[i + 1].pos[1] - vertices_[i].pos[1];
                const float az = vertices_[i + 1].pos[2] - vertices_[i].pos[2];
                const float bx = vertices_[i + 2].pos[0] - vertices_[i].pos[0];
                const float by = vertices_[i + 2].pos[1] - vertices_[i].pos[1];
                const float bz = vertices_[i + 2].pos[2] - vertices_[i].pos[2];

                float nx = ay * bz - az * by;
                float ny = az * bx - ax * bz;
                float nz = ax * by - ay * bx;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0.0f) {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }

                for (int j = 0; j < 3; ++j) {
                    vertices_[i + static_cast<size_t>(j)].normal[0] = nx;
                    vertices_[i + static_cast<size_t>(j)].normal[1] = ny;
                    vertices_[i + static_cast<size_t>(j)].normal[2] = nz;
                }
            }
        }

        compressIndices();
    }

    void MXModel::loadMXMOD(const std::string &path, float positionScale) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw mxvk::Exception("MXModel::loadMXMOD failed to open file: " + path);
        }

        struct TriBlock {
            uint32_t textureIndex = 0;
            std::vector<Vec3> pos{};
            std::vector<Vec2> uv{};
            std::vector<Vec3> nrm{};
            std::vector<uint32_t> fileIndices{};
        };

        std::vector<TriBlock> triBlocks{};
        TriBlock *current = nullptr;
        int sectionType = -1;

        auto trim = [](std::string &s) {
            const size_t begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                s.clear();
                return;
            }
            const size_t end = s.find_last_not_of(" \t\r\n");
            s = s.substr(begin, end - begin + 1);
        };

        std::string line{};
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            const size_t commentPos = line.find('#');
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }
            trim(line);
            if (line.empty()) {
                continue;
            }

            std::istringstream stream(line);
            const char c = line[line.find_first_not_of(" \t")];
            const bool isData = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';

            if (isData && current != nullptr) {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;

                switch (sectionType) {
                case 0:
                    if (stream >> x >> y >> z) {
                        current->pos.push_back({x * positionScale, y * positionScale, z * positionScale});
                    }
                    break;
                case 1:
                    if (stream >> x >> y) {
                        current->uv.push_back({x, y});
                    }
                    break;
                case 2:
                    if (stream >> x >> y >> z) {
                        current->nrm.push_back({x, y, z});
                    }
                    break;
                case 5: {
                    uint32_t idx = 0;
                    while (stream >> idx) {
                        current->fileIndices.push_back(idx);
                    }
                } break;
                default:
                    break;
                }

                continue;
            }

            std::string tag{};
            stream >> tag;
            if (tag == "tri") {
                uint32_t surfaceType = 0;
                uint32_t textureIndex = 0;
                stream >> surfaceType >> textureIndex;
                static_cast<void>(surfaceType);
                triBlocks.emplace_back();
                current = &triBlocks.back();
                current->textureIndex = textureIndex;
                sectionType = -1;
                continue;
            }

            if (tag == "vert") {
                sectionType = 0;
                continue;
            }
            if (tag == "tex") {
                sectionType = 1;
                continue;
            }
            if (tag == "norm") {
                sectionType = 2;
                continue;
            }
            if (tag == "indices") {
                sectionType = 5;
                continue;
            }
        }

        if (triBlocks.empty()) {
            throw mxvk::Exception("MXModel::loadMXMOD no geometry found in " + path);
        }

        vertices_.clear();
        indices_.clear();
        subMeshes_.clear();
        materials_.clear();
        mtlLibPath_.clear();

        for (const TriBlock &blk : triBlocks) {
            if (blk.pos.empty()) {
                continue;
            }

            const uint32_t vertexBase = static_cast<uint32_t>(vertices_.size());
            for (size_t i = 0; i < blk.pos.size(); ++i) {
                VKVertex v{};
                v.pos[0] = blk.pos[i].x;
                v.pos[1] = blk.pos[i].y;
                v.pos[2] = blk.pos[i].z;

                if (i < blk.uv.size()) {
                    v.texCoord[0] = blk.uv[i].x;
                    v.texCoord[1] = blk.uv[i].y;
                }
                if (i < blk.nrm.size()) {
                    v.normal[0] = blk.nrm[i].x;
                    v.normal[1] = blk.nrm[i].y;
                    v.normal[2] = blk.nrm[i].z;
                }

                vertices_.push_back(v);
            }

            const uint32_t firstIndex = static_cast<uint32_t>(indices_.size());
            if (!blk.fileIndices.empty()) {
                for (uint32_t idx : blk.fileIndices) {
                    indices_.push_back(vertexBase + idx);
                }
            } else {
                for (uint32_t i = 0; i < static_cast<uint32_t>(blk.pos.size()); ++i) {
                    indices_.push_back(vertexBase + i);
                }
            }

            SubMesh sm{};
            sm.firstIndex = firstIndex;
            sm.indexCount = static_cast<uint32_t>(indices_.size()) - firstIndex;
            sm.textureIndex = blk.textureIndex;
            subMeshes_.push_back(sm);
        }

        if (vertices_.empty() || indices_.empty()) {
            throw mxvk::Exception("MXModel::loadMXMOD no renderable data found in " + path);
        }

        compressIndices();
    }

    void MXModel::upload(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkCommandPool commandPool, VkQueue graphicsQueue) {
        if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
            commandPool == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE) {
            throw mxvk::Exception("MXModel::upload requires valid Vulkan handles");
        }
        if (vertices_.empty() || indices_.empty()) {
            throw mxvk::Exception("MXModel::upload requires loaded geometry");
        }

        cleanup(device);

        const VkDeviceSize vertexBufferSize = sizeof(VKVertex) * vertices_.size();
        const VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices_.size();

        VkBuffer stagingVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingVertexMemory = VK_NULL_HANDLE;
        createBuffer(device, physicalDevice, vertexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingVertexBuffer, stagingVertexMemory);

        VkBuffer stagingIndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingIndexMemory = VK_NULL_HANDLE;
        createBuffer(device, physicalDevice, indexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingIndexBuffer, stagingIndexMemory);

        void *vertexData = nullptr;
        vkMapMemory(device, stagingVertexMemory, 0, vertexBufferSize, 0, &vertexData);
        std::memcpy(vertexData, vertices_.data(), static_cast<size_t>(vertexBufferSize));
        vkUnmapMemory(device, stagingVertexMemory);

        void *indexData = nullptr;
        vkMapMemory(device, stagingIndexMemory, 0, indexBufferSize, 0, &indexData);
        std::memcpy(indexData, indices_.data(), static_cast<size_t>(indexBufferSize));
        vkUnmapMemory(device, stagingIndexMemory);

        createBuffer(device, physicalDevice, vertexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     vertexBuffer_, vertexBufferMemory_);

        createBuffer(device, physicalDevice, indexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     indexBuffer_, indexBufferMemory_);

        copyBuffer(device, commandPool, graphicsQueue, stagingVertexBuffer, vertexBuffer_, vertexBufferSize);
        copyBuffer(device, commandPool, graphicsQueue, stagingIndexBuffer, indexBuffer_, indexBufferSize);

        vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
        vkFreeMemory(device, stagingVertexMemory, nullptr);
        vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
        vkFreeMemory(device, stagingIndexMemory, nullptr);
    }

    void MXModel::cleanup(VkDevice device) {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        if (vertexBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertexBuffer_, nullptr);
            vertexBuffer_ = VK_NULL_HANDLE;
        }
        if (vertexBufferMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertexBufferMemory_, nullptr);
            vertexBufferMemory_ = VK_NULL_HANDLE;
        }

        if (indexBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, indexBuffer_, nullptr);
            indexBuffer_ = VK_NULL_HANDLE;
        }
        if (indexBufferMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device, indexBufferMemory_, nullptr);
            indexBufferMemory_ = VK_NULL_HANDLE;
        }
    }

    void MXModel::draw(VkCommandBuffer cmd) const {
        if (cmd == VK_NULL_HANDLE || vertexBuffer_ == VK_NULL_HANDLE || indexBuffer_ == VK_NULL_HANDLE || indices_.empty()) {
            return;
        }

        const VkBuffer buffers[] = {vertexBuffer_};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, indexCount(), 1, 0, 0, 0);
    }

    void MXModel::drawSubMesh(VkCommandBuffer cmd, size_t index) const {
        if (cmd == VK_NULL_HANDLE || index >= subMeshes_.size() ||
            vertexBuffer_ == VK_NULL_HANDLE || indexBuffer_ == VK_NULL_HANDLE) {
            return;
        }

        const VkBuffer buffers[] = {vertexBuffer_};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

        const SubMesh &sm = subMeshes_[index];
        if (sm.indexCount == 0) {
            return;
        }

        vkCmdDrawIndexed(cmd, sm.indexCount, 1, sm.firstIndex, 0, 0);
    }

    void MXModel::createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
        if (size == 0) {
            throw mxvk::Exception("MXModel::createBuffer cannot allocate zero-sized buffer");
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw mxvk::Exception("MXModel::createBuffer failed to create VkBuffer");
        }

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            throw mxvk::Exception("MXModel::createBuffer failed to allocate memory");
        }

        if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS) {
            vkDestroyBuffer(device, buffer, nullptr);
            vkFreeMemory(device, bufferMemory, nullptr);
            buffer = VK_NULL_HANDLE;
            bufferMemory = VK_NULL_HANDLE;
            throw mxvk::Exception("MXModel::createBuffer failed to bind memory");
        }
    }

    uint32_t MXModel::findMemoryType(VkPhysicalDevice physicalDevice,
                                     uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            const bool typeMatches = (typeFilter & (1u << i)) != 0u;
            const bool flagsMatch = (memProperties.memoryTypes[i].propertyFlags & properties) == properties;
            if (typeMatches && flagsMatch) {
                return i;
            }
        }

        throw mxvk::Exception("MXModel::findMemoryType failed to find suitable memory type");
    }

    void MXModel::copyBuffer(VkDevice device, VkCommandPool commandPool,
                             VkQueue graphicsQueue,
                             VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        if (size == 0) {
            return;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            throw mxvk::Exception("MXModel::copyBuffer failed to allocate command buffer");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw mxvk::Exception("MXModel::copyBuffer failed to begin command buffer");
        }

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw mxvk::Exception("MXModel::copyBuffer failed to end command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw mxvk::Exception("MXModel::copyBuffer failed to submit command buffer");
        }

        if (vkQueueWaitIdle(graphicsQueue) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw mxvk::Exception("MXModel::copyBuffer failed to wait for queue idle");
        }

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void MXModel::loadMTL(const std::string &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return;
        }

        MXMaterial *current = nullptr;
        std::string line{};
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream stream(line);
            std::string tag{};
            stream >> tag;

            if (tag == "newmtl") {
                materials_.emplace_back();
                current = &materials_.back();
                stream >> current->name;
                continue;
            }

            if (current == nullptr) {
                continue;
            }

            if (tag == "Ka") {
                stream >> current->ka[0] >> current->ka[1] >> current->ka[2];
            } else if (tag == "Kd") {
                stream >> current->kd[0] >> current->kd[1] >> current->kd[2];
            } else if (tag == "Ks") {
                stream >> current->ks[0] >> current->ks[1] >> current->ks[2];
            } else if (tag == "Ns") {
                stream >> current->ns;
            } else if (tag == "d") {
                stream >> current->d;
            } else if (tag == "illum") {
                stream >> current->illum;
            } else if (tag == "map_Kd") {
                stream >> current->map_kd;
            }
        }
    }

} // namespace mxvk
