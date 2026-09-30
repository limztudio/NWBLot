// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestbedUiPopupToolsSource final : public NWB::Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const override{ return 1u; }
    virtual u64 revision()const override{ return 1u; }
    virtual u64 rowCount()const override{ return 5u; }
    virtual u64 key(u64 index)const override{ return index < rowCount() ? index + 1u : 0u; }
    virtual bool indexOf(u64 key, u64& index)const override;
    virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
    virtual StringView text(u64 index)const override;
    virtual bool enabled(u64 index)const override{ return index < rowCount() && key(index) != 3u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

