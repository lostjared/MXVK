#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_io_window.hpp"
#include "mxvk/mxvk_png.hpp"
#include "mxvk/mxvk_shader_module.hpp"

namespace walk {

    struct WallSegment {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        float height = 5.0f;
    };

    struct PillarInstance {
        glm::vec3 position{0.0f};
        float radius = 1.0f;
        float height = 4.0f;
    };

    struct Collectible {
        enum class Type {
            Saturn,
            Bird,
        };

        Type type = Type::Saturn;
        glm::vec3 position{0.0f};
        glm::vec3 hitCenterOffset{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        float rotationSpeed = 12.0f;
        float radius = 1.0f;
        bool active = true;
    };

    struct Projectile {
        struct TrailPoint {
            glm::vec3 position{0.0f};
            float lifetime = 0.0f;
            float maxLifetime = 0.5f;
        };

        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        float speed = 100.0f;
        float lifetime = 0.0f;
        float maxLifetime = 10.0f;
        float distanceTraveled = 0.0f;
        float maxDistance = 160.0f;
        bool active = true;
        std::vector<TrailPoint> trail{};
        float trailTimer = 0.0f;
    };

    struct ExplosionParticle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 color{1.0f, 0.5f, 0.2f};
        float lifetime = 0.0f;
        float maxLifetime = 0.55f;
        float size = 0.08f;
        bool active = true;
    };

    struct ParticlePointVertex {
        glm::vec3 pos{0.0f};
        glm::vec4 color{1.0f};
        float size = 8.0f;
    };

    class MazeWorld {
      public:
        [[nodiscard]] glm::vec3 startPosition() const noexcept { return startPositionValue; }

        [[nodiscard]] const std::vector<WallSegment> &walls() const noexcept { return wallSegments; }

        [[nodiscard]] const std::vector<PillarInstance> &pillars() const noexcept { return pillarInstances; }

        [[nodiscard]] std::vector<Collectible> &collectibles() noexcept { return collectibleItems; }

        [[nodiscard]] const std::vector<Collectible> &collectibles() const noexcept { return collectibleItems; }

        [[nodiscard]] int activeCollectibles() const {
            int count = 0;
            for (const Collectible &obj : collectibleItems) {
                if (obj.active) {
                    ++count;
                }
            }
            return count;
        }

        void generate(uint32_t seed) {
            std::mt19937 rng(seed);
            generateMaze(rng);
            generatePillars(rng);
            // Rescue: if the player start happens to land inside a pillar, push the
            // start point to the safest unoccupied spot in the same cell.
            if (checkPillarCollision(startPositionValue, 0.6f)) {
                for (int attempt = 0; attempt < 64; ++attempt) {
                    const glm::vec3 candidate = randomPointInCell(startCellX, startCellZ, 0.6f, eyeHeight, rng, 0.5f);
                    if (!checkWallCollision(candidate, 0.5f) && !checkPillarCollision(candidate, 0.6f)) {
                        startPositionValue = candidate;
                        break;
                    }
                }
            }
            generateCollectibles(rng);
        }

