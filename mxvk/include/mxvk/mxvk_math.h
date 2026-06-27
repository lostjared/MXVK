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

/**
 * @file mxvk_math.h
 * @brief Math, geometry, rasterization, and simple software 3D pipeline helpers for MXVK examples.
 */

namespace mxvk {

    /// Mathematical constant pi as a single-precision value.
    inline constexpr float PI = 3.14159265358979323846f;

    /// Default tolerance used for floating-point singularity and zero-length checks.
    inline constexpr float EPSILON = 1.0e-5f;

    /// Packed 32-bit color in ARGB byte order.
    using MXCOLOR = std::uint32_t;

    /**
     * @brief Build an opaque ARGB color from red, green, and blue components.
     * @param r Red component in the low 8 bits.
     * @param g Green component in the low 8 bits.
     * @param b Blue component in the low 8 bits.
     * @return Packed color with alpha set to 255.
     */
    [[nodiscard]] inline constexpr MXCOLOR MXVK_RGB(int r, int g, int b) {
        return 0xFF000000u | ((static_cast<MXCOLOR>(r) & 0xFFu) << 16u) | ((static_cast<MXCOLOR>(g) & 0xFFu) << 8u) | (static_cast<MXCOLOR>(b) & 0xFFu);
    }

    /// Extract the red component from a packed ARGB color.
    [[nodiscard]] inline constexpr std::uint8_t color_r(MXCOLOR color) {
        return static_cast<std::uint8_t>((color >> 16u) & 0xFFu);
    }

    /// Extract the green component from a packed ARGB color.
    [[nodiscard]] inline constexpr std::uint8_t color_g(MXCOLOR color) {
        return static_cast<std::uint8_t>((color >> 8u) & 0xFFu);
    }

    /// Extract the blue component from a packed ARGB color.
    [[nodiscard]] inline constexpr std::uint8_t color_b(MXCOLOR color) {
        return static_cast<std::uint8_t>(color & 0xFFu);
    }

    /// Extract the alpha component from a packed ARGB color.
    [[nodiscard]] inline constexpr std::uint8_t color_a(MXCOLOR color) {
        return static_cast<std::uint8_t>((color >> 24u) & 0xFFu);
    }

    /**
     * @brief Scale the RGB channels of a color while preserving alpha.
     * @param color Packed ARGB color to shade.
     * @param intensity Multiplier clamped to the range [0, 1].
     * @return Shaded packed ARGB color.
     */
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

    /// Sine lookup table with one entry per degree from 0 through 360.
    inline std::array<float, 361> sin_look = build_sin_table();

    /// Cosine lookup table with one entry per degree from 0 through 360.
    inline std::array<float, 361> cos_look = build_cos_table();

    /// Rebuild the sine and cosine lookup tables.
    inline void BuildTables() {
        for (int ang = 0; ang <= 360; ++ang) {
            const float theta = static_cast<float>(ang) * PI / 180.0f;
            cos_look[static_cast<std::size_t>(ang)] = std::cos(theta);
            sin_look[static_cast<std::size_t>(ang)] = std::sin(theta);
        }
    }

    /// Convert degrees to radians.
    [[nodiscard]] inline float deg2rad(float ang) {
        return ang * PI / 180.0f;
    }

    /// Convert radians to degrees.
    [[nodiscard]] inline float rad2deg(float rad) {
        return rad * 180.0f / PI;
    }

    /**
     * @brief Approximate cosine using the degree lookup table with linear interpolation.
     * @param theta_degrees Angle in degrees.
     * @return Approximate cosine of the angle.
     */
    [[nodiscard]] inline float fast_cosf(float theta_degrees) {
        theta_degrees = std::fmod(theta_degrees, 360.0f);
        if (theta_degrees < 0.0f) {
            theta_degrees += 360.0f;
        }
        const int theta_int = static_cast<int>(theta_degrees);
        const float theta_frac = theta_degrees - static_cast<float>(theta_int);
        return cos_look[static_cast<std::size_t>(theta_int)] + theta_frac * (cos_look[static_cast<std::size_t>(theta_int + 1)] - cos_look[static_cast<std::size_t>(theta_int)]);
    }

    /**
     * @brief Approximate sine using the degree lookup table with linear interpolation.
     * @param theta_degrees Angle in degrees.
     * @return Approximate sine of the angle.
     */
    [[nodiscard]] inline float fast_sinf(float theta_degrees) {
        theta_degrees = std::fmod(theta_degrees, 360.0f);
        if (theta_degrees < 0.0f) {
            theta_degrees += 360.0f;
        }
        const int theta_int = static_cast<int>(theta_degrees);
        const float theta_frac = theta_degrees - static_cast<float>(theta_int);
        return sin_look[static_cast<std::size_t>(theta_int)] + theta_frac * (sin_look[static_cast<std::size_t>(theta_int + 1)] - sin_look[static_cast<std::size_t>(theta_int)]);
    }

    /**
     * @brief Return a pseudo-random integer in the inclusive range between two bounds.
     * @param x First bound.
     * @param y Second bound.
     * @return Random integer in [min(x, y), max(x, y)].
     */
    [[nodiscard]] inline int rrand(int x, int y) {
        if (x > y) {
            std::swap(x, y);
        }
        return x + (std::rand() % (y - x + 1));
    }

    /// Two-dimensional float vector with common arithmetic helpers.
    class vec2D {
      public:
        /// X coordinate.
        float x = 0.0f;

        /// Y coordinate.
        float y = 0.0f;

        /// Construct the zero vector.
        constexpr vec2D() : x(0.0f), y(0.0f) {}

        /// Construct a vector from explicit coordinates.
        constexpr vec2D(float x_value, float y_value) : x(x_value), y(y_value) {}

        /// Set both vector coordinates.
        void Set(float x_value, float y_value) {
            x = x_value;
            y = y_value;
        }

        vec2D &operator=(const vec2D &) = default;

        /// Add two vectors component-wise.
        [[nodiscard]] constexpr vec2D operator+(const vec2D &v) const {
            return {x + v.x, y + v.y};
        }

