// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>
#include <core/assets/asset_ref.h>
#include <core/graphics/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material final : public Core::Assets::TypedAsset<Material>{
public:
    NWB_DEFINE_ASSET_TYPE("material")


public:
    static constexpr usize s_ShaderStageCount = static_cast<usize>(Core::ShaderType::Count);
    using StageShaderArray = Array<Core::Assets::AssetRef<Shader>, s_ShaderStageCount>;


public:
    Material() = default;
    explicit Material(const Name& virtualPath)
        : Core::Assets::TypedAsset<Material>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setShaderVariant(AStringView variantName){ m_shaderVariant.assign(variantName); }
    bool setShaderForStage(Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<Shader>& shaderAsset);
    bool setShaderForStage(const Name& stageName, const Core::Assets::AssetRef<Shader>& shaderAsset);
    bool setParameter(const CompactString& key, const CompactString& value);

    bool findShaderForStage(Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const;
    bool findShaderForStage(const Name& stageName, Core::Assets::AssetRef<Shader>& outShaderAsset)const;

    [[nodiscard]] const AString& shaderVariant()const{ return m_shaderVariant; }
    [[nodiscard]] const StageShaderArray& stageShaders()const{ return m_stageShaders; }
    [[nodiscard]] u32 stageShaderCount()const{ return m_stageShaderCount; }
    [[nodiscard]] const HashMap<CompactString, CompactString>& parameters()const{ return m_parameters; }


private:
    void clearStageShaders();


private:
    AString m_shaderVariant;
    StageShaderArray m_stageShaders;
    HashMap<CompactString, CompactString> m_parameters;
    u32 m_stageShaderCount = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialAssetCodec final : public Core::Assets::TypedAssetCodec<Material>{
public:
    MaterialAssetCodec() = default;

public:
    virtual bool deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

