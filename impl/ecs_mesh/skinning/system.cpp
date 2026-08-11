// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "arena_names.h"
#include "resource_names.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <impl/ecs_skeleton/runtime_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasPotentialSkinningWork(
    const MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose
){
    if((instance.dirtyFlags & (RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return true;

    return
        !instance.skin.empty()
        && (
            (jointPalette && !jointPalette->joints.empty())
            || SkeletonRuntime::HasSkeletonPose(skeletonPose)
        )
    ;
}

static void ResolveSkeletonComponents(
    Core::ECS::World& world,
    const Core::ECS::EntityID fallbackEntity,
    const Core::ECS::EntityID skeletonEntity,
    const SkeletonJointPaletteComponent*& outJointPalette,
    const SkeletonPoseComponent*& outSkeletonPose
){
    const Core::ECS::EntityID resolvedEntity = skeletonEntity.valid() ? skeletonEntity : fallbackEntity;
    outJointPalette = world.tryGetComponent<SkeletonJointPaletteComponent>(resolvedEntity);
    outSkeletonPose = world.tryGetComponent<SkeletonPoseComponent>(resolvedEntity);
}

static constexpr bool s_RuntimeSkinningMeshletFrustumCullingEnabled = true;
static constexpr bool s_RuntimeSkinningMeshletConeCullingEnabled = false; // Runtime deformations can make meshlet cones unsafe; benchmarks override through a test provider only.

struct PendingRuntimeMeshSubmission{
    RuntimeMeshHandle handle;
    MeshSkinningSubmissionCommit commit;
};

[[nodiscard]] static Core::GpuQueueRequest JointPaletteUploadQueueRequest(){
    Core::GpuQueueRequest request;
    request.requiredCapabilities = Core::GpuQueueCapability::Transfer;
    request.preferredQueue = Core::GpuQueuePreference::Graphics;
    request.allowFallback = false;
    // The following legacy compute list opens from this packet's final handoff. Keep both submissions on the
    // primary Graphics transport until skinning itself becomes a graph-native consumer.
    request.compilerMayOverridePreference = false;
    return request;
}

[[nodiscard]] static Core::GpuTaskSchedulingHint JointPaletteUploadScheduling(const bool mergeWithPrevious){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = mergeWithPrevious;
    return scheduling;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSkinningSystem::MeshSkinningSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    IRuntimeMeshRegistry& runtimeMeshRegistry,
    ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_runtimeMeshRegistry(runtimeMeshRegistry)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_runtimeMeshCache(arena, graphics, assetManager)
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
    , m_graphOwnedJointPaletteStateHandoff(arena)
{
    writeAccess<SkinnedMeshBindingComponent>();
    readAccess<SkeletonJointPaletteComponent>();
    readAccess<SkeletonPoseComponent>();

    m_runtimeMeshRegistry.registerRuntimeMeshProvider(*this);
}

MeshSkinningSystem::~MeshSkinningSystem(){
    m_runtimeMeshRegistry.unregisterRuntimeMeshProvider(*this);
}

void MeshSkinningSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

bool MeshSkinningSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    if(width == 0 || height == 0)
        return true;

    if(!ensureFrameCommandList())
        return false;

    auto& device = m_graphics.getDevice();

    constexpr u32 s_PerRuntimeMeshTimingQueries = 128u;
    const bool timingReady =
        m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_Skinning.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_MeshletBounds.identity, device, s_PerRuntimeMeshTimingQueries)
        && m_graphics.gpuTiming().prepareScopeQueries(MeshSkinningGpuTimingScope::s_RepackNormals.identity, device, s_PerRuntimeMeshTimingQueries)
    ;
    if(!timingReady)
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: GPU timing scope preparation failed; timing samples may be skipped"));
    return true;
}

bool MeshSkinningSystem::ensureFrameCommandList(){
    auto& device = m_graphics.getDevice();

    if(!m_renderCommandList){
        m_renderCommandList = device.createCommandList();
        if(!m_renderCommandList){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create render command list"));
            return false;
        }
    }

    return true;
}

bool MeshSkinningSystem::prepareResources(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    m_runtimeMeshCache.prepareResources(m_world);
    pruneRuntimeResources();

    bool ready = true;
    bool hasRenderWork = false;
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
            if(!ready)
                return;
            if(!binding.runtimeMesh.valid())
                return;

            MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance)
                return;
            NWB_ASSERT(instance->valid());

            const SkeletonJointPaletteComponent* jointPalette = nullptr;
            const SkeletonPoseComponent* skeletonPose = nullptr;
            __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
            ready = prepareRuntimeMeshResources(*instance, jointPalette, skeletonPose);
            const auto foundResources = m_runtimeResources.find(instance->handle.value);
            const bool hasSkinningResources = foundResources != m_runtimeResources.end() && foundResources.value().usesSkinning();
            hasRenderWork =
                hasRenderWork
                || hasSkinningResources
                || __hidden_system::HasPotentialSkinningWork(*instance, jointPalette, skeletonPose)
            ;
        }
    );

    if(!ready || !hasRenderWork)
        return ready;

    NWB_ASSERT(m_renderCommandList);

    return true;
}

