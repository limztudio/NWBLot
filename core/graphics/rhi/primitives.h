#pragma once


#include "foundation.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Color{
    f32 r, g, b, a;

    constexpr Color()noexcept
        : r(0)
        , g(0)
        , b(0)
        , a(0)
    {}
    constexpr Color(f32 c)noexcept
        : r(c)
        , g(c)
        , b(c)
        , a(c)
    {}
    constexpr Color(f32 red, f32 green, f32 blue, f32 alpha)noexcept
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {}
};
inline bool operator==(const Color& lhs, const Color& rhs)noexcept{
    return (lhs.r == rhs.r) && (lhs.g == rhs.g) && (lhs.b == rhs.b) && (lhs.a == rhs.a);
}
inline bool operator!=(const Color& lhs, const Color& rhs)noexcept{ return !(lhs == rhs); }

struct UIntColor{
    u32 r, g, b, a;

    constexpr UIntColor()noexcept
        : r(0)
        , g(0)
        , b(0)
        , a(0)
    {}
    constexpr UIntColor(u32 c)noexcept
        : r(c)
        , g(c)
        , b(c)
        , a(c)
    {}
    constexpr UIntColor(u32 red, u32 green, u32 blue, u32 alpha)noexcept
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {}
};
inline bool operator==(const UIntColor& lhs, const UIntColor& rhs)noexcept{
    return (lhs.r == rhs.r) && (lhs.g == rhs.g) && (lhs.b == rhs.b) && (lhs.a == rhs.a);
}
inline bool operator!=(const UIntColor& lhs, const UIntColor& rhs)noexcept{ return !(lhs == rhs); }

struct IntColor{
    i32 r, g, b, a;

    constexpr IntColor()noexcept
        : r(0)
        , g(0)
        , b(0)
        , a(0)
    {}
    constexpr IntColor(i32 c)noexcept
        : r(c)
        , g(c)
        , b(c)
        , a(c)
    {}
    constexpr IntColor(i32 red, i32 green, i32 blue, i32 alpha)noexcept
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {}
};
inline bool operator==(const IntColor& lhs, const IntColor& rhs)noexcept{
    return (lhs.r == rhs.r) && (lhs.g == rhs.g) && (lhs.b == rhs.b) && (lhs.a == rhs.a);
}
inline bool operator!=(const IntColor& lhs, const IntColor& rhs)noexcept{ return !(lhs == rhs); }


struct Viewport{
    f32 minX, maxX;
    f32 minY, maxY;
    f32 minZ, maxZ;

    constexpr Viewport()noexcept
        : minX(0)
        , maxX(0)
        , minY(0)
        , maxY(0)
        , minZ(0)
        , maxZ(0)
    {}
    constexpr Viewport(f32 width, f32 height)noexcept
        : minX(0)
        , maxX(width)
        , minY(0)
        , maxY(height)
        , minZ(0)
        , maxZ(1)
    {}
    constexpr Viewport(f32 minXValue, f32 maxXValue, f32 minYValue, f32 maxYValue, f32 minZValue, f32 maxZValue)noexcept
        : minX(minXValue)
        , maxX(maxXValue)
        , minY(minYValue)
        , maxY(maxYValue)
        , minZ(minZValue)
        , maxZ(maxZValue)
    {}

    [[nodiscard]] constexpr f32 width()const noexcept{ return maxX - minX; }
    [[nodiscard]] constexpr f32 height()const noexcept{ return maxY - minY; }
};
inline bool operator==(const Viewport& lhs, const Viewport& rhs)noexcept{
    return
        (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX)
        && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY)
        && (lhs.minZ == rhs.minZ) && (lhs.maxZ == rhs.maxZ)
    ;
}
inline bool operator!=(const Viewport& lhs, const Viewport& rhs)noexcept{ return !(lhs == rhs); }


struct Rect{
    i32 minX, maxX;
    i32 minY, maxY;

    constexpr Rect()noexcept
        : minX(0)
        , maxX(0)
        , minY(0)
        , maxY(0)
    {}
    constexpr Rect(i32 width, i32 height)noexcept
        : minX(0)
        , maxX(width)
        , minY(0)
        , maxY(height)
    {}
    constexpr Rect(i32 minXValue, i32 maxXValue, i32 minYValue, i32 maxYValue)noexcept
        : minX(minXValue)
        , maxX(maxXValue)
        , minY(minYValue)
        , maxY(maxYValue)
    {}
    explicit Rect(const Viewport& viewport)noexcept
        : minX(static_cast<i32>(Floor(viewport.minX)))
        , maxX(static_cast<i32>(Ceil(viewport.maxX)))
        , minY(static_cast<i32>(Floor(viewport.minY)))
        , maxY(static_cast<i32>(Ceil(viewport.maxY)))
    {}

    [[nodiscard]] constexpr i32 width()const noexcept{ return maxX - minX; }
    [[nodiscard]] constexpr i32 height()const noexcept{ return maxY - minY; }
};
inline bool operator==(const Rect& lhs, const Rect& rhs)noexcept{
    return (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX) && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY);
}
inline bool operator!=(const Rect& lhs, const Rect& rhs)noexcept{ return !(lhs == rhs); }

struct Box{
    i32 minX, maxX;
    i32 minY, maxY;
    i32 minZ, maxZ;

    constexpr Box()noexcept
        : minX(0)
        , maxX(0)
        , minY(0)
        , maxY(0)
        , minZ(0)
        , maxZ(0)
    {}
    constexpr Box(i32 width, i32 height, i32 depth)noexcept
        : minX(0)
        , maxX(width)
        , minY(0)
        , maxY(height)
        , minZ(0)
        , maxZ(depth)
    {}
    constexpr Box(i32 minXValue, i32 maxXValue, i32 minYValue, i32 maxYValue, i32 minZValue, i32 maxZValue)noexcept
        : minX(minXValue)
        , maxX(maxXValue)
        , minY(minYValue)
        , maxY(maxYValue)
        , minZ(minZValue)
        , maxZ(maxZValue)
    {}
    constexpr Box(const Rect& rect, i32 minZValue, i32 maxZValue)noexcept
        : minX(rect.minX)
        , maxX(rect.maxX)
        , minY(rect.minY)
        , maxY(rect.maxY)
        , minZ(minZValue)
        , maxZ(maxZValue)
    {}

    [[nodiscard]] constexpr i32 width()const noexcept{ return maxX - minX; }
    [[nodiscard]] constexpr i32 height()const noexcept{ return maxY - minY; }
    [[nodiscard]] constexpr i32 depth()const noexcept{ return maxZ - minZ; }
};
inline bool operator==(const Box& lhs, const Box& rhs)noexcept{
    return
        (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX)
        && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY)
        && (lhs.minZ == rhs.minZ) && (lhs.maxZ == rhs.maxZ)
    ;
}
inline bool operator!=(const Box& lhs, const Box& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

