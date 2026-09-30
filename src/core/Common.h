#pragma once

#include <cmath>

#ifdef LYNX_BUILD_DLL
#define LYNX_API __declspec(dllexport)
#else
#define LYNX_API __declspec(dllimport)
#endif

namespace lynx
{
    struct LYNX_API vec2
    {
        float x, y;

        vec2() : x(0.0f), y(0.0f) {}
        vec2(float x, float y) : x(x), y(y) {}
        explicit vec2(float value) : x(value), y(value) {}

        vec2 operator+(const vec2& v) const
        {
            return { x + v.x, y + v.y };
        }

        vec2 operator-(const vec2& v) const
        {
            return { x - v.x, y - v.y };
        }

        vec2 operator*(const vec2& v) const
        {
            return { x * v.x, y * v.y };
        }

        vec2 operator/(const vec2& v) const
        {
            return { x / v.x, y / v.y };
        }

        vec2 operator+(float value) const
        {
            return { x + value, y + value };
        }

        vec2 operator-(float value) const
        {
            return { x - value, y - value };
        }

        vec2 operator*(float value) const
        {
            return { x * value, y * value };
        }

        vec2 operator/(float value) const
        {
            return { x / value, y / value };
        }

        vec2& operator+=(const vec2& v)
        {
            x += v.x;
            y += v.y;
            return *this;
        }

        vec2& operator-=(const vec2& v)
        {
            x -= v.x;
            y -= v.y;
            return *this;
        }

        vec2& operator*=(const vec2& v)
        {
            x *= v.x;
            y *= v.y;
            return *this;
        }

        vec2& operator/=(const vec2& v)
        {
            x /= v.x;
            y /= v.y;
            return *this;
        }

        vec2& operator+=(float value)
        {
            x += value;
            y += value;
            return *this;
        }

        vec2& operator-=(float value)
        {
            x -= value;
            y -= value;
            return *this;
        }

        vec2& operator*=(float value)
        {
            x *= value;
            y *= value;
            return *this;
        }

        vec2& operator/=(float value)
        {
            x /= value;
            y /= value;
            return *this;
        }

        vec2 operator-() const
        {
            return { -x, -y };
        }

        bool operator==(const vec2& v) const
        {
            return x == v.x && y == v.y;
        }

        bool operator!=(const vec2& v) const
        {
            return !(*this == v);
        }

        float length() const
        {
            return std::sqrt(x * x + y * y);
        }

        float lengthSquared() const
        {
            return x * x + y * y;
        }

        vec2 normalized() const
        {
            float len = length();
            return len != 0.0f ? *this / len : vec2();
        }

        float dot(const vec2& v) const
        {
            return x * v.x + y * v.y;
        }
    };


    struct LYNX_API vec3
    {
        float x, y, z;

        vec3() : x(0.0f), y(0.0f), z(0.0f) {}
        vec3(float x, float y, float z) : x(x), y(y), z(z) {}
        explicit vec3(float value) : x(value), y(value), z(value) {}
        vec3(const vec2& v, float z) : x(v.x), y(v.y), z(z) {}

        vec2 xy() const
        {
            return { x, y };
        }

        vec3 operator+(const vec3& v) const
        {
            return { x + v.x, y + v.y, z + v.z };
        }

        vec3 operator-(const vec3& v) const
        {
            return { x - v.x, y - v.y, z - v.z };
        }

        vec3 operator*(const vec3& v) const
        {
            return { x * v.x, y * v.y, z * v.z };
        }

        vec3 operator/(const vec3& v) const
        {
            return { x / v.x, y / v.y, z / v.z };
        }

        vec3 operator+(float value) const
        {
            return { x + value, y + value, z + value };
        }

        vec3 operator-(float value) const
        {
            return { x - value, y - value, z - value };
        }

        vec3 operator*(float value) const
        {
            return { x * value, y * value, z * value };
        }

        vec3 operator/(float value) const
        {
            return { x / value, y / value, z / value };
        }

        vec3& operator+=(const vec3& v)
        {
            x += v.x;
            y += v.y;
            z += v.z;
            return *this;
        }

        vec3& operator-=(const vec3& v)
        {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            return *this;
        }

        vec3& operator*=(const vec3& v)
        {
            x *= v.x;
            y *= v.y;
            z *= v.z;
            return *this;
        }

        vec3& operator/=(const vec3& v)
        {
            x /= v.x;
            y /= v.y;
            z /= v.z;
            return *this;
        }

        vec3& operator+=(float value)
        {
            x += value;
            y += value;
            z += value;
            return *this;
        }

        vec3& operator-=(float value)
        {
            x -= value;
            y -= value;
            z -= value;
            return *this;
        }

        vec3& operator*=(float value)
        {
            x *= value;
            y *= value;
            z *= value;
            return *this;
        }

