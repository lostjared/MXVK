#include "mxvk/mxvk_model.hpp"

#include <cmath>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <zlib.h>

namespace mxvk {

    MXModel::MXModel(MXModel &&other) noexcept
        : verticesData(std::move(other.verticesData)),
          indicesData(std::move(other.indicesData)),
          subMeshList(std::move(other.subMeshList)),
          materialList(std::move(other.materialList)),
          mtlLibraryPath(std::move(other.mtlLibraryPath)),
          vertexBufferHandle(other.vertexBufferHandle),
          vertexBufferMemory(other.vertexBufferMemory),
          indexBufferHandle(other.indexBufferHandle),
          indexBufferMemory(other.indexBufferMemory) {
        other.vertexBufferHandle = VK_NULL_HANDLE;
        other.vertexBufferMemory = VK_NULL_HANDLE;
        other.indexBufferHandle = VK_NULL_HANDLE;
        other.indexBufferMemory = VK_NULL_HANDLE;
    }

    MXModel &MXModel::operator=(MXModel &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        verticesData = std::move(other.verticesData);
        indicesData = std::move(other.indicesData);
        subMeshList = std::move(other.subMeshList);
        materialList = std::move(other.materialList);
        mtlLibraryPath = std::move(other.mtlLibraryPath);
        vertexBufferHandle = other.vertexBufferHandle;
        vertexBufferMemory = other.vertexBufferMemory;
        indexBufferHandle = other.indexBufferHandle;
        indexBufferMemory = other.indexBufferMemory;

        other.vertexBufferHandle = VK_NULL_HANDLE;
        other.vertexBufferMemory = VK_NULL_HANDLE;
        other.indexBufferHandle = VK_NULL_HANDLE;
        other.indexBufferMemory = VK_NULL_HANDLE;
        return *this;
    }

    namespace {
        void logMXModelStep(const std::string &message, bool important = false) {
            if (important) {
                std::cout << "mxvk_model: " << message << '\n';
            }
        }

        struct Vec2 {
            float x{};
            float y{};
        };

        struct Vec3 {
            float x{};
            float y{};
            float z{};
        };

        struct MXMODParseResult {
            std::vector<VKVertex> vertices{};
            std::vector<uint32_t> indices{};
            std::vector<SubMesh> subMeshes{};
        };

        struct OBJFaceVertex {
            VKVertex vertex{};
            bool hasNormal = false;
        };

        struct OBJIndex {
            int position = 0;
            int texcoord = 0;
            int normal = 0;
        };

        void trim(std::string &s) {
            const size_t begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                s.clear();
                return;
            }
            const size_t end = s.find_last_not_of(" \t\r\n");
            s = s.substr(begin, end - begin + 1);
        }

        void stripTrailingComment(std::string &s) {
            const size_t commentPos = s.find('#');
            if (commentPos != std::string::npos) {
                s = s.substr(0, commentPos);
            }
            trim(s);
        }

        [[nodiscard]] bool parseOBJIndexValue(const std::string &text, int &value) {
            if (text.empty()) {
                value = 0;
                return true;
            }

            size_t consumed = 0;
            try {
                value = std::stoi(text, &consumed, 10);
            } catch (const std::exception &) {
                return false;
            }

            return consumed == text.size();
        }

        [[nodiscard]] bool parseOBJFaceToken(const std::string &token, OBJIndex &index) {
            std::array<std::string, 3> fields{};
            size_t fieldIndex = 0;
            size_t fieldBegin = 0;

            while (true) {
                if (fieldIndex >= fields.size()) {
                    return false;
                }

                const size_t slashPos = token.find('/', fieldBegin);
                fields[fieldIndex++] = token.substr(fieldBegin, slashPos == std::string::npos ? std::string::npos : slashPos - fieldBegin);
                if (slashPos == std::string::npos) {
                    break;
                }
                fieldBegin = slashPos + 1;
            }

            if (!parseOBJIndexValue(fields[0], index.position) ||
                !parseOBJIndexValue(fields[1], index.texcoord) ||
                !parseOBJIndexValue(fields[2], index.normal)) {
                return false;
            }

            return index.position != 0;
        }

        template <typename T>
        [[nodiscard]] int resolveOBJIndex(int objIndex, const std::vector<T> &values) {
            if (objIndex > 0) {
                return objIndex - 1;
            }
            if (objIndex < 0) {
                return static_cast<int>(values.size()) + objIndex;
            }
            return -1;
        }

        [[nodiscard]] Vec3 faceNormal(const VKVertex &a, const VKVertex &b, const VKVertex &c) {
            const float ax = b.pos[0] - a.pos[0];
            const float ay = b.pos[1] - a.pos[1];
            const float az = b.pos[2] - a.pos[2];
            const float bx = c.pos[0] - a.pos[0];
            const float by = c.pos[1] - a.pos[1];
            const float bz = c.pos[2] - a.pos[2];

            Vec3 normal{
                ay * bz - az * by,
                az * bx - ax * bz,
                ax * by - ay * bx,
            };
            const float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (len > 0.0f) {
                normal.x /= len;
                normal.y /= len;
                normal.z /= len;
            }
            return normal;
        }

