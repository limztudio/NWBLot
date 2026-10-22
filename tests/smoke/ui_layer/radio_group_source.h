// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiRadioGroupSmokeSource final : public Impl::Ui::IListDataSource, NoCopy{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override;
    [[nodiscard]] virtual u64 revision()const noexcept override;
    [[nodiscard]] virtual u64 rowCount()const noexcept override;
    [[nodiscard]] virtual u64 key(u64 index)const noexcept override;
    [[nodiscard]] virtual Expected<u64> indexOf(u64 key)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, bool reverse)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual bool enabled(u64 index)const noexcept override;


public:
    void reverse()noexcept;
    void remove(u64 key)noexcept;
    void replace()noexcept;
    void beginFrame()noexcept{ m_labelReads = 0u; }

    [[nodiscard]] bool reversed()const noexcept{ return m_reversed; }

    [[nodiscard]] u64 removedKey()const noexcept{ return m_removed; }

    [[nodiscard]] u32 labelReads()const noexcept{ return m_labelReads; }


private:
    [[nodiscard]] u64 rawKey(u64 index)const noexcept;
    void invalidateText()const noexcept;


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

