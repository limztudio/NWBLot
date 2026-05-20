// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArenaBase : NoCopy{
protected:
    static constexpr usize s_LogCapacity = 128;


protected:
    explicit ArenaBase(const char* allocationLog)
        : m_log{}
    {
        NWB_ASSERT_MSG(allocationLog, NWB_TEXT("ArenaBase allocationLog must be non-null"));

        const char* source = allocationLog ? allocationLog : "";
        usize i = 0;
        for(; i + 1 < LengthOf(m_log) && source[i] != '\0'; ++i)
            m_log[i] = source[i];
        m_log[i] = '\0';
    }
    ~ArenaBase() = default;

protected:
    [[nodiscard]] inline const char* log()const{ return m_log; }


private:
    char m_log[s_LogCapacity];
};


template<typename Arena>
class ArenaBaseT : public ArenaBase{
protected:
    explicit ArenaBaseT(const char* allocationLog)
        : ArenaBase(allocationLog)
    {}
    ~ArenaBaseT() = default;


public:
    template<typename T>
    inline T* allocate(usize count){
        if constexpr(requires{ Arena::s_MaxTypedAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxTypedAlignSize, "Arena cannot allocate types with alignment greater than s_MaxTypedAlignSize.");

        return AllocDetail::AllocateTyped<T>(static_cast<Arena&>(*this), count);
    }

    template<typename T>
    inline void deallocate(void* p, usize count){
        if constexpr(requires{ Arena::s_MaxTypedAlignSize; })
            static_assert(alignof(T) <= Arena::s_MaxTypedAlignSize, "Arena cannot deallocate types with alignment greater than s_MaxTypedAlignSize.");

        AllocDetail::DeallocateTyped<T>(static_cast<Arena&>(*this), p, count);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

