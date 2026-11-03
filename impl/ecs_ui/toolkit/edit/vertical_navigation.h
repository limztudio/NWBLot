// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "commands.h"

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caller-owned preferred-column intent is lent synchronously; identical accepted intentions still retire old snapshots.
class EditNavigationState final : NoCopy{
private:
    [[nodiscard]] static u64 NextIdentity()noexcept{
        static Atomic<u64> s_NextIdentity{ 1u };
        return NextNonWrappingIdentity(s_NextIdentity);
    }


public:
    EditNavigationState()noexcept
        : m_instanceGeneration(NextIdentity())
    {}
    EditNavigationState(EditNavigationState&&) = delete;


public:
    EditNavigationState& operator=(EditNavigationState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }

    [[nodiscard]] f32 preferredX()const noexcept{ return m_preferredX; }

    [[nodiscard]] b8 hasPreferredX()const noexcept{ return m_valid; }
    [[nodiscard]] EditNavigationSnapshot snapshot()const noexcept{
        return EditNavigationSnapshot(m_instanceGeneration, m_generation, m_preferredX, m_valid);
    }
    [[nodiscard]] b8 matches(const EditNavigationSnapshot& snapshot)const noexcept{
        return snapshot == this->snapshot();
    }
    [[nodiscard]] b8 setPreferredX(const f32 preferredX)noexcept{
        const auto resolved = TryResolveEditNavigationPreferredX(preferredX);
        if(!resolved)
            return false;
        m_preferredX = *resolved;
        m_valid = true;
        advanceGeneration();
        return true;
    }
    void reset()noexcept{
        advanceGeneration();
        ClearEditNavigationPreferredX(m_preferredX, m_valid);
    }


private:
    void advanceGeneration()noexcept{ AdvanceRevision(m_generation); }


private:
    const u64 m_instanceGeneration;
    u64 m_generation = 1u;
    f32 m_preferredX = 0.0f;
    b8 m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolve current committed text at the key's ordered event position. Return values; never retain the model or snapshot.
interface IEditNavigationResolver : private NoCopy{
public:
    virtual ~IEditNavigationResolver()noexcept = default;


public:
    [[nodiscard]] virtual Expected<EditNavigationResult> resolve(const EditModel& model, EditNavigationDirection::Enum direction, const EditNavigationSnapshot& preferred, f32 viewportHeight) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

