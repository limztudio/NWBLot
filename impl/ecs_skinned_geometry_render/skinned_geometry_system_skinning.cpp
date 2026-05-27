// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_system.h"

#include "skinned_geometry_runtime_mesh_cache.h"
#include "skinned_geometry_skin_payload.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_shader/shader_asset_loader.h>
#include <impl/ecs_geometry/runtime_geometry_buffer_upload.h>

#include "skinned_geometry_runtime_resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_system_skinning{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_SkinnedGeometryGroupSize = 64u;

struct SkinnedGeometryPushConstants{
    u32 vertexCount = 0;
    u32 skinCount = 0;
    u32 jointCount = 0;
    u32 skinningMode = SkinnedGeometrySkinningMode::LinearBlend;
    u32 padding0 = 0;
    u32 padding1 = 0;
    u32 padding2 = 0;
    u32 padding3 = 0;
};
static_assert(sizeof(SkinnedGeometryPushConstants) == 32, "SkinnedGeometry push constants layout must match the shader ABI");

static const Name& SkinnedGeometryComputeShaderName(){
    static const Name s("engine/graphics/skinned_geometry/skinning_cs");
    return s;
}

static u32 DispatchGroupCount(const u32 vertexCount){
    return DivideUp(vertexCount, s_SkinnedGeometryGroupSize);
}

