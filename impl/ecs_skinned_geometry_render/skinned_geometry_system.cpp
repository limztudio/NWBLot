// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_system.h"

#include "skinned_geometry_runtime_mesh_cache.h"
#include "skinned_geometry_skin_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_manager.h>
#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_geometry/skinned_geometry_asset.h>
#include <impl/assets_shader/shader_asset_loader.h>
#include <impl/ecs_render/components.h>
#include <core/common/log.h>

#include "skinned_geometry_runtime_resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_SkinnedGeometryGroupSize = 64u;
static constexpr u32 s_SkinnedGeometryVertexWordStride = sizeof(SkinnedGeometryVertex) / sizeof(u32);

struct SkinnedGeometryPushConstants{
    u32 vertexCount = 0;
    u32 restWordStride = 0;
    u32 skinnedWordStride = 0;
    u32 skinCount = 0;
    u32 jointCount = 0;
    u32 skinningMode = SkinnedGeometrySkinningMode::LinearBlend;
    u32 padding0 = 0;
    u32 padding1 = 0;
};
static_assert(sizeof(SkinnedGeometryPushConstants) == 32, "SkinnedGeometry push constants layout must stay shader-compatible");

static const Name& SkinnedGeometryComputeShaderName(){
    static const Name s("engine/graphics/skinned_geometry_cs");
    return s;
}

static bool HasPotentialSkinnedGeometryWork(
    const SkinnedGeometryRuntimeMeshInstance& instance,
    const SkinnedGeometryJointPaletteComponent* jointPalette,
    const SkinnedGeometrySkeletonPoseComponent* skeletonPose
){
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty) != 0u)
        return true;

    return
        !instance.skin.empty()
        && ((jointPalette && !jointPalette->joints.empty()) || SkinnedGeometryRuntime::HasSkeletonPose(skeletonPose))
    ;
}

static u32 DispatchGroupCount(const u32 vertexCount){
    return DivideUp(vertexCount, s_SkinnedGeometryGroupSize);
}

static bool RuntimeMeshRenderVisible(Core::ECS::World& world, const Core::ECS::EntityID entity){
    const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
    return renderer && renderer->visible;
}

static bool BufferPayloadBytes(const usize count, const usize stride, usize& outBytes, const tchar* label){
    outBytes = 0;
    if(stride == 0u || count > Limit<usize>::s_Max / stride){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: {} payload byte size overflows"), label);
        return false;
    }

    outBytes = count * stride;
    return true;
}

template<typename PayloadT>
static Core::BufferHandle SetupStructuredBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadT* payload,
    const usize count,
    const tchar* label
){
    usize payloadBytes = 0;
    if(!BufferPayloadBytes(count, sizeof(PayloadT), payloadBytes, label))
        return {};

    Core::Graphics::BufferSetupDesc setup;
    setup.bufferDesc
        .setByteSize(static_cast<u64>(payloadBytes))
        .setStructStride(sizeof(PayloadT))
        .setDebugName(debugName)
    ;
    setup.data = payload;
    setup.dataSize = payloadBytes;
    return graphics.setupBuffer(setup);
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SkinnedGeometrySystem::SkinnedGeometrySystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    IRuntimeGeometryRegistry& runtimeGeometryRegistry,
    ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_runtimeGeometryRegistry(runtimeGeometryRegistry)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_runtimeMeshCache(Core::MakeGlobalUnique<SkinnedGeometryRuntimeMeshCache>(arena, arena, graphics, assetManager))
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
{
    writeAccess<SkinnedGeometryComponent>();
    readAccess<RendererComponent>();
    readAccess<SkinnedGeometryJointPaletteComponent>();
    readAccess<SkinnedGeometrySkeletonPoseComponent>();

    m_runtimeGeometryRegistry.registerRuntimeGeometryProvider(*this);
}

SkinnedGeometrySystem::~SkinnedGeometrySystem()
{
    m_runtimeGeometryRegistry.unregisterRuntimeGeometryProvider(*this);
}

void SkinnedGeometrySystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(delta);
    m_runtimeMeshCache->update(world);
}

usize SkinnedGeometrySystem::runtimeGeometryCandidateCount(){
    return m_world.view<SkinnedGeometryComponent>().candidateCount();
}

void SkinnedGeometrySystem::forEachRuntimeGeometry(const RuntimeGeometryVisitor& visitor){
    m_world.view<SkinnedGeometryComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedGeometryComponent& component){
            static_cast<void>(component);
            RuntimeGeometryDesc desc;
            if(!resolveRuntimeGeometry(entity, desc))
                return;

            visitor(desc);
        }
    );
}

