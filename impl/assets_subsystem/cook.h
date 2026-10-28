// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
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


namespace AssetsSubsystemCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_SubsystemDiagnosticPrefix = "Subsystem meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseSubsystemCookMetadata(const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Alloc::ScratchArena& scratchArena, ISubsystem& outSubsystem);

template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SubsystemDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    SubsystemCookEntry<SubsystemT> entry(arena, virtualPath);
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_SubsystemDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!ParseSubsystemCookMetadata(nwbFilePath, asset, scratchArena, entry.subsystem))
        return MakeUnexpected(Failure{});

    return entry;
}

template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, s_SubsystemDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseSubsystemCookMetadata<SubsystemT>(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSubsystemCookMetadata<SubsystemT>(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSubsystemCookMetadata<SubsystemT>(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

template<typename SubsystemT>
[[nodiscard]] Expected<SubsystemT> BuildSubsystemAsset(SubsystemCookEntry<SubsystemT>& entry, Core::Assets::AssetArena&, Core::Alloc::ScratchArena&){
    SubsystemT asset = Move(entry.subsystem);
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}

template<typename SubsystemT>
[[nodiscard]] bool ExtractSubsystemDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto subsystemEntry = context.document
        ? ParseSubsystemCookMetadata<SubsystemT>(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseSubsystemCookMetadata<SubsystemT>(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!subsystemEntry)
        return false;

    const SubsystemT& subsystem = subsystemEntry->subsystem;
    for(const ISubsystem::SubsystemRef& subsystemRef : subsystem.subsystems){
        if(!collector.addAssetDependency(subsystemRef.name(), ISubsystem::AssetTypeName()))
            return false;
    }
    const usize fieldCount = subsystem.fieldCount();
    for(usize fieldIndex = 0u; fieldIndex < fieldCount; ++fieldIndex){
        const SubsystemFieldValueDescriptor descriptor = subsystem.fieldDescriptor(fieldIndex);
        const usize valueCount = subsystem.fieldValueCount(fieldIndex);
        for(usize elementIndex = 0u; elementIndex < valueCount; ++elementIndex){
            if(!collector.addAssetDependency(subsystem.fieldValue(fieldIndex, elementIndex), descriptor.expectedAssetType))
                return false;
        }
    }
    return true;
}

template<typename SubsystemT>
[[nodiscard]] bool RegisterSubsystemCookEntry(Core::Assets::CookEntryRegistry& registry){
    Core::Assets::RegisterCompositeRootAssetType(SubsystemT::AssetTypeName());
    return Core::Assets::RegisterDocumentValueCookEntry<SubsystemCookEntry<SubsystemT>, SubsystemT, TypedSubsystemAssetCodec<SubsystemT>>(
        registry,
        SubsystemBinaryPayload::s_SubsystemAssetKindLabel,
        &ParseSubsystemDocument<SubsystemT>,
        &ParseSubsystemValue<SubsystemT>,
        &BuildSubsystemAsset<SubsystemT>
    );
}

template<typename SubsystemT>
[[nodiscard]] bool RegisterSubsystemCookDependency(Core::Assets::CookDependencyRegistry& registry){
    Core::Assets::RegisterCompositeRootAssetType(SubsystemT::AssetTypeName());
    return registry.registerType(SubsystemT::AssetTypeName(), &ExtractSubsystemDependencies<SubsystemT>, ISubsystem::AssetTypeName());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