        vec3& operator/=(float value)
        {
            x /= value;
            y /= value;
            z /= value;
            return *this;
        }

        vec3 operator-() const
        {
            return { -x, -y, -z };
        }

        bool operator==(const vec3& v) const
        {
            return x == v.x && y == v.y && z == v.z;
        }

        bool operator!=(const vec3& v) const
        {
            return !(*this == v);
        }

        float length() const
        {
            return std::sqrt(x * x + y * y + z * z);
        }

        float lengthSquared() const
        {
            return x * x + y * y + z * z;
        }

        vec3 normalized() const
        {
            float len = length();
            return len != 0.0f ? *this / len : vec3();
        }

        float dot(const vec3& v) const
        {
            return x * v.x + y * v.y + z * v.z;
        }

        vec3 cross(const vec3& v) const
        {
            return {
                y * v.z - z * v.y,
                z * v.x - x * v.z,
                x * v.y - y * v.x
            };
        }
    };


    struct LYNX_API vec4
    {
        float x, y, z, w;

        vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
        vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
        explicit vec4(float value) : x(value), y(value), z(value), w(value) {}
        vec4(const vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

        vec3 xyz() const
        {
            return { x, y, z };
        }

        vec4 operator+(const vec4& v) const
        {
            return { x + v.x, y + v.y, z + v.z, w + v.w };
        }

        vec4 operator-(const vec4& v) const
        {
            return { x - v.x, y - v.y, z - v.z, w - v.w };
        }

        vec4 operator*(const vec4& v) const
        {
            return { x * v.x, y * v.y, z * v.z, w * v.w };
        }

        vec4 operator/(const vec4& v) const
        {
            return { x / v.x, y / v.y, z / v.z, w / v.w };
        }

        vec4 operator+(float value) const
        {
            return { x + value, y + value, z + value, w + value };
        }

        vec4 operator-(float value) const
        {
            return { x - value, y - value, z - value, w - value };
        }

        vec4 operator*(float value) const
        {
            return { x * value, y * value, z * value, w * value };
        }

        vec4 operator/(float value) const
        {
            return { x / value, y / value, z / value, w / value };
        }

        vec4& operator+=(const vec4& v)
        {
            x += v.x;
            y += v.y;
            z += v.z;
            w += v.w;
            return *this;
        }

        vec4& operator-=(const vec4& v)
        {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            w -= v.w;
            return *this;
        }

        vec4& operator*=(const vec4& v)
        {
            x *= v.x;
            y *= v.y;
            z *= v.z;
            w *= v.w;
            return *this;
        }

        vec4& operator/=(const vec4& v)
        {
            x /= v.x;
            y /= v.y;
            z /= v.z;
            w /= v.w;
            return *this;
        }

        vec4& operator+=(float value)
        {
            x += value;
            y += value;
            z += value;
            w += value;
            return *this;
        }

        vec4& operator-=(float value)
        {
            x -= value;
            y -= value;
            z -= value;
            w -= value;
            return *this;
        }

        vec4& operator*=(float value)
        {
            x *= value;
            y *= value;
            z *= value;
            w *= value;
            return *this;
        }

        vec4& operator/=(float value)
        {
            x /= value;
            y /= value;
            z /= value;
            w /= value;
            return *this;
        }

        vec4 operator-() const
        {
            return { -x, -y, -z, -w };
        }

        bool operator==(const vec4& v) const
        {
            return x == v.x && y == v.y && z == v.z && w == v.w;
        }

        bool operator!=(const vec4& v) const
        {
            return !(*this == v);
        }

        float length() const
        {
            return std::sqrt(x * x + y * y + z * z + w * w);
        }

        float lengthSquared() const
        {
            return x * x + y * y + z * z + w * w;
        }

        vec4 normalized() const
        {
            float len = length();
            return len != 0.0f ? *this / len : vec4();
        }

        float dot(const vec4& v) const
        {
            return x * v.x + y * v.y + z * v.z + w * v.w;
        }
    };


    inline vec2 operator+(float value, const vec2& v)
    {
        return v + value;
    }

    inline vec2 operator-(float value, const vec2& v)
    {
        return { value - v.x, value - v.y };
    }

    inline vec2 operator*(float value, const vec2& v)
    {
        return v * value;
    }

    inline vec2 operator/(float value, const vec2& v)
    {
        return { value / v.x, value / v.y };
    }


    inline vec3 operator+(float value, const vec3& v)
    {
        return v + value;
    }

    inline vec3 operator-(float value, const vec3& v)
    {
        return { value - v.x, value - v.y, value - v.z };
    }

    inline vec3 operator*(float value, const vec3& v)
    {
        return v * value;
    }

    inline vec3 operator/(float value, const vec3& v)
    {
        return { value / v.x, value / v.y, value / v.z };
    }


    inline vec4 operator+(float value, const vec4& v)
    {
        return v + value;
    }

