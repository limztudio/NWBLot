// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/ecs_ui/toolkit/id.adhd.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stable keys of widget parts; builder, paint, and input code must derive the same ids.
namespace WidgetPartKey{
    inline constexpr AStringView s_Popup = s_WidgetPartPopup;
    inline constexpr AStringView s_Rows = s_WidgetPartRows;
    inline constexpr AStringView s_Scrollbar = s_WidgetPartScrollbar;
    inline constexpr AStringView s_Track = s_WidgetPartTrack;
    inline constexpr AStringView s_Thumb = s_WidgetPartThumb;
    inline constexpr AStringView s_ScrollHorizontalThumb = s_WidgetPartScrollHorizontalThumb;
    inline constexpr AStringView s_ScrollVerticalThumb = s_WidgetPartScrollVerticalThumb;
};

[[nodiscard]] Expected<WidgetId> MakeRootId(const WidgetRoot& root)noexcept;
[[nodiscard]] Expected<WidgetId> MakeWidgetId(WidgetId parent, AStringView stableKey)noexcept;
// Numeric parts occupy a separate identity domain from nonempty textual declarations.
[[nodiscard]] Expected<WidgetId> MakeWidgetPartId(WidgetId parent, u64 stableKey)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
