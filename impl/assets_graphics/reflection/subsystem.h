// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReflectionSubsystem final : public TypedSubsystem<ReflectionSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("reflection_subsystem")


public:
    using TypedSubsystem<ReflectionSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> buildArgsCs;
    Core::Assets::AssetRef<ComputeShader> classifyCs;
    Core::Assets::AssetRef<ComputeShader> depthReduceCs;
    Core::Assets::AssetRef<ComputeShader> resolveHwCs;
    Core::Assets::AssetRef<ComputeShader> spatialCs;
    Core::Assets::AssetRef<ComputeShader> temporalCs;

    static constexpr SubsystemFieldDescriptor<ReflectionSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::buildArgsCs>("build_args_cs"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::classifyCs>("classify_cs"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::depthReduceCs>("depth_reduce_cs"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::resolveHwCs>("resolve_hw_cs"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::spatialCs>("spatial_cs"),
        MakeSubsystemFieldDescriptor<ReflectionSubsystem, ComputeShader, &ReflectionSubsystem::temporalCs>("temporal_cs"),
    };
};

using ReflectionSubsystemAssetCodec = TypedSubsystemAssetCodec<ReflectionSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
