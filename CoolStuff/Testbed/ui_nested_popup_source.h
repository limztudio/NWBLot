// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_immutable_list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_NestedPopupLabelCount = 5u;
inline constexpr u64 s_NestedPopupDisabledKey = 3u;

class UiNestedPopupSource final : public UiFilteredListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey>{
    friend class UiImmutableListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class UiFilteredListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey>;


private:
    static constexpr Array<StringView, s_NestedPopupLabelCount> s_Labels{ "Alpha", "Beta", "Gamma (disabled)", "Delta", "Echo" };


public:
    using UiFilteredListSource<UiNestedPopupSource, s_NestedPopupLabelCount, s_NestedPopupDisabledKey>::UiFilteredListSource;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

