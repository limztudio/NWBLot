// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/widgets/list.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListSource final : public NWB::Impl::Ui::IListDataSource, NoCopy{
private:
    static constexpr u64 s_RowCount = 100000u;
    static constexpr usize s_LabelCapacity = 32u;


public:
    virtual u64 instanceGeneration()const noexcept override{ return 1u; }
    virtual u64 revision()const noexcept override{ return 1u; }
    virtual u64 rowCount()const noexcept override{ return s_RowCount; }
    virtual u64 key(u64 index)const noexcept override{ return index < rowCount() ? index + 1u : 0u; }
    virtual Expected<u64> indexOf(u64 key)const noexcept override;
    virtual Expected<u64> findEnabled(u64 start, bool reverse)const noexcept override;
    virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual bool enabled(u64 index)const noexcept override{ return index < rowCount(); }


private:
    mutable Array<char, s_LabelCapacity> m_label{};
};

class UiListGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, f32 x, f32 y);


private:
    UiListSource m_source;
    NWB::Impl::Ui::ListState m_state;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

