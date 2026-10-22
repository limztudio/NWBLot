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
private:
    static constexpr u64 s_RowCount = 3u;
    static constexpr u64 s_FirstDisabledIndex = 2u;
    static constexpr u64 s_LastEnabledIndex = 1u;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ return 1u; }

    [[nodiscard]] virtual u64 revision()const noexcept override{ return 1u; }

    [[nodiscard]] virtual u64 rowCount()const noexcept override{ return s_RowCount; }

    [[nodiscard]] virtual u64 key(u64 index)const noexcept override{ return index < s_RowCount ? index + 1u : 0u; }
    [[nodiscard]] virtual Expected<u64> indexOf(u64 key)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, bool reverse)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual bool enabled(u64 index)const noexcept override{ return index < s_FirstDisabledIndex; }
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