        /// Add another vector to this vector.
        vec2D &operator+=(const vec2D &v) {
            x += v.x;
            y += v.y;
            return *this;
        }

        /// Subtract two vectors component-wise.
        [[nodiscard]] constexpr vec2D operator-(const vec2D &v) const {
            return {x - v.x, y - v.y};
        }

        /// Subtract another vector from this vector.
        vec2D &operator-=(const vec2D &v) {
            x -= v.x;
            y -= v.y;
            return *this;
        }

        /// Scale this vector by a scalar.
        [[nodiscard]] constexpr vec2D operator*(float k) const {
            return {x * k, y * k};
        }

        /// Return a scaled copy of this vector.
        [[nodiscard]] vec2D Scale(float k) const {
            return *this * k;
        }

        /// Scale this vector in place.
        void ScaleThis(float k) {
            x *= k;
            y *= k;
        }

        /// Compute the dot product with another vector.
        [[nodiscard]] constexpr float DotProduct(const vec2D &v) const {
            return x * v.x + y * v.y;
        }

        /// Compute the Euclidean length of this vector.
        [[nodiscard]] float Length() const {
            return std::sqrt(DotProduct(*this));
        }

        /// Normalize this vector in place, or reset it to zero if it is too short.
        void Normalize() {
            const float length = Length();
            if (length <= EPSILON) {
                x = 0.0f;
                y = 0.0f;
                return;
            }
            ScaleThis(1.0f / length);
        }

        /// Write a normalized copy of this vector to @p v.
        void Normalize(vec2D &v) const {
            v = *this;
            v.Normalize();
        }

        /// Compute the cosine of the angle between this vector and another vector.
        [[nodiscard]] float Cos(const vec2D &v) const {
            const float denom = Length() * v.Length();
            return denom <= EPSILON ? 0.0f : std::clamp(DotProduct(v) / denom, -1.0f, 1.0f);
        }

        /// Format this vector as a named angle-bracket tuple.
        [[nodiscard]] std::string Print(const std::string &name = "v") const {
            std::ostringstream out;
            out << name << '<' << x << ',' << y << '>';
            return out.str();
        }
    };

    /// Write a 2D vector to a stream using vec2D::Print().
    inline std::ostream &operator<<(std::ostream &out, const vec2D &v) {
        return out << v.Print();
    }

    /// Read a 2D vector from a stream as two scalar coordinates.
    inline std::istream &operator>>(std::istream &in, vec2D &v) {
        return in >> v.x >> v.y;
    }

    /// Three-dimensional float vector with arithmetic, dot, and cross-product helpers.
    class vec3D {
      public:
        /// X coordinate.
        float x = 0.0f;

        /// Y coordinate.
        float y = 0.0f;

        /// Z coordinate.
        float z = 0.0f;

        /// Construct the zero vector.
        constexpr vec3D() : x(0.0f), y(0.0f), z(0.0f) {}

        /// Construct a vector from explicit coordinates.
        constexpr vec3D(float x_value, float y_value, float z_value) : x(x_value), y(y_value), z(z_value) {}

        /// Set all vector coordinates.
        void Set(float x_value, float y_value, float z_value) {
            x = x_value;
            y = y_value;
            z = z_value;
        }

        vec3D &operator=(const vec3D &) = default;

        /// Add two vectors component-wise.
        [[nodiscard]] constexpr vec3D operator+(const vec3D &v) const {
            return {x + v.x, y + v.y, z + v.z};
        }

        /// Add another vector to this vector.
        vec3D &operator+=(const vec3D &v) {
            x += v.x;
            y += v.y;
            z += v.z;
            return *this;
        }

        /// Subtract two vectors component-wise.
        [[nodiscard]] constexpr vec3D operator-(const vec3D &v) const {
            return {x - v.x, y - v.y, z - v.z};
        }

        /// Subtract another vector from this vector.
        vec3D &operator-=(const vec3D &v) {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            return *this;
        }

        /// Scale this vector by a scalar.
        [[nodiscard]] constexpr vec3D operator*(float k) const {
            return {x * k, y * k, z * k};
        }

        /// Return a scaled copy of this vector.
        [[nodiscard]] vec3D Scale(float k) const {
            return *this * k;
        }

        /// Scale this vector in place.
        void ScaleThis(float k) {
            x *= k;
            y *= k;
            z *= k;
        }

        /// Compute the dot product with another vector.
        [[nodiscard]] constexpr float DotProduct(const vec3D &v) const {
            return x * v.x + y * v.y + z * v.z;
        }

        /// Compute the right-handed cross product with another vector.
        [[nodiscard]] constexpr vec3D CrossProduct(const vec3D &v) const {
            return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x};
        }

        /// Compute the Euclidean length of this vector.
        [[nodiscard]] float Length() const {
            return std::sqrt(DotProduct(*this));
        }

        /// Normalize this vector in place, or reset it to zero if it is too short.
        void Normalize() {
            const float len = Length();
            if (len <= EPSILON) {
                x = y = z = 0.0f;
                return;
            }
            ScaleThis(1.0f / len);
        }

        /// Write a normalized copy of this vector to @p v.
        void Normalize(vec3D &v) const {
            v = *this;
            v.Normalize();
        }

        /// Compute the cosine of the angle between this vector and another vector.
        [[nodiscard]] float Cos(const vec3D &v) const {
            const float denom = Length() * v.Length();
            return denom <= EPSILON ? 0.0f : std::clamp(DotProduct(v) / denom, -1.0f, 1.0f);
        }

