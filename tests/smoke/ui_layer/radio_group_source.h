// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ui/widgets/list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiRadioGroupSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const override;
    [[nodiscard]] virtual u64 revision()const override;
    [[nodiscard]] virtual u64 rowCount()const override;
    [[nodiscard]] virtual u64 key(u64 index)const override;
    [[nodiscard]] virtual bool indexOf(u64 key, u64& index)const override;
    [[nodiscard]] virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
    [[nodiscard]] virtual StringView text(u64 index)const override;
    [[nodiscard]] virtual bool enabled(u64 index)const override;


public:
    void reverse();
    void remove(u64 key);
    void replace();
    void beginFrame();
    [[nodiscard]] bool reversed()const{ return m_reversed; }
    [[nodiscard]] u64 removedKey()const{ return m_removed; }
    [[nodiscard]] u32 labelReads()const{ return m_labelReads; }


private:
    [[nodiscard]] u64 rawKey(u64 index)const;
    void invalidateText()const;


private:
    u64 m_generation = 1701u;
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

