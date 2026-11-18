// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/arena_memory.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{

struct OwnerRecordRef{
    const ArenaMemoryOwnerRecord* record = nullptr;

    [[nodiscard]] b8 present()const noexcept{ return record != nullptr; }
    [[nodiscard]] b8 operator==(const OwnerRecordRef&)const noexcept = default;
    [[nodiscard]] OwnerRecordRef readIdentity(ArenaMemoryOwnerIdentity& identity)const{ return { ReadArenaMemoryOwnerIdentity(*record, identity) }; }
    [[nodiscard]] OwnerRecordRef readStats(ArenaMemoryOwnerSnapshot& snapshot)const{ return { ReadArenaMemoryOwnerRecord(*record, snapshot) }; }
};

[[nodiscard]] inline OwnerRecordRef FirstOwnerRecord(){ return { FirstArenaMemoryOwnerRecord() }; }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

