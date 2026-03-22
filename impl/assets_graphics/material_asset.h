// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>
#include <core/assets/asset_ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material final : public Core::Assets::IAsset{
public:
    static constexpr AStringView s_AssetTypeText = "material";


public:
    Material()
        : IAsset(AssetTypeName())
    {}
    explicit Material(const Name& virtualPath)
        : IAsset(AssetTypeName(), virtualPath)
    {}


public:
    [[nodiscard]] static const Name& AssetTypeName(){
        static const Name s_AssetType(s_AssetTypeText.data());
        return s_AssetType;
    }

public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setShaderVariant(AStringView variantName);
    void setShaderForStage(const Name& stageName, const Core::Assets::AssetRef<Shader>& shaderAsset);
    bool setParameter(const CompactString& key, const CompactString& value);

    [[nodiscard]] bool findShaderForStage(const Name& stageName, Core::Assets::AssetRef<Shader>& outShaderAsset)const;

    [[nodiscard]] const AString& shaderVariant()const{ return m_shaderVariant; }
    [[nodiscard]] const HashMap<Name, Core::Assets::AssetRef<Shader>>& stageShaders()const{ return m_stageShaders; }
    [[nodiscard]] const HashMap<CompactString, CompactString>& parameters()const{ return m_parameters; }


private:
    AString m_shaderVariant;
    HashMap<Name, Core::Assets::AssetRef<Shader>> m_stageShaders;
    HashMap<CompactString, CompactString> m_parameters;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialAssetCodec final : public Core::Assets::IAssetCodec{
public:
    MaterialAssetCodec()
        : IAssetCodec(Material::AssetTypeName())
    {}

public:
    virtual bool deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