    inline vec4 operator-(float value, const vec4& v)
    {
        return { value - v.x, value - v.y, value - v.z, value - v.w };
    }

    inline vec4 operator*(float value, const vec4& v)
    {
        return v * value;
    }

    inline vec4 operator/(float value, const vec4& v)
    {
        return { value / v.x, value / v.y, value / v.z, value / v.w };
    }


    struct LYNX_API transform
{
    vec3 location;
    vec3 rotation;
    vec3 scale;

    transform()
        : location(0.0f),
          rotation(0.0f),
          scale(1.0f)
    {
    }

    transform(
        const vec3& location,
        const vec3& rotation,
        const vec3& scale = vec3(1.0f)
    )
        : location(location),
          rotation(rotation),
          scale(scale)
    {
    }

            transform operator+(const transform& t) const
    {
        return {
            location + t.location,
            rotation + t.rotation,
            scale + t.scale
        };
    }

    transform operator-(const transform& t) const
    {
        return {
            location - t.location,
            rotation - t.rotation,
            scale - t.scale
        };
    }

    transform operator*(const transform& t) const
    {
        return {
            location * t.location,
            rotation * t.rotation,
            scale * t.scale
        };
    }

    transform operator/(const transform& t) const
    {
        return {
            location / t.location,
            rotation / t.rotation,
            scale / t.scale
        };
    }

    transform& operator+=(const transform& t)
    {
        location += t.location;
        rotation += t.rotation;
        scale += t.scale;
        return *this;
    }

    transform& operator-=(const transform& t)
    {
        location -= t.location;
        rotation -= t.rotation;
        scale -= t.scale;
        return *this;
    }

    transform& operator*=(const transform& t)
    {
        location *= t.location;
        rotation *= t.rotation;
        scale *= t.scale;
        return *this;
    }

    transform& operator/=(const transform& t)
    {
        location /= t.location;
        rotation /= t.rotation;
        scale /= t.scale;
        return *this;
    }

    transform operator+(float value) const
    {
        return {
            location + value,
            rotation + value,
            scale + value
        };
    }

    transform operator-(float value) const
    {
        return {
            location - value,
            rotation - value,
            scale - value
        };
    }

    transform operator*(float value) const
    {
        return {
            location * value,
            rotation * value,
            scale * value
        };
    }

    transform operator/(float value) const
    {
        return {
            location / value,
            rotation / value,
            scale / value
        };
    }

    transform& operator+=(float value)
    {
        location += value;
        rotation += value;
        scale += value;
        return *this;
    }

    transform& operator-=(float value)
    {
        location -= value;
        rotation -= value;
        scale -= value;
        return *this;
    }

    transform& operator*=(float value)
    {
        location *= value;
        rotation *= value;
        scale *= value;
        return *this;
    }

    transform& operator/=(float value)
    {
        location /= value;
        rotation /= value;
        scale /= value;
        return *this;
    }

    transform operator-() const
    {
        return {
            -location,
            -rotation,
            -scale
        };
    }

    bool operator==(const transform& t) const
    {
        return location == t.location &&
               rotation == t.rotation &&
               scale == t.scale;
    }

    bool operator!=(const transform& t) const
    {
        return !(*this == t);
    }


    // Translation
    transform& translate(const vec3& value)
    {
        location += value;
        return *this;
    }

    transform& translate(float x, float y, float z)
    {
        location += vec3(x, y, z);
        return *this;
    }

    // Rotation
    transform& rotate(const vec3& value)
    {
        rotation += value;
        return *this;
    }

    transform& rotate(float x, float y, float z)
    {
        rotation += vec3(x, y, z);
        return *this;
    }

    // Scale
    transform& scaleBy(const vec3& value)
    {
        scale *= value;
        return *this;
    }

    transform& scaleBy(float x, float y, float z)
    {
        scale *= vec3(x, y, z);
        return *this;
    }

    transform& scaleBy(float value)
    {
        scale *= value;
        return *this;
    }

    // Reset
    void reset()
    {
        location = vec3(0.0f);
        rotation = vec3(0.0f);
        scale = vec3(1.0f);
    }

    // Setters
    void setLocation(const vec3& value)
    {
        location = value;
    }

    void setRotation(const vec3& value)
    {
        rotation = value;
    }

    void setScale(const vec3& value)
    {
        scale = value;
    }

    void setScale(float value)
    {
        scale = vec3(value);
    }

    // Direction vectors
    vec3 forward() const
    {
        const float pitch = rotation.x;
        const float yaw   = rotation.y;

        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);

        return vec3(
            sy * cp,
            -sp,
            cy * cp
        ).normalized();
    }

    vec3 right() const
    {
        return forward().cross(vec3(0.0f, 1.0f, 0.0f)).normalized();
    }

    vec3 up() const
    {
        return right().cross(forward()).normalized();
    }
};
}