// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ui_immutable_list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestbedUiNestedPopupSource final : public TestbedUiFilteredListSource<TestbedUiNestedPopupSource, 5u, 3u>{
    friend class TestbedUiImmutableListSource<TestbedUiNestedPopupSource, 5u, 3u, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class TestbedUiFilteredListSource<TestbedUiNestedPopupSource, 5u, 3u>;


private:
    static constexpr Array<StringView, 5u> s_Labels{ "Alpha", "Beta", "Gamma (disabled)", "Delta", "Echo" };


public:
    using TestbedUiFilteredListSource<TestbedUiNestedPopupSource, 5u, 3u>::TestbedUiFilteredListSource;


public:
    virtual StringView text(const u64 index)const override{
        ++m_labelReads;
        return index < rowCount() ? s_Labels[index] : StringView{};
    }

    void beginFrame(){ m_labelReads = 0u; }
    [[nodiscard]] u32 labelReads()const{ return m_labelReads; }


private:
    mutable u32 m_labelReads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

