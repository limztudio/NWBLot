// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <global/name.h>
#include <global/string_view.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The gallery selector reserves this content area; the text area fills it at selector width.
inline constexpr f32 s_SelectorWidth = 400.0f;
inline constexpr f32 s_ContentHeight = 260.0f;
inline constexpr f32 s_OrdinaryGalleryWidth = 280.0f;
inline constexpr f32 s_GalleryFontSize = 14.0f;

// Stable widget keys reused by several sample panels.
inline constexpr AStringView s_TitleKey = "title";
inline constexpr AStringView s_HintKey = "hint";
inline constexpr AStringView s_SelectedKey = "selected";
inline constexpr AStringView s_ChoiceKey = "choice";
inline constexpr AStringView s_ReadOnlyKey = "read_only";
inline constexpr AStringView s_EnabledKey = "enabled";
inline constexpr AStringView s_AmountKey = "amount";
inline constexpr AStringView s_CommandsKey = "commands";

// Default skin regions drawn by more than one sample.
inline constexpr Name s_ButtonNormal("button.normal");
inline constexpr Name s_ComboArrow("combo.arrow");


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

