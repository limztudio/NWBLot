// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <pipeline/volume_workspace.h>
#include <core/assets/cook_metadata_internal.h>
#include <tests/common/memory_sdk.h>
#include <core/metascript/parser.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GraphicsCookToolSdk{

#if defined(NWB_DEBUG)
inline constexpr bool s_Debug = true;
#else
inline constexpr bool s_Debug = false;
#endif
#if defined(NWB_FINAL)
inline constexpr bool s_Final = true;
#else
inline constexpr bool s_Final = false;
#endif
#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline constexpr bool s_Windows = true;
#else
inline constexpr bool s_Windows = false;
#endif

struct NullAllocation{
    static void* operator new(usize)noexcept{ return nullptr; }
    static void operator delete(void* pointer)noexcept{ ::operator delete(pointer); }
};

template<typename Bucket>
b8 RegisterBucket(Core::Assets::CookEntryRegistry& registry, const Name type){ return registry.registerCustomType<Bucket>(type); }

template<typename Entry>
auto& RegistryEntries(Core::Assets::CookEntryRegistry& registry, const Name type){ return registry.entries<Entry>(type); }

inline bool HasBucket(const Core::Assets::CookEntryRegistry& registry, const Name type)noexcept{ return registry.find(type) != nullptr; }

inline Core::Assets::CookMetadataDetail::DiscoveredPathSet DiscoveredPaths(Core::Alloc::ScratchArena& storage){
    return Core::Assets::CookMetadataDetail::DiscoveredPathSet(0u, Hasher<AStringView>{}, EqualTo<>{}, storage);
}
template<typename Text>
u8 CStringByte(const Text& text, const usize index)noexcept{ return static_cast<u8>(text.c_str()[index]); }

using TestDeclarationRef = Optional<NotNull<const Core::Metascript::Document::Declaration*>>;
inline TestDeclarationRef Declaration(const Core::Metascript::Document& doc, const AStringView name){
    if(const auto* declaration = doc.findDeclaration(name))
        return MakeNotNull(declaration);
    return {};
}
inline TestDeclarationRef FirstDeclaration(const Core::Metascript::Document& doc){
    if(!doc.declarations().empty())
        return MakeNotNull(&doc.declarations().front());
    return {};
}

inline AStringView CurrentTestName(){ return testing::UnitTest::GetInstance()->current_test_info()->name(); }
inline Core::Assets::AssetBytes& MergeOutput(Core::BinaryUpdateContext& context)noexcept{ return context.output; }

template<typename Policy>
b8 Merge(const Name& name, BinaryByteView bytes, Core::BinaryUpdateContext& context){
    return Policy{}.merge(name, bytes, context);
}
template<typename Policy>
Pipeline::GatherMergeCallback BindMerge()noexcept{
    return &Core::Assets::InvokeAssetGatherMerge<&Merge<Policy>>;
}

inline Core::Assets::AssetBytes Bytes(HeapStorage& storage){
    return Core::Assets::AssetBytes(storage.arena);
}

template<typename RootArena, typename SourceArena>
Expected<Core::Assets::AssetString> DeriveOwnedPath(const ::Path<RootArena>& root, const AStringView virtualRoot, const ::Path<SourceArena>& source, HeapStorage& owner, Core::Alloc::ScratchArena& scratch){
    return Core::Assets::AssetPathsDetail::BuildDerivedAssetVirtualPathText(root, virtualRoot, source, owner.arena, scratch);
}

template<typename Asset>
Expected<Asset> TakeAssetValue(Core::Assets::IAsset& source){
    if(!Core::Assets::CastAsset<Asset>(&source))
        return MakeUnexpected(Failure{});
    return Move(static_cast<Asset&>(source));
}

template<typename RootArena, typename FileArena>
void AppendCatalogFile(Pipeline::CookFileCatalog& catalog, const ::Path<RootArena>& root, const ::Path<FileArena>& file, const AStringView normalized, const AStringView virtualRoot){
    auto& arena = catalog.files.get_allocator().arena();
    catalog.files.emplace_back(arena, Path(arena, root), file, normalized, virtualRoot);
}

template<typename T, typename Storage, typename Callable, typename... Args>
T InvokeWithStorage(Storage& storage, Callable&& callable, Args&&... args){
    return callable(storage.arena, Forward<Args>(args)...);
}

template<typename Arena>
Path OwnPathLike(const Path& donor, const ::Path<Arena>& source){ return Path(donor.arena(), source); }

template<typename Storage, typename Arena>
Path OwnPath(Storage& storage, const ::Path<Arena>& source){
    return Path(storage.arena, source);
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