        [[nodiscard]] bool checkWallCollision(const glm::vec3 &position, float radius) const {
            const float halfThickness = wallThicknessValue * 0.5f;
            const float hitRadius = radius + halfThickness;
            for (const WallSegment &wall : wallSegments) {
                const glm::vec3 segment = wall.end - wall.start;
                const float segmentLength = glm::length(segment);
                if (segmentLength < 0.0001f) {
                    continue;
                }

                const glm::vec3 segmentDir = segment / segmentLength;
                const glm::vec3 toPoint = position - wall.start;
                const float projection = glm::clamp(glm::dot(toPoint, segmentDir), 0.0f, segmentLength);
                glm::vec3 closest = wall.start + (segmentDir * projection);
                closest.y = position.y;
                if (glm::length(position - closest) < hitRadius) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool checkPillarCollision(const glm::vec3 &position, float playerRadius) const {
            for (const PillarInstance &pillar : pillarInstances) {
                const glm::vec2 player2d(position.x, position.z);
                const glm::vec2 pillar2d(pillar.position.x, pillar.position.z);
                if (glm::length(player2d - pillar2d) < (playerRadius + pillar.radius)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool checkCollectibleCollision(const glm::vec3 &point, size_t &indexOut) const {
            for (size_t i = 0; i < collectibleItems.size(); ++i) {
                if (!collectibleItems[i].active) {
                    continue;
                }
                const Collectible &collectible = collectibleItems[i];
                const glm::vec3 center = collectible.position + collectible.hitCenterOffset;
                if (collectible.type == Collectible::Type::Bird) {
                    const glm::vec3 delta = point - center;
                    const float halfSide = collectible.radius;
                    if (std::abs(delta.x) <= halfSide &&
                        std::abs(delta.y) <= halfSide &&
                        std::abs(delta.z) <= halfSide) {
                        indexOut = i;
                        return true;
                    }
                    continue;
                }

                if (glm::length(center - point) < collectible.radius) {
                    indexOut = i;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] glm::vec3 randomPointInCell(int cellX, int cellZ, float objectRadius, float y, std::mt19937 &rng, float margin) const {
            const float pad = objectRadius + wallThicknessValue * 0.5f + margin;
            const float x0 = -size + static_cast<float>(cellX) * cellSize;
            const float z0 = -size + static_cast<float>(cellZ) * cellSize;
            const float x1 = x0 + cellSize;
            const float z1 = z0 + cellSize;

            float minX = x0 + pad;
            float maxX = x1 - pad;
            float minZ = z0 + pad;
            float maxZ = z1 - pad;
            if (minX > maxX) {
                minX = maxX = (x0 + x1) * 0.5f;
            }
            if (minZ > maxZ) {
                minZ = maxZ = (z0 + z1) * 0.5f;
            }

            std::uniform_real_distribution<float> distX(minX, maxX);
            std::uniform_real_distribution<float> distZ(minZ, maxZ);
            return glm::vec3(distX(rng), y, distZ(rng));
        }

        [[nodiscard]] float wallThickness() const noexcept { return wallThicknessValue; }

      private:
        struct Cell {
            bool visited = false;
            std::array<bool, 4> walls{true, true, true, true};
        };

        void generateMaze(std::mt19937 &rng) {
            const int gridX = mazeGridX;
            const int gridZ = mazeGridZ;
            cellSize = (size * 2.0f) / static_cast<float>(gridX);

            auto indexFor = [gridX](int x, int z) {
                return z * gridX + x;
            };

            std::vector<Cell> grid(static_cast<size_t>(gridX * gridZ));
            std::vector<std::pair<int, int>> stack;
            stack.emplace_back(0, 0);
            grid[static_cast<size_t>(indexFor(0, 0))].visited = true;

            while (!stack.empty()) {
                const auto [x, z] = stack.back();
                std::vector<int> dirs;
                if (z > 0 && !grid[static_cast<size_t>(indexFor(x, z - 1))].visited) {
                    dirs.push_back(0);
                }
                if (x < (gridX - 1) && !grid[static_cast<size_t>(indexFor(x + 1, z))].visited) {
                    dirs.push_back(1);
                }
                if (z < (gridZ - 1) && !grid[static_cast<size_t>(indexFor(x, z + 1))].visited) {
                    dirs.push_back(2);
                }
                if (x > 0 && !grid[static_cast<size_t>(indexFor(x - 1, z))].visited) {
                    dirs.push_back(3);
                }

                if (dirs.empty()) {
                    stack.pop_back();
                    continue;
                }

                std::uniform_int_distribution<size_t> pick(0, dirs.size() - 1U);
                const int d = dirs[pick(rng)];
                int nx = x;
                int nz = z;
                if (d == 0) {
                    nz = z - 1;
                } else if (d == 1) {
                    nx = x + 1;
                } else if (d == 2) {
                    nz = z + 1;
                } else {
                    nx = x - 1;
                }

                grid[static_cast<size_t>(indexFor(x, z))].walls[static_cast<size_t>(d)] = false;
                grid[static_cast<size_t>(indexFor(nx, nz))].walls[static_cast<size_t>((d + 2) % 4)] = false;
                grid[static_cast<size_t>(indexFor(nx, nz))].visited = true;
                stack.emplace_back(nx, nz);
            }

            wallSegments.clear();
            for (int z = 0; z < gridZ; ++z) {
                for (int x = 0; x < gridX; ++x) {
                    const float cx = -size + static_cast<float>(x) * cellSize;
                    const float cz = -size + static_cast<float>(z) * cellSize;
                    const float x0 = cx;
                    const float z0 = cz;
                    const float x1 = cx + cellSize;
                    const float z1 = cz + cellSize;
                    const Cell &cell = grid[static_cast<size_t>(indexFor(x, z))];

                    if (cell.walls[0]) {
                        wallSegments.push_back({glm::vec3(x0, 0.0f, z0), glm::vec3(x1, 0.0f, z0), wallHeight});
                    }
                    if (cell.walls[3]) {
                        wallSegments.push_back({glm::vec3(x0, 0.0f, z1), glm::vec3(x0, 0.0f, z0), wallHeight});
                    }
                    if (x == (gridX - 1) && cell.walls[1]) {
                        wallSegments.push_back({glm::vec3(x1, 0.0f, z0), glm::vec3(x1, 0.0f, z1), wallHeight});
                    }
                    if (z == (gridZ - 1) && cell.walls[2]) {
                        wallSegments.push_back({glm::vec3(x1, 0.0f, z1), glm::vec3(x0, 0.0f, z1), wallHeight});
                    }
                }
            }
            mergeContiguousWalls();

            const float playerRadius = 0.5f;
            startCellX = 0;
            startCellZ = 0;
            startPositionValue = randomPointInCell(0, 0, playerRadius, eyeHeight, rng, 0.5f);
            if (checkWallCollision(startPositionValue, playerRadius)) {
                for (int z = 0; z < mazeGridZ; ++z) {
                    for (int x = 0; x < mazeGridX; ++x) {
                        startPositionValue = randomPointInCell(x, z, playerRadius, eyeHeight, rng, 0.5f);
                        if (!checkWallCollision(startPositionValue, playerRadius)) {
                            startCellX = x;
                            startCellZ = z;
                            return;
                        }
                    }
                }
            }
        }

        void mergeContiguousWalls() {
            if (wallSegments.empty()) {
                return;
            }

            struct NormalizedWall {
                bool horizontal = false;
                float constantAxis = 0.0f;
                float startAxis = 0.0f;
                float endAxis = 0.0f;
                float height = 0.0f;
            };

            constexpr float epsilon = 0.0001f;
            constexpr float adjacencyEpsilon = 0.001f;
            constexpr float quantizeScale = 1000.0f;

            const auto quantize = [](float value) {
                return static_cast<int>(std::lround(value * quantizeScale));
            };

            std::vector<NormalizedWall> normalized;
            normalized.reserve(wallSegments.size());
            for (const WallSegment &wall : wallSegments) {
                const bool horizontal = std::abs(wall.start.z - wall.end.z) <= epsilon;
                if (horizontal) {
                    const float x0 = std::min(wall.start.x, wall.end.x);
                    const float x1 = std::max(wall.start.x, wall.end.x);
                    normalized.push_back({true, wall.start.z, x0, x1, wall.height});
                } else {
                    const float z0 = std::min(wall.start.z, wall.end.z);
                    const float z1 = std::max(wall.start.z, wall.end.z);
                    normalized.push_back({false, wall.start.x, z0, z1, wall.height});
                }
            }

            std::sort(normalized.begin(), normalized.end(), [&quantize](const NormalizedWall &a, const NormalizedWall &b) {
                const auto keyA = std::array<int, 3>{a.horizontal ? 1 : 0, quantize(a.constantAxis), quantize(a.height)};
                const auto keyB = std::array<int, 3>{b.horizontal ? 1 : 0, quantize(b.constantAxis), quantize(b.height)};
                if (keyA != keyB) {
                    return keyA < keyB;
                }
                if (a.startAxis != b.startAxis) {
                    return a.startAxis < b.startAxis;
                }
                return a.endAxis < b.endAxis;
            });

            std::vector<WallSegment> merged;
            merged.reserve(normalized.size());
            size_t index = 0;
            while (index < normalized.size()) {
                const NormalizedWall first = normalized[index];
                float runStart = first.startAxis;
                float runEnd = first.endAxis;

                size_t next = index + 1;
                while (next < normalized.size()) {
                    const NormalizedWall &candidate = normalized[next];
                    if (candidate.horizontal != first.horizontal || quantize(candidate.constantAxis) != quantize(first.constantAxis) || quantize(candidate.height) != quantize(first.height)) {
                        break;
                    }

                    if (candidate.startAxis <= (runEnd + adjacencyEpsilon)) {
                        runEnd = std::max(runEnd, candidate.endAxis);
                        ++next;
                        continue;
                    }

                    if (first.horizontal) {
                        merged.push_back({glm::vec3(runStart, 0.0f, first.constantAxis), glm::vec3(runEnd, 0.0f, first.constantAxis), first.height});
                    } else {
                        merged.push_back({glm::vec3(first.constantAxis, 0.0f, runStart), glm::vec3(first.constantAxis, 0.0f, runEnd), first.height});
                    }
                    runStart = candidate.startAxis;
                    runEnd = candidate.endAxis;
                    ++next;
                }

                if (first.horizontal) {
                    merged.push_back({glm::vec3(runStart, 0.0f, first.constantAxis), glm::vec3(runEnd, 0.0f, first.constantAxis), first.height});
                } else {
                    merged.push_back({glm::vec3(first.constantAxis, 0.0f, runStart), glm::vec3(first.constantAxis, 0.0f, runEnd), first.height});
                }
                index = next;
            }

            wallSegments.swap(merged);
        }

        void generatePillars(std::mt19937 &rng) {
            pillarInstances.clear();
            std::uniform_real_distribution<float> radiusDist(0.5f, 1.5f);
            std::uniform_real_distribution<float> heightDist(3.0f, 6.0f);

            constexpr int targetPillars = 15;
            constexpr int maxAttempts = targetPillars * 8;
            int created = 0;
            for (int attempt = 0; attempt < maxAttempts && created < targetPillars; ++attempt) {
                const int cellX = static_cast<int>(rng() % static_cast<uint32_t>(mazeGridX));
                const int cellZ = static_cast<int>(rng() % static_cast<uint32_t>(mazeGridZ));
                // Don't drop pillars on top of where the player spawns.
                if (cellX == startCellX && cellZ == startCellZ) {
                    continue;
                }
                PillarInstance pillar{};
                pillar.radius = radiusDist(rng);
                pillar.height = heightDist(rng);
                pillar.position = randomPointInCell(cellX, cellZ, pillar.radius, 0.0f, rng, 0.3f);
                if (!checkWallCollision(pillar.position, pillar.radius)) {
                    pillarInstances.push_back(pillar);
                    ++created;
                }
            }
        }

        void generateCollectibles(std::mt19937 &rng) {
            collectibleItems.clear();
            const int usableCells = std::max(1, (mazeGridX * mazeGridZ) - 1);
            const int targetCollectibles = usableCells * collectiblesPerCell;
            collectibleItems.reserve(static_cast<size_t>(targetCollectibles));

            std::vector<Collectible::Type> types;
            types.reserve(static_cast<size_t>(targetCollectibles));
            const int saturnCount = targetCollectibles / 2;
            for (int i = 0; i < saturnCount; ++i) {
                types.push_back(Collectible::Type::Saturn);
            }
            for (int i = saturnCount; i < targetCollectibles; ++i) {
                types.push_back(Collectible::Type::Bird);
            }
            std::shuffle(types.begin(), types.end(), rng);

            std::uniform_real_distribution<float> saturnScale(0.4f, 0.8f);
            std::uniform_real_distribution<float> saturnRotSpeed(5.0f, 15.0f);
            std::uniform_real_distribution<float> birdScale(0.3f, 0.5f);
            std::uniform_real_distribution<float> birdRotSpeed(20.0f, 60.0f);

            int typeIndex = 0;
            for (int cellZ = 0; cellZ < mazeGridZ; ++cellZ) {
                for (int cellX = 0; cellX < mazeGridX; ++cellX) {
                    if (cellX == startCellX && cellZ == startCellZ) {
                        continue;
                    }
                    for (int slot = 0; slot < collectiblesPerCell; ++slot) {
                        if (typeIndex >= targetCollectibles) {
                            break;
                        }
                        Collectible obj{};
                        obj.type = types[static_cast<size_t>(typeIndex)];
                        if (obj.type == Collectible::Type::Saturn) {
                            const float scale = saturnScale(rng);
                            obj.scale = glm::vec3(scale);
                            obj.rotationSpeed = saturnRotSpeed(rng);
                            obj.radius = 2.0f * scale;
                        } else {
                            const float scale = birdScale(rng);
                            obj.scale = glm::vec3(scale);
                            obj.rotationSpeed = birdRotSpeed(rng);
                            obj.radius = 0.5f * scale;
                        }

                        bool foundSpot = false;
                        glm::vec3 fallback = glm::vec3(0.0f, (obj.type == Collectible::Type::Bird) ? obj.radius : 2.5f, 0.0f);
                        for (int attempt = 0; attempt < 24 && !foundSpot; ++attempt) {
                            const float y = (obj.type == Collectible::Type::Bird) ? obj.radius : 2.5f;
                            const float margin = (attempt < 18) ? 0.45f : 0.10f;
                            const glm::vec3 candidate = randomPointInCell(cellX, cellZ, obj.radius, y, rng, margin);
                            fallback = candidate;

                            bool overlapsOtherCollectible = false;
                            for (const Collectible &placed : collectibleItems) {
                                const float separation = std::max(5.0f, placed.radius + obj.radius + 0.2f);
                                if (glm::length(placed.position - candidate) < separation) {
                                    overlapsOtherCollectible = true;
                                    break;
                                }
                            }

                            if (!overlapsOtherCollectible && !checkWallCollision(candidate, obj.radius) && !checkPillarCollision(candidate, obj.radius)) {
                                obj.position = candidate;
                                foundSpot = true;
                            }
                        }

                        if (!foundSpot) {
                            // Keep spawn count fixed: use the last in-cell candidate as a fallback.
                            obj.position = fallback;
                        }
                        collectibleItems.push_back(obj);
                        ++typeIndex;
                    }
                }
            }
        }

        std::vector<WallSegment> wallSegments{};
        std::vector<PillarInstance> pillarInstances{};
        std::vector<Collectible> collectibleItems{};

        float size = 50.0f;
        float wallHeight = 5.0f;
        float wallThicknessValue = 0.5f;
        int mazeGridX = 6;
        int mazeGridZ = 6;
        int collectiblesPerCell = 1;
        float cellSize = 0.0f;
        float eyeHeight = 1.7f;
        int startCellX = 0;
        int startCellZ = 0;
        glm::vec3 startPositionValue{0.0f, 1.7f, 0.0f};
    };

    class RawPillarRenderer {
      public:
        struct PillarVertex {
            glm::vec3 position{0.0f};
            glm::vec2 texCoord{0.0f};
            glm::vec3 normal{0.0f};
        };

        struct PillarUniforms {
            glm::mat4 view{1.0f};
            glm::mat4 proj{1.0f};
            glm::vec4 fx{0.0f};
        };

        void load(mxvk::VK_Window *targetWindow,
                  const std::string &textureManifestPath,
                  const std::string &textureBasePath,
                  const std::vector<char> &vertSpv,
                  const std::vector<char> &fragSpv) {
            if (targetWindow == nullptr) {
                throw mxvk::Exception("walk: raw pillar renderer requires a valid window");
            }
            window = targetWindow;
            vertexSpv = vertSpv;
            fragmentSpv = fragSpv;

            if (!window->ensureRenderResources()) {
                throw mxvk::Exception("walk: raw pillar renderer requires render resources");
            }

            buildGeometry();
            loadTexture(textureManifestPath, textureBasePath);
            createTextureSampler();
            createDescriptorSetLayout();
            createUniformBuffers();
            createDescriptorPool();
            createDescriptorSets();
            createPipeline();
        }

        void resize(mxvk::VK_Window *targetWindow) {
            if (targetWindow == nullptr || targetWindow->getDevice() == VK_NULL_HANDLE) {
                return;
            }

            window = targetWindow;
            destroyPipeline();
            destroyDescriptors();
            createDescriptorSetLayout();
            createUniformBuffers();
            createDescriptorPool();
            createDescriptorSets();
            createPipeline();
        }

        /// @brief Hot-swap the fragment shader without rebuilding geometry or descriptors.
        /// @param newFragSpv Compiled SPIR-V bytecode for the new fragment shader.
        void reloadFragShader(const std::vector<char> &newFragSpv) {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE || newFragSpv.empty()) {
                return;
            }
            vkDeviceWaitIdle(window->getDevice());
            fragmentSpv = newFragSpv;
            destroyPipeline();
            createPipeline();
        }

        void cleanup(mxvk::VK_Window *targetWindow) {
            if (targetWindow == nullptr || targetWindow->getDevice() == VK_NULL_HANDLE) {
                return;
            }

            window = targetWindow;
            destroyPipeline();
            destroyDescriptors();
            destroyTexture();
            destroyBuffers();
            window = nullptr;
        }

        void render(VkCommandBuffer cmd,
                    uint32_t imageIndex,
                    const std::vector<PillarInstance> &pillars,
                    const glm::mat4 &view,
                    const glm::mat4 &proj,
                    const glm::vec4 &fx) {
            if (cmd == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE || pipelineLayout == VK_NULL_HANDLE) {
                return;
            }
            if (imageIndex >= uniformBuffersMapped.size() || descriptorSets.empty() || vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE) {
                return;
            }

            PillarUniforms uniforms{};
            uniforms.view = view;
            uniforms.proj = proj;
            uniforms.fx = fx;
            std::memcpy(uniformBuffersMapped[imageIndex], &uniforms, sizeof(PillarUniforms));

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout,
                                    0,
                                    1,
                                    &descriptorSets[imageIndex],
                                    0,
                                    nullptr);

            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            for (const PillarInstance &pillar : pillars) {
                // Vertex data is defined with Y in [0..1] (base at 0.0, top at 1.0).
                // To avoid z-fighting with the floor, sink the base slightly into the floor
                // and place the translation at the pillar base Y.
                constexpr float baseSink = 0.02f;
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pillar.position.x, pillar.position.y - baseSink, pillar.position.z));
                model = glm::scale(model, glm::vec3(pillar.radius, pillar.height, pillar.radius));
                vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &model);
                vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
            }
        }

      private:
        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties memProperties{};
            vkGetPhysicalDeviceMemoryProperties(window->getPhysicalDevice(), &memProperties);
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
                if ((typeFilter & (1u << i)) != 0u && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            throw mxvk::Exception("walk: failed to find suitable memory type for raw pillar renderer");
        }

        void createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(window->getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw pillar buffer");
            }

            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(window->getDevice(), buffer, &requirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = requirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);

            if (vkAllocateMemory(window->getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
                vkDestroyBuffer(window->getDevice(), buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to allocate raw pillar buffer memory");
            }

            if (vkBindBufferMemory(window->getDevice(), buffer, bufferMemory, 0) != VK_SUCCESS) {
                vkDestroyBuffer(window->getDevice(), buffer, nullptr);
                vkFreeMemory(window->getDevice(), bufferMemory, nullptr);
                buffer = VK_NULL_HANDLE;
                bufferMemory = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to bind raw pillar buffer memory");
            }
        }

        void createImage(uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage &image,
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

            if (vkCreateImage(window->getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw pillar image");
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(window->getDevice(), image, &requirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = requirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);

            if (vkAllocateMemory(window->getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
                vkDestroyImage(window->getDevice(), image, nullptr);
                image = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to allocate raw pillar image memory");
            }

            if (vkBindImageMemory(window->getDevice(), image, memory, 0) != VK_SUCCESS) {
                vkDestroyImage(window->getDevice(), image, nullptr);
                vkFreeMemory(window->getDevice(), memory, nullptr);
                image = VK_NULL_HANDLE;
                memory = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to bind raw pillar image memory");
            }
        }

        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const {
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
            if (vkCreateImageView(window->getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw pillar image view");
            }
            return imageView;
        }

        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = window->getCommandPool();
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(window->getDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to allocate raw pillar command buffer");
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to begin raw pillar command buffer");
            }

            return commandBuffer;
        }

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
            if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to end raw pillar command buffer");
            }

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            if (vkQueueSubmit(window->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to submit raw pillar command buffer");
            }
            if (vkQueueWaitIdle(window->getGraphicsQueue()) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to wait for raw pillar upload queue");
            }

            vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
        }

        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const {
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
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }

            vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            endSingleTimeCommands(cmd);
        }

        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
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

        void createTextureSampler() {
            if (textureSampler != VK_NULL_HANDLE) {
                return;
            }

            VkPhysicalDeviceFeatures deviceFeatures{};
            vkGetPhysicalDeviceFeatures(window->getPhysicalDevice(), &deviceFeatures);
            VkPhysicalDeviceProperties deviceProperties{};
            vkGetPhysicalDeviceProperties(window->getPhysicalDevice(), &deviceProperties);
            const bool anisotropySupported = deviceFeatures.samplerAnisotropy == VK_TRUE;
            const float anisotropyLevel = anisotropySupported
                                              ? std::min(8.0f, deviceProperties.limits.maxSamplerAnisotropy)
                                              : 1.0f;

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.anisotropyEnable = anisotropySupported ? VK_TRUE : VK_FALSE;
            samplerInfo.maxAnisotropy = anisotropyLevel;
            samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

            if (vkCreateSampler(window->getDevice(), &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw pillar texture sampler");
            }
        }

        void createDescriptorSetLayout() {
            if (descriptorSetLayout != VK_NULL_HANDLE) {
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
            uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {samplerBinding, uboBinding};

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            if (vkCreateDescriptorSetLayout(window->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw pillar descriptor set layout");
            }
        }

        void createUniformBuffers() {
            destroyUniformBuffers();

            const size_t frameCount = window->getSwapchainImageCount();
            if (frameCount == 0) {
                return;
            }

            uniformBuffers.resize(frameCount, VK_NULL_HANDLE);
            uniformBufferMemory.resize(frameCount, VK_NULL_HANDLE);
            uniformBuffersMapped.resize(frameCount, nullptr);

            for (size_t i = 0; i < frameCount; ++i) {
                createBuffer(sizeof(PillarUniforms),
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             uniformBuffers[i],
                             uniformBufferMemory[i]);
                vkMapMemory(window->getDevice(), uniformBufferMemory[i], 0, sizeof(PillarUniforms), 0, &uniformBuffersMapped[i]);
            }
        }

        void createDescriptorPool() {
            const uint32_t frameCount = static_cast<uint32_t>(window->getSwapchainImageCount());
            std::array<VkDescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[0].descriptorCount = frameCount;
            poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[1].descriptorCount = frameCount;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = frameCount;

            if (vkCreateDescriptorPool(window->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw pillar descriptor pool");
            }
        }

        void createDescriptorSets() {
            const size_t frameCount = window->getSwapchainImageCount();
            std::vector<VkDescriptorSetLayout> layouts(frameCount, descriptorSetLayout);

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(frameCount);
            allocInfo.pSetLayouts = layouts.data();

            descriptorSets.resize(frameCount, VK_NULL_HANDLE);
            if (vkAllocateDescriptorSets(window->getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to allocate raw pillar descriptor sets");
            }

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = textureView;
            imageInfo.sampler = textureSampler;

            for (size_t i = 0; i < frameCount; ++i) {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = uniformBuffers[i];
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(PillarUniforms);

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

                vkUpdateDescriptorSets(window->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }

        void createPipeline() {
            if (descriptorSetLayout == VK_NULL_HANDLE || vertexSpv.empty() || fragmentSpv.empty() || window->getSwapchainFormat() == VK_FORMAT_UNDEFINED) {
                return;
            }

            const VkShaderModule vertModule = mxvk::create_shader_module(window->getDevice(), vertexSpv);
            const VkShaderModule fragModule = mxvk::create_shader_module(window->getDevice(), fragmentSpv);

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
            binding.stride = sizeof(PillarVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 3> attrs{};
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PillarVertex, position)};
            attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PillarVertex, texCoord)};
            attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PillarVertex, normal)};

            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
            vertexInput.pVertexAttributeDescriptions = attrs.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            // Disable face culling for the procedural pillar geometry. Winding
            // may differ and disabling culling prevents missing faces and flicker.
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments = &blendAttachment;

            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(glm::mat4);

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &descriptorSetLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;

            if (vkCreatePipelineLayout(window->getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                vkDestroyShaderModule(window->getDevice(), fragModule, nullptr);
                vkDestroyShaderModule(window->getDevice(), vertModule, nullptr);
                throw mxvk::Exception("walk: failed to create raw pillar pipeline layout");
            }

            const VkFormat colorFormat = window->getSwapchainFormat();
            const VkFormat depthFormat = window->getDepthFormat();
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
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(window->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
                vkDestroyPipelineLayout(window->getDevice(), pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
                vkDestroyShaderModule(window->getDevice(), fragModule, nullptr);
                vkDestroyShaderModule(window->getDevice(), vertModule, nullptr);
                throw mxvk::Exception("walk: failed to create raw pillar graphics pipeline");
            }

            vkDestroyShaderModule(window->getDevice(), fragModule, nullptr);
            vkDestroyShaderModule(window->getDevice(), vertModule, nullptr);
        }

        void destroyPipeline() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                pipeline = VK_NULL_HANDLE;
                pipelineLayout = VK_NULL_HANDLE;
                return;
            }

            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(window->getDevice(), pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
            if (pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(window->getDevice(), pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
            }
        }

        void destroyDescriptors() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                descriptorSets.clear();
                descriptorPool = VK_NULL_HANDLE;
                descriptorSetLayout = VK_NULL_HANDLE;
                destroyUniformBuffers();
                return;
            }

            descriptorSets.clear();
            if (descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(window->getDevice(), descriptorPool, nullptr);
                descriptorPool = VK_NULL_HANDLE;
            }
            if (descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(window->getDevice(), descriptorSetLayout, nullptr);
                descriptorSetLayout = VK_NULL_HANDLE;
            }
            destroyUniformBuffers();
        }

        void destroyUniformBuffers() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                uniformBuffers.clear();
                uniformBufferMemory.clear();
                uniformBuffersMapped.clear();
                return;
            }

            for (size_t i = 0; i < uniformBuffers.size(); ++i) {
                if (uniformBuffersMapped[i] != nullptr) {
                    vkUnmapMemory(window->getDevice(), uniformBufferMemory[i]);
                    uniformBuffersMapped[i] = nullptr;
                }
                if (uniformBuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(window->getDevice(), uniformBuffers[i], nullptr);
                }
                if (uniformBufferMemory[i] != VK_NULL_HANDLE) {
                    vkFreeMemory(window->getDevice(), uniformBufferMemory[i], nullptr);
                }
            }

            uniformBuffers.clear();
            uniformBufferMemory.clear();
            uniformBuffersMapped.clear();
        }

        void destroyTexture() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                textureView = VK_NULL_HANDLE;
                textureImage = VK_NULL_HANDLE;
                textureMemory = VK_NULL_HANDLE;
                textureSampler = VK_NULL_HANDLE;
                return;
            }

            if (textureView != VK_NULL_HANDLE) {
                vkDestroyImageView(window->getDevice(), textureView, nullptr);
                textureView = VK_NULL_HANDLE;
            }
            if (textureImage != VK_NULL_HANDLE) {
                vkDestroyImage(window->getDevice(), textureImage, nullptr);
                textureImage = VK_NULL_HANDLE;
            }
            if (textureMemory != VK_NULL_HANDLE) {
                vkFreeMemory(window->getDevice(), textureMemory, nullptr);
                textureMemory = VK_NULL_HANDLE;
            }
            if (textureSampler != VK_NULL_HANDLE) {
                vkDestroySampler(window->getDevice(), textureSampler, nullptr);
                textureSampler = VK_NULL_HANDLE;
            }
        }

        void destroyBuffers() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                vertexBuffer = VK_NULL_HANDLE;
                vertexMemory = VK_NULL_HANDLE;
                indexBuffer = VK_NULL_HANDLE;
                indexMemory = VK_NULL_HANDLE;
                return;
            }

            if (vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(window->getDevice(), vertexBuffer, nullptr);
                vertexBuffer = VK_NULL_HANDLE;
            }
            if (vertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(window->getDevice(), vertexMemory, nullptr);
                vertexMemory = VK_NULL_HANDLE;
            }
            if (indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(window->getDevice(), indexBuffer, nullptr);
                indexBuffer = VK_NULL_HANDLE;
            }
            if (indexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(window->getDevice(), indexMemory, nullptr);
                indexMemory = VK_NULL_HANDLE;
            }
        }