        /// Format this vector as a named angle-bracket tuple.
        [[nodiscard]] std::string Print(const std::string &name = "v") const {
            std::ostringstream out;
            out << name << '<' << x << ',' << y << ',' << z << '>';
            return out.str();
        }
    };

    /// Write a 3D vector to a stream using vec3D::Print().
    inline std::ostream &operator<<(std::ostream &out, const vec3D &v) {
        return out << v.Print();
    }

    /// Read a 3D vector from a stream as three scalar coordinates.
    inline std::istream &operator>>(std::istream &in, vec3D &v) {
        return in >> v.x >> v.y >> v.z;
    }

    /// Four-dimensional float vector used for homogeneous 3D coordinates.
    class vec4D {
      public:
        /// X coordinate.
        float x = 0.0f;

        /// Y coordinate.
        float y = 0.0f;

        /// Z coordinate.
        float z = 0.0f;

        /// Homogeneous W coordinate.
        float w = 1.0f;

        /// Construct the homogeneous origin.
        constexpr vec4D() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

        /// Construct a homogeneous vector from explicit coordinates.
        constexpr vec4D(float x_value, float y_value, float z_value, float w_value = 1.0f) : x(x_value), y(y_value), z(z_value), w(w_value) {}

        /// Set all vector coordinates.
        void Set(float x_value, float y_value, float z_value, float w_value = 1.0f) {
            x = x_value;
            y = y_value;
            z = z_value;
            w = w_value;
        }

        /// Copy coordinates from another vector.
        void Set(const vec4D &v) {
            *this = v;
        }

        vec4D &operator=(const vec4D &) = default;

        /// Add two vectors component-wise.
        [[nodiscard]] constexpr vec4D operator+(const vec4D &v) const {
            return {x + v.x, y + v.y, z + v.z, w + v.w};
        }

        /// Add another vector to this vector.
        vec4D &operator+=(const vec4D &v) {
            x += v.x;
            y += v.y;
            z += v.z;
            w += v.w;
            return *this;
        }

        /// Subtract two vectors component-wise.
        [[nodiscard]] constexpr vec4D operator-(const vec4D &v) const {
            return {x - v.x, y - v.y, z - v.z, w - v.w};
        }

        /// Subtract another vector from this vector.
        vec4D &operator-=(const vec4D &v) {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            w -= v.w;
            return *this;
        }

        /// Scale this vector by a scalar.
        [[nodiscard]] constexpr vec4D operator*(float k) const {
            return {x * k, y * k, z * k, w * k};
        }

        /// Multiply two vectors component-wise.
        [[nodiscard]] constexpr vec4D operator*(const vec4D &v) const {
            return {x * v.x, y * v.y, z * v.z, w * v.w};
        }

        /// Return a scaled copy of this vector.
        [[nodiscard]] vec4D Scale(float k) const {
            return *this * k;
        }

        /// Scale this vector in place.
        void ScaleThis(float k) {
            x *= k;
            y *= k;
            z *= k;
            w *= k;
        }

        /// Compute the 3D dot product, ignoring the W component.
        [[nodiscard]] constexpr float DotProduct(const vec4D &v) const {
            return x * v.x + y * v.y + z * v.z;
        }

        /// Compute the 3D cross product and return it with W set to 1.
        [[nodiscard]] constexpr vec4D CrossProduct(const vec4D &v) const {
            return {y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x, 1.0f};
        }

        /// Compute the 3D Euclidean length, ignoring the W component.
        [[nodiscard]] float Length() const {
            return std::sqrt(DotProduct(*this));
        }

        /// Normalize the 3D components in place and reset W to 1.
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

        /// Write a normalized copy of this vector to @p v.
        void Normalize(vec4D &v) const {
            v = *this;
            v.Normalize();
        }

        /// Compute the cosine of the angle between the 3D components of two vectors.
        [[nodiscard]] float Cos(const vec4D &v) const {
            const float denom = Length() * v.Length();
            return denom <= EPSILON ? 0.0f : std::clamp(DotProduct(v) / denom, -1.0f, 1.0f);
        }

        /// Replace this vector with the direction from this point to @p to.
        void Build(const vec4D &to) {
            *this = Build(*this, to);
        }

        /// Build a direction vector from @p from to @p to with W set to 1.
        [[nodiscard]] vec4D Build(const vec4D &from, const vec4D &to) const {
            return {to.x - from.x, to.y - from.y, to.z - from.z, 1.0f};
        }

        /// Format this vector as a named angle-bracket tuple.
        [[nodiscard]] std::string Print(const std::string &name = "v") const {
            std::ostringstream out;
            out << name << '<' << x << ',' << y << ',' << z << ',' << w << '>';
            return out.str();
        }
    };

    /// Write a 4D vector to a stream using vec4D::Print().
    inline std::ostream &operator<<(std::ostream &out, const vec4D &v) {
        return out << v.Print();
    }

    /// Read a 4D vector from a stream as four scalar coordinates.
    inline std::istream &operator>>(std::istream &in, vec4D &v) {
        return in >> v.x >> v.y >> v.z >> v.w;
    }

    /// Two-element column-vector storage used by 2x2 linear solves.
    class Mat1D {
      public:
        /// Matrix/vector elements.
        float mat[2]{};

        /// Construct a zero-initialized 2-element vector.
        constexpr Mat1D() = default;

        /// Construct from explicit elements.
        constexpr Mat1D(float m0, float m1) : mat{m0, m1} {}

        /// Set both elements.
        void Set(float m0, float m1) {
            mat[0] = m0;
            mat[1] = m1;
        }
    };

    /// Three-element column-vector storage used by 3x3 linear solves.
    class Mat1x3D {
      public:
        /// Matrix/vector elements.
        float mat[3]{};

        /// Construct a zero-initialized 3-element vector.
        constexpr Mat1x3D() = default;

        /// Construct from explicit elements.
        constexpr Mat1x3D(float m0, float m1, float m2) : mat{m0, m1, m2} {}
    };

    /// Four-element column-vector storage.
    class Mat1x4D {
      public:
        /// Matrix/vector elements.
        float mat[4]{};

        /// Construct a zero-initialized 4-element vector.
        constexpr Mat1x4D() = default;

        /// Construct from explicit elements.
        constexpr Mat1x4D(float m0, float m1, float m2, float m3) : mat{m0, m1, m2, m3} {}
    };

    /// Four-by-three matrix storage.
    class Mat4x3D {
      public:
        /// Matrix elements indexed as row, column.
        float mat[4][3]{};
    };

    /// Two-by-two matrix with arithmetic, determinant, inverse, and solve helpers.
    class Mat2D {
      public:
        /// Matrix elements indexed as row, column.
        float mat[2][2]{};

        /// Construct a zero-initialized matrix.
        Mat2D() = default;

        /// Construct from explicit row-major elements.
        Mat2D(float m00, float m01, float m10, float m11) {
            Set(m00, m01, m10, m11);
        }

        /// Set all matrix elements in row-major order.
        void Set(float m00, float m01, float m10, float m11) {
            mat[0][0] = m00;
            mat[0][1] = m01;
            mat[1][0] = m10;
            mat[1][1] = m11;
        }

        /// Set this matrix to the identity matrix.
        void LoadIdentity() {
            Set(1.0f, 0.0f, 0.0f, 1.0f);
        }

        /// Add two matrices component-wise.
        [[nodiscard]] Mat2D operator+(const Mat2D &m) const {
            return {mat[0][0] + m.mat[0][0], mat[0][1] + m.mat[0][1], mat[1][0] + m.mat[1][0], mat[1][1] + m.mat[1][1]};
        }

        /// Subtract two matrices component-wise.
        [[nodiscard]] Mat2D operator-(const Mat2D &m) const {
            return {mat[0][0] - m.mat[0][0], mat[0][1] - m.mat[0][1], mat[1][0] - m.mat[1][0], mat[1][1] - m.mat[1][1]};
        }

        /// Multiply two 2x2 matrices.
        [[nodiscard]] Mat2D operator*(const Mat2D &m) const {
            return {mat[0][0] * m.mat[0][0] + mat[0][1] * m.mat[1][0],
                    mat[0][0] * m.mat[0][1] + mat[0][1] * m.mat[1][1],
                    mat[1][0] * m.mat[0][0] + mat[1][1] * m.mat[1][0],
                    mat[1][0] * m.mat[0][1] + mat[1][1] * m.mat[1][1]};
        }

        /// Compute the matrix determinant.
        [[nodiscard]] float Determinate() const {
            return mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0];
        }

        /**
         * @brief Compute the inverse matrix.
         * @param out Receives the inverse on success.
         * @return True when the matrix is invertible.
         */
        bool Inverse(Mat2D &out) const {
            const float d = Determinate();
            if (std::fabs(d) <= EPSILON) {
                return false;
            }
            const float inv = 1.0f / d;
            out.Set(mat[1][1] * inv, -mat[0][1] * inv, -mat[1][0] * inv, mat[0][0] * inv);
            return true;
        }

        /**
         * @brief Solve a 2x2 linear system.
         * @param a Coefficient matrix.
         * @param out Receives the solution vector.
         * @param b Right-hand-side vector.
         * @return True when the system has a unique solution.
         */
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

    /// Three-by-three matrix with multiplication, vector transform, inverse, and solve helpers.
    class Mat3D {
      public:
        /// Matrix elements indexed as row, column.
        float mat[3][3]{};

        /// Construct a zero-initialized matrix.
        Mat3D() = default;

        /// Construct from explicit row-major elements.
        Mat3D(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22) {
            Set(m00, m01, m02, m10, m11, m12, m20, m21, m22);
        }

        /// Set all matrix elements in row-major order.
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

        /// Set this matrix to the identity matrix.
        void LoadIdentity() {
            Set(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        }

        /// Multiply two 3x3 matrices.
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

        /// Transform a 3D vector by this matrix.
        [[nodiscard]] vec3D MulVec(const vec3D &in) const {
            return {in.x * mat[0][0] + in.y * mat[1][0] + in.z * mat[2][0],
                    in.x * mat[0][1] + in.y * mat[1][1] + in.z * mat[2][1],
                    in.x * mat[0][2] + in.y * mat[1][2] + in.z * mat[2][2]};
        }

        /// Transform a 3D vector and write the result to @p out.
        void MulVec(const vec3D &in, vec3D &out) const {
            out = MulVec(in);
        }

        /// Compute the matrix determinant.
        [[nodiscard]] float Determinate() const {
            return mat[0][0] * (mat[1][1] * mat[2][2] - mat[1][2] * mat[2][1]) - mat[0][1] * (mat[1][0] * mat[2][2] - mat[1][2] * mat[2][0]) + mat[0][2] * (mat[1][0] * mat[2][1] - mat[1][1] * mat[2][0]);
        }

        /**
         * @brief Compute the inverse matrix.
         * @param out Receives the inverse on success.
         * @return True when the matrix is invertible.
         */
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

        /**
         * @brief Solve a 3x3 linear system.
         * @param a Coefficient matrix.
         * @param out Receives the solution vector.
         * @param b Right-hand-side vector.
         * @return True when the system has a unique solution.
         */
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

    /// Four-by-four homogeneous transform matrix.
    class Mat4D {
      public:
        /// Matrix elements indexed as row, column.
        float mat[4][4]{};

        /// Construct a zero-initialized matrix.
        Mat4D() = default;

        /// Construct from explicit row-major elements.
        Mat4D(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13, float m20, float m21, float m22, float m23, float m30, float m31, float m32, float m33) {
            Set(m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33);
        }

        /// Set all matrix elements in row-major order.
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

        /// Set this matrix to the identity matrix.
        void LoadIdentity() {
            Set(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        }

        /// Add two matrices component-wise.
        [[nodiscard]] Mat4D operator+(const Mat4D &m) const {
            Mat4D out;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    out.mat[r][c] = mat[r][c] + m.mat[r][c];
                }
            }
            return out;
        }

        /// Multiply two 4x4 matrices.
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

        /// Multiply this matrix by another matrix in place.
        Mat4D &operator*=(const Mat4D &m) {
            *this = *this * m;
            return *this;
        }

        /// Transform a homogeneous 4D vector by this matrix.
        [[nodiscard]] vec4D MulVec(const vec4D &in) const {
            vec4D out(0.0f, 0.0f, 0.0f, 0.0f);
            out.x = in.x * mat[0][0] + in.y * mat[1][0] + in.z * mat[2][0] + in.w * mat[3][0];
            out.y = in.x * mat[0][1] + in.y * mat[1][1] + in.z * mat[2][1] + in.w * mat[3][1];
            out.z = in.x * mat[0][2] + in.y * mat[1][2] + in.z * mat[2][2] + in.w * mat[3][2];
            out.w = in.x * mat[0][3] + in.y * mat[1][3] + in.z * mat[2][3] + in.w * mat[3][3];
            return out;
        }

        /// Transform a homogeneous 4D vector and write the result to @p out.
        void MulVec(const vec4D &in, vec4D &out) const {
            out = MulVec(in);
        }

        /// Transform a 3D point by this matrix using W = 1.
        [[nodiscard]] vec3D MulVec(const vec3D &in) const {
            const vec4D r = MulVec(vec4D(in.x, in.y, in.z, 1.0f));
            return {r.x, r.y, r.z};
        }

        /// Transform a 3D point and write the result to @p out.
        void MulVec(const vec3D &in, vec3D &out) const {
            out = MulVec(in);
        }

        /**
         * @brief Compute the inverse matrix using Gauss-Jordan elimination.
         * @param out Receives the inverse on success.
         * @return True when the matrix is invertible.
         */
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

        /// Build an XYZ Euler rotation matrix from angles in degrees.
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

    /// Parametric 2D line segment represented by a start point, end point, and direction.
    struct paramLine2D {
        /// Segment start point.
        vec2D p0;

        /// Segment end point.
        vec2D p1;

        /// Direction vector, commonly p1 - p0.
        vec2D v;

        /// Construct an uninitialized line segment.
        paramLine2D() = default;

        /// Construct from explicit endpoints and direction.
        paramLine2D(const vec2D &start, const vec2D &end, const vec2D &dir) {
            Set(start, end, dir);
        }

        /// Set explicit endpoints and direction.
        void Set(const vec2D &start, const vec2D &end, const vec2D &dir) {
            p0 = start;
            p1 = end;
            v = dir;
        }

        /// Initialize from endpoints and derive the direction vector.
        void Init(const vec2D &start, const vec2D &end) {
            p0 = start;
            p1 = end;
            v = end - start;
        }

        /// Compute the point p0 + v * t.
        [[nodiscard]] vec2D ComputePoint(float t) const {
            return p0 + v * t;
        }

        /// Compute the point p0 + v * t and write it to @p out.
        vec2D ComputePoint(float t, vec2D &out) const {
            out = ComputePoint(t);
            return out;
        }

        /**
         * @brief Intersect this segment with another parametric segment.
         * @param line Segment to intersect with.
         * @param t_this Receives this segment's intersection parameter.
         * @param t_other Receives the other segment's intersection parameter.
         * @return 0 for parallel lines, 1 for segment intersection, 2 for line intersection outside one or both segments.
         */
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

        /**
         * @brief Intersect this segment with another segment and compute the point.
         * @param line Segment to intersect with.
         * @param out Receives the intersection point when the lines are not parallel.
         * @return 0 for parallel lines, 1 for segment intersection, 2 for line intersection outside one or both segments.
         */
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

    /// Parametric 3D line segment represented by a start point, end point, and direction.
    struct paramLine3D {
        /// Segment start point.
        vec3D p0;

        /// Segment end point.
        vec3D p1;

        /// Direction vector, commonly p1 - p0.
        vec3D v;

        /// Construct an uninitialized line segment.
        paramLine3D() = default;

        /// Construct from explicit endpoints and direction.
        paramLine3D(const vec3D &start, const vec3D &end, const vec3D &dir) {
            Set(start, end, dir);
        }

        /// Set explicit endpoints and direction.
        void Set(const vec3D &start, const vec3D &end, const vec3D &dir) {
            p0 = start;
            p1 = end;
            v = dir;
        }

        /// Initialize from endpoints and derive the direction vector.
        void Init(const vec3D &start, const vec3D &end) {
            p0 = start;
            p1 = end;
            v = end - start;
        }

        /// Compute the point p0 + v * t.
        [[nodiscard]] vec3D ComputePoint(float t) const {
            return p0 + v * t;
        }

        /// Compute the point p0 + v * t and write it to @p out.
        vec3D ComputePoint(float t, vec3D &out) const {
            out = ComputePoint(t);
            return out;
        }
    };

    /// Plane represented by a point and a normal vector.
    struct Plane3D {
        /// Point on the plane.
        vec3D p0;

        /// Plane normal.
        vec3D v;

        /// Construct an uninitialized plane.
        Plane3D() = default;

        /// Construct from a point and normal vector.
        Plane3D(const vec3D &point, const vec3D &normal) : p0(point), v(normal) {}

        /// Set the plane point and normal, optionally normalizing the normal.
        void Set(const vec3D &point, const vec3D &normal, bool normalize) {
            p0 = point;
            v = normal;
            if (normalize) {
                v.Normalize();
            }
        }
    };

    /// 2D polar coordinate.
    struct Polar {
        /// Radial distance.
        float r = 0.0f;

        /// Angle in degrees for this framework's math helpers.
        float theta = 0.0f;
    };

    /// Cylindrical coordinate.
    struct CyType {
        /// Radial distance.
        float r = 0.0f;

        /// Azimuth angle in degrees for this framework's math helpers.
        float theta = 0.0f;

        /// Height coordinate.
        float z = 0.0f;
    };

    /// Spherical coordinate.
    struct SpType {
        /// Radial distance.
        float p = 0.0f;

        /// Azimuth angle in degrees for this framework's math helpers.
        float theta = 0.0f;

        /// Inclination angle in degrees for this framework's math helpers.
        float phi = 0.0f;
    };

    /// Quaternion used for 3D rotations.
    class QuatType {
      public:
        /// X component of the vector part.
        float x = 0.0f;

        /// Y component of the vector part.
        float y = 0.0f;

        /// Z component of the vector part.
        float z = 0.0f;

        /// Scalar component.
        float w = 1.0f;

        /// Construct the identity quaternion.
        constexpr QuatType() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

        /// Construct from explicit quaternion components.
        constexpr QuatType(float x_value, float y_value, float z_value, float w_value) : x(x_value), y(y_value), z(z_value), w(w_value) {}

        /// Add two quaternions component-wise.
        [[nodiscard]] constexpr QuatType operator+(const QuatType &q) const {
            return {x + q.x, y + q.y, z + q.z, w + q.w};
        }

        /// Subtract two quaternions component-wise.
        [[nodiscard]] constexpr QuatType operator-(const QuatType &q) const {
            return {x - q.x, y - q.y, z - q.z, w - q.w};
        }

        /// Multiply two quaternions.
        [[nodiscard]] constexpr QuatType operator*(const QuatType &q) const {
            return {w * q.x + x * q.w + y * q.z - z * q.y,
                    w * q.y - x * q.z + y * q.w + z * q.x,
                    w * q.z + x * q.y - y * q.x + z * q.w,
                    w * q.w - x * q.x - y * q.y - z * q.z};
        }

        /// Multiply this quaternion by another quaternion in place.
        QuatType &operator*=(const QuatType &q) {
            *this = *this * q;
            return *this;
        }

        /// Conjugate this quaternion in place.
        void Conj() {
            x = -x;
            y = -y;
            z = -z;
        }

        /// Scale all quaternion components in place.
        void Scale(float f) {
            x *= f;
            y *= f;
            z *= f;
            w *= f;
        }

        /// Compute the squared norm.
        [[nodiscard]] float Norm2() const {
            return w * w + x * x + y * y + z * z;
        }

        /// Compute the norm.
        [[nodiscard]] float Norm() const {
            return std::sqrt(Norm2());
        }

        /// Normalize this quaternion in place, or reset it to identity if it is too small.
        void Normalize() {
            const float norm = Norm();
            if (norm <= EPSILON) {
                x = y = z = 0.0f;
                w = 1.0f;
                return;
            }
            Scale(1.0f / norm);
        }

        /// Invert this quaternion in place.
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

        /// Invert a unit quaternion by conjugating it.
        void InverseNormal() {
            Conj();
        }

        /// Return the quaternion product (*this * p1) * p2.
        [[nodiscard]] QuatType TripleProduct(const QuatType &p1, const QuatType &p2) const {
            return (*this * p1) * p2;
        }

        /// Build a quaternion from an axis and angle in degrees.
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

        /// Build a quaternion from a 4D axis vector and angle in degrees.
        void vec4DthetaQuat(float theta_degrees, const vec4D &axis) {
            vec3D n(axis.x, axis.y, axis.z);
            vec3DthetaQuat(theta_degrees, n);
        }

        /// Build a quaternion from Euler angles in ZYX order, with angles in degrees.
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

        /// Convert this quaternion to axis-angle form.
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

    /// Triangle primitive used by the simple software rendering pipeline.
    struct Triangle {
        /// Working vertex positions.
        vec4D vlist[3]{};

        /// Transformed vertex positions.
        vec4D tlist[3]{};

        /// Triangle color.
        MXCOLOR color = MXVK_RGB(255, 255, 255);

        /// Application-defined polygon attributes.
        int attr = 0;

        /// Polygon state flags.
        int state = 0;

        /// Indices into an object's vertex arrays.
        int vert[3]{};
    };

    /// Object and polygon state bit flags.
    enum {
        /// Active object or polygon.
        MX_ACTIVE = 0x1,

        /// Visible object or polygon.
        MX_VISIBLE = 0x2,

        /// Polygon is marked as a backface.
        MX_BACKFACE = 0x4,

        /// Object or polygon is culled.
        MX_CULLED = 0x8
    };

    /// Flat list of triangles prepared for transformation and rasterization.
    class RenderList {
      public:
        /// Triangle storage.
        std::vector<Triangle> polys;

        /// Cached polygon count matching polys.size().
        int num_polys = 0;

        /// Clear all triangles from the render list.
        void Reset() {
            polys.clear();
            num_polys = 0;
        }

        /// Transform active non-backface triangles by a matrix.
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

        /// Mark back-facing triangles relative to a view position.
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

        /// Translate model-space vertices into world-space transformed vertices.
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

        /// Append one triangle to the render list.
        void BuildRenderList(const Triangle &triangle) {
            polys.push_back(triangle);
            num_polys = static_cast<int>(polys.size());
        }
    };

    /// Simple mesh object loaded from PLG-style indexed triangle data.
    class mxObject {
      public:
        /// Object state flags.
        int state = MX_ACTIVE;

        /// Object attributes.
        int attr = 0;

        /// Average radius from the local origin.
        float avg_rad = 0.0f;

        /// Maximum radius from the local origin.
        float max_rad = 0.0f;

        /// Object world position.
        vec4D world_pos;

        /// Object direction.
        vec4D dir;

        /// Local X basis vector.
        vec4D ux;

        /// Local Y basis vector.
        vec4D uy;

        /// Local Z basis vector.
        vec4D uz;

        /// Number of loaded vertices.
        int num_vertices = 0;

        /// Number of loaded polygons.
        int num_polys = 0;

        /// Local-space vertices.
        std::vector<vec4D> local;

        /// Transformed vertices.
        std::vector<vec4D> trans;

        /// Indexed triangle list.
        std::vector<Triangle> vlist;

        /// Object name loaded from the model file.
        std::string object_name;

        /**
         * @brief Load a PLG mesh file.
         * @param path File path to load.
         * @param scale Per-axis scale applied to vertices.
         * @param obj_pos World position assigned to the object.
         * @return True when the file is opened and parsed successfully.
         *
         * The final vector parameter is retained for API compatibility and is not used.
         */
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

        /// Load an MX mesh file using the PLG loader compatibility path.
        bool LoadMX(const std::string &path, const vec4D &scale, const vec4D &obj_pos, const vec4D &rotation) {
            return LoadPLG(path, scale, obj_pos, rotation);
        }

        /// Convert local or transformed vertices to world space.
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

        /// Apply a transform to local vertices, transformed vertices, or local-to-transformed output.
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

        /// Mark object polygons that face away from a view position.
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

        /// Append this object's triangles to a render list.
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

        /// Reset object and polygon state to active.
        void Reset() {
            for (auto &poly : vlist) {
                poly.state = MX_ACTIVE;
            }
            state = MX_ACTIVE;
        }

        /// Compute and cache average and maximum local-space radii.
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

        /// Replace the object state flags.
        void SetState(int new_state) {
            state = new_state;
        }
    };

    /// Camera and projection data for the simple software 3D pipeline.
    class Camera {
      public:
        /// Camera state flags.
        int state = 0;

        /// Camera attributes.
        int attr = 0;

        /// Camera position.
        vec4D pos;

        /// Euler camera direction in degrees.
        vec4D dir;

        /// UVN camera U basis vector.
        vec4D u;

        /// UVN camera V basis vector.
        vec4D v;

        /// UVN camera N basis vector.
        vec4D n;

        /// Camera look-at target.
        vec4D target;

        /// Distance from camera to view plane.
        float view_dist = 1.0f;

        /// Vertical field of view in degrees.
        float fov = 90.0f;

        /// Near clipping plane Z.
        float near_clip_z = 1.0f;

        /// Far clipping plane Z.
        float far_clip_z = 1000.0f;

        /// Right clipping plane.
        Plane3D rt_clip_plane;

        /// Left clipping plane.
        Plane3D lt_clip_plane;

        /// Top clipping plane.
        Plane3D tp_clip_plane;

        /// Bottom clipping plane.
        Plane3D bt_clip_plane;

        /// View-plane height.
        float viewplane_height = 2.0f;

        /// View-plane width.
        float viewplane_width = 2.0f;

        /// Viewport width in pixels.
        float viewport_width = 1.0f;

        /// Viewport height in pixels.
        float viewport_height = 1.0f;

        /// Viewport center X coordinate.
        float viewport_center_x = 0.0f;

        /// Viewport center Y coordinate.
        float viewport_center_y = 0.0f;

        /// Viewport aspect ratio.
        float aspect_ratio = 1.0f;

        /// World-to-camera transform matrix.
        Mat4D mcam;

        /// Perspective transform matrix placeholder.
        Mat4D mper;

        /// Screen transform matrix placeholder.
        Mat4D mscr;

        /// Construct a camera with identity matrices.
        Camera() {
            mcam.LoadIdentity();
            mper.LoadIdentity();
            mscr.LoadIdentity();
        }

        /// Initialize camera fields for the Euler example path.
        void InitalizeForEuler() {
            pos.Set(100.0f, 200.0f, 300.0f);
            dir.Set(-48.0f, 0.0f, 0.0f);
            BuildEuler(5);
        }

        /// Initialize camera projection, viewport, position, and direction parameters.
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

        /// Build the world-to-camera matrix from Euler direction angles.
        void BuildEuler(int) {
            Mat4D translation(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -pos.x, -pos.y, -pos.z, 1);
            Mat4D rotation;
            rotation.BuildXYZ(-dir.x, -dir.y, -dir.z);
            mcam = translation * rotation;
        }

        /// Build the world-to-camera matrix from the camera position and look-at target.
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

        /// Transform a render list from world space to camera space.
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

        /// Transform an object's transformed vertices from world space to camera space.
        void WorldToCamera(mxObject &object) const {
            for (auto &vertex : object.trans) {
                vertex = mcam.MulVec(vertex);
            }
        }

        /// Project a render list from camera space to perspective space.
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

        /// Project an object's transformed vertices from camera space to perspective space.
        void CameraToPerspective(mxObject &object) const {
            for (auto &vertex : object.trans) {
                if (std::fabs(vertex.z) <= EPSILON) {
                    continue;
                }
                vertex.x = view_dist * vertex.x / vertex.z;
                vertex.y = view_dist * vertex.y * aspect_ratio / vertex.z;
            }
        }

        /// Convert a render list from perspective coordinates to screen coordinates.
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

        /// Convert an object's transformed vertices from perspective coordinates to screen coordinates.
        void PerspectiveToScreen(mxObject &object) const {
            const float alpha = viewport_center_x;
            const float beta = viewport_center_y;
            for (auto &vertex : object.trans) {
                vertex.x = alpha + alpha * vertex.x;
                vertex.y = beta - beta * vertex.y;
            }
        }
    };

    /// Pixel plotter adapter that writes packed MXVK colors to an SDL renderer.
    struct SDLRendererPixelPlotter {
        /// SDL renderer receiving plotted pixels.
        SDL_Renderer *renderer = nullptr;

        /// Plot one pixel if the renderer is valid.
        void operator()(int x, int y, MXCOLOR color) const {
            if (renderer == nullptr) {
                return;
            }
            SDL_SetRenderDrawColor(renderer, color_r(color), color_g(color), color_b(color), color_a(color));
            SDL_RenderPoint(renderer, static_cast<float>(x), static_cast<float>(y));
        }
    };

    /// Pixel plotter adapter that draws square pixels into a VK_Sprite.
    struct VKSpritePixelPlotter {
        /// Sprite receiving plotted pixels.
        VK_Sprite *sprite = nullptr;

        /// Square pixel size in sprite coordinates.
        int size = 1;

        /// Plot one square pixel if the sprite is valid.
        void operator()(int x, int y, MXCOLOR) const {
            if (sprite != nullptr) {
                sprite->drawSpriteRect(x, y, std::max(1, size), std::max(1, size));
            }
        }
    };

    /**
     * @brief Draw a line with Bresenham-style integer stepping.
     * @tparam PlotPixel Callable accepting (x, y, MXCOLOR).
     * @param x0 Start X coordinate.
     * @param y0 Start Y coordinate.
     * @param x1 End X coordinate.
     * @param y1 End Y coordinate.
     * @param color Packed ARGB color.
     * @param plot_pixel Pixel plotting callable.
     */
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

    /// Draw a line directly to an SDL renderer.
    inline void draw_line(SDL_Renderer *renderer, int x0, int y0, int x1, int y1, MXCOLOR color) {
        draw_line(x0, y0, x1, y1, color, SDLRendererPixelPlotter{renderer});
    }

    /// Draw a line directly to a VK_Sprite with optional square pixel size.
    inline void draw_line(VK_Sprite &sprite, int x0, int y0, int x1, int y1, MXCOLOR color, int pixel_size = 1) {
        draw_line(x0, y0, x1, y1, color, VKSpritePixelPlotter{&sprite, pixel_size});
    }

    /// Compute the signed edge function value for point @p p relative to edge @p a-@p b.
    [[nodiscard]] inline float edge_function(const vec2D &a, const vec2D &b, const vec2D &p) {
        return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    }

    /**
     * @brief Rasterize a filled triangle by testing pixels against edge functions.
     * @tparam PlotPixel Callable accepting (x, y, MXCOLOR).
     * @param p0 First triangle vertex in screen coordinates.
     * @param p1 Second triangle vertex in screen coordinates.
     * @param p2 Third triangle vertex in screen coordinates.
     * @param color Packed ARGB color.
     * @param plot_pixel Pixel plotting callable.
     */
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

    /// Draw a filled triangle directly to an SDL renderer.
    inline void draw_filled_triangle(SDL_Renderer *renderer, const vec2D &p0, const vec2D &p1, const vec2D &p2, MXCOLOR color) {
        draw_filled_triangle(p0, p1, p2, color, SDLRendererPixelPlotter{renderer});
    }

    /// Draw a filled triangle directly to a VK_Sprite with optional square pixel size.
    inline void draw_filled_triangle(VK_Sprite &sprite, const vec2D &p0, const vec2D &p1, const vec2D &p2, MXCOLOR color, int pixel_size = 1) {
        draw_filled_triangle(p0, p1, p2, color, VKSpritePixelPlotter{&sprite, pixel_size});
    }

    /**
     * @brief Rasterize a filled triangle as horizontal spans.
     * @tparam DrawSpan Callable accepting (x0, x1, y, MXCOLOR).
     * @param p0 First triangle vertex in screen coordinates.
     * @param p1 Second triangle vertex in screen coordinates.
     * @param p2 Third triangle vertex in screen coordinates.
     * @param color Packed ARGB color.
     * @param draw_span Span drawing callable.
     */
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

    /// Clipped software rasterization pipeline for lines and filled triangles.
    class PipeLine {
      public:
        /// Cohen-Sutherland region codes for line clipping.
        enum LINE_CODE {
            /// Center/inside code.
            CODE_C = 0x0000,

            /// North/top code.
            CODE_N = 0x0008,

            /// South/bottom code.
            CODE_S = 0x0004,

            /// East/right code.
            CODE_E = 0x0002,

            /// West/left code.
            CODE_W = 0x0001
        };

        /// Maximum clip X coordinate.
        int max_clip_x = 0;

        /// Maximum clip Y coordinate.
        int max_clip_y = 0;

        /// Minimum clip X coordinate.
        int min_clip_x = 0;

        /// Minimum clip Y coordinate.
        int min_clip_y = 0;

        /// Active minimum clip X coordinate.
        int clip_min_x = 0;

        /// Active maximum clip X coordinate.
        int clip_max_x = 0;

        /// Active minimum clip Y coordinate.
        int clip_min_y = 0;

        /// Active maximum clip Y coordinate.
        int clip_max_y = 0;

        /// Begin rendering with a custom pixel plotter.
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

        /// Begin rendering to an SDL renderer.
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

        /// Begin rendering to a VK_Sprite with optional square pixel size.
        void Begin(VK_Sprite &sprite, int width, int height, int pixel_size = 1) {
            Begin(width, height, [&sprite, pixel_size](int x, int y, MXCOLOR color) { VKSpritePixelPlotter{&sprite, pixel_size}(x, y, color); });
            plot_span = [&sprite, pixel_size](int x0, int x1, int y, MXCOLOR) {
                const int size = std::max(1, pixel_size);
                sprite.drawSpriteRect(x0, y, std::max(1, x1 - x0 + 1), size);
            };
        }

        /// Compute the Cohen-Sutherland region code for a point.
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

        /**
         * @brief Clip a line segment to the active clip rectangle.
         * @param x0 Start X coordinate, updated to clipped value.
         * @param y0 Start Y coordinate, updated to clipped value.
         * @param x1 End X coordinate, updated to clipped value.
         * @param y1 End Y coordinate, updated to clipped value.
         * @return True when some portion of the line remains visible.
         */
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

        /// Draw a clipped line. Kept with the original misspelled name for compatibility.
        void DrawClipedLine(int x0, int y0, int x1, int y1, MXCOLOR color) const {
            if (plot_pixel && ClipLine(x0, y0, x1, y1)) {
                draw_line(x0, y0, x1, y1, color, plot_pixel);
            }
        }

        /// Draw a clipped line.
        void DrawClippedLine(int x0, int y0, int x1, int y1, MXCOLOR color) const {
            DrawClipedLine(x0, y0, x1, y1, color);
        }

        /// Draw a clipped filled triangle from 2D screen-space vertices.
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

        /// Draw a clipped filled triangle from homogeneous screen-space vertices.
        void DrawFilledTriangle(const vec4D &p0, const vec4D &p1, const vec4D &p2, MXCOLOR color) const {
            DrawFilledTriangle(vec2D(p0.x, p0.y), vec2D(p1.x, p1.y), vec2D(p2.x, p2.y), color);
        }

        /// Draw active render-list triangles as filled polygons.
        void DrawSolidPolys(const RenderList &list) const {
            for (const auto &poly : list.polys) {
                if (poly.state == 0 || (poly.state & MX_BACKFACE) != 0) {
                    continue;
                }
                DrawFilledTriangle(poly.tlist[0], poly.tlist[1], poly.tlist[2], poly.color);
            }
        }

        /// Draw active render-list triangles as clipped wireframes.
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

        /// Draw an object's active transformed triangles as clipped wireframes.
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

        /// End rendering and release the current plotting callbacks.
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