        void assignNormalIfMissing(OBJFaceVertex &a, OBJFaceVertex &b, OBJFaceVertex &c) {
            if (a.hasNormal && b.hasNormal && c.hasNormal) {
                return;
            }

            const Vec3 normal = faceNormal(a.vertex, b.vertex, c.vertex);
            for (OBJFaceVertex *faceVertex : {&a, &b, &c}) {
                if (faceVertex->hasNormal) {
                    continue;
                }
                faceVertex->vertex.normal[0] = normal.x;
                faceVertex->vertex.normal[1] = normal.y;
                faceVertex->vertex.normal[2] = normal.z;
                faceVertex->hasNormal = true;
            }
        }

        [[nodiscard]] float projectedArea2(const std::vector<OBJFaceVertex> &face, int dropAxis) {
            float area = 0.0f;
            for (size_t i = 0; i < face.size(); ++i) {
                const VKVertex &a = face[i].vertex;
                const VKVertex &b = face[(i + 1) % face.size()].vertex;
                const float ax = a.pos[(dropAxis + 1) % 3];
                const float ay = a.pos[(dropAxis + 2) % 3];
                const float bx = b.pos[(dropAxis + 1) % 3];
                const float by = b.pos[(dropAxis + 2) % 3];
                area += ax * by - bx * ay;
            }
            return area;
        }

        [[nodiscard]] float edgeCross2(const VKVertex &a, const VKVertex &b, const VKVertex &c, int dropAxis) {
            const float ax = a.pos[(dropAxis + 1) % 3];
            const float ay = a.pos[(dropAxis + 2) % 3];
            const float bx = b.pos[(dropAxis + 1) % 3];
            const float by = b.pos[(dropAxis + 2) % 3];
            const float cx = c.pos[(dropAxis + 1) % 3];
            const float cy = c.pos[(dropAxis + 2) % 3];
            return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        }

        [[nodiscard]] bool pointInProjectedTriangle(const VKVertex &point,
                                                    const VKVertex &a,
                                                    const VKVertex &b,
                                                    const VKVertex &c,
                                                    int dropAxis,
                                                    float windingSign) {
            constexpr float epsilon = 1e-6f;
            const float ab = edgeCross2(a, b, point, dropAxis) * windingSign;
            const float bc = edgeCross2(b, c, point, dropAxis) * windingSign;
            const float ca = edgeCross2(c, a, point, dropAxis) * windingSign;
            return ab >= -epsilon && bc >= -epsilon && ca >= -epsilon;
        }

        void appendOBJTriangle(std::vector<VKVertex> &vertices, OBJFaceVertex a, OBJFaceVertex b, OBJFaceVertex c) {
            assignNormalIfMissing(a, b, c);
            vertices.push_back(a.vertex);
            vertices.push_back(b.vertex);
            vertices.push_back(c.vertex);
        }

        void triangulateOBJFace(const std::vector<OBJFaceVertex> &face, std::vector<VKVertex> &vertices) {
            if (face.size() < 3) {
                return;
            }

            if (face.size() == 3) {
                appendOBJTriangle(vertices, face[0], face[1], face[2]);
                return;
            }

            const Vec3 normal = faceNormal(face[0].vertex, face[1].vertex, face[2].vertex);
            int dropAxis = 0;
            if (std::fabs(normal.y) > std::fabs(normal.x) && std::fabs(normal.y) >= std::fabs(normal.z)) {
                dropAxis = 1;
            } else if (std::fabs(normal.z) > std::fabs(normal.x) && std::fabs(normal.z) > std::fabs(normal.y)) {
                dropAxis = 2;
            }

            float windingSign = projectedArea2(face, dropAxis) >= 0.0f ? 1.0f : -1.0f;
            if (std::fabs(projectedArea2(face, dropAxis)) <= 1e-6f) {
                for (size_t i = 2; i < face.size(); ++i) {
                    appendOBJTriangle(vertices, face[0], face[i - 1], face[i]);
                }
                return;
            }

            std::vector<size_t> remaining(face.size());
            for (size_t i = 0; i < remaining.size(); ++i) {
                remaining[i] = i;
            }

            while (remaining.size() > 3) {
                bool clippedEar = false;
                for (size_t i = 0; i < remaining.size(); ++i) {
                    const size_t previous = remaining[(i + remaining.size() - 1) % remaining.size()];
                    const size_t current = remaining[i];
                    const size_t next = remaining[(i + 1) % remaining.size()];

                    const float cross = edgeCross2(face[previous].vertex, face[current].vertex, face[next].vertex, dropAxis) * windingSign;
                    if (cross <= 1e-6f) {
                        continue;
                    }

                    bool containsPoint = false;
                    for (const size_t test : remaining) {
                        if (test == previous || test == current || test == next) {
                            continue;
                        }
                        if (pointInProjectedTriangle(face[test].vertex,
                                                     face[previous].vertex,
                                                     face[current].vertex,
                                                     face[next].vertex,
                                                     dropAxis,
                                                     windingSign)) {
                            containsPoint = true;
                            break;
                        }
                    }

                    if (containsPoint) {
                        continue;
                    }

                    appendOBJTriangle(vertices, face[previous], face[current], face[next]);
                    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
                    clippedEar = true;
                    break;
                }

                if (!clippedEar) {
                    for (size_t i = 2; i < remaining.size(); ++i) {
                        appendOBJTriangle(vertices, face[remaining[0]], face[remaining[i - 1]], face[remaining[i]]);
                    }
                    return;
                }
            }

            appendOBJTriangle(vertices, face[remaining[0]], face[remaining[1]], face[remaining[2]]);
        }

