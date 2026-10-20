// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct WidgetId{
    u64 value = 0u;


public:
    [[nodiscard]] bool valid()const noexcept{ return value != 0u; }
};

// Root identity comes from the host; include its lifetime generation rather than iteration order or an address.
struct WidgetRoot{
    u64 value = 0u;
    u64 generation = 1u;
};

[[nodiscard]] inline bool operator==(const WidgetId& lhs, const WidgetId& rhs)noexcept{ return lhs.value == rhs.value; }
[[nodiscard]] inline bool operator!=(const WidgetId& lhs, const WidgetId& rhs)noexcept{ return lhs.value != rhs.value; }
[[nodiscard]] inline bool operator==(const WidgetRoot& lhs, const WidgetRoot& rhs)noexcept{
    return lhs.value == rhs.value && lhs.generation == rhs.generation;
}

// Stable keys of widget parts; builder, paint, and input code must derive the same ids.
namespace WidgetPartKey{
    inline constexpr AStringView s_Popup = "popup";
    inline constexpr AStringView s_Rows = "rows";
    inline constexpr AStringView s_Scrollbar = "scrollbar";
    inline constexpr AStringView s_Track = "track";
    inline constexpr AStringView s_Thumb = "thumb";
    inline constexpr AStringView s_ScrollHorizontalThumb = "scroll.x.thumb";
    inline constexpr AStringView s_ScrollVerticalThumb = "scroll.y.thumb";
};

[[nodiscard]] Expected<WidgetId> MakeRootId(const WidgetRoot& root)noexcept;
[[nodiscard]] Expected<WidgetId> MakeWidgetId(WidgetId parent, AStringView stableKey)noexcept;
// Numeric parts occupy a separate identity domain from nonempty textual declarations.
[[nodiscard]] Expected<WidgetId> MakeWidgetPartId(WidgetId parent, u64 stableKey)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

