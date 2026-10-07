// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/widgets/radio_group.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiRadioGroupSource final : public NWB::Impl::Ui::IListDataSource{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override;
    [[nodiscard]] virtual u64 revision()const noexcept override;
    [[nodiscard]] virtual u64 rowCount()const noexcept override;
    [[nodiscard]] virtual u64 key(u64 index)const noexcept override;
    [[nodiscard]] virtual bool indexOf(u64 key, u64& index)const noexcept override;
    [[nodiscard]] virtual bool findEnabled(u64 start, bool reverse, u64& index)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual bool enabled(u64 index)const noexcept override;
};

class UiRadioGroupGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    UiRadioGroupSource m_source;
    NWB::Impl::Ui::RadioGroupState m_state;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

