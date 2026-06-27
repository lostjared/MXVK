#ifndef MXVK_MXVK_MATH_H
#define MXVK_MXVK_MATH_H

#include "mxvk_sprite.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mxvk {

    inline constexpr float PI = 3.14159265358979323846f;
    inline constexpr float EPSILON = 1.0e-5f;

    using MXCOLOR = std::uint32_t;

    [[nodiscard]] inline constexpr MXCOLOR MXVK_RGB(int r, int g, int b) {
        return 0xFF000000u | ((static_cast<MXCOLOR>(r) & 0xFFu) << 16u) | ((static_cast<MXCOLOR>(g) & 0xFFu) << 8u) | (static_cast<MXCOLOR>(b) & 0xFFu);
    }

    [[nodiscard]] inline constexpr std::uint8_t color_r(MXCOLOR color) {
        return static_cast<std::uint8_t>((color >> 16u) & 0xFFu);
    }

    [[nodiscard]] inline constexpr std::uint8_t color_g(MXCOLOR color) {
        return static_cast<std::uint8_t>((color >> 8u) & 0xFFu);
    }

    [[nodiscard]] inline constexpr std::uint8_t color_b(MXCOLOR color) {
        return static_cast<std::uint8_t>(color & 0xFFu);
    }

    [[nodiscard]] inline constexpr std::uint8_t color_a(MXCOLOR color) {
        return static_cast<std::uint8_t>((color >> 24u) & 0xFFu);
    }

    [[nodiscard]] inline MXCOLOR shade_color(MXCOLOR color, float intensity) {
        intensity = std::clamp(intensity, 0.0f, 1.0f);
        const auto scale = [intensity](std::uint8_t component) {
            return static_cast<int>(std::clamp(static_cast<float>(component) * intensity, 0.0f, 255.0f));
        };
        return (static_cast<MXCOLOR>(color_a(color)) << 24u) |
               ((static_cast<MXCOLOR>(scale(color_r(color))) & 0xFFu) << 16u) |
               ((static_cast<MXCOLOR>(scale(color_g(color))) & 0xFFu) << 8u) |
               (static_cast<MXCOLOR>(scale(color_b(color))) & 0xFFu);
    }

    inline std::array<float, 361> build_sin_table() {
        std::array<float, 361> values{};
        for (int ang = 0; ang <= 360; ++ang) {
            values[static_cast<std::size_t>(ang)] = std::sin(static_cast<float>(ang) * PI / 180.0f);
        }
        return values;
    }

    inline std::array<float, 361> build_cos_table() {
        std::array<float, 361> values{};
        for (int ang = 0; ang <= 360; ++ang) {
            values[static_cast<std::size_t>(ang)] = std::cos(static_cast<float>(ang) * PI / 180.0f);
        }
        return values;
    }

    inline std::array<float, 361> sin_look = build_sin_table();
    inline std::array<float, 361> cos_look = build_cos_table();

    inline void BuildTables() {
        for (int ang = 0; ang <= 360; ++ang) {
            const float theta = static_cast<float>(ang) * PI / 180.0f;
            cos_look[static_cast<std::size_t>(ang)] = std::cos(theta);
            sin_look[static_cast<std::size_t>(ang)] = std::sin(theta);
        }
    }

    [[nodiscard]] inline float deg2rad(float ang) {
        return ang * PI / 180.0f;
    }

    [[nodiscard]] inline float rad2deg(float rad) {
        return rad * 180.0f / PI;
    }

    [[nodiscard]] inline float fast_cosf(float theta_degrees) {
        theta_degrees = std::fmod(theta_degrees, 360.0f);
        if (theta_degrees < 0.0f) {
            theta_degrees += 360.0f;
        }
        const int theta_int = static_cast<int>(theta_degrees);
        const float theta_frac = theta_degrees - static_cast<float>(theta_int);
        return cos_look[static_cast<std::size_t>(theta_int)] + theta_frac * (cos_look[static_cast<std::size_t>(theta_int + 1)] - cos_look[static_cast<std::size_t>(theta_int)]);
    }

    [[nodiscard]] inline float fast_sinf(float theta_degrees) {
        theta_degrees = std::fmod(theta_degrees, 360.0f);
        if (theta_degrees < 0.0f) {
            theta_degrees += 360.0f;
        }
        const int theta_int = static_cast<int>(theta_degrees);
        const float theta_frac = theta_degrees - static_cast<float>(theta_int);
        return sin_look[static_cast<std::size_t>(theta_int)] + theta_frac * (sin_look[static_cast<std::size_t>(theta_int + 1)] - sin_look[static_cast<std::size_t>(theta_int)]);
    }

    [[nodiscard]] inline int rrand(int x, int y) {
        if (x > y) {
            std::swap(x, y);
        }
        return x + (std::rand() % (y - x + 1));
    }

    class vec2D {
      public:
        float x = 0.0f;
        float y = 0.0f;

        constexpr vec2D() : x(0.0f), y(0.0f) {}
        constexpr vec2D(float x_value, float y_value) : x(x_value), y(y_value) {}

        void Set(float x_value, float y_value) {
            x = x_value;
            y = y_value;
        }

        vec2D &operator=(const vec2D &) = default;

        [[nodiscard]] constexpr vec2D operator+(const vec2D &v) const {
            return {x + v.x, y + v.y};
        }

        vec2D &operator+=(const vec2D &v) {
            x += v.x;
            y += v.y;
            return *this;
        }

        [[nodiscard]] constexpr vec2D operator-(const vec2D &v) const {
            return {x - v.x, y - v.y};
        }

        vec2D &operator-=(const vec2D &v) {
            x -= v.x;
            y -= v.y;
            return *this;
        }

        [[nodiscard]] constexpr vec2D operator*(float k) const {
            return {x * k, y * k};
        }

        [[nodiscard]] vec2D Scale(float k) const {
            return *this * k;
        }

        void ScaleThis(float k) {
            x *= k;
            y *= k;
        }

        [[nodiscard]] constexpr float DotProduct(const vec2D &v) const {
            return x * v.x + y * v.y;
        }

        [[nodiscard]] float Length() const {
            return std::sqrt(DotProduct(*this));
        }

        void Normalize() {
            const float length = Length();
            if (length <= EPSILON) {
                x = 0.0f;
                y = 0.0f;
                return;
            }
            ScaleThis(1.0f / length);
        }

        void Normalize(vec2D &v) const {
            v = *this;
            v.Normalize();
        }

        [[nodiscard]] float Cos(const vec2D &v) const {
            const float denom = Length() * v.Length();
            return denom <= EPSILON ? 0.0f : std::clamp(DotProduct(v) / denom, -1.0f, 1.0f);
        }

        [[nodiscard]] std::string Print(const std::string &name = "v") const {
            std::ostringstream out;
            out << name << '<' << x << ',' << y << '>';
            return out.str();
        }
    };

    inline std::ostream &operator<<(std::ostream &out, const vec2D &v) {
        return out << v.Print();
    }

    inline std::istream &operator>>(std::istream &in, vec2D &v) {
        return in >> v.x >> v.y;
    }

    class vec3D {
      public:
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr vec3D() : x(0.0f), y(0.0f), z(0.0f) {}
        constexpr vec3D(float x_value, float y_value, float z_value) : x(x_value), y(y_value), z(z_value) {}

        void Set(float x_value, float y_value, float z_value) {
            x = x_value;
            y = y_value;
            z = z_value;
        }

        vec3D &operator=(const vec3D &) = default;

        [[nodiscard]] constexpr vec3D operator+(const vec3D &v) const {
            return {x + v.x, y + v.y, z + v.z};
        }

        vec3D &operator+=(const vec3D &v) {
            x += v.x;
            y += v.y;
            z += v.z;
            return *this;
        }

        [[nodiscard]] constexpr vec3D operator-(const vec3D &v) const {
            return {x - v.x, y - v.y, z - v.z};
        }

        vec3D &operator-=(const vec3D &v) {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            return *this;
        }

        [[nodiscard]] constexpr vec3D operator*(float k) const {
            return {x * k, y * k, z * k};
        }

        [[nodiscard]] vec3D Scale(float k) const {
            return *this * k;
        }

        void ScaleThis(float k) {
            x *= k;
            y *= k;
            z *= k;
        }

        [[nodiscard]] constexpr float DotProduct(const vec3D &v) const {
            return x * v.x + y * v.y + z * v.z;
        }

        [[nodiscard]] constexpr vec3D CrossProduct(const vec3D &v) const {
            return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
        }

        [[nodiscard]] float Length() const {
            return std::sqrt(DotProduct(*this));
        }

        void Normalize() {
            const float len = Length();
            if (len <= EPSILON) {
                x = y = z = 0.0f;
                return;
            }
            ScaleThis(1.0f / len);
        }

        void Normalize(vec3D &v) const {
            v = *this;
            v.Normalize();
        }

        [[nodiscard]] float Cos(const vec3D &v) const {
            const float denom = Length() * v.Length();
            return denom <= EPSILON ? 0.0f : std::clamp(DotProduct(v) / denom, -1.0f, 1.0f);
        }

        [[nodiscard]] std::string Print(const std::string &name = "v") const {
            std::ostringstream out;
            out << name << '<' << x << ',' << y << ',' << z << '>';
            return out.str();
        }
    };

    inline std::ostream &operator<<(std::ostream &out, const vec3D &v) {
        return out << v.Print();
    }

    inline std::istream &operator>>(std::istream &in, vec3D &v) {
        return in >> v.x >> v.y >> v.z;
    }

    class vec4D {
      public:
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        constexpr vec4D() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
        constexpr vec4D(float x_value, float y_value, float z_value, float w_value = 1.0f) : x(x_value), y(y_value), z(z_value), w(w_value) {}

        void Set(float x_value, float y_value, float z_value, float w_value = 1.0f) {
            x = x_value;
            y = y_value;
            z = z_value;
            w = w_value;
        }

        void Set(const vec4D &v) {
            *this = v;
        }

        vec4D &operator=(const vec4D &) = default;

        [[nodiscard]] constexpr vec4D operator+(const vec4D &v) const {
            return {x + v.x, y + v.y, z + v.z, w + v.w};
        }

        vec4D &operator+=(const vec4D &v) {
            x += v.x;
            y += v.y;
            z += v.z;
            w += v.w;
            return *this;
        }

        [[nodiscard]] constexpr vec4D operator-(const vec4D &v) const {
            return {x - v.x, y - v.y, z - v.z, w - v.w};
        }

        vec4D &operator-=(const vec4D &v) {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            w -= v.w;
            return *this;
        }

        [[nodiscard]] constexpr vec4D operator*(float k) const {
            return {x * k, y * k, z * k, w * k};
        }

        [[nodiscard]] constexpr vec4D operator*(const vec4D &v) const {
            return {x * v.x, y * v.y, z * v.z, w * v.w};
        }

        [[nodiscard]] vec4D Scale(float k) const {
            return *this * k;
        }

        void ScaleThis(float k) {
            x *= k;
            y *= k;
            z *= k;
            w *= k;
        }

        [[nodiscard]] constexpr float DotProduct(const vec4D &v) const {
            return x * v.x + y * v.y + z * v.z;
        }

        [[nodiscard]] constexpr vec4D CrossProduct(const vec4D &v) const {
            return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x, 1.0f};
        }

        [[nodiscard]] float Length() const {
            return std::sqrt(DotProduct(*this));
        }

        void Normalize() {
            const float len = Length();
            if (len <= EPSILON) {
                x = y = z = 0.0f;
                w = 1.0f;
                return;
            }
            x /= len;
            y /= len;
            z /= len;
            w = 1.0f;
        }

        void Normalize(vec4D &v) const {
            v = *this;
            v.Normalize();
        }

        [[nodiscard]] float Cos(const vec4D &v) const {
            const float denom = Length() * v.Length();
            return denom <= EPSILON ? 0.0f : std::clamp(DotProduct(v) / denom, -1.0f, 1.0f);
        }

        void Build(const vec4D &to) {
            *this = Build(*this, to);
        }

        [[nodiscard]] vec4D Build(const vec4D &from, const vec4D &to) const {
            return {to.x - from.x, to.y - from.y, to.z - from.z, 1.0f};
        }

        [[nodiscard]] std::string Print(const std::string &name = "v") const {
            std::ostringstream out;
            out << name << '<' << x << ',' << y << ',' << z << ',' << w << '>';
            return out.str();
        }
    };

    inline std::ostream &operator<<(std::ostream &out, const vec4D &v) {
        return out << v.Print();
    }

    inline std::istream &operator>>(std::istream &in, vec4D &v) {
        return in >> v.x >> v.y >> v.z >> v.w;
    }

    class Mat1D {
      public:
        float mat[2]{};

        constexpr Mat1D() = default;
        constexpr Mat1D(float m0, float m1) : mat{m0, m1} {}

        void Set(float m0, float m1) {
            mat[0] = m0;
            mat[1] = m1;
        }
    };

    class Mat1x3D {
      public:
        float mat[3]{};

        constexpr Mat1x3D() = default;
        constexpr Mat1x3D(float m0, float m1, float m2) : mat{m0, m1, m2} {}
    };

    class Mat1x4D {
      public:
        float mat[4]{};

        constexpr Mat1x4D() = default;
        constexpr Mat1x4D(float m0, float m1, float m2, float m3) : mat{m0, m1, m2, m3} {}
    };

    class Mat4x3D {
      public:
        float mat[4][3]{};
    };

    class Mat2D {
      public:
        float mat[2][2]{};

        Mat2D() = default;
        Mat2D(float m00, float m01, float m10, float m11) {
            Set(m00, m01, m10, m11);
        }

        void Set(float m00, float m01, float m10, float m11) {
            mat[0][0] = m00;
            mat[0][1] = m01;
            mat[1][0] = m10;
            mat[1][1] = m11;
        }

        void LoadIdentity() {
            Set(1.0f, 0.0f, 0.0f, 1.0f);
        }

        [[nodiscard]] Mat2D operator+(const Mat2D &m) const {
            return {mat[0][0] + m.mat[0][0], mat[0][1] + m.mat[0][1], mat[1][0] + m.mat[1][0], mat[1][1] + m.mat[1][1]};
        }

        [[nodiscard]] Mat2D operator-(const Mat2D &m) const {
            return {mat[0][0] - m.mat[0][0], mat[0][1] - m.mat[0][1], mat[1][0] - m.mat[1][0], mat[1][1] - m.mat[1][1]};
        }

        [[nodiscard]] Mat2D operator*(const Mat2D &m) const {
            return {mat[0][0] * m.mat[0][0] + mat[0][1] * m.mat[1][0],
                    mat[0][0] * m.mat[0][1] + mat[0][1] * m.mat[1][1],
                    mat[1][0] * m.mat[0][0] + mat[1][1] * m.mat[1][0],
                    mat[1][0] * m.mat[0][1] + mat[1][1] * m.mat[1][1]};
        }

        [[nodiscard]] float Determinate() const {
            return mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0];
        }

        bool Inverse(Mat2D &out) const {
            const float d = Determinate();
            if (std::fabs(d) <= EPSILON) {
                return false;
            }
            const float inv = 1.0f / d;
            out.Set(mat[1][1] * inv, -mat[0][1] * inv, -mat[1][0] * inv, mat[0][0] * inv);
            return true;
        }

        bool Solve2x2(const Mat2D &a, Mat1D &out, const Mat1D &b) const {
            const float d = a.Determinate();
            if (std::fabs(d) <= EPSILON) {
                return false;
            }
            out.mat[0] = (b.mat[0] * a.mat[1][1] - a.mat[0][1] * b.mat[1]) / d;
            out.mat[1] = (a.mat[0][0] * b.mat[1] - b.mat[0] * a.mat[1][0]) / d;
            return true;
        }
    };

    class Mat3D {
      public:
        float mat[3][3]{};

        Mat3D() = default;
        Mat3D(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22) {
            Set(m00, m01, m02, m10, m11, m12, m20, m21, m22);
        }

        void Set(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22) {
            mat[0][0] = m00;
            mat[0][1] = m01;
            mat[0][2] = m02;
            mat[1][0] = m10;
            mat[1][1] = m11;
            mat[1][2] = m12;
            mat[2][0] = m20;
            mat[2][1] = m21;
            mat[2][2] = m22;
        }

        void LoadIdentity() {
            Set(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        }

        [[nodiscard]] Mat3D operator*(const Mat3D &m) const {
            Mat3D out;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    for (int k = 0; k < 3; ++k) {
                        out.mat[r][c] += mat[r][k] * m.mat[k][c];
                    }
                }
            }
            return out;
        }

        [[nodiscard]] vec3D MulVec(const vec3D &in) const {
            return {in.x * mat[0][0] + in.y * mat[1][0] + in.z * mat[2][0],
                    in.x * mat[0][1] + in.y * mat[1][1] + in.z * mat[2][1],
                    in.x * mat[0][2] + in.y * mat[1][2] + in.z * mat[2][2]};
        }

        void MulVec(const vec3D &in, vec3D &out) const {
            out = MulVec(in);
        }

        [[nodiscard]] float Determinate() const {
            return mat[0][0] * (mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1]) - mat[0][1] * (mat[1][0] * mat[2][2] - mat[1][2] * mat[2][0]) + mat[0][2] * (mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0]);
        }

        bool Inverse(Mat3D &out) const {
            const float d = Determinate();
            if (std::fabs(d) <= EPSILON) {
                return false;
            }
            const float inv = 1.0f / d;
            out.mat[0][0] = (mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1]) * inv;
            out.mat[0][1] = (mat[0][2] * mat[2][1] - mat[0][1] * mat[2][2]) * inv;
            out.mat[0][2] = (mat[0][1] * mat[1][2] - mat[0][2] * mat[1][1]) * inv;
            out.mat[1][0] = (mat[1][2] * mat[2][0] - mat[1][0] * mat[2][2]) * inv;
            out.mat[1][1] = (mat[0][0] * mat[2][2] - mat[0][2] * mat[2][0]) * inv;
            out.mat[1][2] = (mat[0][2] * mat[1][0] - mat[0][0] * mat[1][2]) * inv;
            out.mat[2][0] = (mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0]) * inv;
            out.mat[2][1] = (mat[0][1] * mat[2][0] - mat[0][0] * mat[2][1]) * inv;
            out.mat[2][2] = (mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0]) * inv;
            return true;
        }

        bool Solve3x3(const Mat3D &a, Mat1x3D &out, const Mat1x3D &b) const {
            Mat3D inv;
            if (!a.Inverse(inv)) {
                return false;
            }
            const vec3D r = inv.MulVec({b.mat[0], b.mat[1], b.mat[2]});
            out.mat[0] = r.x;
            out.mat[1] = r.y;
            out.mat[2] = r.z;
            return true;
        }
    };

    class Mat4D {
      public:
        float mat[4][4]{};

        Mat4D() = default;
        Mat4D(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13, float m20, float m21, float m22, float m23, float m30, float m31, float m32, float m33) {
            Set(m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33);
        }

        void Set(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13, float m20, float m21, float m22, float m23, float m30, float m31, float m32, float m33) {
            mat[0][0] = m00;
            mat[0][1] = m01;
            mat[0][2] = m02;
            mat[0][3] = m03;
            mat[1][0] = m10;
            mat[1][1] = m11;
            mat[1][2] = m12;
            mat[1][3] = m13;
            mat[2][0] = m20;
            mat[2][1] = m21;
            mat[2][2] = m22;
            mat[2][3] = m23;
            mat[3][0] = m30;
            mat[3][1] = m31;
            mat[3][2] = m32;
            mat[3][3] = m33;
        }

        void LoadIdentity() {
            Set(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        }

        [[nodiscard]] Mat4D operator+(const Mat4D &m) const {
            Mat4D out;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out.mat[r][c] = mat[r][c] + m.mat[r][c];
                }
            }
            return out;
        }

        [[nodiscard]] Mat4D operator*(const Mat4D &m) const {
            Mat4D out;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    for (int k = 0; k < 4; ++k) {
                        out.mat[r][c] += mat[r][k] * m.mat[k][c];
                    }
                }
            }
            return out;
        }

        Mat4D &operator*=(const Mat4D &m) {
            *this = *this * m;
            return *this;
        }

        [[nodiscard]] vec4D MulVec(const vec4D &in) const {
            vec4D out(0.0f, 0.0f, 0.0f, 0.0f);
            out.x = in.x * mat[0][0] + in.y * mat[1][0] + in.z * mat[2][0] + in.w * mat[3][0];
            out.y = in.x * mat[0][1] + in.y * mat[1][1] + in.z * mat[2][1] + in.w * mat[3][1];
            out.z = in.x * mat[0][2] + in.y * mat[1][2] + in.z * mat[2][2] + in.w * mat[3][2];
            out.w = in.x * mat[0][3] + in.y * mat[1][3] + in.z * mat[2][3] + in.w * mat[3][3];
            return out;
        }

        void MulVec(const vec4D &in, vec4D &out) const {
            out = MulVec(in);
        }

        [[nodiscard]] vec3D MulVec(const vec3D &in) const {
            const vec4D r = MulVec(vec4D(in.x, in.y, in.z, 1.0f));
            return {r.x, r.y, r.z};
        }

        void MulVec(const vec3D &in, vec3D &out) const {
            out = MulVec(in);
        }

        bool Inverse(Mat4D &out) const {
            float a[4][8]{};
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    a[r][c] = mat[r][c];
                }
                a[r][r + 4] = 1.0f;
            }

            for (int c = 0; c < 4; ++c) {
                int pivot = c;
                for (int r = c + 1; r < 4; ++r) {
                    if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) {
                        pivot = r;
                    }
                }
                if (std::fabs(a[pivot][c]) <= EPSILON) {
                    return false;
                }
                if (pivot != c) {
                    for (int k = 0; k < 8; ++k) {
                        std::swap(a[c][k], a[pivot][k]);
                    }
                }
                const float inv_pivot = 1.0f / a[c][c];
                for (int k = 0; k < 8; ++k) {
                    a[c][k] *= inv_pivot;
                }
                for (int r = 0; r < 4; ++r) {
                    if (r == c) {
                        continue;
                    }
                    const float factor = a[r][c];
                    for (int k = 0; k < 8; ++k) {
                        a[r][k] -= factor * a[c][k];
                    }
                }
            }

            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out.mat[r][c] = a[r][c + 4];
                }
            }
            return true;
        }

        void BuildXYZ(float theta_x, float theta_y, float theta_z) {
            const float cx = std::cos(deg2rad(theta_x));
            const float sx = std::sin(deg2rad(theta_x));
            const float cy = std::cos(deg2rad(theta_y));
            const float sy = std::sin(deg2rad(theta_y));
            const float cz = std::cos(deg2rad(theta_z));
            const float sz = std::sin(deg2rad(theta_z));

            Mat4D mx(1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1);
            Mat4D my(cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1);
            Mat4D mz(cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
            *this = mx * my * mz;
        }
    };

    struct paramLine2D {
        vec2D p0;
        vec2D p1;
        vec2D v;

        paramLine2D() = default;
        paramLine2D(const vec2D &start, const vec2D &end, const vec2D &dir) {
            Set(start, end, dir);
        }

        void Set(const vec2D &start, const vec2D &end, const vec2D &dir) {
            p0 = start;
            p1 = end;
            v = dir;
        }

        void Init(const vec2D &start, const vec2D &end) {
            p0 = start;
            p1 = end;
            v = end - start;
        }

        [[nodiscard]] vec2D ComputePoint(float t) const {
            return p0 + v * t;
        }

        vec2D ComputePoint(float t, vec2D &out) const {
            out = ComputePoint(t);
            return out;
        }

        int Intersect(const paramLine2D &line, float &t_this, float &t_other) const {
            const float det = v.x * line.v.y - v.y * line.v.x;
            if (std::fabs(det) <= EPSILON) {
                return 0;
            }
            const vec2D delta = line.p0 - p0;
            t_this = (delta.x * line.v.y - delta.y * line.v.x) / det;
            t_other = (delta.x * v.y - delta.y * v.x) / det;
            return (t_this >= 0.0f && t_this <= 1.0f && t_other >= 0.0f && t_other <= 1.0f) ? 1 : 2;
        }

        int Intersect(const paramLine2D &line, vec2D &out) const {
            float t_this = 0.0f;
            float t_other = 0.0f;
            const int result = Intersect(line, t_this, t_other);
            if (result != 0) {
                out = ComputePoint(t_this);
            }
            return result;
        }
    };

    struct paramLine3D {
        vec3D p0;
        vec3D p1;
        vec3D v;

        paramLine3D() = default;
        paramLine3D(const vec3D &start, const vec3D &end, const vec3D &dir) {
            Set(start, end, dir);
        }

        void Set(const vec3D &start, const vec3D &end, const vec3D &dir) {
            p0 = start;
            p1 = end;
            v = dir;
        }

        void Init(const vec3D &start, const vec3D &end) {
            p0 = start;
            p1 = end;
            v = end - start;
        }

        [[nodiscard]] vec3D ComputePoint(float t) const {
            return p0 + v * t;
        }

        vec3D ComputePoint(float t, vec3D &out) const {
            out = ComputePoint(t);
            return out;
        }
    };

    struct Plane3D {
        vec3D p0;
        vec3D v;

        Plane3D() = default;
        Plane3D(const vec3D &point, const vec3D &normal) : p0(point), v(normal) {}

        void Set(const vec3D &point, const vec3D &normal, bool normalize) {
            p0 = point;
            v = normal;
            if (normalize) {
                v.Normalize();
            }
        }
    };

    struct Polar {
        float r = 0.0f;
        float theta = 0.0f;
    };

    struct CyType {
        float r = 0.0f;
        float theta = 0.0f;
        float z = 0.0f;
    };

    struct SpType {
        float p = 0.0f;
        float theta = 0.0f;
        float phi = 0.0f;
    };

    class QuatType {
      public:
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        constexpr QuatType() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
        constexpr QuatType(float x_value, float y_value, float z_value, float w_value) : x(x_value), y(y_value), z(z_value), w(w_value) {}

        [[nodiscard]] constexpr QuatType operator+(const QuatType &q) const {
            return {x + q.x, y + q.y, z + q.z, w + q.w};
        }

        [[nodiscard]] constexpr QuatType operator-(const QuatType &q) const {
            return {x - q.x, y - q.y, z - q.z, w - q.w};
        }

        [[nodiscard]] constexpr QuatType operator*(const QuatType &q) const {
            return {w * q.x + x * q.w + y * q.z - z * q.y,
                    w * q.y - x * q.z + y * q.w + z * q.x,
                    w * q.z + x * q.y - y * q.x + z * q.w,
                    w * q.w - x * q.x - y * q.y - z * q.z};
        }

        QuatType &operator*=(const QuatType &q) {
            *this = *this * q;
            return *this;
        }

        void Conj() {
            x = -x;
            y = -y;
            z = -z;
        }

        void Scale(float f) {
            x *= f;
            y *= f;
            z *= f;
            w *= f;
        }

        [[nodiscard]] float Norm2() const {
            return w * w + x * x + y * y + z * z;
        }

        [[nodiscard]] float Norm() const {
            return std::sqrt(Norm2());
        }

        void Normalize() {
            const float norm = Norm();
            if (norm <= EPSILON) {
                x = y = z = 0.0f;
                w = 1.0f;
                return;
            }
            Scale(1.0f / norm);
        }

        void Inverse() {
            const float n2 = Norm2();
            if (n2 <= EPSILON) {
                x = y = z = 0.0f;
                w = 1.0f;
                return;
            }
            x = -x / n2;
            y = -y / n2;
            z = -z / n2;
            w = w / n2;
        }

        void InverseNormal() {
            Conj();
        }

        [[nodiscard]] QuatType TripleProduct(const QuatType &p1, const QuatType &p2) const {
            return (*this * p1) * p2;
        }

        void vec3DthetaQuat(float theta_degrees, const vec3D &axis) {
            vec3D n = axis;
            n.Normalize();
            const float half = deg2rad(theta_degrees) * 0.5f;
            const float s = std::sin(half);
            x = s * n.x;
            y = s * n.y;
            z = s * n.z;
            w = std::cos(half);
        }

        void vec4DthetaQuat(float theta_degrees, const vec4D &axis) {
            vec3D n(axis.x, axis.y, axis.z);
            vec3DthetaQuat(theta_degrees, n);
        }

        void EulerZYX(float theta_x, float theta_y, float theta_z) {
            const float hx = deg2rad(theta_x) * 0.5f;
            const float hy = deg2rad(theta_y) * 0.5f;
            const float hz = deg2rad(theta_z) * 0.5f;
            const float cx = std::cos(hx);
            const float sx = std::sin(hx);
            const float cy = std::cos(hy);
            const float sy = std::sin(hy);
            const float cz = std::cos(hz);
            const float sz = std::sin(hz);
            w = cz * cy * cx + sz * sy * sx;
            x = cz * cy * sx - sz * sy * cx;
            y = cz * sy * cx + sz * cy * sx;
            z = sz * cy * cx - cz * sy * sx;
        }

        void QuatToVec3D(float *theta_degrees, vec3D &axis) const {
            QuatType q = *this;
            q.Normalize();
            const float s = std::sqrt(std::max(0.0f, 1.0f - q.w * q.w));
            if (s <= EPSILON) {
                axis.Set(1.0f, 0.0f, 0.0f);
            } else {
                axis.Set(q.x / s, q.y / s, q.z / s);
            }
            if (theta_degrees != nullptr) {
                *theta_degrees = rad2deg(2.0f * std::acos(std::clamp(q.w, -1.0f, 1.0f)));
            }
        }
    };

    struct Triangle {
        vec4D vlist[3]{};
        vec4D tlist[3]{};
        MXCOLOR color = MXVK_RGB(255, 255, 255);
        int attr = 0;
        int state = 0;
        int vert[3]{};
    };

    enum {
        MX_ACTIVE = 0x1,
        MX_VISIBLE = 0x2,
        MX_BACKFACE = 0x4,
        MX_CULLED = 0x8
    };

    class RenderList {
      public:
        std::vector<Triangle> polys;
        int num_polys = 0;

        void Reset() {
            polys.clear();
            num_polys = 0;
        }

        void TransformRenderList(const Mat4D &mrot, int type) {
            if (type != 0) {
                return;
            }
            for (auto &poly : polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                for (auto &vertex : poly.vlist) {
                    vertex = mrot.MulVec(vertex);
                }
            }
        }

        void RemoveFaces(const vec4D &pos) {
            for (auto &poly : polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                const vec4D u = vec4D().Build(poly.tlist[0], poly.tlist[1]);
                const vec4D v = vec4D().Build(poly.tlist[0], poly.tlist[2]);
                const vec4D n = u.CrossProduct(v);
                const vec4D view = vec4D().Build(poly.tlist[0], pos);
                if (n.DotProduct(view) <= 0.0f) {
                    poly.state |= MX_BACKFACE;
                }
            }
        }

        void ModelToWorld(const vec4D &pos, int type) {
            if (type != 0) {
                return;
            }
            for (auto &poly : polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                for (int i = 0; i < 3; ++i) {
                    poly.tlist[i] = poly.vlist[i] + pos;
                }
            }
        }

        void BuildRenderList(const Triangle &triangle) {
            polys.push_back(triangle);
            num_polys = static_cast<int>(polys.size());
        }
    };

    class mxObject {
      public:
        int state = MX_ACTIVE;
        int attr = 0;
        float avg_rad = 0.0f;
        float max_rad = 0.0f;
        vec4D world_pos;
        vec4D dir;
        vec4D ux;
        vec4D uy;
        vec4D uz;
        int num_vertices = 0;
        int num_polys = 0;
        std::vector<vec4D> local;
        std::vector<vec4D> trans;
        std::vector<Triangle> vlist;
        std::string object_name;

        bool LoadPLG(const std::string &path, const vec4D &scale, const vec4D &obj_pos, const vec4D &) {
            std::ifstream file(path);
            if (!file.is_open()) {
                return false;
            }
            world_pos = obj_pos;
            std::string name;
            int vertex_count = 0;
            int poly_count = 0;
            if (!(file >> name >> vertex_count >> poly_count)) {
                return false;
            }
            object_name = name;
            num_vertices = vertex_count;
            num_polys = poly_count;
            local.clear();
            trans.clear();
            vlist.clear();
            local.reserve(static_cast<std::size_t>(vertex_count));
            trans.reserve(static_cast<std::size_t>(vertex_count));
            for (int i = 0; i < vertex_count; ++i) {
                vec4D vertex;
                file >> vertex.x >> vertex.y >> vertex.z;
                vertex.x *= scale.x;
                vertex.y *= scale.y;
                vertex.z *= scale.z;
                vertex.w = 1.0f;
                local.push_back(vertex);
                trans.emplace_back();
            }
            for (int i = 0; i < poly_count; ++i) {
                Triangle tri;
                int count = 0;
                file >> std::hex >> tri.state >> std::dec >> count >> tri.vert[0] >> tri.vert[1] >> tri.vert[2];
                tri.color = MXVK_RGB(rrand(0, 255), rrand(0, 255), rrand(0, 255));
                vlist.push_back(tri);
            }
            ComputeRad();
            return true;
        }

        bool LoadMX(const std::string &path, const vec4D &scale, const vec4D &obj_pos, const vec4D &rotation) {
            return LoadPLG(path, scale, obj_pos, rotation);
        }

        void ModelToWorld(int type = 0) {
            if (type == 0) {
                trans.resize(local.size());
                for (std::size_t i = 0; i < local.size(); ++i) {
                    trans[i] = local[i] + world_pos;
                }
            } else if (type == 1) {
                for (auto &vertex : trans) {
                    vertex += world_pos;
                }
            }
        }

        void TransformObject(const Mat4D &mrot, int type = 0) {
            auto transform = [&mrot](std::vector<vec4D> &vertices) {
                for (auto &vertex : vertices) {
                    vertex = mrot.MulVec(vertex);
                }
            };
            if (type == 0) {
                transform(local);
            } else if (type == 1) {
                transform(trans);
            } else if (type == 2) {
                trans.resize(local.size());
                for (std::size_t i = 0; i < local.size(); ++i) {
                    trans[i] = mrot.MulVec(local[i]);
                }
            }
        }

        void RemoveFaces(const vec4D &pos) {
            for (auto &poly : vlist) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0 || (poly.state & MX_CULLED) != 0) {
                    continue;
                }
                if (poly.vert[0] < 0 || poly.vert[1] < 0 || poly.vert[2] < 0 || static_cast<std::size_t>(poly.vert[0]) >= trans.size() || static_cast<std::size_t>(poly.vert[1]) >= trans.size() || static_cast<std::size_t>(poly.vert[2]) >= trans.size()) {
                    continue;
                }
                const vec4D u = vec4D().Build(trans[static_cast<std::size_t>(poly.vert[0])], trans[static_cast<std::size_t>(poly.vert[1])]);
                const vec4D v = vec4D().Build(trans[static_cast<std::size_t>(poly.vert[0])], trans[static_cast<std::size_t>(poly.vert[2])]);
                const vec4D n = u.CrossProduct(v);
                const vec4D view = vec4D().Build(trans[static_cast<std::size_t>(poly.vert[0])], pos);
                if (n.DotProduct(view) <= 0.0f) {
                    poly.state |= MX_BACKFACE;
                }
            }
        }

        void BuildRenderList(RenderList &list) const {
            for (const auto &poly : vlist) {
                Triangle tri = poly;
                for (int i = 0; i < 3; ++i) {
                    const auto index = static_cast<std::size_t>(poly.vert[i]);
                    if (index < local.size()) {
                        tri.vlist[i] = local[index];
                    }
                    if (index < trans.size()) {
                        tri.tlist[i] = trans[index];
                    }
                }
                list.BuildRenderList(tri);
            }
        }

        void Reset() {
            for (auto &poly : vlist) {
                poly.state = MX_ACTIVE;
            }
            state = MX_ACTIVE;
        }

        float ComputeRad() {
            max_rad = 0.0f;
            avg_rad = 0.0f;
            if (local.empty()) {
                return 0.0f;
            }
            for (const auto &vertex : local) {
                const float dist = std::sqrt(vertex.x * vertex.x + vertex.y * vertex.y + vertex.z * vertex.z);
                avg_rad += dist;
                max_rad = std::max(max_rad, dist);
            }
            avg_rad /= static_cast<float>(local.size());
            return max_rad;
        }

        void SetState(int new_state) {
            state = new_state;
        }
    };

    class Camera {
      public:
        int state = 0;
        int attr = 0;
        vec4D pos;
        vec4D dir;
        vec4D u;
        vec4D v;
        vec4D n;
        vec4D target;
        float view_dist = 1.0f;
        float fov = 90.0f;
        float near_clip_z = 1.0f;
        float far_clip_z = 1000.0f;
        Plane3D rt_clip_plane;
        Plane3D lt_clip_plane;
        Plane3D tp_clip_plane;
        Plane3D bt_clip_plane;
        float viewplane_height = 2.0f;
        float viewplane_width = 2.0f;
        float viewport_width = 1.0f;
        float viewport_height = 1.0f;
        float viewport_center_x = 0.0f;
        float viewport_center_y = 0.0f;
        float aspect_ratio = 1.0f;
        Mat4D mcam;
        Mat4D mper;
        Mat4D mscr;

        Camera() {
            mcam.LoadIdentity();
            mper.LoadIdentity();
            mscr.LoadIdentity();
        }

        void InitalizeForEuler() {
            pos.Set(100.0f, 200.0f, 300.0f);
            dir.Set(-48.0f, 0.0f, 0.0f);
            BuildEuler(5);
        }

        void Init(int camera_attr, const vec4D &camera_pos, const vec4D &camera_dir, const vec4D *camera_target, float near_z, float far_z, float fov_degrees, float width, float height) {
            attr = camera_attr;
            pos = camera_pos;
            dir = camera_dir;
            target = camera_target != nullptr ? *camera_target : vec4D();
            near_clip_z = near_z;
            far_clip_z = far_z;
            fov = fov_degrees;
            viewport_width = std::max(1.0f, width);
            viewport_height = std::max(1.0f, height);
            viewport_center_x = (viewport_width - 1.0f) * 0.5f;
            viewport_center_y = (viewport_height - 1.0f) * 0.5f;
            aspect_ratio = viewport_width / viewport_height;
            viewplane_width = 2.0f;
            viewplane_height = 2.0f / aspect_ratio;
            view_dist = (viewplane_width * 0.5f) / std::tan(deg2rad(fov * 0.5f));
            mcam.LoadIdentity();
            mper.LoadIdentity();
            mscr.LoadIdentity();
        }

        void BuildEuler(int) {
            Mat4D translation(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -pos.x, -pos.y, -pos.z, 1);
            Mat4D rotation;
            rotation.BuildXYZ(-dir.x, -dir.y, -dir.z);
            mcam = translation * rotation;
        }

        void BuildUVN(int) {
            n = vec4D().Build(pos, target);
            n.Normalize();
            v.Set(0.0f, 1.0f, 0.0f);
            u = v.CrossProduct(n);
            u.Normalize();
            v = n.CrossProduct(u);
            v.Normalize();
            Mat4D translation(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -pos.x, -pos.y, -pos.z, 1);
            Mat4D uvn(u.x, v.x, n.x, 0, u.y, v.y, n.y, 0, u.z, v.z, n.z, 0, 0, 0, 0, 1);
            mcam = translation * uvn;
        }

        void WorldToCamera(RenderList &list) const {
            for (auto &poly : list.polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                for (auto &vertex : poly.tlist) {
                    vertex = mcam.MulVec(vertex);
                }
            }
        }

        void WorldToCamera(mxObject &object) const {
            for (auto &vertex : object.trans) {
                vertex = mcam.MulVec(vertex);
            }
        }

        void CameraToPerspective(RenderList &list) const {
            for (auto &poly : list.polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                for (auto &vertex : poly.tlist) {
                    if (std::fabs(vertex.z) <= EPSILON) {
                        continue;
                    }
                    vertex.x = view_dist * vertex.x / vertex.z;
                    vertex.y = view_dist * vertex.y * aspect_ratio / vertex.z;
                }
            }
        }

        void CameraToPerspective(mxObject &object) const {
            for (auto &vertex : object.trans) {
                if (std::fabs(vertex.z) <= EPSILON) {
                    continue;
                }
                vertex.x = view_dist * vertex.x / vertex.z;
                vertex.y = view_dist * vertex.y * aspect_ratio / vertex.z;
            }
        }

        void PerspectiveToScreen(RenderList &list) const {
            const float alpha = viewport_center_x;
            const float beta = viewport_center_y;
            for (auto &poly : list.polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                for (auto &vertex : poly.tlist) {
                    vertex.x = alpha + alpha * vertex.x;
                    vertex.y = beta - beta * vertex.y;
                }
            }
        }

        void PerspectiveToScreen(mxObject &object) const {
            const float alpha = viewport_center_x;
            const float beta = viewport_center_y;
            for (auto &vertex : object.trans) {
                vertex.x = alpha + alpha * vertex.x;
                vertex.y = beta - beta * vertex.y;
            }
        }
    };

    struct SDLRendererPixelPlotter {
        SDL_Renderer *renderer = nullptr;

        void operator()(int x, int y, MXCOLOR color) const {
            if (renderer == nullptr) {
                return;
            }
            SDL_SetRenderDrawColor(renderer, color_r(color), color_g(color), color_b(color), color_a(color));
            SDL_RenderPoint(renderer, static_cast<float>(x), static_cast<float>(y));
        }
    };

    struct VKSpritePixelPlotter {
        VK_Sprite *sprite = nullptr;
        int size = 1;

        void operator()(int x, int y, MXCOLOR) const {
            if (sprite != nullptr) {
                sprite->drawSpriteRect(x, y, std::max(1, size), std::max(1, size));
            }
        }
    };

    template <typename PlotPixel>
    void draw_line(int x0, int y0, int x1, int y1, MXCOLOR color, PlotPixel &&plot_pixel) {
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        for (;;) {
            plot_pixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) {
                break;
            }
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    inline void draw_line(SDL_Renderer *renderer, int x0, int y0, int x1, int y1, MXCOLOR color) {
        draw_line(x0, y0, x1, y1, color, SDLRendererPixelPlotter{renderer});
    }

    inline void draw_line(VK_Sprite &sprite, int x0, int y0, int x1, int y1, MXCOLOR color, int pixel_size = 1) {
        draw_line(x0, y0, x1, y1, color, VKSpritePixelPlotter{&sprite, pixel_size});
    }

    [[nodiscard]] inline float edge_function(const vec2D &a, const vec2D &b, const vec2D &p) {
        return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    }

    template <typename PlotPixel>
    void draw_filled_triangle(const vec2D &p0, const vec2D &p1, const vec2D &p2, MXCOLOR color, PlotPixel &&plot_pixel) {
        const float area = edge_function(p0, p1, p2);
        if (std::fabs(area) <= EPSILON) {
            return;
        }

        const int min_x = static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x})));
        const int max_x = static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x})));
        const int min_y = static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y})));
        const int max_y = static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y})));

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const vec2D p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                const float w0 = edge_function(p1, p2, p);
                const float w1 = edge_function(p2, p0, p);
                const float w2 = edge_function(p0, p1, p);
                if ((area > 0.0f && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) ||
                    (area < 0.0f && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f)) {
                    plot_pixel(x, y, color);
                }
            }
        }
    }

    inline void draw_filled_triangle(SDL_Renderer *renderer, const vec2D &p0, const vec2D &p1, const vec2D &p2, MXCOLOR color) {
        draw_filled_triangle(p0, p1, p2, color, SDLRendererPixelPlotter{renderer});
    }

    inline void draw_filled_triangle(VK_Sprite &sprite, const vec2D &p0, const vec2D &p1, const vec2D &p2, MXCOLOR color, int pixel_size = 1) {
        draw_filled_triangle(p0, p1, p2, color, VKSpritePixelPlotter{&sprite, pixel_size});
    }

    template <typename DrawSpan>
    void draw_filled_triangle_spans(vec2D p0, vec2D p1, vec2D p2, MXCOLOR color, DrawSpan &&draw_span) {
        if (std::fabs(edge_function(p0, p1, p2)) <= EPSILON) {
            return;
        }

        const std::array<vec2D, 3> points{p0, p1, p2};
        const int min_y = static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y})));
        const int max_y = static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y})));

        for (int y = min_y; y <= max_y; ++y) {
            const float sample_y = static_cast<float>(y) + 0.5f;
            std::array<float, 3> intersections{};
            int intersection_count = 0;

            for (int edge = 0; edge < 3; ++edge) {
                const vec2D &a = points[static_cast<std::size_t>(edge)];
                const vec2D &b = points[static_cast<std::size_t>((edge + 1) % 3)];
                if (std::fabs(a.y - b.y) <= EPSILON) {
                    continue;
                }
                const float min_edge_y = std::min(a.y, b.y);
                const float max_edge_y = std::max(a.y, b.y);
                if (sample_y < min_edge_y || sample_y >= max_edge_y) {
                    continue;
                }
                const float t = (sample_y - a.y) / (b.y - a.y);
                if (intersection_count < static_cast<int>(intersections.size())) {
                    intersections[static_cast<std::size_t>(intersection_count++)] = a.x + t * (b.x - a.x);
                }
            }

            if (intersection_count < 2) {
                continue;
            }

            float min_x = intersections[0];
            float max_x = intersections[0];
            for (int i = 1; i < intersection_count; ++i) {
                min_x = std::min(min_x, intersections[static_cast<std::size_t>(i)]);
                max_x = std::max(max_x, intersections[static_cast<std::size_t>(i)]);
            }

            const int x0 = static_cast<int>(std::ceil(min_x - 0.5f));
            const int x1 = static_cast<int>(std::floor(max_x - 0.5f));
            if (x0 <= x1) {
                draw_span(x0, x1, y, color);
            }
        }
    }

    class PipeLine {
      public:
        enum LINE_CODE {
            CODE_C = 0x0000,
            CODE_N = 0x0008,
            CODE_S = 0x0004,
            CODE_E = 0x0002,
            CODE_W = 0x0001
        };

        int max_clip_x = 0;
        int max_clip_y = 0;
        int min_clip_x = 0;
        int min_clip_y = 0;
        int clip_min_x = 0;
        int clip_max_x = 0;
        int clip_min_y = 0;
        int clip_max_y = 0;

        void Begin(int width, int height, std::function<void(int, int, MXCOLOR)> plotter) {
            plot_pixel = std::move(plotter);
            plot_span = [this](int x0, int x1, int y, MXCOLOR color) {
                for (int x = x0; x <= x1; ++x) {
                    plot_pixel(x, y, color);
                }
            };
            clip_min_x = min_clip_x = 0;
            clip_min_y = min_clip_y = 0;
            clip_max_x = max_clip_x = std::max(0, width - 1);
            clip_max_y = max_clip_y = std::max(0, height - 1);
        }

        void Begin(SDL_Renderer *renderer, int width, int height) {
            Begin(width, height, [renderer](int x, int y, MXCOLOR color) { SDLRendererPixelPlotter{renderer}(x, y, color); });
            plot_span = [renderer](int x0, int x1, int y, MXCOLOR color) {
                if (renderer == nullptr) {
                    return;
                }
                SDL_SetRenderDrawColor(renderer, color_r(color), color_g(color), color_b(color), color_a(color));
                SDL_RenderLine(renderer, static_cast<float>(x0), static_cast<float>(y), static_cast<float>(x1), static_cast<float>(y));
            };
        }

        void Begin(VK_Sprite &sprite, int width, int height, int pixel_size = 1) {
            Begin(width, height, [&sprite, pixel_size](int x, int y, MXCOLOR color) { VKSpritePixelPlotter{&sprite, pixel_size}(x, y, color); });
            plot_span = [&sprite, pixel_size](int x0, int x1, int y, MXCOLOR) {
                const int size = std::max(1, pixel_size);
                sprite.drawSpriteRect(x0, y, std::max(1, x1 - x0 + 1), size);
            };
        }

        [[nodiscard]] int ComputeCode(int x, int y) const {
            int code = CODE_C;
            if (x < clip_min_x) {
                code |= CODE_W;
            } else if (x > clip_max_x) {
                code |= CODE_E;
            }
            if (y < clip_min_y) {
                code |= CODE_N;
            } else if (y > clip_max_y) {
                code |= CODE_S;
            }
            return code;
        }

        bool ClipLine(int &x0, int &y0, int &x1, int &y1) const {
            int code0 = ComputeCode(x0, y0);
            int code1 = ComputeCode(x1, y1);

            while (true) {
                if ((code0 | code1) == 0) {
                    return true;
                }
                if ((code0 & code1) != 0) {
                    return false;
                }

                const int out_code = code0 != 0 ? code0 : code1;
                int x = 0;
                int y = 0;

                if ((out_code & CODE_N) != 0) {
                    if (y1 == y0) {
                        return false;
                    }
                    x = x0 + (x1 - x0) * (clip_min_y - y0) / (y1 - y0);
                    y = clip_min_y;
                } else if ((out_code & CODE_S) != 0) {
                    if (y1 == y0) {
                        return false;
                    }
                    x = x0 + (x1 - x0) * (clip_max_y - y0) / (y1 - y0);
                    y = clip_max_y;
                } else if ((out_code & CODE_E) != 0) {
                    if (x1 == x0) {
                        return false;
                    }
                    y = y0 + (y1 - y0) * (clip_max_x - x0) / (x1 - x0);
                    x = clip_max_x;
                } else {
                    if (x1 == x0) {
                        return false;
                    }
                    y = y0 + (y1 - y0) * (clip_min_x - x0) / (x1 - x0);
                    x = clip_min_x;
                }

                if (out_code == code0) {
                    x0 = x;
                    y0 = y;
                    code0 = ComputeCode(x0, y0);
                } else {
                    x1 = x;
                    y1 = y;
                    code1 = ComputeCode(x1, y1);
                }
            }
        }

        void DrawClipedLine(int x0, int y0, int x1, int y1, MXCOLOR color) const {
            if (plot_pixel && ClipLine(x0, y0, x1, y1)) {
                draw_line(x0, y0, x1, y1, color, plot_pixel);
            }
        }

        void DrawClippedLine(int x0, int y0, int x1, int y1, MXCOLOR color) const {
            DrawClipedLine(x0, y0, x1, y1, color);
        }

        void DrawFilledTriangle(const vec2D &p0, const vec2D &p1, const vec2D &p2, MXCOLOR color) const {
            if (!plot_pixel) {
                return;
            }

            const int min_x = static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x})));
            const int max_x = static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x})));
            const int min_y = static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y})));
            const int max_y = static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y})));
            if (max_x < clip_min_x || min_x > clip_max_x || max_y < clip_min_y || min_y > clip_max_y) {
                return;
            }

            draw_filled_triangle_spans(p0, p1, p2, color, [this](int x0, int x1, int y, MXCOLOR pixel_color) {
                if (y < clip_min_y || y > clip_max_y || x1 < clip_min_x || x0 > clip_max_x) {
                    return;
                }
                x0 = std::max(x0, clip_min_x);
                x1 = std::min(x1, clip_max_x);
                if (plot_span) {
                    plot_span(x0, x1, y, pixel_color);
                }
            });
        }

        void DrawFilledTriangle(const vec4D &p0, const vec4D &p1, const vec4D &p2, MXCOLOR color) const {
            DrawFilledTriangle(vec2D(p0.x, p0.y), vec2D(p1.x, p1.y), vec2D(p2.x, p2.y), color);
        }

        void DrawSolidPolys(const RenderList &list) const {
            for (const auto &poly : list.polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                DrawFilledTriangle(poly.tlist[0], poly.tlist[1], poly.tlist[2], poly.color);
            }
        }

        void DrawPolys(const RenderList &list) const {
            for (const auto &poly : list.polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                DrawClipedLine(static_cast<int>(poly.tlist[0].x), static_cast<int>(poly.tlist[0].y), static_cast<int>(poly.tlist[1].x), static_cast<int>(poly.tlist[1].y), poly.color);
                DrawClipedLine(static_cast<int>(poly.tlist[1].x), static_cast<int>(poly.tlist[1].y), static_cast<int>(poly.tlist[2].x), static_cast<int>(poly.tlist[2].y), poly.color);
                DrawClipedLine(static_cast<int>(poly.tlist[2].x), static_cast<int>(poly.tlist[2].y), static_cast<int>(poly.tlist[0].x), static_cast<int>(poly.tlist[0].y), poly.color);
            }
        }

        void DrawObject(const mxObject &object) const {
            if ((object.state & MX_CULLED) != 0) {
                return;
            }
            for (const auto &poly : object.vlist) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                const auto a = static_cast<std::size_t>(poly.vert[0]);
                const auto b = static_cast<std::size_t>(poly.vert[1]);
                const auto c = static_cast<std::size_t>(poly.vert[2]);
                if (a >= object.trans.size() || b >= object.trans.size() || c >= object.trans.size()) {
                    continue;
                }
                DrawClipedLine(static_cast<int>(object.trans[a].x), static_cast<int>(object.trans[a].y), static_cast<int>(object.trans[b].x), static_cast<int>(object.trans[b].y), poly.color);
                DrawClipedLine(static_cast<int>(object.trans[b].x), static_cast<int>(object.trans[b].y), static_cast<int>(object.trans[c].x), static_cast<int>(object.trans[c].y), poly.color);
                DrawClipedLine(static_cast<int>(object.trans[c].x), static_cast<int>(object.trans[c].y), static_cast<int>(object.trans[a].x), static_cast<int>(object.trans[a].y), poly.color);
            }
        }

        void End() {
            plot_pixel = nullptr;
            plot_span = nullptr;
        }

      private:
        std::function<void(int, int, MXCOLOR)> plot_pixel;
        std::function<void(int, int, int, MXCOLOR)> plot_span;
    };

} // namespace mxvk

#endif