        [[nodiscard]] std::string inflateCompressedText(const std::vector<unsigned char> &compressedData) {
            z_stream stream{};
            stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(compressedData.data()));
            stream.avail_in = static_cast<uInt>(compressedData.size());

            if (inflateInit2(&stream, 15 + 32) != Z_OK) {
                throw mxvk::Exception("MXModel::loadMXMODZ failed to initialize zlib inflater");
            }

            std::string output{};
            std::array<char, 16384> buffer{};

            int result = Z_OK;
            while (result != Z_STREAM_END) {
                stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
                stream.avail_out = static_cast<uInt>(buffer.size());

                result = inflate(&stream, Z_NO_FLUSH);
                if (result != Z_OK && result != Z_STREAM_END) {
                    inflateEnd(&stream);
                    throw mxvk::Exception("MXModel::loadMXMODZ failed to inflate compressed data");
                }

                const size_t producedBytes = buffer.size() - static_cast<size_t>(stream.avail_out);
                output.append(buffer.data(), producedBytes);
            }

            inflateEnd(&stream);

            if (output.empty()) {
                throw mxvk::Exception("MXModel::loadMXMODZ produced empty decompressed payload");
            }

            return output;
        }

        [[nodiscard]] std::string resolveManifestPath(const std::string &basePath, const std::string &path) {
            if (path.empty()) {
                return {};
            }

            std::filesystem::path resolved(path);
            if (resolved.is_absolute() || basePath.empty()) {
                return resolved.lexically_normal().string();
            }

            return (std::filesystem::path(basePath) / resolved).lexically_normal().string();
        }

        [[nodiscard]] std::string parseMTLTexturePath(std::istream &stream) {
            std::string mapPath{};
            std::string mapToken{};
            while (stream >> mapToken) {
                if (!mapToken.empty() && mapToken[0] == '-') {
                    if (mapToken == "-blendu" || mapToken == "-blendv" || mapToken == "-cc" ||
                        mapToken == "-clamp" || mapToken == "-imfchan" || mapToken == "-type") {
                        stream >> mapToken;
                    } else if (mapToken == "-mm") {
                        stream >> mapToken;
                        stream >> mapToken;
                    } else if (mapToken == "-o" || mapToken == "-s" || mapToken == "-t") {
                        stream >> mapToken;
                        stream >> mapToken;
                        stream >> mapToken;
                    } else if (mapToken == "-bm" || mapToken == "-boost" || mapToken == "-texres") {
                        stream >> mapToken;
                    }
                    continue;
                }

                if (!mapPath.empty()) {
                    mapPath += ' ';
                }
                mapPath += mapToken;
            }
            return mapPath;
        }

        void parseMTLStream(std::istream &file,
                            const std::string &textureBasePath,
                            std::vector<MXMaterial> &materials) {
            MXMaterial *current = nullptr;
            std::string line{};
            while (std::getline(file, line)) {
                stripTrailingComment(line);
                if (line.empty()) {
                    continue;
                }

                std::istringstream stream(line);
                std::string tag{};
                stream >> tag;

                if (tag == "newmtl") {
                    materials.emplace_back();
                    current = &materials.back();
                    std::getline(stream, current->name);
                    trim(current->name);
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
                    const std::string mapPath = parseMTLTexturePath(stream);
                    if (!mapPath.empty()) {
                        current->map_kd = resolveManifestPath(textureBasePath, mapPath);
                    }
                }
            }
        }

        [[nodiscard]] std::string mtlReferencePath(const std::string &objPath, const std::string &mtlPath) {
            const std::filesystem::path objDir = std::filesystem::path(objPath).parent_path();
            const std::filesystem::path materialPath(mtlPath);
            if (objDir.empty()) {
                return materialPath.filename().string();
            }

            std::error_code ec{};
            const std::filesystem::path relative = std::filesystem::relative(materialPath, objDir, ec);
            if (!ec && !relative.empty()) {
                return relative.string();
            }

            return materialPath.filename().string();
        }

        [[nodiscard]] std::string materialNameForTextureIndex(uint32_t textureIndex,
                                                              const std::vector<MXMaterial> &materials) {
            if (textureIndex < static_cast<uint32_t>(materials.size()) && !materials[textureIndex].name.empty()) {
                return materials[textureIndex].name;
            }
            return "material_" + std::to_string(textureIndex);
        }

        [[nodiscard]] std::string mtlTextureReferencePath(const std::filesystem::path &mtlPath,
                                                          const std::string &texturePath) {
            if (texturePath.empty()) {
                return {};
            }

            std::filesystem::path resolvedTexturePath(texturePath);
            if (resolvedTexturePath.is_relative()) {
                resolvedTexturePath = std::filesystem::absolute(resolvedTexturePath);
            }

            std::filesystem::path mtlDir = mtlPath.parent_path();
            if (mtlDir.empty()) {
                mtlDir = std::filesystem::current_path();
            } else if (mtlDir.is_relative()) {
                mtlDir = std::filesystem::absolute(mtlDir);
            }

            std::error_code ec{};
            const std::filesystem::path relative = std::filesystem::relative(resolvedTexturePath, mtlDir, ec);
            if (!ec && !relative.empty()) {
                return relative.string();
            }

            return resolvedTexturePath.lexically_normal().string();
        }

        void writeMTLMaterial(std::ostream &out, const MXMaterial &material) {
            out << "newmtl " << material.name << '\n';
            out << "Ka " << material.ka[0] << ' ' << material.ka[1] << ' ' << material.ka[2] << '\n';
            out << "Kd " << material.kd[0] << ' ' << material.kd[1] << ' ' << material.kd[2] << '\n';
            out << "Ks " << material.ks[0] << ' ' << material.ks[1] << ' ' << material.ks[2] << '\n';
            out << "Ns " << material.ns << '\n';
            out << "d " << material.d << '\n';
            out << "illum " << material.illum << '\n';
            if (!material.map_kd.empty()) {
                out << "map_Kd " << material.map_kd << '\n';
            }
            out << '\n';
        }

        [[nodiscard]] MXMODParseResult parseMXMODStream(std::istream &file,
                                                        const std::string &sourcePath,
                                                        float positionScale) {
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

            std::string line{};
            while (std::getline(file, line)) {
                stripTrailingComment(line);
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
                throw mxvk::Exception("MXModel::loadMXMOD no geometry found in " + sourcePath);
            }

            MXMODParseResult parsed{};
            for (const TriBlock &blk : triBlocks) {
                if (blk.pos.empty()) {
                    continue;
                }

                const uint32_t vertexBase = static_cast<uint32_t>(parsed.vertices.size());
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

                    parsed.vertices.push_back(v);
                }

                const uint32_t firstIndex = static_cast<uint32_t>(parsed.indices.size());
                if (!blk.fileIndices.empty()) {
                    for (uint32_t idx : blk.fileIndices) {
                        parsed.indices.push_back(vertexBase + idx);
                    }
                } else {
                    for (uint32_t i = 0; i < static_cast<uint32_t>(blk.pos.size()); ++i) {
                        parsed.indices.push_back(vertexBase + i);
                    }
                }

                SubMesh sm{};
                sm.firstIndex = firstIndex;
                sm.indexCount = static_cast<uint32_t>(parsed.indices.size()) - firstIndex;
                sm.textureIndex = blk.textureIndex;
                parsed.subMeshes.push_back(sm);
            }

            if (parsed.vertices.empty() || parsed.indices.empty()) {
                throw mxvk::Exception("MXModel::loadMXMOD no renderable data found in " + sourcePath);
            }

            return parsed;
        }
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
        if (verticesData.empty() || indicesData.empty()) {
            return;
        }

        std::vector<VKVertex> uniqueVertices{};
        uniqueVertices.reserve(verticesData.size());

        std::unordered_map<VKVertex, uint32_t, VKVertexHash> vertexMap{};
        std::vector<uint32_t> remap(verticesData.size(), 0);

        for (size_t i = 0; i < verticesData.size(); ++i) {
            const auto it = vertexMap.find(verticesData[i]);
            if (it != vertexMap.end()) {
                remap[i] = it->second;
                continue;
            }

            const uint32_t nextIndex = static_cast<uint32_t>(uniqueVertices.size());
            uniqueVertices.push_back(verticesData[i]);
            vertexMap.emplace(verticesData[i], nextIndex);
            remap[i] = nextIndex;
        }

        for (uint32_t &idx : indicesData) {
            if (idx < static_cast<uint32_t>(remap.size())) {
                idx = remap[idx];
            }
        }

        verticesData = std::move(uniqueVertices);
    }

    void MXModel::load(const std::string &path, float positionScale) {
        if (path.empty()) {
            throw mxvk::Exception("MXModel::load path is empty");
        }

        logMXModelStep("load begin: " + path, true);

        if (path.ends_with(".obj")) {
            loadOBJ(path, positionScale);
            logMXModelStep("load complete (.obj): vertices=" + std::to_string(verticesData.size()) +
                               ", indices=" + std::to_string(indicesData.size()) +
                               ", submeshes=" + std::to_string(subMeshList.size()),
                           true);
            return;
        }

        if (path.ends_with(".mxmod")) {
            loadMXMOD(path, positionScale);
            logMXModelStep("load complete (.mxmod): vertices=" + std::to_string(verticesData.size()) +
                               ", indices=" + std::to_string(indicesData.size()) +
                               ", submeshes=" + std::to_string(subMeshList.size()),
                           true);
            return;
        }

        if (path.ends_with(".mxmod.z")) {
            loadMXMODZ(path, positionScale);
            logMXModelStep("load complete (.mxmod.z): vertices=" + std::to_string(verticesData.size()) +
                               ", indices=" + std::to_string(indicesData.size()) +
                               ", submeshes=" + std::to_string(subMeshList.size()),
                           true);
            return;
        }

        throw mxvk::Exception("MXModel::load unsupported file format: " + path);
    }

    void MXModel::load(const std::string &path,
                       const std::string &textureManifestPath,
                       const std::string &textureBasePath,
                       float positionScale) {
        load(path, positionScale);
        if (!textureManifestPath.empty()) {
            loadTextureManifest(textureManifestPath, textureBasePath);
        }
    }

    void MXModel::exportOBJ(const std::string &objPath, const std::string &mtlPath) const {
        if (objPath.empty()) {
            throw mxvk::Exception("MXModel::exportOBJ objPath is empty");
        }
        if (verticesData.empty() || indicesData.empty()) {
            throw mxvk::Exception("MXModel::exportOBJ requires loaded geometry");
        }

        const std::filesystem::path objOutputPath(objPath);
        const std::filesystem::path mtlOutputPath = mtlPath.empty()
                                                        ? objOutputPath.parent_path() / objOutputPath.stem().concat(".mtl")
                                                        : std::filesystem::path(mtlPath);

        if (!objOutputPath.parent_path().empty()) {
            std::filesystem::create_directories(objOutputPath.parent_path());
        }
        if (!mtlOutputPath.parent_path().empty()) {
            std::filesystem::create_directories(mtlOutputPath.parent_path());
        }

        std::ofstream objFile(objOutputPath);
        if (!objFile.is_open()) {
            throw mxvk::Exception("MXModel::exportOBJ failed to open OBJ file: " + objOutputPath.string());
        }

        std::ofstream mtlFile(mtlOutputPath);
        if (!mtlFile.is_open()) {
            throw mxvk::Exception("MXModel::exportOBJ failed to open MTL file: " + mtlOutputPath.string());
        }

        objFile << "# Exported from MXVK MXModel\n";
        objFile << "mtllib " << mtlReferencePath(objOutputPath.string(), mtlOutputPath.string()) << "\n\n";

        for (const VKVertex &vertex : verticesData) {
            objFile << "v " << vertex.pos[0] << ' ' << vertex.pos[1] << ' ' << vertex.pos[2] << '\n';
        }
        objFile << '\n';

        for (const VKVertex &vertex : verticesData) {
            objFile << "vt " << vertex.texCoord[0] << ' ' << vertex.texCoord[1] << '\n';
        }
        objFile << '\n';

        for (const VKVertex &vertex : verticesData) {
            objFile << "vn " << vertex.normal[0] << ' ' << vertex.normal[1] << ' ' << vertex.normal[2] << '\n';
        }
        objFile << '\n';

        std::vector<MXMaterial> outputMaterials = materialList;
        for (size_t i = 0; i < outputMaterials.size(); ++i) {
            if (outputMaterials[i].name.empty()) {
                outputMaterials[i].name = "material_" + std::to_string(i);
            }
        }

        const auto hasOutputMaterial = [&outputMaterials](const std::string &name) {
            return std::any_of(outputMaterials.begin(), outputMaterials.end(), [&name](const MXMaterial &material) {
                return material.name == name;
            });
        };

        const auto ensureOutputMaterial = [&outputMaterials, &hasOutputMaterial](const std::string &name) {
            if (hasOutputMaterial(name)) {
                return;
            }
            MXMaterial material{};
            material.name = name;
            outputMaterials.push_back(material);
        };

        if (subMeshList.empty()) {
            ensureOutputMaterial("material_0");
        } else {
            for (const SubMesh &sm : subMeshList) {
                const std::string materialName = !sm.materialName.empty()
                                                     ? sm.materialName
                                                     : materialNameForTextureIndex(sm.textureIndex, outputMaterials);
                ensureOutputMaterial(materialName);
            }
        }

        for (const MXMaterial &material : outputMaterials) {
            MXMaterial outputMaterial = material;
            outputMaterial.map_kd = mtlTextureReferencePath(mtlOutputPath, outputMaterial.map_kd);
            writeMTLMaterial(mtlFile, outputMaterial);
        }

        const auto emitFaces = [&](uint32_t firstIndex, uint32_t indexCount, const std::string &materialName) {
            if (firstIndex > indicesData.size() || indexCount > indicesData.size() - firstIndex) {
                throw mxvk::Exception("MXModel::exportOBJ submesh index range is out of bounds");
            }
            if ((indexCount % 3U) != 0U) {
                throw mxvk::Exception("MXModel::exportOBJ only triangle index ranges can be exported");
            }

            objFile << "usemtl " << materialName << '\n';
            for (uint32_t i = 0; i < indexCount; i += 3U) {
                objFile << "f";
                for (uint32_t j = 0; j < 3U; ++j) {
                    const uint32_t vertexIndex = indicesData[firstIndex + i + j];
                    if (vertexIndex >= static_cast<uint32_t>(verticesData.size())) {
                        throw mxvk::Exception("MXModel::exportOBJ vertex index is out of bounds");
                    }
                    const uint32_t objIndex = vertexIndex + 1U;
                    objFile << ' ' << objIndex << '/' << objIndex << '/' << objIndex;
                }
                objFile << '\n';
            }
            objFile << '\n';
        };

        if (subMeshList.empty()) {
            emitFaces(0, static_cast<uint32_t>(indicesData.size()), "material_0");
        } else {
            for (const SubMesh &sm : subMeshList) {
                const std::string materialName = !sm.materialName.empty()
                                                     ? sm.materialName
                                                     : materialNameForTextureIndex(sm.textureIndex, materialList);
                emitFaces(sm.firstIndex, sm.indexCount, materialName);
            }
        }
    }

    void MXModel::exportOBJ(const std::string &modelPath,
                            const std::string &textureManifestPath,
                            const std::string &textureBasePath,
                            const std::string &objPath,
                            float positionScale) {
        MXModel model{};
        model.load(modelPath, textureManifestPath, textureBasePath, positionScale);
        model.exportOBJ(objPath);
    }

    void MXModel::loadOBJ(const std::string &path, float positionScale) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw mxvk::Exception("MXModel::loadOBJ failed to open file: " + path);
        }

        std::vector<Vec3> positions{};
        std::vector<Vec2> texcoords{};
        std::vector<Vec3> normals{};

        verticesData.clear();
        indicesData.clear();
        subMeshList.clear();
        materialList.clear();
        mtlLibraryPath.clear();

        std::vector<VKVertex> currentVerts{};
        std::string currentMaterialName{};

        auto finalizeGroup = [&]() {
            if (currentVerts.empty()) {
                return;
            }

            SubMesh sm{};
            sm.firstIndex = static_cast<uint32_t>(indicesData.size());
            sm.indexCount = static_cast<uint32_t>(currentVerts.size());
            sm.materialName = currentMaterialName;

            const uint32_t baseVertex = static_cast<uint32_t>(verticesData.size());
            verticesData.insert(verticesData.end(), currentVerts.begin(), currentVerts.end());

            for (uint32_t i = 0; i < sm.indexCount; ++i) {
                indicesData.push_back(baseVertex + i);
            }

            subMeshList.push_back(sm);
            currentVerts.clear();
        };

        std::string line{};
        while (std::getline(file, line)) {
            stripTrailingComment(line);
            if (line.empty()) {
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
                std::getline(stream, mtlFile);
                trim(mtlFile);
                if (!mtlFile.empty()) {
                    const std::filesystem::path objPath(path);
                    std::filesystem::path mtlPath(mtlFile);
                    if (mtlPath.is_absolute()) {
                        mtlPath = mtlPath.filename();
                    }
                    mtlLibraryPath = (objPath.parent_path() / mtlPath).string();
                }
                continue;
            }

            if (tag == "g" || tag == "o") {
                finalizeGroup();
                continue;
            }

            if (tag == "usemtl") {
                std::string materialName{};
                std::getline(stream, materialName);
                trim(materialName);
                if (!materialName.empty() && materialName != currentMaterialName) {
                    finalizeGroup();
                    currentMaterialName = materialName;
                }
                continue;
            }

            if (tag != "f") {
                continue;
            }

            std::vector<OBJFaceVertex> faceVerts{};
            std::string token{};
            while (stream >> token) {
                OBJIndex objIndex{};
                if (!parseOBJFaceToken(token, objIndex)) {
                    throw mxvk::Exception("MXModel::loadOBJ malformed face token '" + token + "' in " + path);
                }

                OBJFaceVertex faceVertex{};
                const int positionIndex = resolveOBJIndex(objIndex.position, positions);
                if (positionIndex < 0 || positionIndex >= static_cast<int>(positions.size())) {
                    throw mxvk::Exception("MXModel::loadOBJ face position index out of range in " + path);
                }
                faceVertex.vertex.pos[0] = positions[static_cast<size_t>(positionIndex)].x;
                faceVertex.vertex.pos[1] = positions[static_cast<size_t>(positionIndex)].y;
                faceVertex.vertex.pos[2] = positions[static_cast<size_t>(positionIndex)].z;

                if (objIndex.texcoord != 0) {
                    const int texcoordIndex = resolveOBJIndex(objIndex.texcoord, texcoords);
                    if (texcoordIndex < 0 || texcoordIndex >= static_cast<int>(texcoords.size())) {
                        throw mxvk::Exception("MXModel::loadOBJ face texture index out of range in " + path);
                    }
                    faceVertex.vertex.texCoord[0] = texcoords[static_cast<size_t>(texcoordIndex)].x;
                    faceVertex.vertex.texCoord[1] = texcoords[static_cast<size_t>(texcoordIndex)].y;
                }

                if (objIndex.normal != 0) {
                    const int normalIndex = resolveOBJIndex(objIndex.normal, normals);
                    if (normalIndex < 0 || normalIndex >= static_cast<int>(normals.size())) {
                        throw mxvk::Exception("MXModel::loadOBJ face normal index out of range in " + path);
                    }
                    faceVertex.vertex.normal[0] = normals[static_cast<size_t>(normalIndex)].x;
                    faceVertex.vertex.normal[1] = normals[static_cast<size_t>(normalIndex)].y;
                    faceVertex.vertex.normal[2] = normals[static_cast<size_t>(normalIndex)].z;
                    faceVertex.hasNormal = true;
                }

                faceVerts.push_back(faceVertex);
            }

            triangulateOBJFace(faceVerts, currentVerts);
        }

        finalizeGroup();

        if (!mtlLibraryPath.empty()) {
            loadMTL(mtlLibraryPath);
            std::unordered_map<std::string, uint32_t> materialIndices{};
            for (uint32_t i = 0; i < static_cast<uint32_t>(materialList.size()); ++i) {
                materialIndices.emplace(materialList[i].name, i);
            }

            for (SubMesh &sm : subMeshList) {
                const auto it = materialIndices.find(sm.materialName);
                if (it != materialIndices.end()) {
                    sm.textureIndex = it->second;
                }
            }
        }

        if (verticesData.empty() || indicesData.empty()) {
            throw mxvk::Exception("MXModel::loadOBJ no geometry found in " + path);
        }

        const bool hasNormals = !normals.empty();
        if (!hasNormals) {
            for (size_t i = 0; i + 2 < verticesData.size(); i += 3) {
                const float ax = verticesData[i + 1].pos[0] - verticesData[i].pos[0];
                const float ay = verticesData[i + 1].pos[1] - verticesData[i].pos[1];
                const float az = verticesData[i + 1].pos[2] - verticesData[i].pos[2];
                const float bx = verticesData[i + 2].pos[0] - verticesData[i].pos[0];
                const float by = verticesData[i + 2].pos[1] - verticesData[i].pos[1];
                const float bz = verticesData[i + 2].pos[2] - verticesData[i].pos[2];

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
                    verticesData[i + static_cast<size_t>(j)].normal[0] = nx;
                    verticesData[i + static_cast<size_t>(j)].normal[1] = ny;
                    verticesData[i + static_cast<size_t>(j)].normal[2] = nz;
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

        const MXMODParseResult parsed = parseMXMODStream(file, path, positionScale);

        verticesData = parsed.vertices;
        indicesData = parsed.indices;
        subMeshList = parsed.subMeshes;
        materialList.clear();
        mtlLibraryPath.clear();

        compressIndices();
    }

    void MXModel::loadMXMODZ(const std::string &path, float positionScale) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw mxvk::Exception("MXModel::loadMXMODZ failed to open file: " + path);
        }

        std::vector<unsigned char> compressedData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (compressedData.empty()) {
            throw mxvk::Exception("MXModel::loadMXMODZ empty compressed file: " + path);
        }

        const std::string decompressedText = inflateCompressedText(compressedData);
        std::istringstream stream(decompressedText);
        const MXMODParseResult parsed = parseMXMODStream(stream, path, positionScale);

        verticesData = parsed.vertices;
        indicesData = parsed.indices;
        subMeshList = parsed.subMeshes;
        materialList.clear();
        mtlLibraryPath.clear();

        compressIndices();
    }

    void MXModel::upload(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkCommandPool commandPool, VkQueue graphicsQueue) {
        if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
            commandPool == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE) {
            throw mxvk::Exception("MXModel::upload requires valid Vulkan handles");
        }
        if (verticesData.empty() || indicesData.empty()) {
            throw mxvk::Exception("MXModel::upload requires loaded geometry");
        }

        logMXModelStep("upload begin", true);

        cleanup(device);

        const VkDeviceSize vertexBufferSize = sizeof(VKVertex) * verticesData.size();
        const VkDeviceSize indexBufferSize = sizeof(uint32_t) * indicesData.size();

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
        std::memcpy(vertexData, verticesData.data(), static_cast<size_t>(vertexBufferSize));
        vkUnmapMemory(device, stagingVertexMemory);

        void *indexData = nullptr;
        vkMapMemory(device, stagingIndexMemory, 0, indexBufferSize, 0, &indexData);
        std::memcpy(indexData, indicesData.data(), static_cast<size_t>(indexBufferSize));
        vkUnmapMemory(device, stagingIndexMemory);

        createBuffer(device, physicalDevice, vertexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     vertexBufferHandle, vertexBufferMemory);

        createBuffer(device, physicalDevice, indexBufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     indexBufferHandle, indexBufferMemory);

        copyBuffer(device, commandPool, graphicsQueue, stagingVertexBuffer, vertexBufferHandle, vertexBufferSize);
        copyBuffer(device, commandPool, graphicsQueue, stagingIndexBuffer, indexBufferHandle, indexBufferSize);

        vkDestroyBuffer(device, stagingVertexBuffer, nullptr);
        vkFreeMemory(device, stagingVertexMemory, nullptr);
        vkDestroyBuffer(device, stagingIndexBuffer, nullptr);
        vkFreeMemory(device, stagingIndexMemory, nullptr);

        logMXModelStep("upload complete", true);
    }

    void MXModel::cleanup(VkDevice device) {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        const bool hadBuffers = vertexBufferHandle != VK_NULL_HANDLE || indexBufferHandle != VK_NULL_HANDLE ||
                                vertexBufferMemory != VK_NULL_HANDLE || indexBufferMemory != VK_NULL_HANDLE;
        if (hadBuffers) {
            logMXModelStep("teardown begin", true);
        }

        if (vertexBufferHandle != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertexBufferHandle, nullptr);
            vertexBufferHandle = VK_NULL_HANDLE;
        }
        if (vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertexBufferMemory, nullptr);
            vertexBufferMemory = VK_NULL_HANDLE;
        }

        if (indexBufferHandle != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, indexBufferHandle, nullptr);
            indexBufferHandle = VK_NULL_HANDLE;
        }
        if (indexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, indexBufferMemory, nullptr);
            indexBufferMemory = VK_NULL_HANDLE;
        }

        if (hadBuffers) {
            logMXModelStep("teardown complete", true);
        }
    }

    void MXModel::draw(VkCommandBuffer cmd) const {
        if (cmd == VK_NULL_HANDLE || vertexBufferHandle == VK_NULL_HANDLE || indexBufferHandle == VK_NULL_HANDLE || indicesData.empty()) {
            return;
        }

        const VkBuffer buffers[] = {vertexBufferHandle};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, indexBufferHandle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, indexCount(), 1, 0, 0, 0);
    }

    void MXModel::drawSubMesh(VkCommandBuffer cmd, size_t index) const {
        if (cmd == VK_NULL_HANDLE || index >= subMeshList.size() ||
            vertexBufferHandle == VK_NULL_HANDLE || indexBufferHandle == VK_NULL_HANDLE) {
            return;
        }

        const VkBuffer buffers[] = {vertexBufferHandle};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, indexBufferHandle, 0, VK_INDEX_TYPE_UINT32);

        const SubMesh &sm = subMeshList[index];
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

        try {
            allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);
            if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
                throw mxvk::Exception("MXModel::createBuffer failed to allocate memory");
            }

            if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS) {
                throw mxvk::Exception("MXModel::createBuffer failed to bind memory");
            }
        } catch (...) {
            if (bufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, bufferMemory, nullptr);
                bufferMemory = VK_NULL_HANDLE;
            }
            if (buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
            }
            throw;
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

        parseMTLStream(file, std::filesystem::path(path).parent_path().string(), materialList);
    }

    void MXModel::loadTextureManifest(const std::string &path, const std::string &textureBasePath) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw mxvk::Exception("MXModel::loadTextureManifest failed to open file: " + path);
        }

        std::vector<std::string> lines{};
        std::string line{};
        while (std::getline(file, line)) {
            stripTrailingComment(line);
            if (line.empty()) {
                continue;
            }
            lines.push_back(line);
        }

        bool isStructured = false;
        bool isMtlLike = false;
        for (const std::string &ln : lines) {
            std::istringstream stream(ln);
            std::string tag{};
            stream >> tag;
            if (tag == "submesh" || tag == "texture_dir" || tag == "material_lib" || tag == "model" || tag == "texture") {
                isStructured = true;
                break;
            }
            if (tag == "newmtl") {
                isMtlLike = true;
                break;
            }
        }

        materialList.clear();

        std::string manifestTextureBase = textureBasePath.empty()
                                              ? std::filesystem::path(path).parent_path().string()
                                              : textureBasePath;

        if (isMtlLike) {
            std::istringstream stream{};
            std::string text{};
            for (const std::string &ln : lines) {
                text += ln;
                text += '\n';
            }
            stream.str(text);
            parseMTLStream(stream, manifestTextureBase, materialList);
        } else if (isStructured) {
            for (const std::string &ln : lines) {
                std::istringstream stream(ln);
                std::string tag{};
                stream >> tag;

                if (tag == "texture_dir") {
                    std::string dir{};
                    std::getline(stream, dir);
                    trim(dir);
                    if (!dir.empty()) {
                        manifestTextureBase = resolveManifestPath(std::filesystem::path(path).parent_path().string(), dir);
                    }
                    continue;
                }

                if (tag == "material_lib") {
                    std::string materialPath{};
                    std::getline(stream, materialPath);
                    trim(materialPath);
                    if (!materialPath.empty()) {
                        loadMTL(resolveManifestPath(std::filesystem::path(path).parent_path().string(), materialPath));
                    }
                    continue;
                }

                if (tag == "texture") {
                    std::string image{};
                    if (stream >> image) {
                        MXMaterial material{};
                        material.name = "material_" + std::to_string(materialList.size());
                        material.map_kd = resolveManifestPath(manifestTextureBase, image);
                        materialList.push_back(material);
                    }
                    continue;
                }

                if (tag == "submesh") {
                    uint32_t subMeshIndex = 0;
                    if (!(stream >> subMeshIndex) || subMeshIndex >= subMeshList.size()) {
                        continue;
                    }

                    std::string materialOrTexture{};
                    if (!(stream >> materialOrTexture)) {
                        continue;
                    }

                    try {
                        size_t consumed = 0;
                        const unsigned long value = std::stoul(materialOrTexture, &consumed, 10);
                        if (consumed == materialOrTexture.size()) {
                            subMeshList[subMeshIndex].textureIndex = static_cast<uint32_t>(value);
                            subMeshList[subMeshIndex].materialName = materialNameForTextureIndex(subMeshList[subMeshIndex].textureIndex, materialList);
                            continue;
                        }
                    } catch (const std::exception &) {
                    }

                    subMeshList[subMeshIndex].materialName = materialOrTexture;
                }
            }
        } else {
            for (const std::string &ln : lines) {
                MXMaterial material{};
                material.name = "material_" + std::to_string(materialList.size());
                material.map_kd = resolveManifestPath(manifestTextureBase, ln);
                materialList.push_back(material);
            }
        }

        if (materialList.empty()) {
            return;
        }

        for (SubMesh &sm : subMeshList) {
            if (sm.materialName.empty()) {
                sm.materialName = materialNameForTextureIndex(sm.textureIndex, materialList);
            }
        }
    }

} // namespace mxvk
