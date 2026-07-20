#pragma once


#include "../global.h"

#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader final : public Core::Assets::TypedAsset<Shader>{
public:
    NWB_DEFINE_ASSET_TYPE("shader")


public:
    explicit Shader(Core::Assets::AssetArena& arena)
        : m_bytecode(arena)
    {}
    Shader(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Shader>(virtualPath)
        , m_bytecode(arena)
    {}


public:
    [[nodiscard]] const Core::Assets::AssetBytes& bytecode()const{ return m_bytecode; }

public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);


private:
    Core::Assets::AssetBytes m_bytecode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderAssetCodec final : public Core::Assets::AssetCodec<Shader>{
public:
    ShaderAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

