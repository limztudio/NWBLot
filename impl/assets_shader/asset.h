// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "shader_types.h"

#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IShader : Core::Assets::IAsset{
public:
    virtual ~IShader()noexcept override = 0;


protected:
    IShader(Core::Assets::AssetArena& arena, const Name& assetType, const Name& virtualPath = s_NameNone)noexcept
        : Core::Assets::IAsset(assetType, virtualPath)
        , m_entryPoint(arena)
        , m_bytecode(arena)
    {}
    IShader(const IShader&) = default;
    IShader(IShader&&)noexcept = default;

protected:
    IShader& operator=(const IShader&) = default;
    IShader& operator=(IShader&&)noexcept = default;


public:
    [[nodiscard]] const Core::Assets::AssetString& entryPoint()const noexcept{ return m_entryPoint; }
    [[nodiscard]] const Core::Assets::AssetBytes& bytecode()const noexcept{ return m_bytecode; }


protected:
    bool loadBinaryForStage(const Core::Assets::AssetBytes& binary, Core::ShaderType::Enum shaderType);


private:
    Core::Assets::AssetString m_entryPoint;
    Core::Assets::AssetBytes m_bytecode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ShaderT>
class TypedShader : public IShader{
public:
    explicit TypedShader(Core::Assets::AssetArena& arena)noexcept(noexcept(ShaderT::AssetTypeName()))
        : IShader(arena, ShaderT::AssetTypeName())
    {}
    TypedShader(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept(noexcept(ShaderT::AssetTypeName()))
        : IShader(arena, ShaderT::AssetTypeName(), virtualPath)
    {}
    TypedShader(const TypedShader&) = default;
    TypedShader(TypedShader&&)noexcept = default;
    virtual ~TypedShader()noexcept override = 0;

public:
    TypedShader& operator=(const TypedShader&) = default;
    TypedShader& operator=(TypedShader&&)noexcept = default;


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary){
        static_assert(Core::ShaderType::IsValid(ShaderT::s_Stage));
        return loadBinaryForStage(binary, ShaderT::s_Stage);
    }
};

template<typename ShaderT>
TypedShader<ShaderT>::~TypedShader()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VertexShader final : public TypedShader<VertexShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::VertexStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<VertexShader>::TypedShader;
};


class HullShader final : public TypedShader<HullShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::HullStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<HullShader>::TypedShader;
};


class DomainShader final : public TypedShader<DomainShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::DomainStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<DomainShader>::TypedShader;
};


class GeometryShader final : public TypedShader<GeometryShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::GeometryStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<GeometryShader>::TypedShader;
};


class PixelShader final : public TypedShader<PixelShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::PixelStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<PixelShader>::TypedShader;
};


class ComputeShader final : public TypedShader<ComputeShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::ComputeStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<ComputeShader>::TypedShader;
};


class AmplificationShader final : public TypedShader<AmplificationShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::AmplificationStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<AmplificationShader>::TypedShader;
};


class MeshShader final : public TypedShader<MeshShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::MeshStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<MeshShader>::TypedShader;
};


class RayGenerationShader final : public TypedShader<RayGenerationShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::RayGenerationStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<RayGenerationShader>::TypedShader;
};


class AnyHitShader final : public TypedShader<AnyHitShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::AnyHitStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<AnyHitShader>::TypedShader;
};


class ClosestHitShader final : public TypedShader<ClosestHitShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::ClosestHitStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<ClosestHitShader>::TypedShader;
};


class MissShader final : public TypedShader<MissShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::MissStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<MissShader>::TypedShader;
};


class IntersectionShader final : public TypedShader<IntersectionShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::IntersectionStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<IntersectionShader>::TypedShader;
};


class CallableShader final : public TypedShader<CallableShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::CallableStage;


public:
    [[nodiscard]] static constexpr const Name& AssetTypeName()noexcept{ return ShaderAssetTypes::AssetTypeNameFromShaderType(s_Stage); }


public:
    using TypedShader<CallableShader>::TypedShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderAssetSerialization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool Serialize(const IShader& shader, Core::Assets::AssetBytes& outBinary);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ShaderT>
class ShaderAssetCodec final : public Core::Assets::AssetCodec<ShaderT>{
public:
    ShaderAssetCodec()noexcept = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override{
        if(!this->checkSerializeAssetType(asset, NWB_TEXT("ShaderAssetCodec::serialize")))
            return false;

        return ShaderAssetSerialization::Serialize(*checked_cast<const ShaderT*>(&asset), outBinary);
    }
#endif
};

using VertexShaderAssetCodec = ShaderAssetCodec<VertexShader>;
using HullShaderAssetCodec = ShaderAssetCodec<HullShader>;
using DomainShaderAssetCodec = ShaderAssetCodec<DomainShader>;
using GeometryShaderAssetCodec = ShaderAssetCodec<GeometryShader>;
using PixelShaderAssetCodec = ShaderAssetCodec<PixelShader>;
using ComputeShaderAssetCodec = ShaderAssetCodec<ComputeShader>;
using AmplificationShaderAssetCodec = ShaderAssetCodec<AmplificationShader>;
using MeshShaderAssetCodec = ShaderAssetCodec<MeshShader>;
using RayGenerationShaderAssetCodec = ShaderAssetCodec<RayGenerationShader>;
using AnyHitShaderAssetCodec = ShaderAssetCodec<AnyHitShader>;
using ClosestHitShaderAssetCodec = ShaderAssetCodec<ClosestHitShader>;
using MissShaderAssetCodec = ShaderAssetCodec<MissShader>;
using IntersectionShaderAssetCodec = ShaderAssetCodec<IntersectionShader>;
using CallableShaderAssetCodec = ShaderAssetCodec<CallableShader>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

