// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/graphics/rhi/shader.h>


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
    bool loadBinaryForStage(Span<const u8> binary, Core::ShaderType::Enum shaderType);


private:
    Core::Assets::AssetString m_entryPoint;
    Core::Assets::AssetBytes m_bytecode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ShaderT>
class TypedShader : public IShader{
public:
    explicit TypedShader(Core::Assets::AssetArena& arena)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(ShaderT::AssetTypeName())))
        : IShader(arena, ShaderT::AssetTypeName())
    {}
    TypedShader(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(ShaderT::AssetTypeName())))
        : IShader(arena, ShaderT::AssetTypeName(), virtualPath)
    {}
    TypedShader(const TypedShader&) = default;
    TypedShader(TypedShader&&)noexcept = default;
    virtual ~TypedShader()noexcept override = 0;


public:
    TypedShader& operator=(const TypedShader&) = default;

    TypedShader& operator=(TypedShader&&)noexcept = default;


public:
    bool loadBinary(const Span<const u8> binary){
        static_assert(Core::ShaderType::IsValid(ShaderT::s_Stage));
        return loadBinaryForStage(binary, ShaderT::s_Stage);
    }
};

template<typename ShaderT>
TypedShader<ShaderT>::~TypedShader()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VertexShader final : public TypedShader<VertexShader>{
public:
    NWB_DEFINE_ASSET_TYPE("vertex_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::VertexStage;


public:
    using TypedShader<VertexShader>::TypedShader;
};


class HullShader final : public TypedShader<HullShader>{
public:
    NWB_DEFINE_ASSET_TYPE("hull_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::HullStage;


public:
    using TypedShader<HullShader>::TypedShader;
};


class DomainShader final : public TypedShader<DomainShader>{
public:
    NWB_DEFINE_ASSET_TYPE("domain_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::DomainStage;


public:
    using TypedShader<DomainShader>::TypedShader;
};


class GeometryShader final : public TypedShader<GeometryShader>{
public:
    NWB_DEFINE_ASSET_TYPE("geometry_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::GeometryStage;


public:
    using TypedShader<GeometryShader>::TypedShader;
};


class PixelShader final : public TypedShader<PixelShader>{
public:
    NWB_DEFINE_ASSET_TYPE("pixel_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::PixelStage;


public:
    using TypedShader<PixelShader>::TypedShader;
};


class ComputeShader final : public TypedShader<ComputeShader>{
public:
    NWB_DEFINE_ASSET_TYPE("compute_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::ComputeStage;


public:
    using TypedShader<ComputeShader>::TypedShader;
};


class AmplificationShader final : public TypedShader<AmplificationShader>{
public:
    NWB_DEFINE_ASSET_TYPE("amplification_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::AmplificationStage;


public:
    using TypedShader<AmplificationShader>::TypedShader;
};


class MeshShader final : public TypedShader<MeshShader>{
public:
    NWB_DEFINE_ASSET_TYPE("mesh_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::MeshStage;


public:
    using TypedShader<MeshShader>::TypedShader;
};


class RayGenerationShader final : public TypedShader<RayGenerationShader>{
public:
    NWB_DEFINE_ASSET_TYPE("ray_generation_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::RayGenerationStage;


public:
    using TypedShader<RayGenerationShader>::TypedShader;
};


class AnyHitShader final : public TypedShader<AnyHitShader>{
public:
    NWB_DEFINE_ASSET_TYPE("any_hit_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::AnyHitStage;


public:
    using TypedShader<AnyHitShader>::TypedShader;
};


class ClosestHitShader final : public TypedShader<ClosestHitShader>{
public:
    NWB_DEFINE_ASSET_TYPE("closest_hit_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::ClosestHitStage;


public:
    using TypedShader<ClosestHitShader>::TypedShader;
};


class MissShader final : public TypedShader<MissShader>{
public:
    NWB_DEFINE_ASSET_TYPE("miss_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::MissStage;


public:
    using TypedShader<MissShader>::TypedShader;
};


class IntersectionShader final : public TypedShader<IntersectionShader>{
public:
    NWB_DEFINE_ASSET_TYPE("intersection_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::IntersectionStage;


public:
    using TypedShader<IntersectionShader>::TypedShader;
};


class CallableShader final : public TypedShader<CallableShader>{
public:
    NWB_DEFINE_ASSET_TYPE("callable_shader")

    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::CallableStage;


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
    ShaderAssetCodec()noexcept(noexcept(DeclVal<void(*)(const Name&)noexcept>()(ShaderT::AssetTypeName()))) = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override{
        if(!this->checkSerializeAssetType(asset, NWB_TEXT("ShaderAssetCodec::serialize")))
            return false;

        return ShaderAssetSerialization::Serialize(*MakeNotNull(checked_cast<const ShaderT*>(&asset)), outBinary);
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
