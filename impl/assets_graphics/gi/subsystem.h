// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GiSubsystem final : public TypedSubsystem<GiSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("gi_subsystem")


public:
    using TypedSubsystem<GiSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> surfelAgeFreeCs;
    Core::Assets::AssetRef<ComputeShader> surfelHashBuildCs;
    Core::Assets::AssetRef<ComputeShader> surfelResolveCs;
    Core::Assets::AssetRef<ComputeShader> surfelSpawnCs;
    Core::Assets::AssetRef<ComputeShader> surfelTraceBuildArgsCs;
    Core::Assets::AssetRef<ComputeShader> surfelTraceCs;
    Core::Assets::AssetRef<ComputeShader> surfelTraceHwCs;
    Core::Assets::AssetRef<ComputeShader> surfelUpsampleCs;

    static constexpr SubsystemFieldDescriptor<GiSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelAgeFreeCs>("surfel_age_free_cs"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelHashBuildCs>("surfel_hash_build_cs"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelResolveCs>("surfel_resolve_cs"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelSpawnCs>("surfel_spawn_cs"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceBuildArgsCs>("surfel_trace_build_args_cs"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceCs>("surfel_trace_cs"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceHwCs>("surfel_trace_hw_cs"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelUpsampleCs>("surfel_upsample_cs"),
    };
};

using GiSubsystemAssetCodec = TypedSubsystemAssetCodec<GiSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
