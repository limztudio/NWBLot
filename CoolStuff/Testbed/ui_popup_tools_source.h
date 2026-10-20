// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_immutable_list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_PopupToolsLabelCount = 5u;
inline constexpr u64 s_PopupToolsDisabledKey = 3u;

class UiPopupToolsSource final : public UiImmutableListSource<UiPopupToolsSource, s_PopupToolsLabelCount, s_PopupToolsDisabledKey>{
    friend class UiImmutableListSource<UiPopupToolsSource, s_PopupToolsLabelCount, s_PopupToolsDisabledKey>;


private:
    static constexpr Array<StringView, s_PopupToolsLabelCount> s_Labels{ "Inspect selection", "Duplicate", "Delete (disabled)", "Rename", "Reset view" };


public:
    using UiImmutableListSource<UiPopupToolsSource, s_PopupToolsLabelCount, s_PopupToolsDisabledKey>::UiImmutableListSource;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

