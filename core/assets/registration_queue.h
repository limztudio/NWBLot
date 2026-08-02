// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Static extension registration may run while a consumer takes a stable snapshot. Keep synchronization and duplicate
// handling here so each extension point owns only its value semantics.
template<typename ValueT>
class AutoRegistrationQueue final : NoCopy{
public:
    explicit AutoRegistrationQueue(const Name& arenaName)
        : m_arena(arenaName)
        , m_values(m_arena)
    {}


public:
    template<typename EqualT>
    void appendUnique(const ValueT value, EqualT&& equal){
        ScopedLock lock(m_mutex);
        for(const ValueT current : m_values){
            if(equal(current, value))
                return;
        }

        m_values.push_back(value);
    }

    template<typename OutputVectorT>
    void copyTo(OutputVectorT& outValues){
        static_assert(IsSame_V<typename OutputVectorT::value_type, ValueT>, "auto-registration queue value types must match");
        static_assert(IsTriviallyCopyable_V<ValueT>, "auto-registration queue values must be trivially copyable");

        ScopedLock lock(m_mutex);
        AssignTriviallyCopyableVector(outValues, m_values);
    }


private:
    Futex m_mutex;
    AssetArena m_arena;
    AssetVector<ValueT> m_values;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

