// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Arithmetic row lookup keeps the fixture independent of total row count and allocates labels on demand.
class UiListSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override{ return 1u; }
    virtual u64 revision()const noexcept override{ return m_revision; }
    virtual u64 rowCount()const noexcept override{ return m_removed == 0u ? 100000u : 99999u; }
    virtual u64 key(u64 index)const override;
    virtual bool indexOf(u64 key, u64& index)const override;
    virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
    virtual StringView text(u64 index)const override;
    virtual bool enabled(u64 index)const override;


public:
    void reverse()noexcept;
    void remove(u64 key)noexcept;
    void beginFrame()noexcept{ m_labelReads = 0u; }
    [[nodiscard]] bool reversed()const noexcept{ return m_reversed; }
    [[nodiscard]] u64 removedKey()const noexcept{ return m_removed; }
    [[nodiscard]] u32 labelReads()const noexcept{ return m_labelReads; }


private:
    u64 m_revision = 1u;
    u64 m_removed = 0u;
    bool m_reversed = false;
    mutable Array<char, 32u> m_label{};
    mutable u32 m_labelReads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

