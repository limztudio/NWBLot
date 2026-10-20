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


public:
    virtual StringView text(const u64 index)const noexcept override{
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