bool SkinnedGeometrySystem::resolveRuntimeGeometry(const Core::ECS::EntityID entity, RuntimeGeometryDesc& outGeometry){
    outGeometry = RuntimeGeometryDesc{};
    if(!__hidden_skinned_geometry_system::RuntimeMeshRenderVisible(m_world, entity))
        return false;

    const SkinnedGeometryComponent* renderer = m_world.tryGetComponent<SkinnedGeometryComponent>(entity);
    if(!renderer || !renderer->runtimeMesh.valid())
        return false;

    const SkinnedGeometryRuntimeMeshInstance* instance = m_runtimeMeshCache->findInstance(renderer->runtimeMesh);
    if(!instance || !instance->valid() || instance->entity != entity)
        return false;
    if(instance->indices.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outGeometry.entity = entity;
    outGeometry.geometryKey = DeriveRuntimeResourceName(
        instance->source.name(),
        instance->handle.value,
        instance->editRevision,
        "skinned_draw"
    );
    outGeometry.shaderVertexBuffer = instance->skinnedVertexBuffer;
    outGeometry.shaderIndexBuffer = instance->indexBuffer;
    outGeometry.indexCount = static_cast<u32>(instance->indices.size());
    outGeometry.sourceVertexLayout = MeshSourceLayout::SkinnedGeometryRuntime;
    outGeometry.version = instance->editRevision;
    return outGeometry.valid();
}

bool SkinnedGeometrySystem::containsRuntimeGeometry(const Name& geometryKey, const u64 version){
    if(!geometryKey)
        return false;

    bool found = false;
    forEachRuntimeGeometry(
        [&](const RuntimeGeometryDesc& desc){
            if(found)
                return;
            found = desc.geometryKey == geometryKey && desc.version == version;
        }
    );
    return found;
}

void SkinnedGeometrySystem::render(Core::IFramebuffer* framebuffer){
    static_cast<void>(framebuffer);

    if(!m_runtimeResources.empty()){
        for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
            const RuntimeResources& resources = it.value();
            const SkinnedGeometryRuntimeMeshInstance* instance =
                m_runtimeMeshCache->findInstance(resources.handle)
            ;
            if(!instance || !instance->valid() || instance->editRevision != resources.editRevision){
                it = m_runtimeResources.erase(it);
                continue;
            }

            ++it;
        }
    }

    Core::IDevice* device = m_graphics.getDevice();
    Core::CommandListHandle commandList;
    bool commandListOpen = false;
    bool commandListFailed = false;
    bool submittedWork = false;

    auto ensureCommandList = [&]() -> bool{
        if(commandListOpen)
            return true;
        if(commandListFailed)
            return false;

        commandList = device->createCommandList();
        if(!commandList){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create command list"));
            commandListFailed = true;
            return false;
        }

        commandList->open();
        commandListOpen = true;
        return true;
    };

    m_world.view<SkinnedGeometryComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedGeometryComponent& renderer){
            if(!__hidden_skinned_geometry_system::RuntimeMeshRenderVisible(m_world, entity) || !renderer.runtimeMesh.valid())
                return;

            SkinnedGeometryRuntimeMeshInstance* instance =
                m_runtimeMeshCache->findInstance(renderer.runtimeMesh)
            ;
            if(!instance || !instance->valid())
                return;

            const SkinnedGeometryJointPaletteComponent* jointPalette =
                m_world.tryGetComponent<SkinnedGeometryJointPaletteComponent>(entity)
            ;
            const SkinnedGeometrySkeletonPoseComponent* skeletonPose =
                m_world.tryGetComponent<SkinnedGeometrySkeletonPoseComponent>(entity)
            ;
            if(!__hidden_skinned_geometry_system::HasPotentialSkinnedGeometryWork(
                *instance,
                jointPalette,
                skeletonPose
            ))
                return;
            if(!ensureCommandList())
                return;
            if(dispatchRuntimeMesh(*commandList, *instance, jointPalette, skeletonPose))
                submittedWork = true;
        }
    );

    if(!commandListOpen)
        return;

    commandList->close();

    if(submittedWork){
        Core::ICommandList* commandLists[] = { commandList.get() };
        device->executeCommandLists(commandLists, 1);
    }
}

void SkinnedGeometrySystem::invalidateResources(){
    m_runtimeResources.clear();
    m_runtimeMeshCache->clear();

    m_bindingLayout.reset();
    m_computeShader.reset();
    m_computePipeline.reset();
    m_defaultSkinBuffer.reset();
    m_defaultJointPaletteBuffer.reset();
}