static bool BufferPayloadBytes(const usize count, const usize stride, usize& outBytes, const tchar* label){
    if(RuntimeGeometryBufferUpload::PayloadByteCount(count, stride, outBytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: {} payload byte size overflows"), label);
    return false;
}

template<typename PayloadT>
static Core::BufferHandle SetupStructuredBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadT* payload,
    const usize count,
    const tchar* label
){
    if(!RuntimeGeometryBufferUpload::PayloadByteCountFits<PayloadT>(count)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: {} payload byte size overflows"), label);
        return {};
    }

    return RuntimeGeometryBufferUpload::SetupBuffer<PayloadT>(
        graphics,
        debugName,
        payload,
        count
    );
}

static bool ResolveRestBufferBytes(
    const SkinnedGeometryRuntimeMeshInstance& instance,
    usize& outPositionBytes,
    usize& outNormalBytes,
    usize& outTangentBytes
){
    outPositionBytes = 0u;
    outNormalBytes = 0u;
    outTangentBytes = 0u;
    return
        BufferPayloadBytes(
            instance.restPositions.size(),
            sizeof(Float3U),
            outPositionBytes,
            NWB_TEXT("rest position")
        )
        && BufferPayloadBytes(
            instance.restNormals.size(),
            sizeof(Half4U),
            outNormalBytes,
            NWB_TEXT("rest normal")
        )
        && BufferPayloadBytes(
            instance.restTangents.size(),
            sizeof(Half4U),
            outTangentBytes,
            NWB_TEXT("rest tangent")
        )
    ;
}

static void SetRestBufferStates(
    Core::ICommandList& commandList,
    SkinnedGeometryRuntimeMeshInstance& instance,
    const Core::ResourceStates::Mask state
){
    commandList.setBufferState(instance.restPositionBuffer.get(), state);
    commandList.setBufferState(instance.restNormalBuffer.get(), state);
    commandList.setBufferState(instance.restTangentBuffer.get(), state);
}

static void SetSkinnedBufferStates(
    Core::ICommandList& commandList,
    SkinnedGeometryRuntimeMeshInstance& instance,
    const Core::ResourceStates::Mask state
){
    commandList.setBufferState(instance.skinnedPositionBuffer.get(), state);
    commandList.setBufferState(instance.skinnedNormalBuffer.get(), state);
    commandList.setBufferState(instance.skinnedTangentBuffer.get(), state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedGeometrySystem::ensurePipeline(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_bindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(6, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(7, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(8, 1));
        bindingLayoutDesc.addItem(
            Core::BindingLayoutItem::PushConstants(
                0,
                sizeof(__hidden_skinned_geometry_system_skinning::SkinnedGeometryPushConstants)
            )
        );

        m_bindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_bindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create binding layout"));
            return false;
        }
    }

    if(!ShaderAssetLoader::Load(
        m_computeShader,
        __hidden_skinned_geometry_system_skinning::SkinnedGeometryComputeShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        Name("ECSSkinnedGeometryRender_SkinnedGeometryCS"),
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        NWB_TEXT("SkinnedGeometrySystem")
    ))
        return false;

    if(m_computePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_computeShader);
    pipelineDesc.addBindingLayout(m_bindingLayout);
    m_computePipeline = device->createComputePipeline(pipelineDesc);
    if(!m_computePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create compute pipeline"));
        return false;
    }

    return true;
}

bool SkinnedGeometrySystem::dispatchRuntimeMesh(
    Core::ICommandList& commandList,
    SkinnedGeometryRuntimeMeshInstance& instance,
    const SkinnedGeometryJointPaletteComponent* jointPalette,
    const SkinnedGeometrySkeletonPoseComponent* skeletonPose
){
    Core::Alloc::ScratchArena scratchArena;
    Vector<SkinnedGeometrySkinInfluenceGpu, Core::Alloc::ScratchArena> skinInfluences{ scratchArena };
    Vector<SkinnedGeometryJointMatrix, Core::Alloc::ScratchArena> jointMatrices{ scratchArena };
    u32 resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkinnedGeometrySkinningMode::LinearBlend;
    if(SkinnedGeometryRuntime::HasSkeletonPose(skeletonPose)){
        Vector<SkinnedGeometryJointMatrix, Core::Alloc::ScratchArena> poseJoints{ scratchArena };
        if(!SkinnedGeometryRuntime::BuildJointPaletteFromSkeletonPose(*skeletonPose, poseJoints, resolvedSkinningMode)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' skeleton pose is invalid"), instance.handle.value);
            return false;
        }
        if(!SkinnedGeometrySkinPayload::BuildSkinPayloadFromJointMatrices(
            instance,
            poseJoints,
            resolvedSkinningMode,
            skinInfluences,
            jointMatrices
        ))
            return false;
    }
    else{
        if(!SkinnedGeometrySkinPayload::BuildSkinPayload(instance, jointPalette, skinInfluences, jointMatrices))
            return false;
    }

    const bool hasActiveSkin = !skinInfluences.empty() && !jointMatrices.empty();
    if(!hasActiveSkin){
        const bool skinnedGeometryInputDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty) != 0u;
        const auto foundResources = m_runtimeResources.find(instance.handle.value);
        if(foundResources == m_runtimeResources.end() && !skinnedGeometryInputDirty)
            return false;

        return copyRestToSkinned(commandList, instance);
    }
    if(!ensurePipeline())
        return false;

    RuntimePayloadViews payloadViews;
    payloadViews.skinInfluences = skinInfluences.data();
    payloadViews.jointPalette = jointMatrices.data();
    payloadViews.skinInfluenceCount = skinInfluences.size();
    payloadViews.jointPaletteCount = jointMatrices.size();

    RuntimeResources* resources = nullptr;
    bool resourcesRebuilt = false;
    if(!ensureRuntimeResources(
        instance,
        payloadViews,
        resources,
        resourcesRebuilt
    ))
        return false;
    if(
        !resources
        || !resources->bindingSet
        || !resources->skinBuffer
        || !resources->jointPaletteBuffer
    )
        return false;

    usize jointPaletteBytes = 0;
    if(!resourcesRebuilt){
        if(!__hidden_skinned_geometry_system_skinning::BufferPayloadBytes(
            jointMatrices.size(),
            sizeof(SkinnedGeometryJointMatrix),
            jointPaletteBytes,
            NWB_TEXT("joint palette")
        ))
            return false;

        commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.writeBuffer(resources->jointPaletteBuffer.get(), jointMatrices.data(), jointPaletteBytes);
    }

    __hidden_skinned_geometry_system_skinning::SetRestBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    __hidden_skinned_geometry_system_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::UnorderedAccess
    );
    commandList.setBufferState(instance.vertexRefBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->skinBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_computePipeline.get());
    computeState.addBindingSet(resources->bindingSet.get());
    commandList.setComputeState(computeState);

    __hidden_skinned_geometry_system_skinning::SkinnedGeometryPushConstants pushConstants;
    pushConstants.vertexCount = static_cast<u32>(instance.vertexRefs.size());
    pushConstants.skinCount = static_cast<u32>(skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(jointMatrices.size());
    pushConstants.skinningMode = resolvedSkinningMode;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(__hidden_skinned_geometry_system_skinning::DispatchGroupCount(pushConstants.vertexCount), 1, 1);

    __hidden_skinned_geometry_system_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    commandList.commitBarriers();

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
    );
    return true;
}

