// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_system.h"

#include "skinned_geometry_morph_payload.h"
#include "skinned_geometry_skin_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_manager.h>
#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_geometry/skinned_geometry_asset.h>
#include <impl/assets_geometry/skinned_geometry_validation.h>
#include <impl/assets_shader/shader_asset_loader.h>
#include <impl/ecs_skinned_geometry/skinned_geometry_runtime_names.h>
#include <impl/ecs_render/components.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_SkinnedGeometryGroupSize = 64u;
static constexpr u32 s_SkinnedGeometryVertexScalarStride = sizeof(SkinnedGeometryVertex) / sizeof(f32);

struct SkinnedGeometryPushConstants{
    u32 vertexCount = 0;
    u32 morphRangeCount = 0;
    u32 restScalarStride = 0;
    u32 deformedScalarStride = 0;
    u32 skinCount = 0;
    u32 jointCount = 0;
    u32 displacementMode = SkinnedGeometryDisplacementMode::None;
    u32 skinningMode = SkinnedGeometrySkinningMode::LinearBlend;
    f32 displacementAmplitude = 0.0f;
    f32 displacementBias = 0.0f;
    f32 displacementUvScaleX = 1.0f;
    f32 displacementUvScaleY = 1.0f;
    f32 displacementUvOffsetX = 0.0f;
    f32 displacementUvOffsetY = 0.0f;
    u32 padding1 = 0;
    u32 padding2 = 0;
};
static_assert(sizeof(SkinnedGeometryPushConstants) == 64, "SkinnedGeometry push constants layout must stay shader-compatible");

static const Name& SkinnedGeometryComputeShaderName(){
    static const Name s("engine/graphics/skinned_geometry_cs");
    return s;
}

namespace SkinnedGeometryDisplacementResolveFailure{
    enum Enum : u8{
        None,
        Descriptor,
        Scale,
        Amplitude,
    };
};

static bool ResolveEffectiveSkinnedGeometryDisplacement(
    const SkinnedGeometryDisplacement& sourceDisplacement,
    const SkinnedGeometryDisplacementComponent* component,
    SkinnedGeometryDisplacement& outDisplacement,
    SkinnedGeometryDisplacementResolveFailure::Enum& outFailure
){
    outDisplacement = sourceDisplacement;
    outFailure = SkinnedGeometryDisplacementResolveFailure::None;

    if(!ValidSkinnedGeometryDisplacementDescriptor(outDisplacement)){
        outFailure = SkinnedGeometryDisplacementResolveFailure::Descriptor;
        return false;
    }
    if(outDisplacement.mode == SkinnedGeometryDisplacementMode::None)
        return true;
    if(component && !component->enabled){
        outDisplacement = SkinnedGeometryDisplacement{};
        return true;
    }

    const f32 scale = component ? component->amplitudeScale : 1.0f;
    if(!IsFinite(scale)){
        outFailure = SkinnedGeometryDisplacementResolveFailure::Scale;
        return false;
    }

    outDisplacement.amplitude *= scale;
    if(!IsFinite(outDisplacement.amplitude)){
        outFailure = SkinnedGeometryDisplacementResolveFailure::Amplitude;
        return false;
    }
    if(!SkinnedGeometryRuntime::ActiveWeight(outDisplacement.amplitude))
        outDisplacement = SkinnedGeometryDisplacement{};
    return true;
}

static bool ResolveSkinnedGeometryDisplacement(
    const SkinnedGeometryRuntimeMeshInstance& instance,
    const SkinnedGeometryDisplacementComponent* component,
    SkinnedGeometryDisplacement& outDisplacement
){
    SkinnedGeometryDisplacementResolveFailure::Enum failure =
        SkinnedGeometryDisplacementResolveFailure::None
    ;
    if(ResolveEffectiveSkinnedGeometryDisplacement(instance.displacement, component, outDisplacement, failure))
        return true;

    switch(failure){
    case SkinnedGeometryDisplacementResolveFailure::Descriptor:
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has an invalid displacement descriptor"), instance.handle.value);
        break;
    case SkinnedGeometryDisplacementResolveFailure::Scale:
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' displacement amplitude scale is invalid"), instance.handle.value);
        break;
    case SkinnedGeometryDisplacementResolveFailure::Amplitude:
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' effective displacement amplitude is invalid"), instance.handle.value);
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' failed to resolve displacement"), instance.handle.value);
        break;
    }
    return false;
}

