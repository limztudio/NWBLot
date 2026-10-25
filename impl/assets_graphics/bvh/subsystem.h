// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComputeShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BvhSubsystem final : public TypedSubsystem<BvhSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("bvh_subsystem")

    static constexpr usize s_FieldCount = 5u;


public:
    using TypedSubsystem<BvhSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> bitonicSortCs;
    Core::Assets::AssetRef<ComputeShader> fitCs;
    Core::Assets::AssetRef<ComputeShader> mortonCs;
    Core::Assets::AssetRef<ComputeShader> topologyCs;
    Core::Assets::AssetRef<ComputeShader> sceneRefitCs;

    static constexpr SubsystemFieldDescriptor<BvhSubsystem> s_Fields[s_FieldCount] = {
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::bitonicSortCs>("bitonic_sort_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::fitCs>("fit_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::mortonCs>("morton_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::topologyCs>("topology_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<BvhSubsystem, ComputeShader, &BvhSubsystem::sceneRefitCs>("scene_refit_cs", "compute_shader"),
    };
};

using BvhSubsystemAssetCodec = TypedSubsystemAssetCodec<BvhSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
