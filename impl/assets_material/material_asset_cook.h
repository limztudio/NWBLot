// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_asset.h"

#include <impl/assets_shader/shader_cook.h>

#include <core/graphics/shader_archive.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialCookEntry{
    using StageShaderMap = ShaderCook::CookMap<Core::ShaderType::Enum, Core::Assets::AssetRef<Shader>>;
    using ParameterMap = ShaderCook::CookMap<CompactString, CompactString>;

    Name virtualPath = NAME_NONE;
    Name materialInterface = NAME_NONE;
    ShaderCook::CookString shaderVariant;
    StageShaderMap stageShaders;
    ParameterMap parameters;

    explicit MaterialCookEntry(ShaderCook::CookArena& arena)
        : shaderVariant(Core::ShaderArchive::s_DefaultVariant, arena)
        , stageShaders(0, Hasher<Core::ShaderType::Enum>(), EqualTo<Core::ShaderType::Enum>(), arena)
        , parameters(0, Hasher<CompactString>(), EqualTo<CompactString>(), arena)
    {}

    void reset(){
        virtualPath = NAME_NONE;
        materialInterface = NAME_NONE;
        shaderVariant = Core::ShaderArchive::s_DefaultVariant;
        stageShaders.clear();
        parameters.clear();
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
    const ShaderCook::CookVector<ShaderCook::MaterialBindEntry>& materialBindEntries,
    const ShaderCook::CookVector<MaterialCookEntry>& materialEntries
);
[[nodiscard]] bool BuildMaterialAsset(const MaterialCookEntry& materialEntry, Material& outMaterial);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

