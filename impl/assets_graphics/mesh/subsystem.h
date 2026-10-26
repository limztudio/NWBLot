// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VertexShader;
class ComputeShader;
class MeshShader;


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

    static constexpr SubsystemFieldDescriptor<MeshSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<MeshSubsystem, VertexShader, &MeshSubsystem::emulationVs>("emulation_vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, ComputeShader, &MeshSubsystem::objectDecodeCs>("object_decode_cs", "compute_shader"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, VertexShader, &MeshSubsystem::objectVs>("object_vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<MeshSubsystem, MeshShader, &MeshSubsystem::sharedMs>("shared_ms", "mesh_shader"),
    };
};

using MeshSubsystemAssetCodec = TypedSubsystemAssetCodec<MeshSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
