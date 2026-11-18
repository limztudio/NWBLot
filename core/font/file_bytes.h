// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/shared.h>

#include <global/containers.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// FreeType and HarfBuzz retain pointers to the same immutable file.
class FontFileBytes{
private:
    struct Storage{
        using uses_leading_arena_t = ::ArenaBase;

        Storage(::ArenaBase& arena, const Span<const u8> source)
            : bytes(source.begin(), source.end(), arena)
        {}

        Vector<u8, ::ArenaBase> bytes;
    };


public:
    FontFileBytes()noexcept = default;
    FontFileBytes(const Alloc::SharedPool& pool, const Span<const u8> source){
        auto storage = pool.make<Storage>(source);
        if(storage)
            m_storage = Move(*storage);
    }


public:
    [[nodiscard]] Span<const u8> bytes()const noexcept{
        return m_storage ? Span<const u8>(m_storage->bytes) : Span<const u8>{};
    }


private:
    Alloc::Shared<Storage> m_storage;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

