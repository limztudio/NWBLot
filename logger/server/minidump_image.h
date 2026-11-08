// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_symbolicate_internal.h"

#include <global/limit.h>
#include <global/not_null.h>

#include <dbghelp.h>
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct MinidumpRecords{
    NotNull<const u8*> bytes;
    usize count = 0u;


public:
    [[nodiscard]] T record(const usize index)const noexcept{
        static_assert(IsTriviallyCopyable_V<T> && IsNothrowConstructible_V<T> && IsNothrowMoveConstructible_V<T>);
        NWB_ASSERT(index < count);
        T value;
        NWB_MEMCPY(&value, sizeof(value), bytes.get() + index * sizeof(T), sizeof(value));
        return value;
    }
};

struct DumpImage{
    const u8* bytes = nullptr;
    usize byteCount = 0u;


public:
    [[nodiscard]] const u8* rva(const u64 offset, const u64 size)const noexcept{
        if(!bytes || offset > static_cast<u64>(byteCount) || size > static_cast<u64>(byteCount) - offset)
            return nullptr;
        return bytes + static_cast<usize>(offset);
    }

    template<typename T>
    [[nodiscard]] Expected<T> read(const u64 offset)const noexcept{
        static_assert(IsTriviallyCopyable_V<T> && IsNothrowConstructible_V<T> && IsNothrowMoveConstructible_V<T>);
        const u8* const source = rva(offset, sizeof(T));
        if(!source)
            return MakeUnexpected(Failure{});
        T value;
        NWB_MEMCPY(&value, sizeof(value), source, sizeof(value));
        return value;
    }

    [[nodiscard]] Expected<WString<Core::Alloc::ScratchArena>> readString(RVA rvaValue, Core::Alloc::ScratchArena& scratchArena)const;
    [[nodiscard]] Expected<Optional<DumpImage>> stream(MINIDUMP_STREAM_TYPE streamType)const noexcept;

    template<typename T>
    [[nodiscard]] Expected<MinidumpRecords<T>> records(const usize recordsOffset, const u64 count)const noexcept{
        if(!bytes || recordsOffset > byteCount || count > static_cast<u64>((byteCount - recordsOffset) / sizeof(T)))
            return MakeUnexpected(Failure{});
        return MinidumpRecords<T>{ MakeNotNull(bytes + recordsOffset), static_cast<usize>(count) };
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

