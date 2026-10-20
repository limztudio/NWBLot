// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_immutable_list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_SearchComboLabelCount = 12u;
inline constexpr u64 s_SearchComboDisabledKey = 5u;

class UiSearchComboSource final : public UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>{
    friend class UiImmutableListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>;


private:
    static constexpr Array<StringView, s_SearchComboLabelCount> s_Labels{ "Apple", "Banana", "Blueberry", "Cherry", "Fig (disabled)", "Grape", "Kiwi", "Lemon", "Mango", "Orange", "Pear", "Plum" };


public:
    using UiFilteredListSource<UiSearchComboSource, s_SearchComboLabelCount, s_SearchComboDisabledKey>::UiFilteredListSource;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