bool SkinnedGeometrySystem::ensurePipeline(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_bindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
        bindingLayoutDesc.addItem(
            Core::BindingLayoutItem::PushConstants(
                0,
                sizeof(__hidden_skinned_geometry_system::SkinnedGeometryPushConstants)
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
        __hidden_skinned_geometry_system::SkinnedGeometryComputeShaderName(),
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
    Core::Alloc::ScratchArena<> scratchArena;
    Vector<SkinnedGeometrySkinInfluenceGpu, Core::Alloc::ScratchArena<>> skinInfluences{
        scratchArena
    };
    Vector<SkinnedGeometryJointMatrix, Core::Alloc::ScratchArena<>> jointMatrices{
        scratchArena
    };
    u32 resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkinnedGeometrySkinningMode::LinearBlend;
    if(SkinnedGeometryRuntime::HasSkeletonPose(skeletonPose)){
        Vector<SkinnedGeometryJointMatrix, Core::Alloc::ScratchArena<>> poseJoints{
            scratchArena
        };
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
    if(!m_computePipeline)
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
    if(hasActiveSkin && !resourcesRebuilt){
        if(!__hidden_skinned_geometry_system::BufferPayloadBytes(
            jointMatrices.size(),
            sizeof(SkinnedGeometryJointMatrix),
            jointPaletteBytes,
            NWB_TEXT("joint palette")
        ))
            return false;

        commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::CopyDest);
    }
    if(hasActiveSkin && !resourcesRebuilt){
        commandList.commitBarriers();
        commandList.writeBuffer(resources->jointPaletteBuffer.get(), jointMatrices.data(), jointPaletteBytes);
    }

    commandList.setBufferState(instance.restVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.skinnedVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(resources->skinBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_computePipeline.get());
    computeState.addBindingSet(resources->bindingSet.get());
    commandList.setComputeState(computeState);

    __hidden_skinned_geometry_system::SkinnedGeometryPushConstants pushConstants;
    pushConstants.vertexCount = static_cast<u32>(instance.restVertices.size());
    pushConstants.restWordStride = __hidden_skinned_geometry_system::s_SkinnedGeometryVertexWordStride;
    pushConstants.skinnedWordStride = __hidden_skinned_geometry_system::s_SkinnedGeometryVertexWordStride;
    pushConstants.skinCount = static_cast<u32>(skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(jointMatrices.size());
    pushConstants.skinningMode = hasActiveSkin
        ? resolvedSkinningMode
        : SkinnedGeometrySkinningMode::LinearBlend
    ;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(__hidden_skinned_geometry_system::DispatchGroupCount(pushConstants.vertexCount), 1, 1);

    commandList.setBufferState(instance.skinnedVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
    );
    return true;
}

bool SkinnedGeometrySystem::copyRestToSkinned(Core::ICommandList& commandList, SkinnedGeometryRuntimeMeshInstance& instance){
    usize copyBytes = 0;
    if(!__hidden_skinned_geometry_system::BufferPayloadBytes(
        instance.restVertices.size(),
        sizeof(SkinnedGeometryVertex),
        copyBytes,
        NWB_TEXT("rest vertex")
    ))
        return false;

    commandList.setBufferState(instance.restVertexBuffer.get(), Core::ResourceStates::CopySource);
    commandList.setBufferState(instance.skinnedVertexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.copyBuffer(instance.skinnedVertexBuffer.get(), 0, instance.restVertexBuffer.get(), 0, copyBytes);
    commandList.setBufferState(instance.skinnedVertexBuffer.get(), Core::ResourceStates::ShaderResource);
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
    if(!ensureDefaultSkinnedGeometryBuffers())
        return false;

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
        || resources.vertexCount != static_cast<u32>(instance.restVertices.size())
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
    rebuilt.vertexCount = static_cast<u32>(instance.restVertices.size());
    rebuilt.skinCount = static_cast<u32>(payloadViews.skinInfluenceCount);
    rebuilt.jointCount = static_cast<u32>(payloadViews.jointPaletteCount);

    rebuilt.skinBuffer = __hidden_skinned_geometry_system::SetupStructuredBuffer(
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

    rebuilt.jointPaletteBuffer = __hidden_skinned_geometry_system::SetupStructuredBuffer(
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
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, instance.restVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, instance.skinnedVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(4, rebuilt.skinBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, rebuilt.jointPaletteBuffer.get()));

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

bool SkinnedGeometrySystem::ensureDefaultSkinnedGeometryBuffers(){
    if(
        m_defaultSkinBuffer
        && m_defaultJointPaletteBuffer
    )
        return true;

    const SkinnedGeometrySkinInfluenceGpu defaultSkin{};
    const SkinnedGeometryJointMatrix defaultJoint = MakeIdentitySkinnedGeometryJointMatrix();

    if(!m_defaultSkinBuffer){
        m_defaultSkinBuffer = __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/skinned_geometry_default_skin"),
            &defaultSkin,
            1u,
            NWB_TEXT("default skin")
        );
        if(!m_defaultSkinBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create default skin buffer"));
            return false;
        }
    }

    if(!m_defaultJointPaletteBuffer){
        m_defaultJointPaletteBuffer = __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/skinned_geometry_default_joint"),
            &defaultJoint,
            1u,
            NWB_TEXT("default joint")
        );
        if(!m_defaultJointPaletteBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create default joint palette buffer"));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