bool MeshSkinningSystem::resolveRuntimeMesh(const Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh){
    outMesh = RuntimeMeshDesc{};
    RuntimeMeshHandle runtimeMesh;
    if(const SkinnedMeshBindingComponent* binding = m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
        runtimeMesh = binding->runtimeMesh;
    if(!runtimeMesh.valid())
        return false;

    const MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(runtimeMesh);
    if(!instance || instance->entity != entity)
        return false;
    if((instance->dirtyFlags & (RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return false;
    NWB_ASSERT(instance->valid());
    NWB_ASSERT(instance->meshlets.size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(instance->meshletPrimitiveIndices.size() <= static_cast<usize>(Limit<u32>::s_Max));

    outMesh.entity = entity;
    outMesh.meshKey = DeriveRuntimeResourceName(
        instance->sourceName,
        instance->handle.value,
        instance->editRevision,
        "skinned_draw"
    );
    outMesh.positionBuffer = instance->skinnedPositionBuffer;
    outMesh.normalBuffer = instance->skinnedNormalBuffer;
    outMesh.tangentBuffer = instance->skinnedTangentBuffer;
    outMesh.uv0Buffer = instance->uv0Buffer;
    outMesh.colorBuffer = instance->colorBuffer;
    outMesh.meshletDescBuffer = instance->meshletDescBuffer;
    outMesh.meshletBoundsBuffer = instance->meshletBoundsBuffer;
    outMesh.meshletPositionRefDeltaBuffer = instance->meshletPositionRefDeltaBuffer;
    outMesh.meshletAttributeRefDeltaBuffer = instance->meshletAttributeRefDeltaBuffer;
    outMesh.meshletLocalVertexRefBuffer = instance->meshletLocalVertexRefBuffer;
    outMesh.meshletPrimitiveIndexBuffer = instance->meshletPrimitiveIndexBuffer;
    outMesh.triangleIndexBuffer = instance->triangleIndexBuffer;
    outMesh.attributeBuffer = instance->attributeBuffer;
    outMesh.localBounds = instance->localBounds;
    outMesh.meshletCount = static_cast<u32>(instance->meshlets.size());
    outMesh.meshletPrimitiveIndexCount = static_cast<u32>(instance->meshletPrimitiveIndices.size());
    outMesh.version = instance->editRevision;
    outMesh.dynamicMeshletBoundsFresh = __hidden_system::s_RuntimeSkinningMeshletFrustumCullingEnabled;
    outMesh.dynamicMeshletConesFresh = __hidden_system::s_RuntimeSkinningMeshletConeCullingEnabled;
    NWB_ASSERT(outMesh.valid());
    return true;
}

bool MeshSkinningSystem::containsRuntimeMesh(const Name& meshKey, const u64 version){
    if(!meshKey)
        return false;

    auto testEntity = [&](Core::ECS::EntityID entity, bool& found){
            if(found)
                return;

            RuntimeMeshDesc desc;
            if(!resolveRuntimeMesh(entity, desc))
                return;

            found = desc.meshKey == meshKey && desc.version == version;
    };

    bool found = false;
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
            static_cast<void>(component);
            testEntity(entity, found);
        }
    );
    return found;
}

bool MeshSkinningSystem::submitFrameJointPaletteUploads(){
    m_graphOwnedJointPaletteStateHandoff.reset();

    auto& device = m_graphics.getDevice();
    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: cannot declare joint-palette uploads without a physical queue topology"));
        return false;
    }

    Core::GpuTaskGraph graph(m_arena);
    Core::GpuTaskId terminalTask;
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_FrameUploadArena);
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    bool declarationFailed = false;
    skinningBindings.each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
            if(declarationFailed || !binding.runtimeMesh.valid())
                return;

            MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance)
                return;
            NWB_ASSERT(instance->valid());

            const SkeletonJointPaletteComponent* jointPalette = nullptr;
            const SkeletonPoseComponent* skeletonPose = nullptr;
            __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
            const auto foundResources = m_runtimeResources.find(instance->handle.value);
            const bool hadSkinningResources = foundResources != m_runtimeResources.end() && foundResources.value().usesSkinning();
            if(!__hidden_system::HasPotentialSkinningWork(*instance, jointPalette, skeletonPose) && !hadSkinningResources)
                return;

            RuntimeSkinPayloadScratch payload{ scratchArena };
            // Keep a failed pose local to this mesh, matching the established direct-recording behavior.  The
            // compute path will report the same invalid payload and leave that mesh retryable next frame.
            if(!MeshSkinningPayload::BuildRuntimeSkinPayload(*instance, jointPalette, skeletonPose, payload))
                return;
            if(!payload.hasActiveSkin())
                return;

            if(foundResources == m_runtimeResources.end() || !foundResources.value().jointPaletteBuffer){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: active runtime mesh '{}' has no joint palette buffer"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            usize jointPaletteBytes = 0u;
            if(
                payload.jointMatrices.size() > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix)
                || (jointPaletteBytes = payload.jointMatrices.size() * sizeof(SkeletonJointMatrix)) == 0u
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette payload byte size overflows"));
                declarationFailed = true;
                return;
            }

            const Core::BufferHandle& destinationBuffer = foundResources.value().jointPaletteBuffer;
            const Core::BufferDesc& destinationDesc = destinationBuffer->getDescription();
            if(!destinationDesc.debugName){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette buffer for runtime mesh '{}' has no graph identity"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            const Core::GpuGraphResourceId destination = graph.importBuffer(
                destinationBuffer,
                Core::GpuGraphResourceDesc{}
                    .setIdentity(destinationDesc.debugName)
                    .setMarkerLabel("Skinning Joint Palette")
                    .setType(Core::GpuGraphResourceType::Buffer)
                    .setInitialState(destinationDesc.initialState)
                    .setQueueSharing(destinationDesc.queueSharing)
            );
            const Core::GpuUploadBlobId source = graph.copyUploadData(
                payload.jointMatrices.data(),
                jointPaletteBytes,
                alignof(SkeletonJointMatrix)
            );
            if(!destination.valid() || !source.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned joint palette data for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }

            Core::GpuTaskDesc uploadDesc;
            uploadDesc
                .setIdentity(Name("mesh_skinning.frame_joint_palette_upload"))
                .setMarkerLabel("Skinning Joint Palette Upload")
                .setQueue(__hidden_system::JointPaletteUploadQueueRequest())
                .setScheduling(__hidden_system::JointPaletteUploadScheduling(terminalTask.valid()))
            ;
            if(terminalTask.valid())
                uploadDesc.setDependencies(&terminalTask, 1u);

            const Core::GpuTaskId uploadTask = graph.addUploadBufferTask(
                uploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = source,
                    .destination = destination,
                    .finalState = Core::ResourceStates::ShaderResource,
                }
            );
            if(!uploadTask.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned joint palette upload for runtime mesh '{}'"), instance->handle.value);
                declarationFailed = true;
                return;
            }
            terminalTask = uploadTask;
        }
    );

    if(declarationFailed)
        return false;
    if(!terminalTask.valid())
        return true;

    Core::GpuTaskGraphAnalysis analysis(m_arena);
    Core::GpuTaskGraphQueueAssignments assignments(m_arena);
    Core::GpuCompiledGraph compiledGraph(m_arena);
    Core::GpuRecordedGraph recordedGraph(m_arena);
    Core::GpuGraphSubmissionTransaction transaction(m_arena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to compile graph-owned joint palette uploads"));
        return false;
    }

    const Core::GpuSubmissionPacketId terminalPacket = compiledGraph.packetForTask(terminalTask);
    if(!terminalPacket.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette upload graph has no terminal packet"));
        return false;
    }
    const Core::GpuSubmissionPacket& packet = compiledGraph.packet(terminalPacket);
    const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    if(packet.queue != graphicsQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette upload graph did not retain the primary Graphics queue"));
        return false;
    }

    transaction.reset(compiledGraph);
    const Core::GpuNativePacketRecorder recorder(device);
    if(!recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph
    )){
        transaction.discardUnaccepted(graph, compiledGraph);
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to record graph-owned joint palette uploads"));
        return false;
    }

    const Core::CommandListResourceStateHandoff* const finalStates = recordedGraph.packetFinalStateSeed(terminalPacket);
    if(!finalStates || !m_graphOwnedJointPaletteStateHandoff.copyFrom(*finalStates)){
        transaction.discardUnaccepted(graph, compiledGraph);
        m_graphOwnedJointPaletteStateHandoff.reset();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned joint palette state"));
        return false;
    }

    const Core::GpuTaskGraphSubmitter submitter(device);
    if(!submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    )){
        transaction.discardUnaccepted(graph, compiledGraph);
        m_graphOwnedJointPaletteStateHandoff.reset();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned joint palette submission was rejected"));
        return false;
    }

    const Core::QueueSubmissionToken uploadToken = transaction.packetToken(terminalPacket);
    if(!uploadToken.valid() || !uploadToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration)){
        m_graphOwnedJointPaletteStateHandoff.reset();
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: graph-owned joint palette submission lost its Graphics queue identity"));
        return false;
    }
    return true;
}