static bool HasPotentialSkinnedGeometryWork(
    const SkinnedGeometryRuntimeMeshInstance& instance,
    const SkinnedGeometryMorphWeightsComponent* morphWeights,
    const SkinnedGeometryJointPaletteComponent* jointPalette,
    const SkinnedGeometrySkeletonPoseComponent* skeletonPose,
    const SkinnedGeometryDisplacementComponent* displacement,
    SkinnedGeometryDisplacement& outResolvedDisplacement
){
    if(!ResolveSkinnedGeometryDisplacement(instance, displacement, outResolvedDisplacement))
        return false;
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty) != 0u)
        return true;
    if(SkinnedGeometryRuntime::HasMorphWeights(morphWeights))
        return true;
    if(
        !instance.skin.empty()
        && ((jointPalette && !jointPalette->joints.empty()) || SkinnedGeometryRuntime::HasSkeletonPose(skeletonPose))
    )
        return true;

    return
        outResolvedDisplacement.mode != SkinnedGeometryDisplacementMode::None
        && SkinnedGeometryValidation::ActiveWeight(outResolvedDisplacement.amplitude)
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

[[nodiscard]] Core::TextureHandle SetupDisplacementTexture(
    Core::Graphics& graphics,
    const Name& debugName,
    const u32 width,
    const u32 height,
    const Float4U* texels,
    const usize texelCount
){
    if(width == 0u || height == 0u || !texels || texelCount == 0u)
        return {};
    usize uploadBytes = 0u;
    if(!BufferPayloadBytes(texelCount, sizeof(Float4U), uploadBytes, NWB_TEXT("displacement texture")))
        return {};

    const usize rowPitch = static_cast<usize>(width) * sizeof(Float4U);
    if(height > Limit<usize>::s_Max / rowPitch)
        return {};

    Core::Graphics::TextureSetupDesc setup;
    setup.textureDesc
        .setWidth(width)
        .setHeight(height)
        .setDepth(1u)
        .setArraySize(1u)
        .setMipLevels(1u)
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setDimension(Core::TextureDimension::Texture2D)
        .setName(debugName)
    ;
    setup.data = texels;
    setup.uploadDataSize = uploadBytes;
    setup.rowPitch = rowPitch;
    setup.depthPitch = rowPitch * static_cast<usize>(height);
    return graphics.setupTexture(setup);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SkinnedGeometrySystem::SkinnedGeometrySystem(
    Core::Alloc::CustomArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    IRuntimeGeometryRegistry& runtimeGeometryRegistry,
    ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_runtimeGeometryRegistry(runtimeGeometryRegistry)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_runtimeMeshCache(arena, graphics, assetManager)
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), RuntimeResourceMapAllocator(arena))
{
    writeAccess<SkinnedGeometryComponent>();
    readAccess<RendererComponent>();
    readAccess<SkinnedGeometryMorphWeightsComponent>();
    readAccess<SkinnedGeometryJointPaletteComponent>();
    readAccess<SkinnedGeometrySkeletonPoseComponent>();
    readAccess<SkinnedGeometryDisplacementComponent>();

    m_runtimeGeometryRegistry.registerRuntimeGeometryProvider(*this);
}

SkinnedGeometrySystem::~SkinnedGeometrySystem()
{
    m_runtimeGeometryRegistry.unregisterRuntimeGeometryProvider(*this);
}

void SkinnedGeometrySystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(delta);
    m_runtimeMeshCache.update(world);
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

    const SkinnedGeometryRuntimeMeshInstance* instance = m_runtimeMeshCache.findInstance(renderer->runtimeMesh);
    if(!instance || !instance->valid() || instance->entity != entity)
        return false;
    if(instance->indices.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outGeometry.entity = entity;
    outGeometry.geometryKey = DeriveRuntimeResourceName(
        instance->source.name(),
        instance->handle.value,
        instance->editRevision,
        "deformed_draw"
    );
    outGeometry.shaderVertexBuffer = instance->deformedVertexBuffer;
    outGeometry.shaderIndexBuffer = instance->indexBuffer;
    outGeometry.indexCount = static_cast<u32>(instance->indices.size());
    outGeometry.sourceVertexLayout = MeshSourceLayout::SkinnedGeometryVertex;
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
                m_runtimeMeshCache.findInstance(resources.handle)
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
                m_runtimeMeshCache.findInstance(renderer.runtimeMesh)
            ;
            if(!instance || !instance->valid())
                return;

            const SkinnedGeometryMorphWeightsComponent* morphWeights =
                m_world.tryGetComponent<SkinnedGeometryMorphWeightsComponent>(entity)
            ;
            const SkinnedGeometryJointPaletteComponent* jointPalette =
                m_world.tryGetComponent<SkinnedGeometryJointPaletteComponent>(entity)
            ;
            const SkinnedGeometrySkeletonPoseComponent* skeletonPose =
                m_world.tryGetComponent<SkinnedGeometrySkeletonPoseComponent>(entity)
            ;
            const SkinnedGeometryDisplacementComponent* displacement =
                m_world.tryGetComponent<SkinnedGeometryDisplacementComponent>(entity)
            ;
            SkinnedGeometryDisplacement resolvedDisplacement;
            if(!__hidden_skinned_geometry_system::HasPotentialSkinnedGeometryWork(
                *instance,
                morphWeights,
                jointPalette,
                skeletonPose,
                displacement,
                resolvedDisplacement
            ))
                return;
            if(!ensureCommandList())
                return;
            if(dispatchRuntimeMesh(*commandList, *instance, morphWeights, jointPalette, skeletonPose, resolvedDisplacement))
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

bool SkinnedGeometrySystem::ensurePipeline(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_bindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(6, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(7, 1));
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
    const SkinnedGeometryMorphWeightsComponent* morphWeights,
    const SkinnedGeometryJointPaletteComponent* jointPalette,
    const SkinnedGeometrySkeletonPoseComponent* skeletonPose,
    const SkinnedGeometryDisplacement& resolvedDisplacement
){
    if(!SkinnedGeometryMorphPayload::ValidateRuntimeMeshVertexCount(instance))
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<SkinnedGeometryVertexMorphRangeGpu, Core::Alloc::ScratchAllocator<SkinnedGeometryVertexMorphRangeGpu>> morphRanges{
        Core::Alloc::ScratchAllocator<SkinnedGeometryVertexMorphRangeGpu>(scratchArena)
    };
    Vector<SkinnedGeometryBlendedMorphDeltaGpu, Core::Alloc::ScratchAllocator<SkinnedGeometryBlendedMorphDeltaGpu>> morphDeltas{
        Core::Alloc::ScratchAllocator<SkinnedGeometryBlendedMorphDeltaGpu>(scratchArena)
    };
    usize morphSignature = 0;
    if(!SkinnedGeometryMorphPayload::BuildBlendedMorphPayload(instance, morphWeights, morphRanges, morphDeltas, morphSignature))
        return false;

    Vector<SkinnedGeometrySkinInfluenceGpu, Core::Alloc::ScratchAllocator<SkinnedGeometrySkinInfluenceGpu>> skinInfluences{
        Core::Alloc::ScratchAllocator<SkinnedGeometrySkinInfluenceGpu>(scratchArena)
    };
    Vector<SkinnedGeometryJointMatrix, Core::Alloc::ScratchAllocator<SkinnedGeometryJointMatrix>> jointMatrices{
        Core::Alloc::ScratchAllocator<SkinnedGeometryJointMatrix>(scratchArena)
    };
    u32 resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkinnedGeometrySkinningMode::LinearBlend;
    if(SkinnedGeometryRuntime::HasSkeletonPose(skeletonPose)){
        Vector<SkinnedGeometryJointMatrix, Core::Alloc::ScratchAllocator<SkinnedGeometryJointMatrix>> poseJoints{
            Core::Alloc::ScratchAllocator<SkinnedGeometryJointMatrix>(scratchArena)
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

    const bool hasActiveMorphs = !morphRanges.empty();
    const bool hasActiveSkin = !skinInfluences.empty() && !jointMatrices.empty();
    const bool hasDisplacement = resolvedDisplacement.mode != SkinnedGeometryDisplacementMode::None
        && SkinnedGeometryValidation::ActiveWeight(resolvedDisplacement.amplitude)
    ;
    if(!hasActiveMorphs && !hasActiveSkin && !hasDisplacement){
        const bool skinnedGeometryInputDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty) != 0u;
        const auto foundResources = m_runtimeResources.find(instance.handle.value);
        if(foundResources == m_runtimeResources.end() && !skinnedGeometryInputDirty)
            return false;

        return copyRestToDeformed(commandList, instance);
    }
    if(!ensurePipeline())
        return false;
    if(!m_computePipeline)
        return false;

    RuntimePayloadViews payloadViews;
    payloadViews.morphRanges = morphRanges.data();
    payloadViews.morphDeltas = morphDeltas.data();
    payloadViews.skinInfluences = skinInfluences.data();
    payloadViews.jointPalette = jointMatrices.data();
    payloadViews.morphRangeCount = morphRanges.size();
    payloadViews.morphDeltaCount = morphDeltas.size();
    payloadViews.skinInfluenceCount = skinInfluences.size();
    payloadViews.jointPaletteCount = jointMatrices.size();

    RuntimeResources* resources = nullptr;
    bool resourcesRebuilt = false;
    if(!ensureRuntimeResources(
        instance,
        payloadViews,
        resolvedDisplacement,
        hasDisplacement,
        morphSignature,
        resources,
        resourcesRebuilt
    ))
        return false;
    if(
        !resources
        || !resources->bindingSet
        || !resources->morphRangeBuffer
        || !resources->morphDeltaBuffer
        || !resources->skinBuffer
        || !resources->jointPaletteBuffer
        || !resources->displacementTexture
    )
        return false;

    const bool morphPayloadDirty =
        hasActiveMorphs
        && !resourcesRebuilt
        && resources->morphSignature != morphSignature
    ;
    usize morphRangeBytes = 0;
    usize morphDeltaBytes = 0;
    if(morphPayloadDirty){
        if(
            !__hidden_skinned_geometry_system::BufferPayloadBytes(
                morphRanges.size(),
                sizeof(SkinnedGeometryVertexMorphRangeGpu),
                morphRangeBytes,
                NWB_TEXT("morph range")
            )
            || !__hidden_skinned_geometry_system::BufferPayloadBytes(
                morphDeltas.size(),
                sizeof(SkinnedGeometryBlendedMorphDeltaGpu),
                morphDeltaBytes,
                NWB_TEXT("morph delta")
            )
        )
            return false;

        commandList.setBufferState(resources->morphRangeBuffer.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(resources->morphDeltaBuffer.get(), Core::ResourceStates::CopyDest);
    }

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
    if(morphPayloadDirty){
        commandList.commitBarriers();
        commandList.writeBuffer(resources->morphRangeBuffer.get(), morphRanges.data(), morphRangeBytes);
        commandList.writeBuffer(resources->morphDeltaBuffer.get(), morphDeltas.data(), morphDeltaBytes);
        resources->morphSignature = morphSignature;
    }
    if(hasActiveSkin && !resourcesRebuilt){
        commandList.commitBarriers();
        commandList.writeBuffer(resources->jointPaletteBuffer.get(), jointMatrices.data(), jointPaletteBytes);
    }

    commandList.setBufferState(instance.restVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.deformedVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(resources->morphRangeBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->morphDeltaBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->skinBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setTextureState(resources->displacementTexture.get(), Core::s_AllSubresources, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_computePipeline.get());
    computeState.addBindingSet(resources->bindingSet.get());
    commandList.setComputeState(computeState);

    __hidden_skinned_geometry_system::SkinnedGeometryPushConstants pushConstants;
    pushConstants.vertexCount = static_cast<u32>(instance.restVertices.size());
    pushConstants.morphRangeCount = static_cast<u32>(morphRanges.size());
    pushConstants.restScalarStride = __hidden_skinned_geometry_system::s_SkinnedGeometryVertexScalarStride;
    pushConstants.deformedScalarStride = __hidden_skinned_geometry_system::s_SkinnedGeometryVertexScalarStride;
    pushConstants.skinCount = static_cast<u32>(skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(jointMatrices.size());
    pushConstants.skinningMode = hasActiveSkin
        ? resolvedSkinningMode
        : SkinnedGeometrySkinningMode::LinearBlend
    ;
    pushConstants.displacementAmplitude = hasDisplacement ? resolvedDisplacement.amplitude : 0.0f;
    pushConstants.displacementMode = hasDisplacement ? resolvedDisplacement.mode : SkinnedGeometryDisplacementMode::None;
    pushConstants.displacementBias = hasDisplacement ? resolvedDisplacement.bias : 0.0f;
    pushConstants.displacementUvScaleX = hasDisplacement ? resolvedDisplacement.uvScale.x : 1.0f;
    pushConstants.displacementUvScaleY = hasDisplacement ? resolvedDisplacement.uvScale.y : 1.0f;
    pushConstants.displacementUvOffsetX = hasDisplacement ? resolvedDisplacement.uvOffset.x : 0.0f;
    pushConstants.displacementUvOffsetY = hasDisplacement ? resolvedDisplacement.uvOffset.y : 0.0f;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(__hidden_skinned_geometry_system::DispatchGroupCount(pushConstants.vertexCount), 1, 1);

    commandList.setBufferState(instance.deformedVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
    );
    return true;
}

bool SkinnedGeometrySystem::copyRestToDeformed(Core::ICommandList& commandList, SkinnedGeometryRuntimeMeshInstance& instance){
    usize copyBytes = 0;
    if(!__hidden_skinned_geometry_system::BufferPayloadBytes(
        instance.restVertices.size(),
        sizeof(SkinnedGeometryVertex),
        copyBytes,
        NWB_TEXT("rest vertex")
    ))
        return false;

    commandList.setBufferState(instance.restVertexBuffer.get(), Core::ResourceStates::CopySource);
    commandList.setBufferState(instance.deformedVertexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.copyBuffer(instance.deformedVertexBuffer.get(), 0, instance.restVertexBuffer.get(), 0, copyBytes);
    commandList.setBufferState(instance.deformedVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty
    );
    return true;
}

bool SkinnedGeometrySystem::ensureRuntimeResources(
    SkinnedGeometryRuntimeMeshInstance& instance,
    const RuntimePayloadViews& payloadViews,
    const SkinnedGeometryDisplacement& displacement,
    const bool hasDisplacement,
    const usize morphSignature,
    RuntimeResources*& outResources,
    bool& outResourcesRebuilt
){
    outResources = nullptr;
    outResourcesRebuilt = false;
    if((payloadViews.morphRangeCount == 0u) != (payloadViews.morphDeltaCount == 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has mismatched morph range/delta payloads"), instance.handle.value);
        return false;
    }
    if((payloadViews.skinInfluenceCount == 0u) != (payloadViews.jointPaletteCount == 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has mismatched skin influence/joint payloads"), instance.handle.value);
        return false;
    }

    const bool hasActiveMorphs = payloadViews.hasActiveMorphs();
    const bool hasActiveSkin = payloadViews.hasActiveSkin();
    if(!hasActiveMorphs && !hasActiveSkin && !hasDisplacement)
        return false;
    if(hasActiveMorphs && payloadViews.morphRangeCount != instance.restVertices.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has {} morph ranges for {} vertices")
            , instance.handle.value
            , payloadViews.morphRangeCount
            , instance.restVertices.size()
        );
        return false;
    }
    const bool hasTextureDisplacement = hasDisplacement
        && SkinnedGeometryDisplacementModeUsesTexture(displacement.mode)
    ;
    const Name displacementTextureName = hasTextureDisplacement ? displacement.texture.name() : NAME_NONE;

    if(
        (hasActiveMorphs && (!payloadViews.morphRanges || !payloadViews.morphDeltas))
        || (hasActiveSkin && (!payloadViews.skinInfluences || !payloadViews.jointPalette))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has null active skinned geometry payloads"), instance.handle.value);
        return false;
    }
    if(!ensureDefaultSkinnedGeometryBuffers() || !ensureDefaultDisplacementResources())
        return false;

    if(
        payloadViews.morphRangeCount > static_cast<usize>(Limit<u32>::s_Max)
        || payloadViews.morphDeltaCount > static_cast<usize>(Limit<u32>::s_Max)
        || payloadViews.skinInfluenceCount > static_cast<usize>(Limit<u32>::s_Max)
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
        || resources.morphRangeCount != static_cast<u32>(payloadViews.morphRangeCount)
        || resources.deltaCount != static_cast<u32>(payloadViews.morphDeltaCount)
        || resources.skinCount != static_cast<u32>(payloadViews.skinInfluenceCount)
        || resources.jointCount != static_cast<u32>(payloadViews.jointPaletteCount)
        || resources.displacementTextureName != displacementTextureName
        || !resources.morphRangeBuffer
        || !resources.morphDeltaBuffer
        || !resources.skinBuffer
        || !resources.jointPaletteBuffer
        || !resources.displacementTexture
        || !resources.bindingSet
    ;
    if(!rebuild){
        outResources = &resources;
        return true;
    }

    Name rangeBufferName = NAME_NONE;
    Name deltaBufferName = NAME_NONE;
    if(hasActiveMorphs){
        rangeBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_geometry_ranges");
        deltaBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_geometry_deltas");
    }

    Name skinBufferName = NAME_NONE;
    Name jointPaletteBufferName = NAME_NONE;
    if(hasActiveSkin){
        skinBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_geometry_skin");
        jointPaletteBufferName = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "skinned_geometry_joints");
    }

    if(
        (hasActiveMorphs && (!rangeBufferName || !deltaBufferName))
        || (hasActiveSkin && (!skinBufferName || !jointPaletteBufferName))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to derive skinned geometry buffer names for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    RuntimeResources rebuilt;
    rebuilt.handle = instance.handle;
    rebuilt.editRevision = instance.editRevision;
    rebuilt.vertexCount = static_cast<u32>(instance.restVertices.size());
    rebuilt.morphRangeCount = static_cast<u32>(payloadViews.morphRangeCount);
    rebuilt.deltaCount = static_cast<u32>(payloadViews.morphDeltaCount);
    rebuilt.skinCount = static_cast<u32>(payloadViews.skinInfluenceCount);
    rebuilt.jointCount = static_cast<u32>(payloadViews.jointPaletteCount);
    rebuilt.morphSignature = morphSignature;
    rebuilt.displacementTextureName = displacementTextureName;

    rebuilt.morphRangeBuffer = hasActiveMorphs
        ? __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            rangeBufferName,
            payloadViews.morphRanges,
            payloadViews.morphRangeCount,
            NWB_TEXT("morph range")
        )
        : m_defaultMorphRangeBuffer
    ;
    if(!rebuilt.morphRangeBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create morph range buffer for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    rebuilt.morphDeltaBuffer = hasActiveMorphs
        ? __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            deltaBufferName,
            payloadViews.morphDeltas,
            payloadViews.morphDeltaCount,
            NWB_TEXT("morph delta")
        )
        : m_defaultMorphDeltaBuffer
    ;
    if(!rebuilt.morphDeltaBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create morph delta buffer for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    rebuilt.skinBuffer = hasActiveSkin
        ? __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            skinBufferName,
            payloadViews.skinInfluences,
            payloadViews.skinInfluenceCount,
            NWB_TEXT("skin influence")
        )
        : m_defaultSkinBuffer
    ;
    if(!rebuilt.skinBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create skin buffer for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    rebuilt.jointPaletteBuffer = hasActiveSkin
        ? __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            jointPaletteBufferName,
            payloadViews.jointPalette,
            payloadViews.jointPaletteCount,
            NWB_TEXT("joint palette")
        )
        : m_defaultJointPaletteBuffer
    ;
    if(!rebuilt.jointPaletteBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create joint palette buffer for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    if(hasTextureDisplacement){
        if(!ensureDisplacementTexture(displacement, rebuilt.displacementTexture)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to load displacement texture '{}' for runtime mesh '{}'")
                , StringConvert(displacement.texture.name().c_str())
                , instance.handle.value
            );
            return false;
        }
    }
    else{
        rebuilt.displacementTexture = m_defaultDisplacementTexture;
    }
    if(!rebuilt.displacementTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to resolve displacement texture resource for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, instance.restVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, instance.deformedVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, rebuilt.morphRangeBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, rebuilt.morphDeltaBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(4, rebuilt.skinBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, rebuilt.jointPaletteBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(6, rebuilt.displacementTexture.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(7, m_displacementSampler.get()));

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
        m_defaultMorphRangeBuffer
        && m_defaultMorphDeltaBuffer
        && m_defaultSkinBuffer
        && m_defaultJointPaletteBuffer
    )
        return true;

    const SkinnedGeometryVertexMorphRangeGpu defaultRange{};
    const SkinnedGeometryBlendedMorphDeltaGpu defaultDelta{};
    const SkinnedGeometrySkinInfluenceGpu defaultSkin{};
    const SkinnedGeometryJointMatrix defaultJoint = MakeIdentitySkinnedGeometryJointMatrix();

    if(!m_defaultMorphRangeBuffer){
        m_defaultMorphRangeBuffer = __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/skinned_geometry_default_morph_range"),
            &defaultRange,
            1u,
            NWB_TEXT("default morph range")
        );
        if(!m_defaultMorphRangeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create default morph range buffer"));
            return false;
        }
    }

    if(!m_defaultMorphDeltaBuffer){
        m_defaultMorphDeltaBuffer = __hidden_skinned_geometry_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/skinned_geometry_default_morph_delta"),
            &defaultDelta,
            1u,
            NWB_TEXT("default morph delta")
        );
        if(!m_defaultMorphDeltaBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create default morph delta buffer"));
            return false;
        }
    }

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

bool SkinnedGeometrySystem::ensureDefaultDisplacementResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_displacementSampler){
        Core::SamplerDesc samplerDesc;
        samplerDesc
            .setAllFilters(false)
            .setAllAddressModes(Core::SamplerAddressMode::Clamp)
        ;
        m_displacementSampler = device->createSampler(samplerDesc);
        if(!m_displacementSampler){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create displacement sampler"));
            return false;
        }
    }

    if(!m_defaultDisplacementTexture){
        const Float4U defaultTexel(0.0f, 0.0f, 0.0f, 0.0f);
        m_defaultDisplacementTexture = __hidden_skinned_geometry_system::SetupDisplacementTexture(
            m_graphics,
            Name("engine/graphics/skinned_geometry_default_displacement_texture"),
            1u,
            1u,
            &defaultTexel,
            1u
        );
        if(!m_defaultDisplacementTexture){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create default displacement texture"));
            return false;
        }
    }

    return true;
}

bool SkinnedGeometrySystem::ensureDisplacementTexture(
    const SkinnedGeometryDisplacement& displacement,
    Core::TextureHandle& outTexture
){
    outTexture = nullptr;
    if(!SkinnedGeometryDisplacementModeUsesTexture(displacement.mode)){
        outTexture = m_defaultDisplacementTexture;
        return static_cast<bool>(outTexture);
    }
    if(!displacement.texture.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: displacement texture asset ref is empty"));
        return false;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(SkinnedGeometryDisplacementTexture::AssetTypeName(), displacement.texture.name(), loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to load displacement texture asset '{}'"), StringConvert(displacement.texture.name().c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != SkinnedGeometryDisplacementTexture::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: asset '{}' is not a skinned geometry displacement texture"), StringConvert(displacement.texture.name().c_str()));
        return false;
    }

    const SkinnedGeometryDisplacementTexture& textureAsset = static_cast<const SkinnedGeometryDisplacementTexture&>(*loadedAsset);
    if(!textureAsset.validatePayload())
        return false;

    outTexture = __hidden_skinned_geometry_system::SetupDisplacementTexture(
        m_graphics,
        textureAsset.virtualPath(),
        textureAsset.width(),
        textureAsset.height(),
        textureAsset.texels().data(),
        textureAsset.texels().size()
    );
    return static_cast<bool>(outTexture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