        void buildGeometry() {
            constexpr int segments = 16;
            constexpr float bottomCapScale = 1.5f;
            constexpr float baseDepth = -0.05f;

            std::vector<float> vertices;
            std::vector<uint32_t> indices;
            vertices.reserve(128 * 8);
            indices.reserve(192);

            for (int i = 0; i <= segments; ++i) {
                const float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265358979323846f;
                const float xBottom = std::cos(angle) * bottomCapScale;
                const float zBottom = std::sin(angle) * bottomCapScale;
                const float xTop = std::cos(angle);
                const float zTop = std::sin(angle);
                const float u = static_cast<float>(i) / static_cast<float>(segments);
                vertices.insert(vertices.end(), {
                                                    xBottom,
                                                    0.0f,
                                                    zBottom,
                                                    u,
                                                    0.0f,
                                                    xBottom,
                                                    0.0f,
                                                    zBottom,
                                                });
                vertices.insert(vertices.end(), {
                                                    xTop,
                                                    1.0f,
                                                    zTop,
                                                    u,
                                                    1.0f,
                                                    xTop,
                                                    0.0f,
                                                    zTop,
                                                });
            }

            for (int i = 0; i < segments; ++i) {
                const int current = i * 2;
                const int next = (i + 1) * 2;
                indices.insert(indices.end(), {
                                                  static_cast<uint32_t>(current),
                                                  static_cast<uint32_t>(current + 1),
                                                  static_cast<uint32_t>(next),
                                                  static_cast<uint32_t>(next),
                                                  static_cast<uint32_t>(current + 1),
                                                  static_cast<uint32_t>(next + 1),
                                              });
            }

            const uint32_t bottomCenterIndex = static_cast<uint32_t>(vertices.size() / 8);
            vertices.insert(vertices.end(), {
                                                0.0f,
                                                baseDepth,
                                                0.0f,
                                                0.5f,
                                                0.5f,
                                                0.0f,
                                                -1.0f,
                                                0.0f,
                                            });

            const uint32_t bottomCapStart = static_cast<uint32_t>(vertices.size() / 8);
            for (int i = 0; i <= segments; ++i) {
                const float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265358979323846f;
                const float x = std::cos(angle) * bottomCapScale;
                const float z = std::sin(angle) * bottomCapScale;
                vertices.insert(vertices.end(), {
                                                    x,
                                                    0.0f,
                                                    z,
                                                    0.5f + x * 0.5f / bottomCapScale,
                                                    0.5f + z * 0.5f / bottomCapScale,
                                                    0.0f,
                                                    -1.0f,
                                                    0.0f,
                                                });
            }
            for (int i = 0; i < segments; ++i) {
                indices.insert(indices.end(), {
                                                  bottomCenterIndex,
                                                  bottomCapStart + static_cast<uint32_t>(i + 1),
                                                  bottomCapStart + static_cast<uint32_t>(i),
                                              });
            }

            const uint32_t topCenterIndex = static_cast<uint32_t>(vertices.size() / 8);
            vertices.insert(vertices.end(), {
                                                0.0f,
                                                1.0f,
                                                0.0f,
                                                0.5f,
                                                0.5f,
                                                0.0f,
                                                1.0f,
                                                0.0f,
                                            });

            const uint32_t topCapStart = static_cast<uint32_t>(vertices.size() / 8);
            for (int i = 0; i <= segments; ++i) {
                const float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265358979323846f;
                const float x = std::cos(angle);
                const float z = std::sin(angle);
                vertices.insert(vertices.end(), {
                                                    x,
                                                    1.0f,
                                                    z,
                                                    0.5f + x * 0.5f,
                                                    0.5f + z * 0.5f,
                                                    0.0f,
                                                    1.0f,
                                                    0.0f,
                                                });
            }
            for (int i = 0; i < segments; ++i) {
                indices.insert(indices.end(), {
                                                  topCenterIndex,
                                                  topCapStart + static_cast<uint32_t>(i),
                                                  topCapStart + static_cast<uint32_t>(i + 1),
                                              });
            }

            vertexCount = static_cast<uint32_t>(vertices.size() / 8);
            indexCount = static_cast<uint32_t>(indices.size());

            std::vector<PillarVertex> pillarVertices(vertexCount);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                const size_t base = static_cast<size_t>(i) * 8;
                pillarVertices[i].position = glm::vec3(vertices[base + 0], vertices[base + 1], vertices[base + 2]);
                pillarVertices[i].texCoord = glm::vec2(vertices[base + 3], vertices[base + 4]);
                pillarVertices[i].normal = glm::vec3(vertices[base + 5], vertices[base + 6], vertices[base + 7]);
            }

            createBuffer(static_cast<VkDeviceSize>(pillarVertices.size() * sizeof(PillarVertex)),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         vertexBuffer,
                         vertexMemory);
            void *mapped = nullptr;
            vkMapMemory(window->getDevice(), vertexMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
            std::memcpy(mapped, pillarVertices.data(), pillarVertices.size() * sizeof(PillarVertex));
            vkUnmapMemory(window->getDevice(), vertexMemory);

            createBuffer(static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t)),
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         indexBuffer,
                         indexMemory);
            vkMapMemory(window->getDevice(), indexMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
            std::memcpy(mapped, indices.data(), indices.size() * sizeof(uint32_t));
            vkUnmapMemory(window->getDevice(), indexMemory);
        }

