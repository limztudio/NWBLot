// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/widgets/radio_group.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestbedUiRadioGroupSource final : public NWB::Impl::Ui::IListDataSource{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const override;
    [[nodiscard]] virtual u64 revision()const override;
    [[nodiscard]] virtual u64 rowCount()const override;
    [[nodiscard]] virtual u64 key(u64 index)const override;
    [[nodiscard]] virtual bool indexOf(u64 key, u64& index)const override;
    [[nodiscard]] virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
    [[nodiscard]] virtual StringView text(u64 index)const override;
    [[nodiscard]] virtual bool enabled(u64 index)const override;
};

class TestbedUiRadioGroupGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    TestbedUiRadioGroupSource m_source;
    NWB::Impl::Ui::RadioGroupState m_state;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

