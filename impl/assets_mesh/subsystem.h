// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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


public:
    static constexpr SubsystemFieldDescriptor<SkinningSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::localBoundsCs>("local_bounds_cs"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::meshletBoundsCs>("meshlet_bounds_cs"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::repackNormalsCs>("repack_normals_cs"),
        MakeSubsystemFieldDescriptor<SkinningSubsystem, ComputeShader, &SkinningSubsystem::skinningCs>("skinning_cs"),
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

