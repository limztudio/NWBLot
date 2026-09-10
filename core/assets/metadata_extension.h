// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ParsedMetadataExtensionDeleter final{
public:
    using DestroyFunction = void (*)(AssetArena& arena, void* value)noexcept;


public:
    template<typename T>
    static void destroyObject(AssetArena& arena, void* value)noexcept{
        DestroyArenaObjectNoexcept(arena, static_cast<T*>(value));
    }


public:
    ParsedMetadataExtensionDeleter(AssetArena& arena, const NotNull<DestroyFunction> destroy)noexcept
        : m_arena(MakeNotNull(&arena))
        , m_destroy(destroy)
    {}

    void operator()(void* value)const noexcept{
        m_destroy.get()(*m_arena, value);
    }


private:
    NotNull<AssetArena*> m_arena;
    NotNull<DestroyFunction> m_destroy;
};

// The allocation arena must outlive the extension, including transfers between maps.
using ParsedMetadataExtension = UniquePtr<void, ParsedMetadataExtensionDeleter>;


template<typename T, typename... Args>
[[nodiscard]] ParsedMetadataExtension MakeParsedMetadataExtension(AssetArena& arena, Args&&... args){
    auto created = MakeGlobalUnique<T>(arena, Forward<Args>(args)...);
    if(!created)
        throw RuntimeException("AssetCook metadata extension allocation failed");

    return ParsedMetadataExtension(created.release(), ParsedMetadataExtensionDeleter(arena, MakeNotNull(&ParsedMetadataExtensionDeleter::destroyObject<T>)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

