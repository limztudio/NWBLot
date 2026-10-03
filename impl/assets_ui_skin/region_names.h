// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/basic_string.h>
#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stock widget-region names shared by the UI skin toolkit contract and the ECS widget style defaults.
// Generic UiSkin assets may contain fewer regions.
namespace UiSkinToolkitRegions{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_UI_SKIN_REGION_ENTRIES(Entry)                                                                                     \
    Entry(PanelNormal, "panel.normal")                                                                                        \
    Entry(WindowNormal, "window.normal")                                                                                      \
    Entry(WindowTitle, "window.title")                                                                                        \
    Entry(WindowCollapse, "window.collapse")                                                                                  \
    Entry(WindowResize, "window.resize")                                                                                      \
    Entry(Separator, "separator")                                                                                             \
    Entry(White, "white")                                                                                                     \
    Entry(ButtonNormal, "button.normal")                                                                                      \
    Entry(ButtonHover, "button.hover")                                                                                        \
    Entry(ButtonPressed, "button.pressed")                                                                                    \
    Entry(ButtonDisabled, "button.disabled")                                                                                  \
    Entry(CheckboxNormal, "checkbox.normal")                                                                                  \
    Entry(CheckboxHover, "checkbox.hover")                                                                                     \
    Entry(CheckboxChecked, "checkbox.checked")                                                                                \
    Entry(CheckboxDisabled, "checkbox.disabled")                                                                               \
    Entry(CheckboxMark, "checkbox.mark")                                                                                      \
    Entry(EditNormal, "edit.normal")                                                                                          \
    Entry(EditHover, "edit.hover")                                                                                            \
    Entry(EditFocused, "edit.focused")                                                                                        \
    Entry(EditDisabled, "edit.disabled")                                                                                      \
    Entry(ListBackground, "list.background")                                                                                  \
    Entry(ListRowNormal, "list.row.normal")                                                                                   \
    Entry(ListRowHover, "list.row.hover")                                                                                     \
    Entry(ListRowSelected, "list.row.selected")                                                                                \
    Entry(ListRowDisabled, "list.row.disabled")                                                                                \
    Entry(ScrollTrack, "scroll.track")                                                                                        \
    Entry(ScrollThumb, "scroll.thumb")                                                                                         \
    Entry(ScrollbarTrack, "scrollbar.track")                                                                                  \
    Entry(ScrollbarThumbNormal, "scrollbar.thumb.normal")                                                                       \
    Entry(PopupNormal, "popup.normal")                                                                                          \
    Entry(TooltipNormal, "tooltip.normal")                                                                                       \
    Entry(ComboNormal, "combo.normal")                                                                                          \
    Entry(ComboHover, "combo.hover")                                                                                             \
    Entry(ComboOpen, "combo.open")                                                                                               \
    Entry(ComboFocused, "combo.focused")                                                                                          \
    Entry(ComboDisabled, "combo.disabled")                                                                                        \
    Entry(ComboArrow, "combo.arrow")                                                                                              \
    Entry(RadioNormal, "radio.normal")                                                                                             \
    Entry(RadioHover, "radio.hover")                                                                                                \
    Entry(RadioPressed, "radio.pressed")                                                                                             \
    Entry(RadioChecked, "radio.checked")                                                                                              \
    Entry(RadioDisabled, "radio.disabled")                                                                                             \
    Entry(RadioMark, "radio.mark")                                                                                                     \
    Entry(SliderTrack, "slider.track")                                                                                                \
    Entry(SliderThumbNormal, "slider.thumb.normal")                                                                                     \
    Entry(SliderThumbHover, "slider.thumb.hover")                                                                                       \
    Entry(SliderThumbPressed, "slider.thumb.pressed")                                                                                    \
    Entry(SliderThumbDisabled, "slider.thumb.disabled")                                                                                   \
    Entry(ProgressTrack, "progress.track")                                                                                             \
    Entry(ProgressFill, "progress.fill")                                                                                              \
    Entry(FocusOverlay, "focus.overlay")

#define NWB_UI_SKIN_REGION_NAME(Key, Text) inline constexpr Name s_##Key##RegionName(Text);
NWB_UI_SKIN_REGION_ENTRIES(NWB_UI_SKIN_REGION_NAME)
#undef NWB_UI_SKIN_REGION_NAME

#define NWB_UI_SKIN_REGION_TEXT(Key, Text) inline constexpr AStringView s_##Key##RegionText = Text;
NWB_UI_SKIN_REGION_ENTRIES(NWB_UI_SKIN_REGION_TEXT)
#undef NWB_UI_SKIN_REGION_TEXT

#undef NWB_UI_SKIN_REGION_ENTRIES


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
