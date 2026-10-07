// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/widgets/list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListSource final : public NWB::Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override{ return 1u; }
    virtual u64 revision()const noexcept override{ return 1u; }
    virtual u64 rowCount()const noexcept override{ return 100000u; }
    virtual u64 key(u64 index)const noexcept override{ return index < rowCount() ? index + 1u : 0u; }
    virtual bool indexOf(u64 key, u64& index)const noexcept override;
    virtual bool findEnabled(u64 start, bool reverse, u64& index)const noexcept override;
    virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual bool enabled(u64 index)const noexcept override{ return index < rowCount(); }


private:
    mutable Array<char, 32u> m_label{};
};

class UiListGallery final : NoCopy{
public:
    void paint(NWB::Impl::UiPaintContext& context, f32 x, f32 y);


private:
    UiListSource m_source;
    NWB::Impl::Ui::ListState m_state;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