void MeshSkinningSystem::render(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    auto& device = m_graphics.getDevice();
    Core::CommandList* commandList = m_renderCommandList.get();
    NWB_ASSERT(commandList);

    if(!submitFrameJointPaletteUploads()){
        NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: skipped skinning dispatch because graph-owned joint palette uploads were not accepted"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_SubmissionArena);
    Vector<__hidden_system::PendingRuntimeMeshSubmission, Core::Alloc::ScratchArena> pendingSubmissions{ scratchArena };
    auto skinningBindings = m_world.view<SkinnedMeshBindingComponent>();
    pendingSubmissions.reserve(skinningBindings.candidateCount());

    Core::GpuTimingSubmissionTicket timingTicket(m_graphics.gpuTiming());
    bool submittedWork = false;

    {
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        commandList->open(
            m_graphOwnedJointPaletteStateHandoff.valid()
                ? &m_graphOwnedJointPaletteStateHandoff
                : nullptr
        );

        skinningBindings.each(
            [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
                if(!binding.runtimeMesh.valid())
                    return;

                MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
                if(!instance)
                    return;
                NWB_ASSERT(instance->valid());

                const SkeletonJointPaletteComponent* jointPalette = nullptr;
                const SkeletonPoseComponent* skeletonPose = nullptr;
                __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
                const auto foundResources = m_runtimeResources.find(instance->handle.value);
                const bool hadSkinningResources = foundResources != m_runtimeResources.end() && foundResources.value().usesSkinning();
                if(!__hidden_system::HasPotentialSkinningWork(
                    *instance,
                    jointPalette,
                    skeletonPose
                ) && !hadSkinningResources)
                    return;
                MeshSkinningSubmissionCommit submissionCommit;
                if(dispatchRuntimeMesh(*commandList, *instance, jointPalette, skeletonPose, submissionCommit)){
                    submittedWork = true;
                    if(!submissionCommit.empty()){
                        __hidden_system::PendingRuntimeMeshSubmission pendingSubmission;
                        pendingSubmission.handle = instance->handle;
                        pendingSubmission.commit = submissionCommit;
                        pendingSubmissions.push_back(pendingSubmission);
                    }
                }
            }
        );

        commandList->close();
    }

    if(submittedWork){
        Core::CommandList* commandLists[] = { commandList };
        const bool submissionAccepted = timingTicket.submit(device, commandLists, 1u);
        // CPU visibility follows GPU queue acceptance. If this submission is rejected, both the deformation dirty
        // state and the selector upload remain pending so the next frame records the complete pose again.
        for(const __hidden_system::PendingRuntimeMeshSubmission& pendingSubmission : pendingSubmissions){
            MeshSkinningRuntimeInstance* submittedInstance = m_runtimeMeshCache.findInstance(pendingSubmission.handle);
            if(!submittedInstance)
                continue;
            const auto foundResources = m_runtimeResources.find(pendingSubmission.handle.value);
            if(
                foundResources == m_runtimeResources.end()
                || foundResources.value().editRevision != pendingSubmission.commit.editRevision
            )
                continue;

            ApplyMeshSkinningSubmissionCommit(
                submissionAccepted,
                submittedInstance->editRevision,
                submittedInstance->dirtyFlags,
                foundResources.value().bindlessResourceSlotsUploaded,
                pendingSubmission.commit
            );
        }
        if(!submissionAccepted)
            NWB_LOGGER_WARNING(NWB_TEXT("MeshSkinningSystem: skinning command submission was rejected"));
    }
}

void MeshSkinningSystem::pruneRuntimeResources(){
    if(m_runtimeResources.empty())
        return;

    for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
        RuntimeResources& resources = it.value();
        const MeshSkinningRuntimeInstance* instance = m_runtimeMeshCache.findInstance(resources.handle);
        if(instance && instance->valid() && instance->editRevision == resources.editRevision){
            ++it;
            continue;
        }

        // Heap descriptors retain their backing buffers independently of this cache. Retire the generation before
        // erasing it so the heap's in-flight quarantine can release both descriptor storage and retained resources.
        releaseRuntimeResourceBindlessHeapHandles(resources);
        it = m_runtimeResources.erase(it);
    }
}

void MeshSkinningSystem::invalidateResources(){
    m_renderCommandList.reset();
    m_graphOwnedJointPaletteStateHandoff.reset();
    for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end(); ++it)
        releaseRuntimeResourceBindlessHeapHandles(it.value());
    m_runtimeResources.clear();
    m_runtimeMeshCache.clear();

    m_skinningBindingLayout.reset();
    m_skinningComputeShader.reset();
    m_skinningComputePipeline.reset();
    m_boundsBindingLayout.reset();
    m_boundsComputeShader.reset();
    m_boundsComputePipeline.reset();
    m_repackBindingLayout.reset();
    m_repackComputeShader.reset();
    m_repackComputePipeline.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

