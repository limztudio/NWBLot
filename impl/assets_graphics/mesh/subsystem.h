// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshSubsystem final : public TypedSubsystem<MeshSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("mesh_subsystem")


public:
    using TypedSubsystem<MeshSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<VertexShader> emulationVs;
    Core::Assets::AssetRef<ComputeShader> objectDecodeCs;
    Core::Assets::AssetRef<VertexShader> objectVs;
    Core::Assets::AssetRef<MeshShader> sharedMs;


public:
    static constexpr SubsystemFieldDescriptor<MeshSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<MeshSubsystem, VertexShader, &MeshSubsystem::emulationVs>("emulation_vs"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, ComputeShader, &MeshSubsystem::objectDecodeCs>("object_decode_cs"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, VertexShader, &MeshSubsystem::objectVs>("object_vs"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, MeshShader, &MeshSubsystem::sharedMs>("shared_ms"),
    };
};

using MeshSubsystemAssetCodec = TypedSubsystemAssetCodec<MeshSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

