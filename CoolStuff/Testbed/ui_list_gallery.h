// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/widgets/list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestbedUiListSource final : public NWB::Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const override{ return 1u; }
    virtual u64 revision()const override{ return 1u; }
    virtual u64 rowCount()const override{ return 100000u; }
    virtual u64 key(u64 index)const override{ return index < rowCount() ? index + 1u : 0u; }
    virtual bool indexOf(u64 key, u64& index)const override;
    virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
    virtual StringView text(u64 index)const override;
    [[nodiscard]] virtual bool enabled(u64 index)const override{ return index < rowCount(); }


private:
    mutable Array<char, 32u> m_label{};
};

class TestbedUiListGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    TestbedUiListSource m_source;
    NWB::Impl::Ui::ListState m_state;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

