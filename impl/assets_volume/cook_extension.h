// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetVolumePrepareContext{
    Core::Alloc::GlobalArena& arena;
    ShaderCook& shaderCook;
    const ResolvedCookPaths& resolvedPaths;
    CookString& configurationSafeName;
    ParsedAssetMetadata& parsedMetadata;
    u64& plannedFileCount;
    AssetVolumeExternalWriterVector& externalWriters;
    ScratchArena& scratchArena;
};

using AssetVolumePrepareFunction = bool (*)(AssetVolumePrepareContext& context);

enum class AssetVolumeMetadataParseResult : u8{
    Unsupported,
    Parsed,
    Error
};

struct AssetVolumeDocumentMetadataParseContext{
    ShaderCook::CookArena& cookArena;
    ShaderCook& shaderCook;
    const DiscoveredNwbFile& discoveredNwbFile;
    Name assetType = NAME_NONE;
    const Core::Metascript::Document& doc;
    ParsedAssetMetadata& parsedMetadata;
    ScratchArena& scratchArena;
};

struct AssetVolumeValueMetadataParseContext{
    ShaderCook::CookArena& cookArena;
    ShaderCook& shaderCook;
    const DiscoveredNwbFile& discoveredNwbFile;
    Name assetType = NAME_NONE;
    Name virtualPath = NAME_NONE;
    const Core::Metascript::Value& value;
    ParsedAssetMetadata& parsedMetadata;
    ScratchArena& scratchArena;
};

using AssetVolumeDocumentMetadataParseFunction = AssetVolumeMetadataParseResult (*)(AssetVolumeDocumentMetadataParseContext& context);
using AssetVolumeValueMetadataParseFunction = AssetVolumeMetadataParseResult (*)(AssetVolumeValueMetadataParseContext& context);

class AssetVolumePrepareAutoRegistrar final{
public:
    explicit AssetVolumePrepareAutoRegistrar(AssetVolumePrepareFunction function);
};

class AssetVolumeMetadataParserAutoRegistrar final{
public:
    AssetVolumeMetadataParserAutoRegistrar(
        AssetVolumeDocumentMetadataParseFunction documentFunction,
        AssetVolumeValueMetadataParseFunction valueFunction
    );
};

[[nodiscard]] bool RegisterAutoCollectedAssetVolumePreparers(AssetVolumePrepareContext& context);
[[nodiscard]] AssetVolumeMetadataParseResult TryAutoCollectedDocumentMetadataParsers(AssetVolumeDocumentMetadataParseContext& context);
[[nodiscard]] AssetVolumeMetadataParseResult TryAutoCollectedValueMetadataParsers(AssetVolumeValueMetadataParseContext& context);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
