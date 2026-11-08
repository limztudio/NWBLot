// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/list.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiListBehaviorTestSupport{


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RangeSource final : public IListDataSource{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ return generation; }

    [[nodiscard]] virtual u64 revision()const noexcept override{ return revisionValue; }

    [[nodiscard]] virtual u64 rowCount()const noexcept override{ return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const noexcept override{
        ++keyCalls;
        if(index >= count)
            return 0u;
        return reversed ? count - index : index + 1u;
    }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 key)const noexcept override{
        u64 index = 0u;
        ++lookupCalls;
        if(changeRevisionOnLookup){
            changeRevisionOnLookup = false;
            ++revisionValue;
        }
        if(key == 0u || key > count)
            return MakeUnexpected(Failure{});
        index = badLookup ? count : wrongLookup ? 0u : reversed ? count - key : key - 1u;
        return index;
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const b8 reverse)const noexcept override{
        u64 index = 0u;
        ++findCalls;
        if(start >= count)
            return MakeUnexpected(Failure{});
        if(badFind){
            index = count;
            return index;
        }
        index = start;
        if(index >= disabledBegin && index < disabledEnd){
            if(reverse){
                if(disabledBegin == 0u)
                    return MakeUnexpected(Failure{});
                index = disabledBegin - 1u;
            }
            else{
                if(disabledEnd >= count)
                    return MakeUnexpected(Failure{});
                index = disabledEnd;
            }
        }
        return index;
    }

    [[nodiscard]] virtual StringView text(u64)const noexcept override{ return "row"; }

    [[nodiscard]] virtual b8 enabled(const u64 index)const noexcept override{
        ++enabledCalls;
        return index < count && (index < disabledBegin || index >= disabledEnd);
    }

    void clearCalls()noexcept{
        keyCalls = 0u;
        lookupCalls = 0u;
        findCalls = 0u;
        enabledCalls = 0u;
    }


public:
    u64 generation = 41u;
    mutable u64 revisionValue = 1u;
    u64 count = 10u;
    u64 disabledBegin = Limit<u64>::s_Max;
    u64 disabledEnd = Limit<u64>::s_Max;
    b8 reversed = false;
    b8 badLookup = false;
    b8 wrongLookup = false;
    b8 badFind = false;
    mutable b8 changeRevisionOnLookup = false;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 findCalls = 0u;
    mutable u64 enabledCalls = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ControlAction Action(const ListState& state, const RangeSource& source, const ControlActionKind::Enum kind)noexcept{
    ControlAction action;
    action.control = { state.inputGeneration(), source.instanceGeneration(), source.revision() };
    action.kind = kind;
    return action;
}

inline void ExpectResult(const ListResult& actual, const ListResult& expected){
    EXPECT_EQ(actual.valid, expected.valid);
    EXPECT_EQ(actual.selectionChanged, expected.selectionChanged);
    EXPECT_EQ(actual.activated, expected.activated);
    EXPECT_EQ(actual.focused, expected.focused);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


