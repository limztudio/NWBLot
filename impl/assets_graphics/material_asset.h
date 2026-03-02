// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material final : public Core::Assets::IAsset{
public:
    [[nodiscard]] virtual AStringView assetType()const override{ return "material"; }
    [[nodiscard]] virtual AStringView virtualPath()const override{ return m_virtualPath; }

    bool loadBinary(AStringView virtualPath, const Core::Assets::AssetBytes& binary, AString& outError);
#if defined(NWB_COOK)
    bool saveBinary(Core::Assets::AssetBytes& outBinary, AString& outError)const;
#endif

    void setName(const Name& name){ m_name = name; }
    void setShader(const Name& shaderName, const Name& variantName);
    void setParameter(AStringView key, AStringView value){ m_parameters[AString(key)] = AString(value); }

    [[nodiscard]] const Name& name()const{ return m_name; }
    [[nodiscard]] const Name& shaderName()const{ return m_shaderName; }
    [[nodiscard]] const Name& shaderVariant()const{ return m_shaderVariant; }
    [[nodiscard]] const Core::Assets::AssetMap<AString, AString>& parameters()const{ return m_parameters; }


private:
    Name m_name;
    Name m_shaderName;
    Name m_shaderVariant;
    AString m_virtualPath;
    Core::Assets::AssetMap<AString, AString> m_parameters;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialAssetCodec final : public Core::Assets::IAssetCodec{
public:
    [[nodiscard]] virtual AStringView assetType()const override{ return "material"; }

    virtual bool deserialize(
        AStringView virtualPath,
        const Core::Assets::AssetBytes& binary,
        UniquePtr<Core::Assets::IAsset>& outAsset,
        AString& outError
    )const override;

#if defined(NWB_COOK)
    virtual bool serialize(
        const Core::Assets::IAsset& asset,
        Core::Assets::AssetBytes& outBinary,
        AString& outError
    )const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

