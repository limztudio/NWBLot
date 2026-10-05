// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ui_immutable_list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestbedUiSearchComboSource final : public TestbedUiFilteredListSource<TestbedUiSearchComboSource, 12u, 5u>{
    friend class TestbedUiImmutableListSource<TestbedUiSearchComboSource, 12u, 5u, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class TestbedUiFilteredListSource<TestbedUiSearchComboSource, 12u, 5u>;


private:
    static constexpr Array<StringView, 12u> s_Labels{ "Apple", "Banana", "Blueberry", "Cherry", "Fig (disabled)", "Grape",
        "Kiwi", "Lemon", "Mango", "Orange", "Pear", "Plum" };


public:
    using TestbedUiFilteredListSource<TestbedUiSearchComboSource, 12u, 5u>::TestbedUiFilteredListSource;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

