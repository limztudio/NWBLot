// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/alloc/scratch.h>
#include <core/assets/cook_entry_registry.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
struct SubsystemCookEntry{
    Name virtualPath = s_NameNone;
    SubsystemT subsystem;


public:
    explicit SubsystemCookEntry(Core::Assets::AssetArena& arena)noexcept
        : subsystem(arena)
    {}
    SubsystemCookEntry(Core::Assets::AssetArena& arena, const Name& inVirtualPath)noexcept
        : virtualPath(inVirtualPath)
        , subsystem(arena, inVirtualPath)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);
template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemT> BuildSubsystemAsset(
    SubsystemCookEntry<SubsystemT>& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
);

void CollectCookedSubsystemVirtualPaths(
    const Core::Assets::CookEntryRegistry& entryRegistry,
    Vector<ISubsystem::SubsystemRef, Core::Alloc::ScratchArena>& inOutSubsystems
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

