// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_immutable_list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupSource final : public UiFilteredListSource<UiNestedPopupSource, 5u, 3u>{
    friend class UiImmutableListSource<UiNestedPopupSource, 5u, 3u, NWB::Impl::Ui::ISearchableListDataSource>;
    friend class UiFilteredListSource<UiNestedPopupSource, 5u, 3u>;


private:
    static constexpr Array<StringView, 5u> s_Labels{ "Alpha", "Beta", "Gamma (disabled)", "Delta", "Echo" };


public:
    using UiFilteredListSource<UiNestedPopupSource, 5u, 3u>::UiFilteredListSource;


public:
    virtual StringView text(const u64 index)const override{
        ++m_labelReads;
        return index < rowCount() ? s_Labels[index] : StringView{};
    }

    void beginFrame()noexcept{ m_labelReads = 0u; }
    [[nodiscard]] u32 labelReads()const noexcept{ return m_labelReads; }


private:
    mutable u32 m_labelReads = 0u;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

