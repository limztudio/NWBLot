// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This wrapper changes source lifetime and emptiness without storing the 100000 arithmetic rows.
class UiComboSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override{ return m_generation; }
    virtual u64 revision()const override{ return m_revision + m_rows.revision(); }
    virtual u64 rowCount()const override{ return m_empty ? 0u : m_rows.rowCount(); }
    virtual u64 key(u64 index)const override{ return m_empty ? 0u : m_rows.key(index); }
    virtual Expected<u64> indexOf(u64 key)const override{
        if(m_empty)
            return MakeUnexpected(Failure{});
        return m_rows.indexOf(key);
    }
    virtual Expected<u64> findEnabled(u64 start, bool reverse)const override;
    virtual StringView text(u64 index)const override{ return m_empty ? StringView{} : m_rows.text(index); }
    virtual bool enabled(u64 index)const override{ return !m_empty && m_rows.enabled(index); }


public:
    void reverse(){ m_rows.reverse(); }
    void remove(u64 key){ m_rows.remove(key); }
    void beginFrame(){ m_rows.beginFrame(); }
    void toggleEmpty()noexcept{ m_empty = !m_empty; ++m_revision; }
    void replace()noexcept{ ++m_generation; }
    [[nodiscard]] bool reversed()const{ return m_rows.reversed(); }
    [[nodiscard]] u64 removedKey()const{ return m_rows.removedKey(); }
    [[nodiscard]] u32 labelReads()const{ return m_rows.labelReads(); }


private:
    UiListSmokeSource m_rows;
    u64 m_generation = 1u;
    u64 m_revision = 1u;
    bool m_empty = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

