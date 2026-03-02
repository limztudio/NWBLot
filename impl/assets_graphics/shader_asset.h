// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>

#if defined(NWB_COOK)
#include <core/assets/asset_cooker.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader final : public Core::Assets::IAsset{
public:
    [[nodiscard]] virtual AStringView assetType()const override{ return "shader"; }
    [[nodiscard]] virtual AStringView virtualPath()const override{ return m_virtualPath; }

    bool loadBinary(AStringView virtualPath, const Core::Assets::AssetBytes& binary, AString& outError);
#if defined(NWB_COOK)
    bool saveBinary(Core::Assets::AssetBytes& outBinary, AString& outError)const;
#endif

    void setMetadata(
        const Name& shaderName,
        const Name& variantName,
        AStringView stage,
        AStringView entryPoint,
        AStringView sourceChecksumHex,
        AStringView bytecodeChecksumHex,
        AStringView virtualPath
    );

    bool buildIndexLine(AString& outLine, AString& outError)const;


private:
    Name m_shaderName;
    Name m_variantName;
    AString m_stage;
    AString m_entryPoint;
    AString m_sourceChecksumHex;
    AString m_bytecodeChecksumHex;
    AString m_virtualPath;
    Core::Assets::AssetBytes m_bytecode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderAssetCodec final : public Core::Assets::IAssetCodec{
public:
    [[nodiscard]] virtual AStringView assetType()const override{ return "shader"; }

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

