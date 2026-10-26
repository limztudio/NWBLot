// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComputeShader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinningSubsystem final : public TypedSubsystem<SkinningSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("skinning_subsystem")


public:
    using TypedSubsystem<SkinningSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<ComputeShader> localBoundsCs;
    Core::Assets::AssetRef<ComputeShader> meshletBoundsCs;
    Core::Assets::AssetRef<ComputeShader> repackNormalsCs;
    Core::Assets::AssetRef<ComputeShader> skinningCs;

    static constexpr SubsystemFieldDescriptor<SkinningSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::localBoundsCs>("local_bounds_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::meshletBoundsCs>("meshlet_bounds_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::repackNormalsCs>("repack_normals_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::skinningCs>("skinning_cs", "compute_shader"),
    };
};

using SkinningSubsystemAssetCodec = TypedSubsystemAssetCodec<SkinningSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
