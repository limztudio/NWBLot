// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComputeShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GiSubsystem final : public TypedSubsystem<GiSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("gi_subsystem")

    static constexpr usize s_FieldCount = 8u;


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

    static constexpr SubsystemFieldDescriptor<GiSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelAgeFreeCs>("surfel_age_free_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelHashBuildCs>("surfel_hash_build_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelResolveCs>("surfel_resolve_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelSpawnCs>("surfel_spawn_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceBuildArgsCs>("surfel_trace_build_args_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceCs>("surfel_trace_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelTraceHwCs>("surfel_trace_hw_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<GiSubsystem, ComputeShader, &GiSubsystem::surfelUpsampleCs>("surfel_upsample_cs", "compute_shader"),
    };
};

using GiSubsystemAssetCodec = TypedSubsystemAssetCodec<GiSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
