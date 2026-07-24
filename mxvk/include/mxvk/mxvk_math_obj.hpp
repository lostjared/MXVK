#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mxvk {

    /**
     * @brief Wavefront material data used by the software rasterizer.
     *
     * The diffuse color is used for solid triangle rendering. The remaining
     * fields and resolved diffuse texture path are retained for applications
     * that provide their own lit or textured pixel shader.
     */
    struct OBJMaterial {
        std::string name;
        std::array<float, 3> ambient{0.2f, 0.2f, 0.2f};
        std::array<float, 3> diffuse{0.8f, 0.8f, 0.8f};
        std::array<float, 3> specular{};
        float shininess = 0.0f;
        float dissolve = 1.0f;
        int illumination_model = 0;
        std::string diffuse_map;
    };

    namespace detail {

        struct OBJIndex {
            int position = 0;
            int texcoord = 0;
            int normal = 0;
        };

        struct OBJVertex {
            std::array<float, 3> position{};
            std::array<float, 2> texcoord{};
        };

        struct OBJTriangle {
            std::array<OBJVertex, 3> vertices{};
            std::string material_name;
            std::string object_name;
        };

        struct OBJLoadResult {
            std::string object_name;
            std::string material_library_path;
            std::vector<OBJMaterial> materials;
            std::vector<OBJTriangle> triangles;
        };

        inline void trim_obj_line(std::string &text) {
            const std::size_t begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                text.clear();
                return;
            }
            const std::size_t end = text.find_last_not_of(" \t\r\n");
            text = text.substr(begin, end - begin + 1);
        }

        inline void strip_obj_comment(std::string &text) {
            const std::size_t comment = text.find('#');
            if (comment != std::string::npos) {
                text.erase(comment);
            }
            trim_obj_line(text);
        }

        [[nodiscard]] inline bool parse_obj_index_value(const std::string &text, int &value) {
            if (text.empty()) {
                value = 0;
                return true;
            }

            std::size_t consumed = 0;
            try {
                value = std::stoi(text, &consumed, 10);
            } catch (const std::exception &) {
                return false;
            }
            return consumed == text.size();
        }

        [[nodiscard]] inline bool parse_obj_face_token(const std::string &token, OBJIndex &index) {
            std::array<std::string, 3> fields{};
            std::size_t field_index = 0;
            std::size_t field_begin = 0;
            while (true) {
                if (field_index >= fields.size()) {
                    return false;
                }
                const std::size_t slash = token.find('/', field_begin);
                fields[field_index++] = token.substr(field_begin, slash == std::string::npos ? std::string::npos : slash - field_begin);
                if (slash == std::string::npos) {
                    break;
                }
                field_begin = slash + 1;
            }

            return parse_obj_index_value(fields[0], index.position) &&
                   parse_obj_index_value(fields[1], index.texcoord) &&
                   parse_obj_index_value(fields[2], index.normal) &&
                   index.position != 0;
        }

        template <typename T>
        [[nodiscard]] inline int resolve_obj_index(int index, const std::vector<T> &values) {
            if (index > 0) {
                return index - 1;
            }
            if (index < 0) {
                return static_cast<int>(values.size()) + index;
            }
            return -1;
        }

        [[nodiscard]] inline std::string resolve_obj_path(const std::filesystem::path &base, const std::string &path) {
            if (path.empty()) {
                return {};
            }
            const std::filesystem::path value(path);
            if (value.is_absolute()) {
                return value.lexically_normal().string();
            }
            return (base / value).lexically_normal().string();
        }

        [[nodiscard]] inline std::string parse_mtl_texture_path(std::istream &stream) {
            std::string path;
            std::string token;
            while (stream >> token) {
                if (!token.empty() && token.front() == '-') {
                    if (token == "-blendu" || token == "-blendv" || token == "-cc" ||
                        token == "-clamp" || token == "-imfchan" || token == "-type" ||
                        token == "-bm" || token == "-boost" || token == "-texres") {
                        stream >> token;
                    } else if (token == "-mm") {
                        stream >> token >> token;
                    } else if (token == "-o" || token == "-s" || token == "-t") {
                        stream >> token >> token >> token;
                    }
                    continue;
                }
                if (!path.empty()) {
                    path += ' ';
                }
                path += token;
            }
            return path;
        }

        [[nodiscard]] inline bool load_mtl_file(const std::string &path, std::vector<OBJMaterial> &materials, std::string &error) {
            std::ifstream file(path);
            if (!file.is_open()) {
                error = "could not open material library '" + path + "'";
                return false;
            }

            std::vector<OBJMaterial> loaded_materials;
            OBJMaterial *current = nullptr;
            std::string line;
            std::size_t line_number = 0;
            while (std::getline(file, line)) {
                ++line_number;
                strip_obj_comment(line);
                if (line.empty()) {
                    continue;
                }

                std::istringstream stream(line);
                std::string tag;
                stream >> tag;
                if (tag == "newmtl") {
                    loaded_materials.emplace_back();
                    current = &loaded_materials.back();
                    std::getline(stream, current->name);
                    trim_obj_line(current->name);
                    if (current->name.empty()) {
                        error = "unnamed material at line " + std::to_string(line_number) + " in '" + path + "'";
                        return false;
                    }
                    continue;
                }
                if (current == nullptr) {
                    continue;
                }

                bool parsed = true;
                if (tag == "Ka") {
                    parsed = static_cast<bool>(stream >> current->ambient[0] >> current->ambient[1] >> current->ambient[2]);
                } else if (tag == "Kd") {
                    parsed = static_cast<bool>(stream >> current->diffuse[0] >> current->diffuse[1] >> current->diffuse[2]);
                } else if (tag == "Ks") {
                    parsed = static_cast<bool>(stream >> current->specular[0] >> current->specular[1] >> current->specular[2]);
                } else if (tag == "Ns") {
                    parsed = static_cast<bool>(stream >> current->shininess);
                } else if (tag == "d") {
                    parsed = static_cast<bool>(stream >> current->dissolve);
                } else if (tag == "Tr") {
                    float transparency = 0.0f;
                    parsed = static_cast<bool>(stream >> transparency);
                    current->dissolve = 1.0f - transparency;
                } else if (tag == "illum") {
                    parsed = static_cast<bool>(stream >> current->illumination_model);
                } else if (tag == "map_Kd") {
                    const std::string texture_path = parse_mtl_texture_path(stream);
                    if (!texture_path.empty()) {
                        current->diffuse_map = resolve_obj_path(std::filesystem::path(path).parent_path(), texture_path);
                    }
                }

                if (!parsed) {
                    error = "invalid '" + tag + "' value at line " + std::to_string(line_number) + " in '" + path + "'";
                    return false;
                }
            }

            for (OBJMaterial &material : loaded_materials) {
                for (float &component : material.ambient) {
                    component = std::clamp(component, 0.0f, 1.0f);
                }
                for (float &component : material.diffuse) {
                    component = std::clamp(component, 0.0f, 1.0f);
                }
                for (float &component : material.specular) {
                    component = std::clamp(component, 0.0f, 1.0f);
                }
                material.dissolve = std::clamp(material.dissolve, 0.0f, 1.0f);
            }

            materials = std::move(loaded_materials);
            return true;
        }

        [[nodiscard]] inline std::array<float, 3> obj_face_normal(const OBJVertex &a, const OBJVertex &b, const OBJVertex &c) {
            const float ux = b.position[0] - a.position[0];
            const float uy = b.position[1] - a.position[1];
            const float uz = b.position[2] - a.position[2];
            const float vx = c.position[0] - a.position[0];
            const float vy = c.position[1] - a.position[1];
            const float vz = c.position[2] - a.position[2];
            return {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
        }

        [[nodiscard]] inline float obj_projected_area(const std::vector<OBJVertex> &face, int drop_axis) {
            float area = 0.0f;
            for (std::size_t index = 0; index < face.size(); ++index) {
                const OBJVertex &a = face[index];
                const OBJVertex &b = face[(index + 1) % face.size()];
                area += a.position[(drop_axis + 1) % 3] * b.position[(drop_axis + 2) % 3] -
                        b.position[(drop_axis + 1) % 3] * a.position[(drop_axis + 2) % 3];
            }
            return area;
        }

        [[nodiscard]] inline float obj_edge_cross(const OBJVertex &a, const OBJVertex &b, const OBJVertex &c, int drop_axis) {
            const float ax = a.position[(drop_axis + 1) % 3];
            const float ay = a.position[(drop_axis + 2) % 3];
            const float bx = b.position[(drop_axis + 1) % 3];
            const float by = b.position[(drop_axis + 2) % 3];
            const float cx = c.position[(drop_axis + 1) % 3];
            const float cy = c.position[(drop_axis + 2) % 3];
            return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        }

        [[nodiscard]] inline bool obj_point_in_triangle(const OBJVertex &point, const OBJVertex &a, const OBJVertex &b, const OBJVertex &c, int drop_axis, float winding) {
            constexpr float OBJ_EPSILON = 1.0e-6f;
            return obj_edge_cross(a, b, point, drop_axis) * winding >= -OBJ_EPSILON &&
                   obj_edge_cross(b, c, point, drop_axis) * winding >= -OBJ_EPSILON &&
                   obj_edge_cross(c, a, point, drop_axis) * winding >= -OBJ_EPSILON;
        }

        inline void append_obj_triangle(const std::vector<OBJVertex> &face, std::size_t a, std::size_t b, std::size_t c, const std::string &material_name, const std::string &object_name, std::vector<OBJTriangle> &triangles) {
            triangles.push_back({{face[a], face[b], face[c]}, material_name, object_name});
        }

        inline void triangulate_obj_face(const std::vector<OBJVertex> &face, const std::string &material_name, const std::string &object_name, std::vector<OBJTriangle> &triangles) {
            if (face.size() < 3) {
                return;
            }
            if (face.size() == 3) {
                append_obj_triangle(face, 0, 1, 2, material_name, object_name, triangles);
                return;
            }

            const std::array<float, 3> normal = obj_face_normal(face[0], face[1], face[2]);
            int drop_axis = 0;
            if (std::fabs(normal[1]) > std::fabs(normal[0]) && std::fabs(normal[1]) >= std::fabs(normal[2])) {
                drop_axis = 1;
            } else if (std::fabs(normal[2]) > std::fabs(normal[0]) && std::fabs(normal[2]) > std::fabs(normal[1])) {
                drop_axis = 2;
            }

            const float area = obj_projected_area(face, drop_axis);
            if (std::fabs(area) <= 1.0e-6f) {
                for (std::size_t index = 2; index < face.size(); ++index) {
                    append_obj_triangle(face, 0, index - 1, index, material_name, object_name, triangles);
                }
                return;
            }
            const float winding = area >= 0.0f ? 1.0f : -1.0f;

            std::vector<std::size_t> remaining(face.size());
            for (std::size_t index = 0; index < remaining.size(); ++index) {
                remaining[index] = index;
            }
            while (remaining.size() > 3) {
                bool clipped_ear = false;
                for (std::size_t index = 0; index < remaining.size(); ++index) {
                    const std::size_t previous = remaining[(index + remaining.size() - 1) % remaining.size()];
                    const std::size_t current = remaining[index];
                    const std::size_t next = remaining[(index + 1) % remaining.size()];
                    if (obj_edge_cross(face[previous], face[current], face[next], drop_axis) * winding <= 1.0e-6f) {
                        continue;
                    }

                    bool contains_point = false;
                    for (const std::size_t test : remaining) {
                        if (test != previous && test != current && test != next &&
                            obj_point_in_triangle(face[test], face[previous], face[current], face[next], drop_axis, winding)) {
                            contains_point = true;
                            break;
                        }
                    }
                    if (contains_point) {
                        continue;
                    }

                    append_obj_triangle(face, previous, current, next, material_name, object_name, triangles);
                    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
                    clipped_ear = true;
                    break;
                }

                if (!clipped_ear) {
                    for (std::size_t index = 2; index < remaining.size(); ++index) {
                        append_obj_triangle(face, remaining[0], remaining[index - 1], remaining[index], material_name, object_name, triangles);
                    }
                    return;
                }
            }
            append_obj_triangle(face, remaining[0], remaining[1], remaining[2], material_name, object_name, triangles);
        }

        [[nodiscard]] inline bool load_obj_file(const std::string &path, OBJLoadResult &result, std::string &error) {
            std::ifstream file(path);
            if (!file.is_open()) {
                error = "could not open file";
                return false;
            }

            std::vector<std::array<float, 3>> positions;
            std::vector<std::array<float, 2>> texcoords;
            std::vector<std::array<float, 3>> normals;
            OBJLoadResult loaded;
            loaded.object_name = std::filesystem::path(path).stem().string();
            std::string current_object = loaded.object_name;
            std::string current_material;
            std::string line;
            std::size_t line_number = 0;

            while (std::getline(file, line)) {
                ++line_number;
                strip_obj_comment(line);
                if (line.empty()) {
                    continue;
                }

                std::istringstream stream(line);
                std::string tag;
                stream >> tag;
                if (tag == "v") {
                    std::array<float, 3> position{};
                    if (!(stream >> position[0] >> position[1] >> position[2]) ||
                        !std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) {
                        error = "invalid vertex at line " + std::to_string(line_number);
                        return false;
                    }
                    positions.push_back(position);
                } else if (tag == "vt") {
                    std::array<float, 2> texcoord{};
                    if (!(stream >> texcoord[0] >> texcoord[1]) ||
                        !std::isfinite(texcoord[0]) || !std::isfinite(texcoord[1])) {
                        error = "invalid texture coordinate at line " + std::to_string(line_number);
                        return false;
                    }
                    texcoords.push_back(texcoord);
                } else if (tag == "vn") {
                    std::array<float, 3> normal{};
                    if (!(stream >> normal[0] >> normal[1] >> normal[2]) ||
                        !std::isfinite(normal[0]) || !std::isfinite(normal[1]) || !std::isfinite(normal[2])) {
                        error = "invalid normal at line " + std::to_string(line_number);
                        return false;
                    }
                    normals.push_back(normal);
                } else if (tag == "o") {
                    std::string name;
                    std::getline(stream, name);
                    trim_obj_line(name);
                    if (!name.empty()) {
                        current_object = name;
                        loaded.object_name = std::move(name);
                    }
                } else if (tag == "mtllib") {
                    std::string library;
                    std::getline(stream, library);
                    trim_obj_line(library);
                    if (!library.empty() && loaded.material_library_path.empty()) {
                        loaded.material_library_path = resolve_obj_path(std::filesystem::path(path).parent_path(), library);
                    }
                } else if (tag == "usemtl") {
                    std::getline(stream, current_material);
                    trim_obj_line(current_material);
                } else if (tag == "f") {
                    std::vector<OBJVertex> face;
                    std::string token;
                    while (stream >> token) {
                        OBJIndex index;
                        if (!parse_obj_face_token(token, index)) {
                            error = "malformed face token '" + token + "' at line " + std::to_string(line_number);
                            return false;
                        }

                        const int position_index = resolve_obj_index(index.position, positions);
                        if (position_index < 0 || position_index >= static_cast<int>(positions.size())) {
                            error = "face position index out of range at line " + std::to_string(line_number);
                            return false;
                        }

                        OBJVertex vertex;
                        vertex.position = positions[static_cast<std::size_t>(position_index)];
                        if (index.texcoord != 0) {
                            const int texcoord_index = resolve_obj_index(index.texcoord, texcoords);
                            if (texcoord_index < 0 || texcoord_index >= static_cast<int>(texcoords.size())) {
                                error = "face texture index out of range at line " + std::to_string(line_number);
                                return false;
                            }
                            vertex.texcoord = texcoords[static_cast<std::size_t>(texcoord_index)];
                        }
                        if (index.normal != 0) {
                            const int normal_index = resolve_obj_index(index.normal, normals);
                            if (normal_index < 0 || normal_index >= static_cast<int>(normals.size())) {
                                error = "face normal index out of range at line " + std::to_string(line_number);
                                return false;
                            }
                        }
                        face.push_back(vertex);
                    }

                    if (face.size() < 3) {
                        error = "face has fewer than three vertices at line " + std::to_string(line_number);
                        return false;
                    }
                    triangulate_obj_face(face, current_material, current_object, loaded.triangles);
                }
            }

            if (loaded.triangles.empty()) {
                error = "no geometry found";
                return false;
            }
            if (!loaded.material_library_path.empty() &&
                !load_mtl_file(loaded.material_library_path, loaded.materials, error)) {
                return false;
            }

            result = std::move(loaded);
            return true;
        }

    } // namespace detail
} // namespace mxvk