bool SkinnedGeometrySystem::copyRestToSkinned(Core::ICommandList& commandList, SkinnedGeometryRuntimeMeshInstance& instance){
    usize positionBytes = 0;
    usize normalBytes = 0;
    usize tangentBytes = 0;
    if(!__hidden_skinned_geometry_system_skinning::ResolveRestBufferBytes(
        instance,
        positionBytes,
        normalBytes,
        tangentBytes
    ))
        return false;

    __hidden_skinned_geometry_system_skinning::SetRestBufferStates(
        commandList,
        instance,
        Core::ResourceStates::CopySource
    );
    __hidden_skinned_geometry_system_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::CopyDest
    );
    commandList.commitBarriers();
    commandList.copyBuffer(instance.skinnedPositionBuffer.get(), 0, instance.restPositionBuffer.get(), 0, positionBytes);
    commandList.copyBuffer(instance.skinnedNormalBuffer.get(), 0, instance.restNormalBuffer.get(), 0, normalBytes);
    commandList.copyBuffer(instance.skinnedTangentBuffer.get(), 0, instance.restTangentBuffer.get(), 0, tangentBytes);
    __hidden_skinned_geometry_system_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    commandList.commitBarriers();

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
    );
    return true;
}

bool SkinnedGeometrySystem::ensureRuntimeResources(
    SkinnedGeometryRuntimeMeshInstance& instance,
    const RuntimePayloadViews& payloadViews,
    RuntimeResources*& outResources,
    bool& outResourcesRebuilt
){
    outResources = nullptr;
    outResourcesRebuilt = false;
    if((payloadViews.skinInfluenceCount == 0u) != (payloadViews.jointPaletteCount == 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has mismatched skin influence/joint payloads"), instance.handle.value);
        return false;
    }

    const bool hasActiveSkin = payloadViews.hasActiveSkin();
    if(!hasActiveSkin)
        return false;

    if(!payloadViews.skinInfluences || !payloadViews.jointPalette){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has null active skinned geometry payloads"), instance.handle.value);
        return false;
    }

    if(
        payloadViews.skinInfluenceCount > static_cast<usize>(Limit<u32>::s_Max)
        || payloadViews.jointPaletteCount > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' skinned geometry payload exceeds u32 limits"), instance.handle.value);
        return false;
    }

    auto [it, inserted] = m_runtimeResources.try_emplace(instance.handle.value);
    RuntimeResources& resources = it.value();
    const bool rebuild =
        inserted
        || resources.editRevision != instance.editRevision
        || resources.vertexCount != static_cast<u32>(instance.vertexRefs.size())
        || resources.skinCount != static_cast<u32>(payloadViews.skinInfluenceCount)
        || resources.jointCount != static_cast<u32>(payloadViews.jointPaletteCount)
        || !resources.skinBuffer
        || !resources.jointPaletteBuffer
        || !resources.bindingSet
    ;
    if(!rebuild){
        outResources = &resources;
        return true;
    }

    const Name skinBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_geometry_skin");
    const Name jointPaletteBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_geometry_joints");

    if(!skinBufferName || !jointPaletteBufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to derive skinned geometry buffer names for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    RuntimeResources rebuilt;
    rebuilt.handle = instance.handle;
    rebuilt.editRevision = instance.editRevision;
    rebuilt.vertexCount = static_cast<u32>(instance.vertexRefs.size());
    rebuilt.skinCount = static_cast<u32>(payloadViews.skinInfluenceCount);
    rebuilt.jointCount = static_cast<u32>(payloadViews.jointPaletteCount);

    rebuilt.skinBuffer = __hidden_skinned_geometry_system_skinning::SetupStructuredBuffer(
        m_graphics,
        skinBufferName,
        payloadViews.skinInfluences,
        payloadViews.skinInfluenceCount,
        NWB_TEXT("skin influence")
    )
    ;
    if(!rebuilt.skinBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create skin buffer for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    rebuilt.jointPaletteBuffer = __hidden_skinned_geometry_system_skinning::SetupStructuredBuffer(
        m_graphics,
        jointPaletteBufferName,
        payloadViews.jointPalette,
        payloadViews.jointPaletteCount,
        NWB_TEXT("joint palette")
    )
    ;
    if(!rebuilt.jointPaletteBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create joint palette buffer for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    Core::BindingSetDesc bindingSetDesc(m_arena);
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, instance.restPositionBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, instance.skinnedPositionBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, instance.restNormalBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(3, instance.skinnedNormalBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(4, instance.restTangentBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(5, instance.skinnedTangentBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(6, instance.vertexRefBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(7, rebuilt.skinBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(8, rebuilt.jointPaletteBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    rebuilt.bindingSet = device->createBindingSet(bindingSetDesc, m_bindingLayout);
    if(!rebuilt.bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create binding set for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    resources = Move(rebuilt);
    outResources = &resources;
    outResourcesRebuilt = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