        void loadTexture([[maybe_unused]] const std::string &textureManifestPath, const std::string &textureBasePath) {
            SDL_Surface *surface = mxvk::LoadPNG((textureBasePath + "/ground.png").c_str());
            if (surface == nullptr) {
                throw mxvk::Exception("walk: failed to load raw pillar texture");
            }

            const uint32_t width = static_cast<uint32_t>(surface->w);
            const uint32_t height = static_cast<uint32_t>(surface->h);
            const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4U;

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            createBuffer(imageSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer,
                         stagingMemory);

            void *mapped = nullptr;
            vkMapMemory(window->getDevice(), stagingMemory, 0, imageSize, 0, &mapped);
            std::memcpy(mapped, surface->pixels, static_cast<size_t>(imageSize));
            vkUnmapMemory(window->getDevice(), stagingMemory);

            createImage(width, height,
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        textureImage,
                        textureMemory);

            transitionImageLayout(textureImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copyBufferToImage(stagingBuffer, textureImage, width, height);
            transitionImageLayout(textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            textureView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

            vkDestroyBuffer(window->getDevice(), stagingBuffer, nullptr);
            vkFreeMemory(window->getDevice(), stagingMemory, nullptr);
            SDL_DestroySurface(surface);
        }

        mxvk::VK_Window *window = nullptr;
        std::vector<char> vertexSpv{};
        std::vector<char> fragmentSpv{};

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;

        VkImage textureImage = VK_NULL_HANDLE;
        VkDeviceMemory textureMemory = VK_NULL_HANDLE;
        VkImageView textureView = VK_NULL_HANDLE;
        VkSampler textureSampler = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets{};

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        std::vector<VkBuffer> uniformBuffers{};
        std::vector<VkDeviceMemory> uniformBufferMemory{};
        std::vector<void *> uniformBuffersMapped{};
    };

    class RawWallRenderer {
      public:
        struct WallVertex {
            glm::vec3 position{0.0f};
            glm::vec2 texCoord{0.0f};
            glm::vec3 normal{0.0f};
        };

        struct WallUniforms {
            glm::mat4 view{1.0f};
            glm::mat4 proj{1.0f};
            glm::vec4 fx{0.0f};
        };

        void load(mxvk::VK_Window *targetWindow,
                  const std::string &textureManifestPath,
                  const std::string &textureBasePath,
                  const std::vector<char> &vertexShaderSpv,
                  const std::vector<char> &fragmentShaderSpv) {
            if (targetWindow == nullptr) {
                throw mxvk::Exception("walk: raw wall renderer requires a valid window");
            }
            window = targetWindow;
            vertSpv = vertexShaderSpv;
            fragSpv = fragmentShaderSpv;

            if (!window->ensureRenderResources()) {
                throw mxvk::Exception("walk: raw wall renderer requires render resources");
            }

            buildGeometry();
            loadTexture(textureManifestPath, textureBasePath);
            createTextureSampler();
            createDescriptorSetLayout();
            createUniformBuffers();
            createDescriptorPool();
            createDescriptorSets();
            createPipeline();
        }

        void resize(mxvk::VK_Window *targetWindow) {
            if (targetWindow == nullptr || targetWindow->getDevice() == VK_NULL_HANDLE) {
                return;
            }

            window = targetWindow;
            destroyPipeline();
            destroyDescriptors();
            createDescriptorSetLayout();
            createUniformBuffers();
            createDescriptorPool();
            createDescriptorSets();
            createPipeline();
        }

        /// @brief Hot-swap the fragment shader without rebuilding geometry or descriptors.
        /// @param newFragSpv Compiled SPIR-V bytecode for the new fragment shader.
        void reloadFragShader(const std::vector<char> &newFragSpv) {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE || newFragSpv.empty()) {
                return;
            }
            vkDeviceWaitIdle(window->getDevice());
            fragSpv = newFragSpv;
            destroyPipeline();
            createPipeline();
        }

        void cleanup(mxvk::VK_Window *targetWindow) {
            if (targetWindow == nullptr || targetWindow->getDevice() == VK_NULL_HANDLE) {
                return;
            }

            window = targetWindow;
            destroyPipeline();
            destroyDescriptors();
            destroyTexture();
            destroyBuffers();
            window = nullptr;
        }

        void render(VkCommandBuffer cmd,
                    uint32_t imageIndex,
                    const std::vector<WallSegment> &walls,
                    float wallThickness,
                    const glm::mat4 &view,
                    const glm::mat4 &proj,
                    const glm::vec4 &fx) {
            if (cmd == VK_NULL_HANDLE || pipeline == VK_NULL_HANDLE || pipelineLayout == VK_NULL_HANDLE) {
                return;
            }
            if (imageIndex >= uniformBuffersMapped.size() || descriptorSets.empty() || vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE) {
                return;
            }

            WallUniforms uniforms{};
            uniforms.view = view;
            uniforms.proj = proj;
            uniforms.fx = fx;
            std::memcpy(uniformBuffersMapped[imageIndex], &uniforms, sizeof(WallUniforms));

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineLayout,
                                    0,
                                    1,
                                    &descriptorSets[imageIndex],
                                    0,
                                    nullptr);

            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            const float thickness = std::max(0.02f, wallThickness);
            // Extend each wall by about half its thickness on both ends so adjoining
            // runs meet cleanly without visible seam slivers.
            const float wallOverlap = thickness * 0.55f;
            for (const WallSegment &segment : walls) {
                const glm::vec3 center = (segment.start + segment.end) * 0.5f;
                const glm::vec3 span = segment.end - segment.start;
                const float length = glm::length(span);
                if (length < 0.0001f) {
                    continue;
                }
                // Unit wall geometry has Y in [0..1]. Sink slightly to avoid floor/wall
                // depth fighting where the floor top plane is also at y=0.
                constexpr float baseSink = 0.01f;
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(center.x, -baseSink, center.z));
                model = glm::rotate(model, std::atan2(span.z, span.x), glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::scale(model, glm::vec3(length + wallOverlap * 2.0f, segment.height, thickness));
                vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &model);
                vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
            }
        }

      private:
        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties memProperties{};
            vkGetPhysicalDeviceMemoryProperties(window->getPhysicalDevice(), &memProperties);
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
                if ((typeFilter & (1u << i)) != 0u && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            throw mxvk::Exception("walk: failed to find suitable memory type for raw wall renderer");
        }

        void createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(window->getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw wall buffer");
            }

            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(window->getDevice(), buffer, &requirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = requirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);

            if (vkAllocateMemory(window->getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
                vkDestroyBuffer(window->getDevice(), buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to allocate raw wall buffer memory");
            }

            if (vkBindBufferMemory(window->getDevice(), buffer, bufferMemory, 0) != VK_SUCCESS) {
                vkDestroyBuffer(window->getDevice(), buffer, nullptr);
                vkFreeMemory(window->getDevice(), bufferMemory, nullptr);
                buffer = VK_NULL_HANDLE;
                bufferMemory = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to bind raw wall buffer memory");
            }
        }

        void buildGeometry() {
            // Unit wall prism: X in [-0.5..0.5], Y in [0..1], Z in [-0.5..0.5].
            // Per-instance scaling in render() controls segment length/height/thickness.
            std::vector<WallVertex> verts;
            std::vector<uint32_t> inds;
            verts.reserve(24);
            inds.reserve(36);

            const auto addFace = [&verts, &inds](const glm::vec3 &v0,
                                                 const glm::vec3 &v1,
                                                 const glm::vec3 &v2,
                                                 const glm::vec3 &v3,
                                                 const glm::vec3 &normal) {
                const uint32_t base = static_cast<uint32_t>(verts.size());
                verts.push_back({v0, glm::vec2(0.0f, 0.0f), normal});
                verts.push_back({v1, glm::vec2(1.0f, 0.0f), normal});
                verts.push_back({v2, glm::vec2(1.0f, 1.0f), normal});
                verts.push_back({v3, glm::vec2(0.0f, 1.0f), normal});
                inds.insert(inds.end(), {base + 0, base + 1, base + 2, base + 2, base + 3, base + 0});
            };

            constexpr float x = 0.5f;
            constexpr float z = 0.5f;
            constexpr float y0 = 0.0f;
            constexpr float y1 = 1.0f;

            addFace(glm::vec3(-x, y0, z), glm::vec3(x, y0, z), glm::vec3(x, y1, z), glm::vec3(-x, y1, z), glm::vec3(0.0f, 0.0f, 1.0f));
            addFace(glm::vec3(x, y0, -z), glm::vec3(-x, y0, -z), glm::vec3(-x, y1, -z), glm::vec3(x, y1, -z), glm::vec3(0.0f, 0.0f, -1.0f));
            addFace(glm::vec3(x, y0, z), glm::vec3(x, y0, -z), glm::vec3(x, y1, -z), glm::vec3(x, y1, z), glm::vec3(1.0f, 0.0f, 0.0f));
            addFace(glm::vec3(-x, y0, -z), glm::vec3(-x, y0, z), glm::vec3(-x, y1, z), glm::vec3(-x, y1, -z), glm::vec3(-1.0f, 0.0f, 0.0f));
            addFace(glm::vec3(-x, y1, z), glm::vec3(x, y1, z), glm::vec3(x, y1, -z), glm::vec3(-x, y1, -z), glm::vec3(0.0f, 1.0f, 0.0f));
            addFace(glm::vec3(-x, y0, -z), glm::vec3(x, y0, -z), glm::vec3(x, y0, z), glm::vec3(-x, y0, z), glm::vec3(0.0f, -1.0f, 0.0f));

            vertexCount = static_cast<uint32_t>(verts.size());
            indexCount = static_cast<uint32_t>(inds.size());

            createBuffer(static_cast<VkDeviceSize>(verts.size() * sizeof(WallVertex)),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         vertexBuffer,
                         vertexMemory);
            void *mapped = nullptr;
            vkMapMemory(window->getDevice(), vertexMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
            std::memcpy(mapped, verts.data(), verts.size() * sizeof(WallVertex));
            vkUnmapMemory(window->getDevice(), vertexMemory);

            createBuffer(static_cast<VkDeviceSize>(inds.size() * sizeof(uint32_t)),
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         indexBuffer,
                         indexMemory);
            vkMapMemory(window->getDevice(), indexMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
            std::memcpy(mapped, inds.data(), inds.size() * sizeof(uint32_t));
            vkUnmapMemory(window->getDevice(), indexMemory);
        }

        void loadTexture([[maybe_unused]] const std::string &textureManifestPath, const std::string &textureBasePath) {
            SDL_Surface *surface = mxvk::LoadPNG((textureBasePath + "/wall_bricks.png").c_str());
            if (surface == nullptr) {
                throw mxvk::Exception("walk: failed to load raw wall texture");
            }

            const uint32_t width = static_cast<uint32_t>(surface->w);
            const uint32_t height = static_cast<uint32_t>(surface->h);
            const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4U;

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            createBuffer(imageSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         stagingBuffer,
                         stagingMemory);

            void *mapped = nullptr;
            vkMapMemory(window->getDevice(), stagingMemory, 0, imageSize, 0, &mapped);
            std::memcpy(mapped, surface->pixels, static_cast<size_t>(imageSize));
            vkUnmapMemory(window->getDevice(), stagingMemory);

            createImage(width, height,
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        textureImage,
                        textureMemory);

            transitionImageLayout(textureImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copyBufferToImage(stagingBuffer, textureImage, width, height);
            transitionImageLayout(textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            textureView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

            vkDestroyBuffer(window->getDevice(), stagingBuffer, nullptr);
            vkFreeMemory(window->getDevice(), stagingMemory, nullptr);
            SDL_DestroySurface(surface);
        }

        void createTextureSampler() {
            if (textureSampler != VK_NULL_HANDLE) {
                return;
            }

            VkPhysicalDeviceFeatures deviceFeatures{};
            vkGetPhysicalDeviceFeatures(window->getPhysicalDevice(), &deviceFeatures);
            VkPhysicalDeviceProperties deviceProperties{};
            vkGetPhysicalDeviceProperties(window->getPhysicalDevice(), &deviceProperties);
            const bool anisotropySupported = deviceFeatures.samplerAnisotropy == VK_TRUE;
            const float anisotropyLevel = anisotropySupported
                                              ? std::min(8.0f, deviceProperties.limits.maxSamplerAnisotropy)
                                              : 1.0f;

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            samplerInfo.anisotropyEnable = anisotropySupported ? VK_TRUE : VK_FALSE;
            samplerInfo.maxAnisotropy = anisotropyLevel;
            samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

            if (vkCreateSampler(window->getDevice(), &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw wall texture sampler");
            }
        }

        void createDescriptorSetLayout() {
            if (descriptorSetLayout != VK_NULL_HANDLE) {
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
            uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {samplerBinding, uboBinding};

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            if (vkCreateDescriptorSetLayout(window->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw wall descriptor set layout");
            }
        }

        void createUniformBuffers() {
            destroyUniformBuffers();

            const size_t frameCount = window->getSwapchainImageCount();
            if (frameCount == 0) {
                return;
            }

            uniformBuffers.resize(frameCount, VK_NULL_HANDLE);
            uniformBufferMemory.resize(frameCount, VK_NULL_HANDLE);
            uniformBuffersMapped.resize(frameCount, nullptr);

            for (size_t i = 0; i < frameCount; ++i) {
                createBuffer(sizeof(WallUniforms),
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             uniformBuffers[i],
                             uniformBufferMemory[i]);
                vkMapMemory(window->getDevice(), uniformBufferMemory[i], 0, sizeof(WallUniforms), 0, &uniformBuffersMapped[i]);
            }
        }

        void createDescriptorPool() {
            const uint32_t frameCount = static_cast<uint32_t>(window->getSwapchainImageCount());
            std::array<VkDescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[0].descriptorCount = frameCount;
            poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[1].descriptorCount = frameCount;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = frameCount;

            if (vkCreateDescriptorPool(window->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw wall descriptor pool");
            }
        }

        void createDescriptorSets() {
            const size_t frameCount = window->getSwapchainImageCount();
            std::vector<VkDescriptorSetLayout> layouts(frameCount, descriptorSetLayout);

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(frameCount);
            allocInfo.pSetLayouts = layouts.data();

            descriptorSets.resize(frameCount, VK_NULL_HANDLE);
            if (vkAllocateDescriptorSets(window->getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to allocate raw wall descriptor sets");
            }

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = textureView;
            imageInfo.sampler = textureSampler;

            for (size_t i = 0; i < frameCount; ++i) {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = uniformBuffers[i];
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(WallUniforms);

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

                vkUpdateDescriptorSets(window->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }

        void createPipeline() {
            if (descriptorSetLayout == VK_NULL_HANDLE || vertSpv.empty() || fragSpv.empty() || window->getSwapchainFormat() == VK_FORMAT_UNDEFINED) {
                return;
            }

            const VkShaderModule vertModule = mxvk::create_shader_module(window->getDevice(), vertSpv);
            const VkShaderModule fragModule = mxvk::create_shader_module(window->getDevice(), fragSpv);

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
            binding.stride = sizeof(WallVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 3> attrs{};
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(WallVertex, position)};
            attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(WallVertex, texCoord)};
            attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(WallVertex, normal)};

            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
            vertexInput.pVertexAttributeDescriptions = attrs.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments = &blendAttachment;

            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(glm::mat4);

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &descriptorSetLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;

            if (vkCreatePipelineLayout(window->getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
                vkDestroyShaderModule(window->getDevice(), fragModule, nullptr);
                vkDestroyShaderModule(window->getDevice(), vertModule, nullptr);
                throw mxvk::Exception("walk: failed to create raw wall pipeline layout");
            }

            const VkFormat colorFormat = window->getSwapchainFormat();
            const VkFormat depthFormat = window->getDepthFormat();
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
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(window->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
                vkDestroyPipelineLayout(window->getDevice(), pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
                vkDestroyShaderModule(window->getDevice(), fragModule, nullptr);
                vkDestroyShaderModule(window->getDevice(), vertModule, nullptr);
                throw mxvk::Exception("walk: failed to create raw wall graphics pipeline");
            }

            vkDestroyShaderModule(window->getDevice(), fragModule, nullptr);
            vkDestroyShaderModule(window->getDevice(), vertModule, nullptr);
        }

        void destroyPipeline() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                pipeline = VK_NULL_HANDLE;
                pipelineLayout = VK_NULL_HANDLE;
                return;
            }

            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(window->getDevice(), pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
            if (pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(window->getDevice(), pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
            }
        }

        void destroyDescriptors() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                descriptorSets.clear();
                descriptorPool = VK_NULL_HANDLE;
                descriptorSetLayout = VK_NULL_HANDLE;
                destroyUniformBuffers();
                return;
            }

            descriptorSets.clear();
            if (descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(window->getDevice(), descriptorPool, nullptr);
                descriptorPool = VK_NULL_HANDLE;
            }
            if (descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(window->getDevice(), descriptorSetLayout, nullptr);
                descriptorSetLayout = VK_NULL_HANDLE;
            }
            destroyUniformBuffers();
        }

        void destroyUniformBuffers() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                uniformBuffers.clear();
                uniformBufferMemory.clear();
                uniformBuffersMapped.clear();
                return;
            }

            for (size_t i = 0; i < uniformBuffers.size(); ++i) {
                if (uniformBuffersMapped[i] != nullptr) {
                    vkUnmapMemory(window->getDevice(), uniformBufferMemory[i]);
                    uniformBuffersMapped[i] = nullptr;
                }
                if (uniformBuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(window->getDevice(), uniformBuffers[i], nullptr);
                }
                if (uniformBufferMemory[i] != VK_NULL_HANDLE) {
                    vkFreeMemory(window->getDevice(), uniformBufferMemory[i], nullptr);
                }
            }

            uniformBuffers.clear();
            uniformBufferMemory.clear();
            uniformBuffersMapped.clear();
        }

        void destroyTexture() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                textureView = VK_NULL_HANDLE;
                textureImage = VK_NULL_HANDLE;
                textureMemory = VK_NULL_HANDLE;
                textureSampler = VK_NULL_HANDLE;
                return;
            }

            if (textureView != VK_NULL_HANDLE) {
                vkDestroyImageView(window->getDevice(), textureView, nullptr);
                textureView = VK_NULL_HANDLE;
            }
            if (textureImage != VK_NULL_HANDLE) {
                vkDestroyImage(window->getDevice(), textureImage, nullptr);
                textureImage = VK_NULL_HANDLE;
            }
            if (textureMemory != VK_NULL_HANDLE) {
                vkFreeMemory(window->getDevice(), textureMemory, nullptr);
                textureMemory = VK_NULL_HANDLE;
            }
            if (textureSampler != VK_NULL_HANDLE) {
                vkDestroySampler(window->getDevice(), textureSampler, nullptr);
                textureSampler = VK_NULL_HANDLE;
            }
        }

        void destroyBuffers() {
            if (window == nullptr || window->getDevice() == VK_NULL_HANDLE) {
                vertexBuffer = VK_NULL_HANDLE;
                vertexMemory = VK_NULL_HANDLE;
                indexBuffer = VK_NULL_HANDLE;
                indexMemory = VK_NULL_HANDLE;
                return;
            }

            if (vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(window->getDevice(), vertexBuffer, nullptr);
                vertexBuffer = VK_NULL_HANDLE;
            }
            if (vertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(window->getDevice(), vertexMemory, nullptr);
                vertexMemory = VK_NULL_HANDLE;
            }
            if (indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(window->getDevice(), indexBuffer, nullptr);
                indexBuffer = VK_NULL_HANDLE;
            }
            if (indexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(window->getDevice(), indexMemory, nullptr);
                indexMemory = VK_NULL_HANDLE;
            }
        }

        void createImage(uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage &image,
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

            if (vkCreateImage(window->getDevice(), &imageInfo, nullptr, &image) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw wall image");
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(window->getDevice(), image, &requirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = requirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);

            if (vkAllocateMemory(window->getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
                vkDestroyImage(window->getDevice(), image, nullptr);
                image = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to allocate raw wall image memory");
            }

            if (vkBindImageMemory(window->getDevice(), image, memory, 0) != VK_SUCCESS) {
                vkDestroyImage(window->getDevice(), image, nullptr);
                vkFreeMemory(window->getDevice(), memory, nullptr);
                image = VK_NULL_HANDLE;
                memory = VK_NULL_HANDLE;
                throw mxvk::Exception("walk: failed to bind raw wall image memory");
            }
        }

        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const {
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
            if (vkCreateImageView(window->getDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create raw wall image view");
            }
            return imageView;
        }

        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = window->getCommandPool();
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(window->getDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to allocate raw wall command buffer");
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to begin raw wall command buffer");
            }

            return commandBuffer;
        }

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
            if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to end raw wall command buffer");
            }

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            if (vkQueueSubmit(window->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to submit raw wall command buffer");
            }
            if (vkQueueWaitIdle(window->getGraphicsQueue()) != VK_SUCCESS) {
                vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
                throw mxvk::Exception("walk: failed to wait for raw wall upload queue");
            }

            vkFreeCommandBuffers(window->getDevice(), window->getCommandPool(), 1, &commandBuffer);
        }

        void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) const {
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
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }

            vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            endSingleTimeCommands(cmd);
        }

        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
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

        std::vector<char> vertSpv{};
        std::vector<char> fragSpv{};

        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;

        VkImage textureImage = VK_NULL_HANDLE;
        VkDeviceMemory textureMemory = VK_NULL_HANDLE;
        VkImageView textureView = VK_NULL_HANDLE;
        VkSampler textureSampler = VK_NULL_HANDLE;

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets{};

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        std::vector<VkBuffer> uniformBuffers{};
        std::vector<VkDeviceMemory> uniformBufferMemory{};
        std::vector<void *> uniformBuffersMapped{};

        VkDevice device [[maybe_unused]] = VK_NULL_HANDLE;
        mxvk::VK_Window *window = nullptr;
    };

    class WalkWindow final : public mxvk::VK_IOWindow {
      public:
        WalkWindow(const Arguments &args)
            : mxvk::VK_IOWindow(args.path, "FPS Maze Room - MXVK", args.width, args.height, args.fullscreen, args.enable_vsync),
              assetRoot((args.path.empty() || args.path == ".") ? std::string(WALK_ASSET_DIR) : args.path),
              shaderRoot(assetRoot + "/data"),
              modelRoot(assetRoot + "/data") {
            logEnv(std::format("initializing window {}x{} (fullscreen={})", args.width, args.height, args.fullscreen ? "true" : "false"));
            logEnv(std::format("asset root: {}", assetRoot));
            logEnv(std::format("model root: {}", modelRoot));

            std::mt19937 rng(static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            world.generate(rng());
            logEnv(std::format("world generated (walls={}, pillars={}, collectibles={})",
                               world.walls().size(),
                               world.pillars().size(),
                               world.collectibles().size()));
            setClearColor(100.0f / 255.0f, 181.0f / 255.0f, 246.0f / 255.0f, 1.0f);

            cameraPos = world.startPosition();
            yaw = chooseBestSpawnYaw(cameraPos);
            pitch = 0.0f;
            updateCameraVectors();

            setFont(assetRoot + "/data/font.ttf", 22);

            const std::string vertPath = shaderRoot + "/model.vert.spv";
            const std::string wallFragPath = shaderRoot + "/wall.frag.spv";
            const std::string floorFragPath = shaderRoot + "/floor.frag.spv";
            const std::string pillarVertPath = shaderRoot + "/pillar.vert.spv";
            const std::string pillarFragPath = shaderRoot + "/pillar.frag.spv";
            const std::string objectFragPath = shaderRoot + "/object.frag.spv";
            const std::string bulletFragPath = shaderRoot + "/bullet.frag.spv";
            const std::string particleFragPath = shaderRoot + "/particle.frag.spv";
            const std::string groundTexManifest = assetRoot + "/data/ground.tex";

            modelVertSpv = vertPath;
            pillarVertSpv = pillarVertPath;
            wallFragSpv = wallFragPath;
            floorFragSpv = floorFragPath;
            pillarFragSpv = pillarFragPath;
            objectFragSpv = objectFragPath;
            bulletFragSpv = bulletFragPath;

            loadModel(floorModel, modelRoot + "/cube.mxmod.z", groundTexManifest, assetRoot + "/data", vertPath, floorFragPath);
            loadModel(bulletModel, modelRoot + "/sphere.mxmod.z", "", "", vertPath, bulletFragPath);

            logEnv("loading wall renderer assets");
            rawWallRenderer.load(this,
                                 groundTexManifest,
                                 assetRoot + "/data",
                                 loadSpv(pillarVertPath),
                                 loadSpv(wallFragPath));
            logEnv("wall renderer ready");

            logEnv("loading pillar renderer assets");
            rawPillarRenderer.load(this,
                                   groundTexManifest,
                                   assetRoot + "/data",
                                   loadSpv(pillarVertPath),
                                   loadSpv(pillarFragPath));
            logEnv("pillar renderer ready");

            loadModel(saturnModel, assetRoot + "/data/saturn.mxmod.z",
                      assetRoot + "/data/planet.tex", assetRoot + "/data", vertPath, objectFragPath);
            loadModel(birdModel, assetRoot + "/data/tux.obj",
                      assetRoot + "/data/tux.mtl", assetRoot + "/data", vertPath, objectFragPath);
            loadModel(blasterModel, assetRoot + "/data/blaster.obj",
                      assetRoot + "/data/blaster.mtl", assetRoot + "/data", vertPath, objectFragPath);
            normalizeCollectiblesToModel();

            pointParticleVertSpv = shaderRoot + "/particle_points.vert.spv";
            pointParticleFragSpv = shaderRoot + "/particle_points.frag.spv";
            initializePointParticles();
            logEnv("point-particle pipeline initialized");

            tryOpenFirstGamepad();
            SDL_SetWindowRelativeMouseMode(getSDLWindow(), true);
            logEnv("mouse capture enabled");
        }

        ~WalkWindow() override {
            logEnv("shutting down walk window");
            if (gamepad != nullptr) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
                gamepadId = 0;
            }
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
                destroyPointParticles();
                cleanupModels();
            }
        }

        void event(SDL_Event &e) override {
            const bool is_left_double_click =
                (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                 e.button.button == SDL_BUTTON_LEFT &&
                 e.button.clicks >= 2);

            if (is_left_double_click) {
                if (SDL_Window *const sdlWindow = getSDLWindow(); sdlWindow != nullptr) {
                    SDL_RaiseWindow(sdlWindow);
                    SDL_SetWindowMouseGrab(sdlWindow, true);
                    SDL_SetWindowRelativeMouseMode(sdlWindow, true);
                }

                mouseCapture = true;
                firstMouse = true;
                if (!visible()) {
                    suppressProjectileOnNextLeftDown = true;
                }
                logEnv("mouse capture enabled (double-click)");
            }

            mxvk::VK_IOWindow::event(e);
        }

        void console_event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_QUIT) {
                logEnv("received quit event");
                exit();
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    if (mouseCapture) {
                        mouseCapture = false;
                        SDL_SetWindowRelativeMouseMode(getSDLWindow(), false);
                        suppressProjectileOnNextLeftDown = false;
                        logEnv("mouse capture disabled (ESC)");
                    } else {
                        logEnv("exit requested by ESC");
                        exit();
                        return;
                    }
                } else if (e.key.key == SDLK_F) {
                    showFps = !showFps;
                    logEnv(std::format("FPS overlay {}", showFps ? "enabled" : "disabled"));
                }
            }

            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                logEnv(std::format("gamepad added (id={})", static_cast<int>(e.gdevice.which)));
                openGamepad(e.gdevice.which);
            }

            if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad != nullptr && e.gdevice.which == gamepadId) {
                    logEnv(std::format("gamepad removed (id={})", static_cast<int>(e.gdevice.which)));
                    SDL_CloseGamepad(gamepad);
                    gamepad = nullptr;
                    gamepadId = 0;
                }
            }

            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK || e.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    logEnv("exit requested by gamepad back/start");
                    exit();
                } else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                    fireProjectile();
                } else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH && cameraPos.y <= 1.71f) {
                    jumpVelocity = 0.3f;
                    logEnv("jump triggered by gamepad");
                }
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && mouseCapture) {
                if (firstMouse) {
                    firstMouse = false;
                    return;
                }
                yaw += static_cast<float>(e.motion.xrel) * mouseSensitivity;
                pitch -= static_cast<float>(e.motion.yrel) * mouseSensitivity;
                pitch = glm::clamp(pitch, -89.0f, 89.0f);
                updateCameraVectors();
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && mouseCapture) {
                if (suppressProjectileOnNextLeftDown) {
                    suppressProjectileOnNextLeftDown = false;
                    return;
                }
                fireProjectile();
            }
        }

        void console_proc() override {
            tryOpenFirstGamepad();
            const auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastTick).count();
            lastTick = now;
            deltaTime = std::clamp(deltaTime, 0.0f, 0.05f);

            if (!visible()) {
                updatePlayer(deltaTime);
            }
            updateProjectiles(deltaTime);
            updateExplosions(deltaTime);
            updateCollectibles(deltaTime);

            const int aliveObjects = world.activeCollectibles();
            if (!visible()) {
                printText(std::format("Objects left: {}", aliveObjects), 20, 20, {255, 255, 255, 255});
                printText(std::format("Active Bullets: {}", bullets.size()), 20, 48, {255, 220, 120, 255});
                if (showFps && deltaTime > 0.0001f) {
                    const int fps = static_cast<int>(1.0f / deltaTime);
                    printText(std::format("FPS: {}", fps), 20, 76, {120, 255, 120, 255});
                }

                const VkExtent2D extent = getSwapchainExtent();
                const int cx = static_cast<int>(extent.width / 2U);
                const int cy = static_cast<int>(extent.height / 2U);
                printText("+", cx - 6, cy - 12, {255, 64, 64, 255});
                printText("3D Room - WASD/Left Stick move, Mouse/Right Stick look, Click/RB shoot, Back/Start quit", 20, static_cast<int>(extent.height) - 36, {210, 210, 210, 255});
            }
        }

        void onSwapchainRecreated() override {
            logEnv("swapchain recreated; resizing render resources");
            floorModel.resize(this);
            rawWallRenderer.resize(this);
            rawPillarRenderer.resize(this);
            saturnModel.resize(this);
            birdModel.resize(this);
            blasterModel.resize(this);
            bulletModel.resize(this);
            rebuildPointParticlePipeline();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            const glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
            proj[1][1] *= -1.0f;

            const float t = static_cast<float>(SDL_GetTicks()) * 0.001f;

            // Floor: a thin slab sized to cover the maze footprint.
            {
                constexpr float floorHalfSize = 100.0f;
                constexpr float floorThickness = 0.04f;
                const glm::vec3 extent = floorModel.modelAxisExtent();
                const glm::vec3 srcScale(
                    (floorHalfSize * 2.0f) / std::max(extent.x, 1e-4f),
                    floorThickness / std::max(extent.y, 1e-4f),
                    (floorHalfSize * 2.0f) / std::max(extent.z, 1e-4f));
                glm::mat4 floorWorld = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.02f, 0.0f));
                floorWorld = glm::scale(floorWorld, srcScale);
                renderModel(cmd, imageIndex, floorModel, floorWorld, view, proj,
                            glm::vec4(0.0f, 0.0f, 0.0f, t), false);
            }

            rawWallRenderer.render(cmd,
                                   imageIndex,
                                   world.walls(),
                                   world.wallThickness(),
                                   view,
                                   proj,
                                   glm::vec4(0.58f, 0.58f, 0.65f, t));

            rawPillarRenderer.render(cmd, imageIndex, world.pillars(), view, proj, glm::vec4(0.0f, 0.0f, 0.0f, t));

            for (const Collectible &obj : world.collectibles()) {
                if (!obj.active) {
                    continue;
                }
                glm::mat4 world = glm::translate(glm::mat4(1.0f), obj.position);
                world = glm::rotate(world, glm::radians(obj.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                world = glm::scale(world, obj.scale);
                if (obj.type == Collectible::Type::Saturn) {
                    renderRawModel(cmd, imageIndex, saturnModel, world, view, proj, glm::vec4(cameraPos, 0.0f));
                } else {
                    renderRawModel(cmd, imageIndex, birdModel, world, view, proj, glm::vec4(cameraPos, 0.0f));
                }
            }

            if (!visible()) {
                renderRawModel(cmd,
                               imageIndex,
                               blasterModel,
                               blasterWorldTransform(),
                               view,
                               proj,
                               glm::vec4(cameraPos, 0.0f));
            }

            for (const Projectile &bullet : bullets) {
                if (!bullet.active) {
                    continue;
                }
                if (bullet.lifetime < 0.05f) {
                    continue;
                }
                glm::mat4 world = glm::translate(glm::mat4(1.0f), bullet.position);
                world = glm::scale(world, glm::vec3(0.07f, 0.07f, 0.20f));
                const float fadeProgress = glm::clamp(bullet.lifetime / bullet.maxLifetime, 0.0f, 1.0f);
                const float distanceProgress = glm::clamp(bullet.distanceTraveled / bullet.maxDistance, 0.0f, 1.0f);
                const float alpha = std::min(1.0f - fadeProgress, 1.0f - distanceProgress);
                renderRawModel(cmd, imageIndex, bulletModel, world, view, proj, glm::vec4(alpha, 0.0f, 0.0f, 0.0f));
            }
            renderPointParticles(cmd, view, proj);
        }

      private:
        enum class ProjectileHitType {
            None,
            Floor,
            Wall,
            Pillar,
        };

        struct ProjectileTraceHit {
            ProjectileHitType type = ProjectileHitType::None;
            glm::vec3 impact{0.0f};
            size_t collectibleIndex = 0;
        };

        [[nodiscard]] ProjectileTraceHit traceProjectileSegment(const glm::vec3 &from, const glm::vec3 &to) const {
            const glm::vec3 dir = to - from;
            const float travel = glm::length(dir);
            if (travel <= 1e-8f) {
                return {};
            }

            constexpr float sampleStride = 0.03f;
            constexpr float projectileRadius = 0.015f;
            const int steps = std::max(1, static_cast<int>(std::ceil(travel / sampleStride)));
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const glm::vec3 point = from + (dir * t);
                if (pointHitsWall3D(point, projectileRadius)) {
                    return {ProjectileHitType::Wall, point, 0};
                }
                if (pointHitsPillar3D(point, projectileRadius)) {
                    return {ProjectileHitType::Pillar, point, 0};
                }
                if (point.y <= 0.0f) {
                    return {ProjectileHitType::Floor, point, 0};
                }
            }

            return {};
        }

        bool handleConsoleCommand(const std::vector<std::string> &args, std::ostream &out) override {
            if (args.empty()) {
                return true;
            }

            const std::string &cmd = args[0];

            if (cmd == "spawn_random" || (cmd == "spawn" && args.size() >= 2 && args[1] == "random")) {
                const int attempts = (args.size() >= 3 && cmd == "spawn") ? parseIntOrDefault(args[2], 128)
                                                                          : ((args.size() >= 2 && cmd == "spawn_random") ? parseIntOrDefault(args[1], 128) : 128);
                glm::vec3 candidate = cameraPos;
                if (!sampleNavigablePoint(1.7f, 0.68f, candidate, std::max(1, attempts))) {
                    candidate = world.startPosition();
                }

                cameraPos = candidate;
                yaw = chooseBestSpawnYaw(cameraPos);
                pitch = 0.0f;
                updateCameraVectors();

                out << std::format("Spawned at random location ({:.2f}, {:.2f}, {:.2f})", cameraPos.x, cameraPos.y, cameraPos.z);
                logEnv("command: spawn_random");
                return true;
            }

            if (cmd == "reset" || cmd == "reset_collectibles") {
                std::vector<Collectible> &collectibles = world.collectibles();
                for (size_t i = 0; i < collectibles.size(); ++i) {
                    Collectible &obj = collectibles[i];
                    obj.active = true;
                    obj.rotation = glm::vec3(0.0f);
                    relocateCollectible(i, 2.0f, 128);
                }
                resolveCollectibleClusters(2.0f, 4);
                destroyedCount = 0;
                out << std::format("Collectibles reset. Active collectibles: {}", world.activeCollectibles());
                logEnv("command: reset collectibles");
                return true;
            }

            if (cmd == "add_collectibles" || cmd == "add_collectables") {
                const int requested = (args.size() >= 2) ? parseIntOrDefault(args[1], 10) : 10;
                const int toAdd = std::clamp(requested, 1, 200);
                std::uniform_int_distribution<int> typeDist(0, 1);
                std::uniform_real_distribution<float> saturnScale(0.4f, 0.8f);
                std::uniform_real_distribution<float> saturnRotSpeed(5.0f, 15.0f);
                std::uniform_real_distribution<float> birdScale(0.3f, 0.5f);
                std::uniform_real_distribution<float> birdRotSpeed(20.0f, 60.0f);

                int added = 0;
                for (int i = 0; i < toAdd; ++i) {
                    Collectible obj{};
                    obj.type = (typeDist(rng) == 0) ? Collectible::Type::Saturn : Collectible::Type::Bird;
                    if (obj.type == Collectible::Type::Saturn) {
                        const float scale = saturnScale(rng);
                        obj.scale = glm::vec3(scale);
                        obj.rotationSpeed = saturnRotSpeed(rng);
                        obj.radius = saturnHitRadiusForScale(scale);
                        obj.hitCenterOffset = saturnHitCenterOffsetForScale(scale);
                    } else {
                        const float scale = birdScale(rng);
                        obj.scale = glm::vec3(scale);
                        obj.rotationSpeed = birdRotSpeed(rng);
                        obj.radius = birdHitHalfSideForScale(scale);
                        obj.hitCenterOffset = birdHitCenterOffsetForScale(scale);
                    }

                    bool placed = false;
                    for (int attempt = 0; attempt < 96; ++attempt) {
                        const float y = (obj.type == Collectible::Type::Bird) ? birdGroundYForScale(obj.scale.x) : 2.5f;
                        const float placementRadius = placementRadiusForCollectible(obj);
                        glm::vec3 candidate{};
                        if (!sampleNavigablePoint(y, placementRadius, candidate, 1)) {
                            continue;
                        }

                        bool overlaps = false;
                        for (const Collectible &existing : world.collectibles()) {
                            if (!existing.active) {
                                continue;
                            }
                            const float separation = std::max(5.0f, existing.radius + obj.radius + 0.2f);
                            if (glm::length(existing.position - candidate) < separation) {
                                overlaps = true;
                                break;
                            }
                        }

                        if (!overlaps) {
                            obj.position = candidate;
                            placed = true;
                            break;
                        }
                    }

                    if (placed) {
                        world.collectibles().push_back(obj);
                        ++added;
                    }
                }

                out << std::format("Added {} collectible(s). Active collectibles: {}",
                                   added,
                                   world.activeCollectibles());
                resolveCollectibleClusters(2.0f, 4);
                logEnv(std::format("command: add_collectibles requested={} added={}", toAdd, added));
                return true;
            }

            if (cmd == "status") {
                out << std::format("pos=({:.2f}, {:.2f}, {:.2f}) yaw={:.2f} pitch={:.2f}\n"
                                   "walls={} pillars={} collectibles(active/total)={}/{} bullets={} particles={} destroyed={}",
                                   cameraPos.x,
                                   cameraPos.y,
                                   cameraPos.z,
                                   yaw,
                                   pitch,
                                   world.walls().size(),
                                   world.pillars().size(),
                                   world.activeCollectibles(),
                                   world.collectibles().size(),
                                   bullets.size(),
                                   explosionParticles.size(),
                                   destroyedCount);
                return true;
            }

            if (cmd == "teleport") {
                if (args.size() < 4) {
                    out << "Usage: teleport <x> <y> <z>";
                    return true;
                }

                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                if (!tryParseFloat(args[1], x) || !tryParseFloat(args[2], y) || !tryParseFloat(args[3], z)) {
                    out << "teleport: invalid numeric argument(s)";
                    return true;
                }

                const glm::vec3 candidate{x, y, z};
                if (world.checkWallCollision(candidate, 0.68f) || world.checkPillarCollision(candidate, 0.68f)) {
                    out << "teleport blocked: target intersects wall/pillar";
                    return true;
                }

                cameraPos = candidate;
                out << std::format("Teleported to ({:.2f}, {:.2f}, {:.2f})", x, y, z);
                logEnv("command: teleport");
                return true;
            }

            if (cmd == "clear_bullets") {
                const std::size_t removed = bullets.size();
                bullets.clear();
                out << std::format("Cleared {} bullet(s)", removed);
                return true;
            }

            if (cmd == "clear_fx") {
                const std::size_t removed = explosionParticles.size();
                explosionParticles.clear();
                out << std::format("Cleared {} particle effect(s)", removed);
                return true;
            }

            if (cmd == "set_fps") {
                if (args.size() < 2) {
                    out << std::format("FPS overlay is currently {}. Usage: set_fps <on|off>", showFps ? "on" : "off");
                    return true;
                }
                const std::string value = toLowerCopy(args[1]);
                if (value == "on" || value == "1" || value == "true") {
                    showFps = true;
                    out << "FPS overlay enabled";
                    return true;
                }
                if (value == "off" || value == "0" || value == "false") {
                    showFps = false;
                    out << "FPS overlay disabled";
                    return true;
                }

                out << "Usage: set_fps <on|off>";
                return true;
            }

            if (cmd == "regen_world") {
                const uint32_t seed = (args.size() >= 2) ? static_cast<uint32_t>(parseIntOrDefault(args[1], static_cast<int>(rng())))
                                                         : rng();
                world.generate(seed);
                normalizeCollectiblesToModel();
                cameraPos = world.startPosition();
                yaw = chooseBestSpawnYaw(cameraPos);
                pitch = 0.0f;
                updateCameraVectors();
                bullets.clear();
                explosionParticles.clear();
                destroyedCount = 0;

                out << std::format("Regenerated world with seed {} (walls={}, pillars={}, collectibles={})",
                                   seed,
                                   world.walls().size(),
                                   world.pillars().size(),
                                   world.collectibles().size());
                logEnv(std::format("command: regen_world seed={}", seed));
                return true;
            }

            if (cmd == "set_wall" || cmd == "set_floor" || cmd == "set_pillar" || cmd == "set_object" || cmd == "set_bullet") {
                if (args.size() < 2) {
                    out << std::format("Usage: {} <shader.spv|full/path/to/shader.spv>", cmd);
                    return true;
                }

                const std::string shaderPath = resolveShaderPath(args[1]);
                std::vector<char> shaderBytes;
                try {
                    shaderBytes = loadSpv(shaderPath);
                } catch (const mxvk::Exception &e) {
                    out << std::format("{}: failed to load shader '{}': {}", cmd, shaderPath, e.text());
                    return true;
                }

                if (shaderBytes.empty()) {
                    out << std::format("{}: shader '{}' is empty", cmd, shaderPath);
                    return true;
                }

                if (cmd == "set_wall") {
                    wallFragSpv = shaderPath;
                    rawWallRenderer.reloadFragShader(shaderBytes);
                    out << std::format("Wall shader reloaded from {}", shaderPath);
                } else if (cmd == "set_floor") {
                    floorFragSpv = shaderPath;
                    floorModel.setShaders(this, modelVertSpv, shaderPath);
                    out << std::format("Floor shader reloaded from {}", shaderPath);
                } else if (cmd == "set_pillar") {
                    pillarFragSpv = shaderPath;
                    rawPillarRenderer.reloadFragShader(shaderBytes);
                    out << std::format("Pillar shader reloaded from {}", shaderPath);
                } else if (cmd == "set_object") {
                    objectFragSpv = shaderPath;
                    saturnModel.setShaders(this, modelVertSpv, shaderPath);
                    birdModel.setShaders(this, modelVertSpv, shaderPath);
                    out << std::format("Object shader reloaded from {}", shaderPath);
                } else if (cmd == "set_bullet") {
                    bulletFragSpv = shaderPath;
                    bulletModel.setShaders(this, modelVertSpv, shaderPath);
                    out << std::format("Bullet shader reloaded from {}", shaderPath);
                }

                logEnv(std::format("command: {} shader={}", cmd, shaderPath));
                return true;
            }

            if (cmd == "list_shaders") {
                const std::array<std::pair<std::string_view, std::string>, 11> shaders{{
                    {"wall.frag", resolveShaderPath("wall.frag.spv")},
                    {"floor.frag", resolveShaderPath("floor.frag.spv")},
                    {"pillar.frag", resolveShaderPath("pillar.frag.spv")},
                    {"object.frag", resolveShaderPath("object.frag.spv")},
                    {"bullet.frag", resolveShaderPath("bullet.frag.spv")},
                    {"particle.frag", resolveShaderPath("particle.frag.spv")},
                    {"particle_points.frag", resolveShaderPath("particle_points.frag.spv")},
                    {"bubble.frag", resolveShaderPath("bubble.frag.spv")},
                    {"floor_kale.frag", resolveShaderPath("floor_kale.frag.spv")},
                    {"floor_swirl.frag", resolveShaderPath("floor_swirl.frag.spv")},
                    {"floor_twist.frag", resolveShaderPath("floor_twist.frag.spv")},
                }};

                out << "Available shaders:\n";
                for (const auto &[name, path] : shaders) {
                    out << std::format("  {:<20} {}\n", name, path);
                }
                out << std::format("Current bindings:\n"
                                   "  wall     {}\n"
                                   "  floor    {}\n"
                                   "  pillar   {}\n"
                                   "  object   {}\n"
                                   "  bullet   {}\n",
                                   wallFragSpv,
                                   floorFragSpv,
                                   pillarFragSpv,
                                   objectFragSpv,
                                   bulletFragSpv);
                return true;
            }

            return false;
        }

        void appendConsoleHelp(std::ostream &out) const override {
            out << "\nWalk debug commands:\n"
                << "  spawn_random [attempts]          Spawn player at random valid location\n"
                << "  spawn random [attempts]          Alias for spawn_random\n"
                << "  reset                            Reset all collectibles to active\n"
                << "  add_collectibles [count]         Add random collectibles (alias: add_collectables)\n"
                << "  status                           Print camera/world/debug state\n"
                << "  teleport <x> <y> <z>             Teleport player if destination is valid\n"
                << "  clear_bullets                    Remove all active bullets\n"
                << "  clear_fx                         Remove all active explosion particles\n"
                << "  set_fps <on|off>                 Toggle FPS overlay\n"
                << "  set_wall <shader.spv>            Reload wall fragment shader\n"
                << "  set_floor <shader.spv>           Reload floor fragment shader\n"
                << "  set_pillar <shader.spv>          Reload pillar fragment shader\n"
                << "  set_object <shader.spv>          Reload object fragment shader\n"
                << "  set_bullet <shader.spv>          Reload bullet fragment shader\n"
                << "  list_shaders                     Print available shaders and current bindings\n"
                << "  regen_world [seed]               Regenerate maze, pillars, and collectibles";
        }

        void logEnv(const std::string &message) {
            print(std::format("[walk] {}", message), {255, 100, 255, 255});
        }

        /// @brief Resolve a shader SPV name to a full path.
        ///
        /// Looks in the runtime shader directory first; if the file is not
        /// found there, the provided @p name is returned as-is so callers can pass
        /// absolute paths directly.
        [[nodiscard]] std::string resolveShaderPath(const std::string &name) const {
            const std::string runtimePath = shaderRoot + "/" + name;
            if (std::filesystem::exists(runtimePath)) {
                return runtimePath;
            }
            return name;
        }

        [[nodiscard]] static std::string toLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        [[nodiscard]] static int parseIntOrDefault(const std::string &text, const int fallback) {
            int value = fallback;
            const auto begin = text.data();
            const auto end = text.data() + text.size();
            const auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc{} || ptr != end) {
                return fallback;
            }
            return value;
        }

        [[nodiscard]] static bool tryParseFloat(const std::string &text, float &outValue) {
            try {
                size_t parsed = 0;
                const float value = std::stof(text, &parsed);
                if (parsed != text.size()) {
                    return false;
                }
                outValue = value;
                return true;
            } catch (...) {
                return false;
            }
        }

        bool sampleNavigablePoint(const float y, const float radius, glm::vec3 &outPoint, const int maxAttempts) {
            float minX = -50.0f;
            float maxX = 50.0f;
            float minZ = -50.0f;
            float maxZ = 50.0f;

            bool haveBounds = false;
            for (const WallSegment &wall : world.walls()) {
                if (!haveBounds) {
                    minX = std::min(wall.start.x, wall.end.x);
                    maxX = std::max(wall.start.x, wall.end.x);
                    minZ = std::min(wall.start.z, wall.end.z);
                    maxZ = std::max(wall.start.z, wall.end.z);
                    haveBounds = true;
                } else {
                    minX = std::min(minX, std::min(wall.start.x, wall.end.x));
                    maxX = std::max(maxX, std::max(wall.start.x, wall.end.x));
                    minZ = std::min(minZ, std::min(wall.start.z, wall.end.z));
                    maxZ = std::max(maxZ, std::max(wall.start.z, wall.end.z));
                }
            }

            if (!haveBounds) {
                outPoint = world.startPosition();
                outPoint.y = y;
                return true;
            }

            const float margin = std::max(0.8f, radius + 0.5f);
            minX += margin;
            maxX -= margin;
            minZ += margin;
            maxZ -= margin;

            if (minX > maxX || minZ > maxZ) {
                outPoint = world.startPosition();
                outPoint.y = y;
                return true;
            }

            std::uniform_real_distribution<float> distX(minX, maxX);
            std::uniform_real_distribution<float> distZ(minZ, maxZ);
            for (int i = 0; i < maxAttempts; ++i) {
                const glm::vec3 candidate{distX(rng), y, distZ(rng)};
                if (!world.checkWallCollision(candidate, radius) && !world.checkPillarCollision(candidate, radius)) {
                    outPoint = candidate;
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] static const char *collectibleTypeName(Collectible::Type type) noexcept {
            return type == Collectible::Type::Saturn ? "saturn" : "bird";
        }

        void loadModel(mxvk::VKAbstractModel &model,
                       const std::string &modelPath,
                       const std::string &textureManifest,
                       const std::string &textureBase,
                       const std::string &vertSpv,
                       const std::string &fragSpv,
                       bool backfaceCulling = false) {
            logEnv(std::format("loading model '{}'", modelPath));
            model.load(this, modelPath, textureManifest, textureBase, 1.0f);
            model.setBackfaceCulling(backfaceCulling);
            model.setShaders(this, vertSpv, fragSpv);
            logEnv(std::format("model ready '{}'", modelPath));
        }

        void cleanupModels() {
            floorModel.cleanup(this);
            rawWallRenderer.cleanup(this);
            rawPillarRenderer.cleanup(this);

            saturnModel.cleanup(this);
            birdModel.cleanup(this);
            blasterModel.cleanup(this);
            bulletModel.cleanup(this);
        }

        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties memProperties{};
            vkGetPhysicalDeviceMemoryProperties(getPhysicalDevice(), &memProperties);
            for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
                if ((typeFilter & (1u << i)) != 0u && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            throw mxvk::Exception("walk: failed to find suitable Vulkan memory type for point particles");
        }

        void initializePointParticles() {
            if (!ensureRenderResources()) {
                throw mxvk::Exception("walk: render resources unavailable for point particles");
            }

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = maxPointVertices * sizeof(ParticlePointVertex);
            bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(getDevice(), &bufferInfo, nullptr, &pointVertexBuffer) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to create point particle vertex buffer");
            }

            VkMemoryRequirements memReq{};
            vkGetBufferMemoryRequirements(getDevice(), pointVertexBuffer, &memReq);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (vkAllocateMemory(getDevice(), &allocInfo, nullptr, &pointVertexMemory) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to allocate point particle vertex memory");
            }
            if (vkBindBufferMemory(getDevice(), pointVertexBuffer, pointVertexMemory, 0) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to bind point particle vertex memory");
            }
            if (vkMapMemory(getDevice(), pointVertexMemory, 0, bufferInfo.size, 0, &pointVertexMapped) != VK_SUCCESS) {
                throw mxvk::Exception("walk: failed to map point particle vertex memory");
            }

            rebuildPointParticlePipeline();
        }

        void rebuildPointParticlePipeline() {
            if (pointPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(getDevice(), pointPipeline, nullptr);
                pointPipeline = VK_NULL_HANDLE;
            }
            if (pointPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(getDevice(), pointPipelineLayout, nullptr);
                pointPipelineLayout = VK_NULL_HANDLE;
            }

            if (getSwapchainFormat() == VK_FORMAT_UNDEFINED) {
                return;
            }

            const std::vector<char> vertBytes = loadSpv(pointParticleVertSpv);
            const std::vector<char> fragBytes = loadSpv(pointParticleFragSpv);
            const VkShaderModule vertModule = mxvk::create_shader_module(getDevice(), vertBytes);
            const VkShaderModule fragModule = mxvk::create_shader_module(getDevice(), fragBytes);

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
            binding.stride = sizeof(ParticlePointVertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 3> attrs{};
            attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ParticlePointVertex, pos)};
            attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticlePointVertex, color)};
            attrs[2] = {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(ParticlePointVertex, size)};

            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
            vertexInput.pVertexAttributeDescriptions = attrs.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

            const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_FALSE;
            depthStencil.depthWriteEnable = VK_FALSE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments = &blendAttachment;

            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(glm::mat4);

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            if (vkCreatePipelineLayout(getDevice(), &layoutInfo, nullptr, &pointPipelineLayout) != VK_SUCCESS) {
                vkDestroyShaderModule(getDevice(), fragModule, nullptr);
                vkDestroyShaderModule(getDevice(), vertModule, nullptr);
                throw mxvk::Exception("walk: failed to create point particle pipeline layout");
            }

            const VkFormat colorFormat = getSwapchainFormat();
            const VkFormat depthFormat = getDepthFormat();
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
            pipelineInfo.layout = pointPipelineLayout;
            pipelineInfo.renderPass = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pointPipeline) != VK_SUCCESS) {
                vkDestroyShaderModule(getDevice(), fragModule, nullptr);
                vkDestroyShaderModule(getDevice(), vertModule, nullptr);
                throw mxvk::Exception("walk: failed to create point particle graphics pipeline");
            }

            vkDestroyShaderModule(getDevice(), fragModule, nullptr);
            vkDestroyShaderModule(getDevice(), vertModule, nullptr);
        }

        void destroyPointParticles() {
            if (pointPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(getDevice(), pointPipeline, nullptr);
                pointPipeline = VK_NULL_HANDLE;
            }
            if (pointPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(getDevice(), pointPipelineLayout, nullptr);
                pointPipelineLayout = VK_NULL_HANDLE;
            }
            if (pointVertexMapped != nullptr) {
                vkUnmapMemory(getDevice(), pointVertexMemory);
                pointVertexMapped = nullptr;
            }
            if (pointVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(getDevice(), pointVertexBuffer, nullptr);
                pointVertexBuffer = VK_NULL_HANDLE;
            }
            if (pointVertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(getDevice(), pointVertexMemory, nullptr);
                pointVertexMemory = VK_NULL_HANDLE;
            }
        }

        void renderPointParticles(VkCommandBuffer cmd, const glm::mat4 &view, const glm::mat4 &proj) {
            if (pointPipeline == VK_NULL_HANDLE || pointPipelineLayout == VK_NULL_HANDLE || pointVertexMapped == nullptr) {
                return;
            }

            std::vector<ParticlePointVertex> vertices{};
            vertices.reserve(2048);

            for (const Projectile &bullet : bullets) {
                if (!bullet.active) {
                    continue;
                }
                for (const Projectile::TrailPoint &point : bullet.trail) {
                    const float life = glm::clamp(point.lifetime / point.maxLifetime, 0.0f, 1.0f);
                    const float fade = 1.0f - life;
                    vertices.push_back({point.position, glm::vec4(1.0f, 0.2f, 0.0f, fade * 0.8f), 12.0f});
                }
            }

            for (const ExplosionParticle &particle : explosionParticles) {
                if (!particle.active) {
                    continue;
                }
                const float life = glm::clamp(particle.lifetime / particle.maxLifetime, 0.0f, 1.0f);
                const float fade = 1.0f - life;
                const float sizePx = glm::clamp(particle.size * 320.0f, 12.0f, 160.0f);
                vertices.push_back({particle.position, glm::vec4(particle.color, fade), sizePx});
            }

            if (vertices.empty()) {
                return;
            }

            if (vertices.size() > maxPointVertices) {
                vertices.resize(maxPointVertices);
            }
            std::memcpy(pointVertexMapped, vertices.data(), vertices.size() * sizeof(ParticlePointVertex));

            const VkBuffer vb = pointVertexBuffer;
            const VkDeviceSize offset = 0;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pointPipeline);
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            const glm::mat4 vp = proj * view;
            vkCmdPushConstants(cmd, pointPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vp);
            vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
        }

        bool openGamepad(SDL_JoystickID id) {
            if (gamepad != nullptr && gamepadId == id) {
                return true;
            }
            if (gamepad != nullptr) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
                gamepadId = 0;
            }
            gamepad = SDL_OpenGamepad(id);
            if (gamepad == nullptr) {
                logEnv(std::format("failed to open gamepad id={}", static_cast<int>(id)));
                return false;
            }
            gamepadId = id;
            const char *padName = SDL_GetGamepadName(gamepad);
            logEnv(std::format("gamepad connected: id={} name='{}'",
                               static_cast<int>(id),
                               padName != nullptr ? padName : "unknown"));
            return true;
        }

        void tryOpenFirstGamepad() {
            if (gamepad != nullptr) {
                return;
            }
            int count = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&count);
            if (ids == nullptr || count <= 0) {
                if (ids != nullptr) {
                    SDL_free(ids);
                }
                return;
            }
            openGamepad(ids[0]);
            SDL_free(ids);
        }

        [[nodiscard]] static glm::mat4 composeNormalizedModel(const mxvk::VKAbstractModel &model, const glm::mat4 &world) {
            glm::mat4 transform = world;
            transform = transform * glm::scale(glm::mat4(1.0f), glm::vec3(model.modelRenderScale()));
            transform = transform * glm::translate(glm::mat4(1.0f), model.modelCenterOffset());
            return transform;
        }

        // For meshes whose final world-space dimensions are already baked into `world`
        // (walls/pillars/floor) we still want to recenter the source mesh on its
        // bounding-box center, but we must NOT compound the renderScale on top.
        [[nodiscard]] static glm::mat4 composeRecenteredModel(const mxvk::VKAbstractModel &model, const glm::mat4 &world) {
            return world * glm::translate(glm::mat4(1.0f), model.modelCenterOffset());
        }

        void renderModel(VkCommandBuffer cmd,
                         uint32_t imageIndex,
                         mxvk::VKAbstractModel &model,
                         const glm::mat4 &world,
                         const glm::mat4 &view,
                         const glm::mat4 &proj,
                         const glm::vec4 &fx,
                         bool autoNormalize = true) {
            mxvk::UniformBufferObject ubo{};
            ubo.model = autoNormalize ? composeNormalizedModel(model, world)
                                      : composeRecenteredModel(model, world);
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = fx;
            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

        void renderRawModel(VkCommandBuffer cmd,
                            uint32_t imageIndex,
                            mxvk::VKAbstractModel &model,
                            const glm::mat4 &world,
                            const glm::mat4 &view,
                            const glm::mat4 &proj,
                            const glm::vec4 &fx) {
            mxvk::UniformBufferObject ubo{};
            ubo.model = world;
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = fx;
            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

        void updateCameraVectors() {
            glm::vec3 front(0.0f);
            front.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
            front.y = std::sin(glm::radians(pitch));
            front.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
            cameraFront = glm::normalize(front);
        }

        void buildCameraBasis(glm::vec3 &forward, glm::vec3 &right, glm::vec3 &up) const {
            forward = cameraFront;
            if (glm::length(forward) <= 1e-5f) {
                forward = glm::vec3(0.0f, 0.0f, -1.0f);
            } else {
                forward = glm::normalize(forward);
            }

            right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(right) <= 1e-5f) {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                right = glm::normalize(right);
            }

            up = glm::cross(right, forward);
            if (glm::length(up) <= 1e-5f) {
                up = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                up = glm::normalize(up);
            }
        }

        [[nodiscard]] glm::vec3 blasterMuzzleTipPosition() const {
            glm::vec3 forward(0.0f);
            glm::vec3 right(0.0f);
            glm::vec3 up(0.0f);
            buildCameraBasis(forward, right, up);
            return cameraPos + (forward * 0.55f) + (right * 0.18f) - (up * 0.12f);
        }

        [[nodiscard]] glm::vec3 projectileSpawnPosition() const {
            glm::vec3 forward(0.0f);
            glm::vec3 right(0.0f);
            glm::vec3 up(0.0f);
            buildCameraBasis(forward, right, up);
            constexpr float projectileForwardOffset = 0.015f;
            return blasterMuzzleTipPosition() + (forward * projectileForwardOffset);
        }

        [[nodiscard]] glm::mat4 blasterWorldTransform() const {
            glm::vec3 forward(0.0f);
            glm::vec3 right(0.0f);
            glm::vec3 up(0.0f);
            buildCameraBasis(forward, right, up);

            constexpr float blasterScale = 0.45f;
            constexpr glm::vec3 localMuzzle(0.95f, 0.09f, 0.0f);
            const glm::vec3 desiredMuzzle = blasterMuzzleTipPosition();
            const glm::vec3 origin = desiredMuzzle - (forward * (localMuzzle.x * blasterScale)) - (up * (localMuzzle.y * blasterScale)) - (right * (localMuzzle.z * blasterScale));

            glm::mat4 world(1.0f);
            world[0] = glm::vec4(forward * blasterScale, 0.0f);
            world[1] = glm::vec4(up * blasterScale, 0.0f);
            world[2] = glm::vec4(right * blasterScale, 0.0f);
            world[3] = glm::vec4(origin, 1.0f);
            return world;
        }

        [[nodiscard]] float viewDistanceInDirection(const glm::vec3 &origin, const glm::vec3 &direction) const {
            const glm::vec3 dir = glm::normalize(glm::vec3(direction.x, 0.0f, direction.z));
            constexpr float maxDistance = 14.0f;
            constexpr float step = 0.35f;
            constexpr float probeRadius = 0.30f;
            for (float d = step; d <= maxDistance; d += step) {
                const glm::vec3 point = origin + (dir * d);
                if (world.checkWallCollision(point, probeRadius) || world.checkPillarCollision(point, probeRadius)) {
                    return d - step;
                }
            }
            return maxDistance;
        }

        [[nodiscard]] float chooseBestSpawnYaw(const glm::vec3 &origin) const {
            constexpr float pi = 3.14159265358979323846f;
            constexpr int sampleCount = 48;
            float bestDistance = -1.0f;
            float bestYaw = yaw;
            for (int i = 0; i < sampleCount; ++i) {
                const float angle = (-pi) + (2.0f * pi * static_cast<float>(i) / static_cast<float>(sampleCount));
                const glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
                const float dist = viewDistanceInDirection(origin, dir);
                if (dist > bestDistance) {
                    bestDistance = dist;
                    bestYaw = glm::degrees(angle);
                }
            }
            return bestYaw;
        }

        void updatePlayer(float deltaTime) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            glm::vec3 horizontalFront = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z));
            if (glm::length(horizontalFront) < 0.0001f) {
                horizontalFront = glm::vec3(0.0f, 0.0f, -1.0f);
            }
            const glm::vec3 right = glm::normalize(glm::cross(horizontalFront, glm::vec3(0.0f, 1.0f, 0.0f)));

            glm::vec3 desired = cameraPos;
            bool sprint = keys[SDL_SCANCODE_LSHIFT] != 0;
            if (gamepad != nullptr && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) {
                sprint = true;
            }
            const float cameraSpeed = 0.2f;
            const float speed = sprint ? cameraSpeed * 2.0f : cameraSpeed;
            const float frameScale = deltaTime * 60.0f;
            const float moveStep = speed * frameScale;

            if (keys[SDL_SCANCODE_W]) {
                desired += horizontalFront * moveStep;
            }
            if (keys[SDL_SCANCODE_S]) {
                desired -= horizontalFront * moveStep;
            }
            if (keys[SDL_SCANCODE_A]) {
                desired -= right * moveStep;
            }
            if (keys[SDL_SCANCODE_D]) {
                desired += right * moveStep;
            }

            if (gamepad != nullptr) {
                const Sint16 leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
                const Sint16 leftY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
                if (std::abs(leftX) > stickDeadZone) {
                    desired += moveStep * (static_cast<float>(leftX) / 32768.0f) * right;
                }
                if (std::abs(leftY) > stickDeadZone) {
                    desired -= moveStep * (static_cast<float>(leftY) / 32768.0f) * horizontalFront;
                }

                const Sint16 rightX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
                const Sint16 rightY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
                if (std::abs(rightX) > stickDeadZone || std::abs(rightY) > stickDeadZone) {
                    yaw += (static_cast<float>(rightX) / 32768.0f) * controllerLookSensitivity;
                    pitch -= (static_cast<float>(rightY) / 32768.0f) * controllerLookSensitivity;
                    pitch = glm::clamp(pitch, -89.0f, 89.0f);
                    updateCameraVectors();
                }
            }

            constexpr float playerRadius = 0.5f;
            constexpr float cameraStandOff = 0.18f;
            const float collisionRadius = playerRadius + cameraStandOff;
            const auto isBlocked = [this, collisionRadius](const glm::vec3 &position) {
                return world.checkWallCollision(position, collisionRadius) || world.checkPillarCollision(position, collisionRadius);
            };

            if (!isBlocked(desired)) {
                cameraPos = desired;
            } else {
                // Resolve per-axis so the player slides along obstacles instead of
                // clipping into them or fully stopping on diagonal movement.
                glm::vec3 tryX = cameraPos;
                tryX.x = desired.x;
                if (!isBlocked(tryX)) {
                    cameraPos.x = tryX.x;
                }

                glm::vec3 tryZ = cameraPos;
                tryZ.z = desired.z;
                if (!isBlocked(tryZ)) {
                    cameraPos.z = tryZ.z;
                }
            }

            const bool crouch = keys[SDL_SCANCODE_LCTRL] != 0;
            const float minHeight = crouch ? 0.8f : 1.7f;
            if (keys[SDL_SCANCODE_SPACE] && cameraPos.y <= minHeight + 0.01f) {
                jumpVelocity = 0.3f;
            }

            cameraPos.y += jumpVelocity * deltaTime * 60.0f;
            jumpVelocity -= gravity * deltaTime * 60.0f;
            if (cameraPos.y < minHeight) {
                cameraPos.y = minHeight;
                jumpVelocity = 0.0f;
            }
        }

        void fireProjectile() {
            emitMuzzleParticles();

            Projectile bullet{};
            bullet.position = projectileSpawnPosition();
            bullet.direction = glm::normalize(cameraFront);
            bullets.push_back(bullet);
            logEnv(std::format("projectile fired from ({:.2f}, {:.2f}, {:.2f}) dir=({:.2f}, {:.2f}, {:.2f}) active_bullets={}",
                               bullet.position.x,
                               bullet.position.y,
                               bullet.position.z,
                               bullet.direction.x,
                               bullet.direction.y,
                               bullet.direction.z,
                               bullets.size()));
        }

        void emitMuzzleParticles() {
            glm::vec3 forward(0.0f);
            glm::vec3 right(0.0f);
            glm::vec3 up(0.0f);
            buildCameraBasis(forward, right, up);

            const glm::vec3 muzzle = blasterMuzzleTipPosition();
            std::uniform_real_distribution<float> lateralJitter(-0.20f, 0.20f);
            std::uniform_real_distribution<float> verticalJitter(-0.12f, 0.12f);
            std::uniform_real_distribution<float> speedDist(8.0f, 26.0f);
            std::uniform_real_distribution<float> lifeDist(0.06f, 0.16f);
            std::uniform_real_distribution<float> warmDist(0.75f, 1.0f);

            constexpr int particleCount = 24;
            for (int i = 0; i < particleCount; ++i) {
                ExplosionParticle p{};
                p.position = muzzle + (forward * 0.01f);

                glm::vec3 dir = forward + (right * lateralJitter(rng)) + (up * verticalJitter(rng));
                if (glm::length(dir) <= 1e-5f) {
                    dir = forward;
                } else {
                    dir = glm::normalize(dir);
                }

                const float speed = speedDist(rng);
                p.velocity = dir * speed;
                p.color = glm::vec3(warmDist(rng), warmDist(rng) * 0.7f, warmDist(rng) * 0.18f);
                p.maxLifetime = lifeDist(rng);
                p.size = 0.035f + (speed * 0.003f);
                explosionParticles.push_back(p);
            }
        }

        void updateProjectiles(float deltaTime) {
            for (size_t bulletIndex = 0; bulletIndex < bullets.size(); ++bulletIndex) {
                Projectile &bullet = bullets[bulletIndex];
                if (!bullet.active) {
                    continue;
                }

                const glm::vec3 previous = bullet.position;
                const glm::vec3 displacement = bullet.direction * bullet.speed * deltaTime;
                bullet.position += displacement;
                bullet.lifetime += deltaTime;
                bullet.distanceTraveled += glm::length(displacement);
                bullet.trailTimer += deltaTime;
                if (bullet.trailTimer >= 0.02f) {
                    Projectile::TrailPoint point{};
                    point.position = bullet.position;
                    bullet.trail.push_back(point);
                    bullet.trailTimer = 0.0f;
                }
                for (Projectile::TrailPoint &point : bullet.trail) {
                    point.lifetime += deltaTime;
                }
                bullet.trail.erase(
                    std::remove_if(bullet.trail.begin(), bullet.trail.end(), [](const Projectile::TrailPoint &point) {
                        return point.lifetime >= point.maxLifetime;
                    }),
                    bullet.trail.end());

                size_t collectibleIndex = 0;
                glm::vec3 collectibleImpact{0.0f};
                if (lineHitCollectible(previous, bullet.position, collectibleIndex, collectibleImpact)) {
                    createExplosion(collectibleImpact, 5000, false);
                    const Collectible::Type hitType = world.collectibles()[collectibleIndex].type;
                    const bool removed = deactivateCollectibleAt(collectibleIndex);
                    resolveCollectibleClusters(2.0f, 3);
                    bullet.active = false;
                    if (removed) {
                        ++destroyedCount;
                    }
                    logEnv(std::format("bullet {} hit {} collectible {} at ({:.2f}, {:.2f}, {:.2f}); destroyed={}",
                                       bulletIndex,
                                       collectibleTypeName(hitType),
                                       collectibleIndex,
                                       collectibleImpact.x,
                                       collectibleImpact.y,
                                       collectibleImpact.z,
                                       destroyedCount));
                    continue;
                }

                const ProjectileTraceHit segmentHit = traceProjectileSegment(previous, bullet.position);
                if (segmentHit.type != ProjectileHitType::None) {

                    if (segmentHit.type == ProjectileHitType::Floor) {
                        createExplosion(glm::vec3(segmentHit.impact.x, 0.0f, segmentHit.impact.z), 1500, true);
                        bullet.active = false;
                        logEnv(std::format("bullet {} hit floor at ({:.2f}, {:.2f}, {:.2f})",
                                           bulletIndex,
                                           segmentHit.impact.x,
                                           0.0f,
                                           segmentHit.impact.z));
                        continue;
                    }

                    createExplosion(segmentHit.impact, 1500, true);
                    bullet.active = false;
                    logEnv(std::format("bullet {} hit {} at ({:.2f}, {:.2f}, {:.2f})",
                                       bulletIndex,
                                       (segmentHit.type == ProjectileHitType::Pillar) ? "pillar" : "wall",
                                       segmentHit.impact.x,
                                       segmentHit.impact.y,
                                       segmentHit.impact.z));
                    continue;
                }

                if (bullet.lifetime >= bullet.maxLifetime) {
                    bullet.active = false;
                    logEnv(std::format("bullet {} expired after {:.2f}s", bulletIndex, bullet.lifetime));
                    continue;
                }

                if (bullet.distanceTraveled >= bullet.maxDistance) {
                    bullet.active = false;
                    logEnv(std::format("bullet {} faded after traveling {:.2f} units", bulletIndex, bullet.distanceTraveled));
                }
            }

            bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Projectile &b) { return !b.active; }), bullets.end());
        }

        void updateCollectibles(float deltaTime) {
            for (Collectible &obj : world.collectibles()) {
                if (!obj.active) {
                    continue;
                }
                obj.rotation.y += obj.rotationSpeed * deltaTime;
                if (obj.rotation.y > 360.0f) {
                    obj.rotation.y -= 360.0f;
                }
            }

            collectibleClusterResolveTimer += deltaTime;
            if (collectibleClusterResolveTimer >= 0.75f) {
                collectibleClusterResolveTimer = 0.0f;
                resolveCollectibleClusters(2.0f, 2);
            }
        }

        void createExplosion(const glm::vec3 &position, int requestedCount, bool isRed) {
            if (requestedCount <= 0) {
                return;
            }

            constexpr float pi = 3.14159265358979323846f;
            std::uniform_real_distribution<float> speedDist(3.0f, 15.0f);
            std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * pi);
            std::uniform_real_distribution<float> elevationDist(-(pi / 6.0f), pi / 3.0f);
            std::uniform_real_distribution<float> colorDist(0.7f, 1.0f);

            const int count = std::min(requestedCount * 2, 800);
            logEnv(std::format("explosion at ({:.2f}, {:.2f}, {:.2f}) particles={} style={}",
                               position.x,
                               position.y,
                               position.z,
                               count,
                               isRed ? "impact" : "collectible"));
            for (int i = 0; i < count; ++i) {
                ExplosionParticle p{};
                p.position = position;
                const float theta = angleDist(rng);
                const float phi = elevationDist(rng);
                const float v = speedDist(rng);
                p.velocity = glm::vec3(v * std::cos(phi) * std::cos(theta), v * std::sin(phi), v * std::cos(phi) * std::sin(theta));
                if (isRed) {
                    p.color = glm::vec3(colorDist(rng), colorDist(rng) * 0.3f, colorDist(rng) * 0.1f);
                } else {
                    p.color = glm::vec3(colorDist(rng), colorDist(rng) * 0.7f, colorDist(rng) * 0.2f);
                }
                p.maxLifetime = 0.55f;
                p.size = 0.08f + (v * 0.010f);
                explosionParticles.push_back(p);
            }
        }

        void updateExplosions(float deltaTime) {
            for (ExplosionParticle &particle : explosionParticles) {
                if (!particle.active) {
                    continue;
                }
                particle.position += particle.velocity * deltaTime;
                particle.velocity.y -= 9.8f * deltaTime;

                for (const PillarInstance &pillar : world.pillars()) {
                    const glm::vec2 particle2d(particle.position.x, particle.position.z);
                    const glm::vec2 pillar2d(pillar.position.x, pillar.position.z);
                    const float distance = glm::length(particle2d - pillar2d);
                    if (distance < pillar.radius && particle.position.y > 0.0f && particle.position.y < pillar.height) {
                        glm::vec2 normal(1.0f, 0.0f);
                        if (distance > 0.00001f) {
                            normal = glm::normalize(particle2d - pillar2d);
                        }
                        const glm::vec2 vel2d(particle.velocity.x, particle.velocity.z);
                        const glm::vec2 reflected = vel2d - 2.0f * glm::dot(vel2d, normal) * normal;
                        particle.velocity.x = reflected.x * 0.5f;
                        particle.velocity.z = reflected.y * 0.5f;
                        const glm::vec2 correction = normal * (pillar.radius - distance + 0.1f);
                        particle.position.x += correction.x;
                        particle.position.z += correction.y;
                    }
                }

                for (const WallSegment &wall : world.walls()) {
                    glm::vec3 wallDir = wall.end - wall.start;
                    const float wallLength = glm::length(wallDir);
                    if (wallLength < 0.0001f) {
                        continue;
                    }
                    wallDir = glm::normalize(wallDir);
                    const glm::vec3 toStart = particle.position - wall.start;
                    float projection = glm::dot(toStart, wallDir);
                    projection = glm::clamp(projection, 0.0f, wallLength);
                    glm::vec3 closest = wall.start + wallDir * projection;
                    closest.y = particle.position.y;
                    const float distance = glm::length(particle.position - closest);
                    if (distance < 0.5f && particle.position.y >= 0.0f && particle.position.y <= wall.height) {
                        glm::vec3 normal(1.0f, 0.0f, 0.0f);
                        if (distance > 0.0001f) {
                            normal = glm::normalize(particle.position - closest);
                        }
                        particle.velocity = glm::reflect(particle.velocity, normal) * 0.5f;
                        particle.position += normal * 0.2f;
                    }
                }

                if (particle.position.y < 0.0f) {
                    particle.position.y = 0.0f;
                    particle.velocity.y = -particle.velocity.y * 0.3f;
                    particle.velocity.x *= 0.8f;
                    particle.velocity.z *= 0.8f;
                }

                particle.lifetime += deltaTime;
                particle.size *= 0.98f;
                if (particle.lifetime >= particle.maxLifetime) {
                    particle.active = false;
                }
            }

            explosionParticles.erase(
                std::remove_if(explosionParticles.begin(), explosionParticles.end(), [](const ExplosionParticle &p) {
                    return !p.active;
                }),
                explosionParticles.end());
        }

        [[nodiscard]] bool lineHitWall(const glm::vec3 &from, const glm::vec3 &to, glm::vec3 &impactOut) const {
            const glm::vec3 dir = to - from;
            constexpr float bulletRadius = 0.015f;
            const float travel = glm::length(dir);
            if (travel <= 1e-8f) {
                return false;
            }

            constexpr float sampleStride = 0.05f;
            const int steps = std::max(1, static_cast<int>(std::ceil(travel / sampleStride)));
            float previousT = 0.0f;
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const glm::vec3 point = from + (dir * t);
                if (pointHitsWall3D(point, bulletRadius)) {
                    float lo = previousT;
                    float hi = t;
                    for (int iter = 0; iter < 10; ++iter) {
                        const float mid = 0.5f * (lo + hi);
                        const glm::vec3 midPoint = from + (dir * mid);
                        if (pointHitsWall3D(midPoint, bulletRadius)) {
                            hi = mid;
                        } else {
                            lo = mid;
                        }
                    }
                    impactOut = from + (dir * hi);
                    return true;
                }
                previousT = t;
            }
            return false;
        }

        [[nodiscard]] bool lineHitPillar(const glm::vec3 &from, const glm::vec3 &to, glm::vec3 &impactOut) const {
            const glm::vec3 dir = to - from;
            constexpr float bulletRadius = 0.015f;
            const float travel = glm::length(dir);
            if (travel <= 1e-8f) {
                return false;
            }

            constexpr float sampleStride = 0.05f;
            const int steps = std::max(1, static_cast<int>(std::ceil(travel / sampleStride)));
            float previousT = 0.0f;
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const glm::vec3 point = from + (dir * t);
                if (pointHitsPillar3D(point, bulletRadius)) {
                    float lo = previousT;
                    float hi = t;
                    for (int iter = 0; iter < 10; ++iter) {
                        const float mid = 0.5f * (lo + hi);
                        const glm::vec3 midPoint = from + (dir * mid);
                        if (pointHitsPillar3D(midPoint, bulletRadius)) {
                            hi = mid;
                        } else {
                            lo = mid;
                        }
                    }
                    impactOut = from + (dir * hi);
                    return true;
                }
                previousT = t;
            }
            return false;
        }

        [[nodiscard]] bool lineHitCollectible(const glm::vec3 &from, const glm::vec3 &to, size_t &indexOut, glm::vec3 &impactOut) const {
            const glm::vec3 dir = to - from;
            const float dirLen2 = glm::dot(dir, dir);
            constexpr float bulletRadius = 0.015f;
            if (dirLen2 <= 1e-8f) {
                return false;
            }

            bool found = false;
            float bestT = 2.0f;
            size_t bestIndex = 0;

            const std::vector<Collectible> &collectibles = world.collectibles();
            for (size_t i = 0; i < collectibles.size(); ++i) {
                const Collectible &obj = collectibles[i];
                if (!obj.active) {
                    continue;
                }

                float tHit = 2.0f;
                bool hit = false;

                if (obj.type == Collectible::Type::Bird) {
                    const glm::vec3 halfExtents(obj.radius + bulletRadius);
                    const glm::vec3 center = obj.position + obj.hitCenterOffset;
                    const glm::vec3 boxMin = center - halfExtents;
                    const glm::vec3 boxMax = center + halfExtents;

                    float tMin = 0.0f;
                    float tMax = 1.0f;
                    bool slabMiss = false;

                    for (int axis = 0; axis < 3; ++axis) {
                        const float origin = from[axis];
                        const float delta = dir[axis];
                        const float minB = boxMin[axis];
                        const float maxB = boxMax[axis];

                        if (std::abs(delta) <= 1e-8f) {
                            if (origin < minB || origin > maxB) {
                                slabMiss = true;
                                break;
                            }
                            continue;
                        }

                        float t0 = (minB - origin) / delta;
                        float t1 = (maxB - origin) / delta;
                        if (t0 > t1) {
                            std::swap(t0, t1);
                        }

                        tMin = std::max(tMin, t0);
                        tMax = std::min(tMax, t1);
                        if (tMin > tMax) {
                            slabMiss = true;
                            break;
                        }
                    }

                    if (!slabMiss) {
                        hit = true;
                        tHit = tMin;
                    }
                } else {
                    const glm::vec3 center = obj.position + obj.hitCenterOffset;
                    const glm::vec3 m = from - center;
                    const float a = dirLen2;
                    const float b = 2.0f * glm::dot(m, dir);
                    const float hitRadius = obj.radius + bulletRadius;
                    const float c = glm::dot(m, m) - (hitRadius * hitRadius);
                    const float discriminant = (b * b) - (4.0f * a * c);
                    if (discriminant >= 0.0f) {
                        const float sqrtD = std::sqrt(discriminant);
                        const float invDen = 1.0f / (2.0f * a);
                        const float t0 = (-b - sqrtD) * invDen;
                        const float t1 = (-b + sqrtD) * invDen;
                        if (t0 >= 0.0f && t0 <= 1.0f) {
                            hit = true;
                            tHit = t0;
                        } else if (t1 >= 0.0f && t1 <= 1.0f) {
                            hit = true;
                            tHit = t1;
                        }
                    }
                }

                if (hit && tHit >= 0.0f && tHit <= 1.0f && tHit < bestT) {
                    found = true;
                    bestT = tHit;
                    bestIndex = i;
                }
            }

            if (!found) {
                return false;
            }

            indexOut = bestIndex;
            impactOut = from + (dir * bestT);
            return true;
        }

        [[nodiscard]] bool pointHitsWall3D(const glm::vec3 &point, float radius) const {
            const float halfThickness = (world.wallThickness() * 0.5f) + radius;
            const float halfThicknessSq = halfThickness * halfThickness;
            for (const WallSegment &wall : world.walls()) {
                if (point.y < 0.0f || point.y > wall.height) {
                    continue;
                }

                const glm::vec2 start(wall.start.x, wall.start.z);
                const glm::vec2 end(wall.end.x, wall.end.z);
                const glm::vec2 seg = end - start;
                const float segLen2 = glm::dot(seg, seg);
                if (segLen2 <= 1e-8f) {
                    continue;
                }

                const glm::vec2 p(point.x, point.z);
                const glm::vec2 toPoint = p - start;
                const float t = glm::clamp(glm::dot(toPoint, seg) / segLen2, 0.0f, 1.0f);
                const glm::vec2 closest = start + (seg * t);
                const glm::vec2 d = p - closest;
                if (glm::dot(d, d) <= halfThicknessSq) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool pointHitsPillar3D(const glm::vec3 &point, float radius) const {
            for (const PillarInstance &pillar : world.pillars()) {
                if (point.y < 0.0f || point.y > pillar.height) {
                    continue;
                }

                const glm::vec2 p(point.x, point.z);
                const glm::vec2 c(pillar.position.x, pillar.position.z);
                const float hitRadius = pillar.radius + radius;
                const glm::vec2 d = p - c;
                if (glm::dot(d, d) <= (hitRadius * hitRadius)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] float birdGroundYForScale(float scale) const {
            const glm::vec3 extent = birdModel.modelAxisExtent();
            const glm::vec3 centerOffset = birdModel.modelCenterOffset();
            const float modelMinY = -centerOffset.y - (extent.y * 0.5f);
            const float clampedScale = std::max(scale, 0.0001f);
            return std::max(0.0f, -modelMinY * clampedScale);
        }

        [[nodiscard]] float birdHitHalfSideForScale(float scale) const {
            const glm::vec3 extent = birdModel.modelAxisExtent();
            const float modelSide = std::max({extent.x, extent.y, extent.z, 0.0001f});
            const float clampedScale = std::max(scale, 0.0001f);
            return 0.5f * modelSide * clampedScale;
        }

        [[nodiscard]] float saturnHitRadiusForScale(float scale) const {
            const glm::vec3 extent = saturnModel.modelAxisExtent();
            const float modelDiameter = std::max({extent.x, extent.y, extent.z, 0.0001f});
            const float clampedScale = std::max(scale, 0.0001f);
            return 0.5f * modelDiameter * clampedScale;
        }

        [[nodiscard]] glm::vec3 saturnHitCenterOffsetForScale(float scale) const {
            const glm::vec3 centerOffset = saturnModel.modelCenterOffset();
            const float clampedScale = std::max(scale, 0.0001f);
            return glm::vec3(-centerOffset.x * clampedScale,
                             -centerOffset.y * clampedScale,
                             -centerOffset.z * clampedScale);
        }

        [[nodiscard]] glm::vec3 birdHitCenterOffsetForScale(float scale) const {
            const glm::vec3 centerOffset = birdModel.modelCenterOffset();
            const float clampedScale = std::max(scale, 0.0001f);
            return glm::vec3(0.0f, -centerOffset.y * clampedScale, 0.0f);
        }

        [[nodiscard]] float birdSpawnClearanceRadiusForScale(float scale) const {
            const glm::vec3 extent = birdModel.modelAxisExtent();
            const glm::vec3 centerOffset = birdModel.modelCenterOffset();
            const float clampedScale = std::max(scale, 0.0001f);

            const float halfXFromOrigin = (extent.x * 0.5f) + std::abs(centerOffset.x);
            const float halfZFromOrigin = (extent.z * 0.5f) + std::abs(centerOffset.z);
            const float horizontalRadius = std::max(halfXFromOrigin, halfZFromOrigin) * clampedScale;
            return horizontalRadius + 0.05f;
        }

        [[nodiscard]] float placementRadiusForCollectible(const Collectible &obj) const {
            if (obj.type == Collectible::Type::Bird) {
                return std::max(obj.radius, birdSpawnClearanceRadiusForScale(obj.scale.x));
            }
            return obj.radius;
        }

        void normalizeCollectiblesToModel() {
            for (Collectible &obj : world.collectibles()) {
                if (obj.type == Collectible::Type::Bird) {
                    obj.radius = birdHitHalfSideForScale(obj.scale.x);
                    obj.hitCenterOffset = birdHitCenterOffsetForScale(obj.scale.x);
                    obj.position.y = birdGroundYForScale(obj.scale.x);
                } else {
                    obj.radius = saturnHitRadiusForScale(obj.scale.x);
                    obj.hitCenterOffset = saturnHitCenterOffsetForScale(obj.scale.x);
                }
            }

            resolveCollectibleEnvironmentCollisions();
            resolveCollectibleOverlaps();
            resolveCollectibleClusters(2.0f, 5);
        }

        [[nodiscard]] bool overlapsCollectibleAt(const glm::vec3 &candidate,
                                                 float radius,
                                                 size_t ignoreIndex,
                                                 bool includeInactive) const {
            const std::vector<Collectible> &collectibles = world.collectibles();
            for (size_t i = 0; i < collectibles.size(); ++i) {
                if (i == ignoreIndex) {
                    continue;
                }
                const Collectible &other = collectibles[i];
                if (!includeInactive && !other.active) {
                    continue;
                }

                const float separation = std::max(5.0f, other.radius + radius + 0.2f);
                if (glm::length(other.position - candidate) < separation) {
                    return true;
                }
            }
            return false;
        }

        bool relocateCollectible(size_t index, float minMoveDistance, int maxAttempts) {
            std::vector<Collectible> &collectibles = world.collectibles();
            if (index >= collectibles.size()) {
                return false;
            }

            Collectible &obj = collectibles[index];
            const glm::vec3 oldPosition = obj.position;
            const float y = (obj.type == Collectible::Type::Bird) ? birdGroundYForScale(obj.scale.x) : 2.5f;
            const float placementRadius = placementRadiusForCollectible(obj);

            for (int attempt = 0; attempt < maxAttempts; ++attempt) {
                glm::vec3 candidate{};
                if (!sampleNavigablePoint(y, placementRadius, candidate, 4)) {
                    continue;
                }
                if (glm::length(candidate - oldPosition) < minMoveDistance) {
                    continue;
                }
                if (overlapsCollectibleAt(candidate, obj.radius, index, false)) {
                    continue;
                }

                obj.position = candidate;
                return true;
            }

            return false;
        }

        void resolveCollectibleEnvironmentCollisions() {
            std::vector<Collectible> &collectibles = world.collectibles();
            for (size_t i = 0; i < collectibles.size(); ++i) {
                if (!collectibles[i].active) {
                    continue;
                }

                const float placementRadius = placementRadiusForCollectible(collectibles[i]);
                if (!world.checkWallCollision(collectibles[i].position, placementRadius) &&
                    !world.checkPillarCollision(collectibles[i].position, placementRadius)) {
                    continue;
                }

                relocateCollectible(i, 2.0f, 512);
            }
        }

        void resolveCollectibleOverlaps() {
            std::vector<Collectible> &collectibles = world.collectibles();
            for (size_t i = 0; i < collectibles.size(); ++i) {
                if (!collectibles[i].active) {
                    continue;
                }
                if (!overlapsCollectibleAt(collectibles[i].position, collectibles[i].radius, i, false)) {
                    continue;
                }
                relocateCollectible(i, 1.5f, 320);
            }
        }

        void resolveCollectibleClusters(float minVisualSeparation, int passes) {
            if (minVisualSeparation <= 0.0f || passes <= 0) {
                return;
            }

            std::vector<Collectible> &collectibles = world.collectibles();
            const float minVisualSeparationSq = minVisualSeparation * minVisualSeparation;
            for (int pass = 0; pass < passes; ++pass) {
                bool movedAny = false;
                for (size_t i = 0; i < collectibles.size(); ++i) {
                    if (!collectibles[i].active) {
                        continue;
                    }

                    for (size_t j = i + 1; j < collectibles.size(); ++j) {
                        if (!collectibles[j].active) {
                            continue;
                        }

                        const glm::vec3 delta = collectibles[j].position - collectibles[i].position;
                        if (glm::dot(delta, delta) >= minVisualSeparationSq) {
                            continue;
                        }

                        if (relocateCollectible(j, minVisualSeparation, 512)) {
                            movedAny = true;
                        }
                    }
                }

                if (!movedAny) {
                    break;
                }
            }
        }

        void disperseNearbyCollectibles(const glm::vec3 &center, float radius, size_t ignoreIndex) {
            std::vector<Collectible> &collectibles = world.collectibles();
            for (size_t i = 0; i < collectibles.size(); ++i) {
                if (i == ignoreIndex) {
                    continue;
                }
                if (!collectibles[i].active) {
                    continue;
                }
                if (glm::length(collectibles[i].position - center) > radius) {
                    continue;
                }

                relocateCollectible(i, std::max(3.0f, radius), 256);
            }
        }

        [[nodiscard]] bool deactivateCollectibleAt(size_t index) {
            std::vector<Collectible> &collectibles = world.collectibles();
            if (index >= collectibles.size()) {
                return false;
            }

            Collectible &obj = collectibles[index];
            if (!obj.active) {
                return false;
            }

            obj.active = false;
            return true;
        }

        std::string assetRoot;
        std::string shaderRoot;
        std::string modelRoot;

        MazeWorld world{};
        mxvk::VKAbstractModel floorModel{};
        RawWallRenderer rawWallRenderer{};
        RawPillarRenderer rawPillarRenderer{};
        mxvk::VKAbstractModel saturnModel{};
        mxvk::VKAbstractModel birdModel{};
        mxvk::VKAbstractModel blasterModel{};
        mxvk::VKAbstractModel bulletModel{};

        VkPipelineLayout pointPipelineLayout = VK_NULL_HANDLE;
        VkPipeline pointPipeline = VK_NULL_HANDLE;
        VkBuffer pointVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory pointVertexMemory = VK_NULL_HANDLE;
        void *pointVertexMapped = nullptr;
        size_t maxPointVertices = 200000;
        std::string pointParticleVertSpv{};
        std::string pointParticleFragSpv{};
        std::string modelVertSpv{};
        std::string pillarVertSpv{};
        std::string wallFragSpv{};
        std::string floorFragSpv{};
        std::string pillarFragSpv{};
        std::string objectFragSpv{};
        std::string bulletFragSpv{};

        std::vector<Projectile> bullets{};
        std::vector<ExplosionParticle> explosionParticles{};
        std::mt19937 rng{std::random_device{}()};

        glm::vec3 cameraPos{0.0f, 1.7f, 0.0f};
        glm::vec3 cameraFront{0.0f, 0.0f, -1.0f};
        float yaw = -90.0f;
        float pitch = 0.0f;
        bool mouseCapture = true;
        bool firstMouse = true;
        bool suppressProjectileOnNextLeftDown = false;
        bool showFps = true;
        float mouseSensitivity = 0.15f;

        float jumpVelocity = 0.0f;
        float gravity = 0.015f;
        float collectibleClusterResolveTimer = 0.0f;
        uint32_t destroyedCount = 0;

        SDL_Gamepad *gamepad = nullptr;
        SDL_JoystickID gamepadId = 0;
        int stickDeadZone = 8000;
        float controllerLookSensitivity = 2.0f;

        std::chrono::steady_clock::time_point lastTick{std::chrono::steady_clock::now()};
    };

} // namespace walk

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        walk::WalkWindow window(args);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
