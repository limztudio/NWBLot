// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"
#include "material_bind.h"

#include <impl/assets_shader/shader_cook.h>

#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialCookEntry{
    using StageShaderMap = ShaderCook::CookMap<Core::ShaderType::Enum, Core::Assets::AssetRef<Shader>>;
    using ParameterMap = MaterialBindParameterMap;

    Name virtualPath = NAME_NONE;
    Name materialInterface = NAME_NONE;
    u64 typedLayoutHash = 0u;
    Material::TypedLayoutBlockVector typedLayoutBlocks;
    Material::TypedLayoutFieldVector typedLayoutFields;
    Material::TypedBlockByteVector typedBlockBytes;
    ShaderCook::CookString shaderVariant;
    StageShaderMap stageShaders;
    ParameterMap parameters;
    f32 alpha = 1.f;
    bool transparent = false;

    explicit MaterialCookEntry(ShaderCook::CookArena& arena)
        : typedLayoutBlocks(arena)
        , typedLayoutFields(arena)
        , typedBlockBytes(arena)
        , shaderVariant(arena)
        , stageShaders(0, Hasher<Core::ShaderType::Enum>(), EqualTo<Core::ShaderType::Enum>(), arena)
        , parameters(0, Hasher<CompactString>(), EqualTo<CompactString>(), arena)
    {}

    void reset(){
        virtualPath = NAME_NONE;
        materialInterface = NAME_NONE;
        typedLayoutHash = 0u;
        typedLayoutBlocks.clear();
        typedLayoutFields.clear();
        typedBlockBytes.clear();
        shaderVariant.clear();
        stageShaders.clear();
        parameters.clear();
        alpha = 1.f;
        transparent = false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseMaterialCookMetadata(
    ShaderCook& shaderCook,
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry
);
[[nodiscard]] bool ValidateMaterialCookInterfaces(
    const ShaderCook::CookVector<MaterialBindEntry>& materialBindEntries,
    ShaderCook::CookVector<MaterialCookEntry>& materialEntries
);
[[nodiscard]] bool BuildMaterialBindIncludeSource(
    ShaderCook::CookArena& arena,
    const MaterialBindEntry& entry,
    ShaderCook::CookString& outSource
);
[[nodiscard]] bool EmitMaterialBindIncludes(
    ShaderCook::CookArena& arena,
    const Path& cacheDirectory,
    AStringView configurationSafeName,
    const ShaderCook::CookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot
);
[[nodiscard]] bool ResolveMaterialBindDependencyInterface(
    AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const ShaderCook::CookVector<Path>& dependencies,
    ShaderCook::CookString& outInterfacePath,
    Name& outInterfaceName
);
[[nodiscard]] bool BuildMaterialAsset(const MaterialCookEntry& materialEntry, Material& outMaterial);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

