// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader final : public Core::Assets::IAsset{
public:
    static constexpr AStringView s_AssetTypeText = "shader";


public:
    Shader()
        : IAsset(AssetTypeName())
    {}
    explicit Shader(const Name& virtualPath)
        : IAsset(AssetTypeName(), virtualPath)
    {}


public:
    [[nodiscard]] static const Name& AssetTypeName(){
        static const Name s_AssetType(s_AssetTypeText.data());
        return s_AssetType;
    }

    [[nodiscard]] const Core::Assets::AssetBytes& bytecode()const{ return m_bytecode; }

public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);


private:
    Core::Assets::AssetBytes m_bytecode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderAssetCodec final : public Core::Assets::IAssetCodec{
public:
    ShaderAssetCodec()
        : IAssetCodec(Shader::AssetTypeName())
    {}

    virtual bool deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

