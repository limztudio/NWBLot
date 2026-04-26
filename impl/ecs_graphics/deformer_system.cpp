// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformer_system.h"

#include "deformer_morph_payload.h"
#include "deformable_runtime_helpers.h"
#include "renderer_system.h"

#include <core/alloc/scratch.h>
#include <core/ecs/world.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_graphics/deformable_geometry_validation.h>
#include <impl/assets_graphics/shader_asset.h>
#include <impl/assets_graphics/shader_stage_names.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformer_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_DeformerGroupSize = 64u;
static constexpr u32 s_DeformableVertexScalarStride = sizeof(DeformableVertexRest) / sizeof(f32);

struct DeformerPushConstants{
    u32 vertexCount = 0;
    u32 morphRangeCount = 0;
    u32 restScalarStride = 0;
    u32 deformedScalarStride = 0;
    u32 skinCount = 0;
    u32 jointCount = 0;
    u32 displacementMode = DeformableDisplacementMode::None;
    u32 padding0 = 0;
    f32 displacementAmplitude = 0.0f;
    f32 displacementBias = 0.0f;
    f32 displacementUvScaleX = 1.0f;
    f32 displacementUvScaleY = 1.0f;
    f32 displacementUvOffsetX = 0.0f;
    f32 displacementUvOffsetY = 0.0f;
    u32 padding1 = 0;
    u32 padding2 = 0;
};
static_assert(sizeof(DeformerPushConstants) == 64, "Deformer push constants layout must stay shader-compatible");

static const Name& DeformerComputeShaderName(){
    static const Name s("engine/graphics/deformer_cs");
    return s;
}

static bool ResolveDeformerDisplacement(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableDisplacementComponent* component,
    DeformableDisplacement& outDisplacement)
{
    DeformableRuntime::DisplacementResolveFailure::Enum failure =
        DeformableRuntime::DisplacementResolveFailure::None
    ;
    if(DeformableRuntime::ResolveEffectiveDisplacement(instance.displacement, component, outDisplacement, failure))
        return true;

    switch(failure){
    case DeformableRuntime::DisplacementResolveFailure::Descriptor:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' has an invalid displacement descriptor"),
            instance.handle.value
        );
        break;
    case DeformableRuntime::DisplacementResolveFailure::Scale:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' displacement amplitude scale is invalid"),
            instance.handle.value
        );
        break;
    case DeformableRuntime::DisplacementResolveFailure::Amplitude:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' effective displacement amplitude is invalid"),
            instance.handle.value
        );
        break;
    default:
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' failed to resolve displacement"),
            instance.handle.value
        );
        break;
    }
    return false;
}

static bool HasPotentialDeformerWork(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* morphWeights,
    const DeformableJointPaletteComponent* jointPalette,
    const DeformableDisplacementComponent* displacement)
{
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::DeformerInputDirty) != 0u)
        return true;
    if(DeformableRuntime::HasMorphWeights(morphWeights))
        return true;
    if(!instance.skin.empty() && jointPalette && !jointPalette->joints.empty())
        return true;

    DeformableDisplacement resolvedDisplacement;
    if(!ResolveDeformerDisplacement(instance, displacement, resolvedDisplacement))
        return false;

    return resolvedDisplacement.mode != DeformableDisplacementMode::None
        && DeformableValidation::ActiveWeight(resolvedDisplacement.amplitude)
    ;
}

static u32 DispatchGroupCount(const u32 vertexCount){
    return vertexCount == 0
        ? 0
        : ((vertexCount - 1u) / s_DeformerGroupSize) + 1u
    ;
}

template<typename SkinInfluenceVector, typename JointPaletteVector>
static bool BuildSkinPayload(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableJointPaletteComponent* jointPalette,
    SkinInfluenceVector& outSkinInfluences,
    JointPaletteVector& outJointPalette)
{
    outSkinInfluences.clear();
    outJointPalette.clear();

    if(instance.skin.empty() || !jointPalette || jointPalette->joints.empty())
        return true;
    if(instance.skin.size() != instance.restVertices.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' skin count does not match vertex count"),
            instance.handle.value
        );
        return false;
    }
    if(instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max)
        || jointPalette->joints.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' skin payload exceeds u32 limits"),
            instance.handle.value
        );
        return false;
    }

    outSkinInfluences.reserve(instance.skin.size());
    outJointPalette.reserve(jointPalette->joints.size());

    for(usize jointIndex = 0; jointIndex < jointPalette->joints.size(); ++jointIndex){
        const DeformableJointMatrix& joint = jointPalette->joints[jointIndex];
        const SIMDMatrix jointMatrix = DeformableRuntime::LoadJointMatrix(joint);
        if(!DeformableRuntime::IsInvertibleAffineJointMatrix(jointMatrix)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' joint palette entry {} is not a finite invertible affine matrix"),
                instance.handle.value,
                jointIndex
            );
            return false;
        }
        outJointPalette.push_back(joint);
    }

    for(usize vertexIndex = 0; vertexIndex < instance.skin.size(); ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        if(!DeformableValidation::ValidSkinInfluence(sourceSkin)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} has invalid skin weights"),
                instance.handle.value,
                vertexIndex
            );
            return false;
        }

        DeformerSystem::DeformerSkinInfluenceGpu gpuSkin;
        for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
            const u32 joint = static_cast<u32>(sourceSkin.joint[influenceIndex]);
            const f32 weight = sourceSkin.weight[influenceIndex];
            if(DeformableValidation::ActiveWeight(weight) && joint >= jointPalette->joints.size()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} references joint {} outside palette size {}"),
                    instance.handle.value,
                    vertexIndex,
                    joint,
                    jointPalette->joints.size()
                );
                return false;
            }
            gpuSkin.joint[influenceIndex] = joint;
        }
        gpuSkin.weight = Float4(
            sourceSkin.weight[0],
            sourceSkin.weight[1],
            sourceSkin.weight[2],
            sourceSkin.weight[3]
        );
        outSkinInfluences.push_back(gpuSkin);
    }

    return true;
}

static bool BufferPayloadBytes(const usize count, const usize stride, usize& outBytes, const tchar* label){
    outBytes = 0;
    if(stride == 0u || count > Limit<usize>::s_Max / stride){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: {} payload byte size overflows"), label);
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
    const tchar* label)
{
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

[[nodiscard]] bool DisplacementModeUsesTexture(const u32 mode){
    return mode == DeformableDisplacementMode::ScalarTexture
        || mode == DeformableDisplacementMode::VectorTangentTexture
        || mode == DeformableDisplacementMode::VectorObjectTexture
    ;
}

[[nodiscard]] Core::TextureHandle SetupDisplacementTexture(
    Core::Graphics& graphics,
    const Name& debugName,
    const u32 width,
    const u32 height,
    const Float4U* texels,
    const usize texelCount)
{
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


static_assert(
    sizeof(DeformerSystem::DeformerVertexMorphRangeGpu) == sizeof(f32) * 4u,
    "Deformer vertex morph range GPU layout drifted"
);
static_assert(
    alignof(DeformerSystem::DeformerVertexMorphRangeGpu) >= alignof(Float4),
    "Deformer vertex morph range GPU layout must stay SIMD-aligned"
);
static_assert(
    sizeof(DeformerSystem::DeformerBlendedMorphDeltaGpu) == sizeof(f32) * 12u,
    "Deformer blended morph delta GPU layout drifted"
);
static_assert(
    alignof(DeformerSystem::DeformerBlendedMorphDeltaGpu) >= alignof(Float4),
    "Deformer blended morph delta GPU layout must stay SIMD-aligned"
);
static_assert(
    sizeof(DeformerSystem::DeformerSkinInfluenceGpu) == sizeof(f32) * 8u,
    "Deformer skin influence GPU layout drifted"
);
static_assert(
    alignof(DeformerSystem::DeformerSkinInfluenceGpu) >= alignof(Float4),
    "Deformer skin influence GPU layout must stay SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeformerSystem::DeformerSystem(
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    RendererSystem& rendererSystem,
    ShaderPathResolveCallback shaderPathResolver)
    : Core::IRenderPass(graphics)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_rendererSystem(rendererSystem)
    , m_shaderPathResolver(Move(shaderPathResolver))
{
    readAccess<DeformableRendererComponent>();
    readAccess<DeformableMorphWeightsComponent>();
    readAccess<DeformableJointPaletteComponent>();
    readAccess<DeformableDisplacementComponent>();
}

DeformerSystem::~DeformerSystem()
{}

void DeformerSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

void DeformerSystem::render(Core::IFramebuffer* framebuffer){
    static_cast<void>(framebuffer);

    if(!m_runtimeResources.empty()){
        for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
            const RuntimeResources& resources = it.value();
            const DeformableRuntimeMeshInstance* instance =
                m_rendererSystem.findDeformableRuntimeMesh(resources.handle)
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
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create command list"));
            commandListFailed = true;
            return false;
        }

        commandList->open();
        commandListOpen = true;
        return true;
    };

    m_world.view<DeformableRendererComponent>().each(
        [&](Core::ECS::EntityID entity, DeformableRendererComponent& renderer){
            if(!renderer.visible || !renderer.runtimeMesh.valid())
                return;

            DeformableRuntimeMeshInstance* instance =
                m_rendererSystem.findDeformableRuntimeMesh(renderer.runtimeMesh)
            ;
            if(!instance || !instance->valid())
                return;

            const DeformableMorphWeightsComponent* morphWeights =
                m_world.tryGetComponent<DeformableMorphWeightsComponent>(entity)
            ;
            const DeformableJointPaletteComponent* jointPalette =
                m_world.tryGetComponent<DeformableJointPaletteComponent>(entity)
            ;
            const DeformableDisplacementComponent* displacement =
                m_world.tryGetComponent<DeformableDisplacementComponent>(entity)
            ;
            if(!__hidden_deformer_system::HasPotentialDeformerWork(
                *instance,
                morphWeights,
                jointPalette,
                displacement
            ))
                return;
            if(!ensureCommandList())
                return;
            if(dispatchRuntimeMesh(*commandList, *instance, morphWeights, jointPalette, displacement))
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

bool DeformerSystem::ensurePipeline(){
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
                sizeof(__hidden_deformer_system::DeformerPushConstants)
            )
        );

        m_bindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_bindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create binding layout"));
            return false;
        }
    }

    if(!ensureShaderLoaded(
        m_computeShader,
        __hidden_deformer_system::DeformerComputeShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        Name("ECSGraphics_DeformerCS")
    ))
        return false;

    if(m_computePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_computeShader.get());
    pipelineDesc.addBindingLayout(m_bindingLayout.get());
    m_computePipeline = device->createComputePipeline(pipelineDesc);
    if(!m_computePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create compute pipeline"));
        return false;
    }

    return true;
}

bool DeformerSystem::ensureShaderLoaded(
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    const AStringView variantName,
    const Core::ShaderType::Mask shaderType,
    const Name& debugName)
{
    if(outShader)
        return true;
    if(!shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: shader name is empty"));
        return false;
    }

    const Name& stageName = ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: unsupported shader stage {}"), static_cast<u32>(shaderType));
        return false;
    }

    const AStringView resolvedVariantName = variantName.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : variantName
    ;
    if(!m_shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: shader path resolver is null"));
        return false;
    }

    Name shaderVirtualPath = NAME_NONE;
    if(!m_shaderPathResolver(shaderName, resolvedVariantName, stageName, shaderVirtualPath)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to resolve shader '{}' variant '{}' stage '{}'"),
            StringConvert(shaderName.c_str()),
            StringConvert(resolvedVariantName),
            StringConvert(stageName.c_str())
        );
        return false;
    }
    if(!shaderVirtualPath){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: shader resolver returned an empty path for shader '{}' variant '{}' stage '{}'"),
            StringConvert(shaderName.c_str()),
            StringConvert(resolvedVariantName),
            StringConvert(stageName.c_str())
        );
        return false;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Shader::AssetTypeName(), shaderVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to load shader '{}'"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Shader::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: asset '{}' is not a shader"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    const Shader& shaderAsset = static_cast<const Shader&>(*loadedAsset);
    const Vector<u8>& shaderBinary = shaderAsset.bytecode();
    if(shaderBinary.empty() || (shaderBinary.size() & 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: shader '{}' has invalid bytecode"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    Core::ShaderDesc shaderDesc;
    shaderDesc.setShaderType(shaderType);
    shaderDesc.setDebugName(debugName);

    Core::IDevice* device = m_graphics.getDevice();
    outShader = device->createShader(shaderDesc, shaderBinary.data(), shaderBinary.size());
    if(!outShader){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to create shader '{}' from asset '{}'"),
            StringConvert(debugName.c_str()),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    return true;
}

bool DeformerSystem::dispatchRuntimeMesh(
    Core::ICommandList& commandList,
    DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* morphWeights,
    const DeformableJointPaletteComponent* jointPalette,
    const DeformableDisplacementComponent* displacement)
{
    if(instance.restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex count exceeds u32 limits"),
            instance.handle.value
        );
        return false;
    }

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<DeformerVertexMorphRangeGpu, Core::Alloc::ScratchAllocator<DeformerVertexMorphRangeGpu>> morphRanges{
        Core::Alloc::ScratchAllocator<DeformerVertexMorphRangeGpu>(scratchArena)
    };
    Vector<DeformerBlendedMorphDeltaGpu, Core::Alloc::ScratchAllocator<DeformerBlendedMorphDeltaGpu>> morphDeltas{
        Core::Alloc::ScratchAllocator<DeformerBlendedMorphDeltaGpu>(scratchArena)
    };
    usize morphSignature = 0;
    if(!DeformerMorphPayload::BuildBlendedMorphPayload(instance, morphWeights, morphRanges, morphDeltas, morphSignature))
        return false;

    Vector<DeformerSkinInfluenceGpu, Core::Alloc::ScratchAllocator<DeformerSkinInfluenceGpu>> skinInfluences{
        Core::Alloc::ScratchAllocator<DeformerSkinInfluenceGpu>(scratchArena)
    };
    Vector<DeformableJointMatrix, Core::Alloc::ScratchAllocator<DeformableJointMatrix>> jointMatrices{
        Core::Alloc::ScratchAllocator<DeformableJointMatrix>(scratchArena)
    };
    if(!__hidden_deformer_system::BuildSkinPayload(instance, jointPalette, skinInfluences, jointMatrices))
        return false;

    DeformableDisplacement resolvedDisplacement;
    if(!__hidden_deformer_system::ResolveDeformerDisplacement(instance, displacement, resolvedDisplacement))
        return false;

    const bool hasActiveMorphs = !morphRanges.empty();
    const bool hasActiveSkin = !skinInfluences.empty() && !jointMatrices.empty();
    const bool hasDisplacement = resolvedDisplacement.mode != DeformableDisplacementMode::None
        && DeformableValidation::ActiveWeight(resolvedDisplacement.amplitude)
    ;
    if(!hasActiveMorphs && !hasActiveSkin && !hasDisplacement){
        const bool deformerInputDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::DeformerInputDirty) != 0u;
        const auto foundResources = m_runtimeResources.find(instance.handle.value);
        if(foundResources == m_runtimeResources.end() && !deformerInputDirty)
            return false;

        if(foundResources != m_runtimeResources.end())
            m_runtimeResources.erase(foundResources);
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
    if(!ensureRuntimeResources(
        instance,
        payloadViews,
        resolvedDisplacement,
        hasDisplacement,
        morphSignature,
        resources
    ))
        return false;
    if(!resources
        || !resources->bindingSet
        || !resources->morphRangeBuffer
        || !resources->morphDeltaBuffer
        || !resources->skinBuffer
        || !resources->jointPaletteBuffer
        || !resources->displacementTexture
    )
        return false;

    usize rangeBytes = 0;
    usize deltaBytes = 0;
    usize jointPaletteBytes = 0;
    if(hasActiveMorphs){
        if(!__hidden_deformer_system::BufferPayloadBytes(
            morphRanges.size(),
            sizeof(DeformerVertexMorphRangeGpu),
            rangeBytes,
            NWB_TEXT("morph range")
        ))
            return false;
        if(!__hidden_deformer_system::BufferPayloadBytes(
            morphDeltas.size(),
            sizeof(DeformerBlendedMorphDeltaGpu),
            deltaBytes,
            NWB_TEXT("morph delta")
        ))
            return false;

        commandList.setBufferState(resources->morphRangeBuffer.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(resources->morphDeltaBuffer.get(), Core::ResourceStates::CopyDest);
    }
    if(hasActiveSkin){
        if(!__hidden_deformer_system::BufferPayloadBytes(
            jointMatrices.size(),
            sizeof(DeformableJointMatrix),
            jointPaletteBytes,
            NWB_TEXT("joint palette")
        ))
            return false;

        commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::CopyDest);
    }
    if(hasActiveMorphs || hasActiveSkin){
        commandList.commitBarriers();
        if(hasActiveMorphs){
            commandList.writeBuffer(resources->morphRangeBuffer.get(), morphRanges.data(), rangeBytes);
            commandList.writeBuffer(resources->morphDeltaBuffer.get(), morphDeltas.data(), deltaBytes);
        }
        if(hasActiveSkin)
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

    __hidden_deformer_system::DeformerPushConstants pushConstants;
    pushConstants.vertexCount = static_cast<u32>(instance.restVertices.size());
    pushConstants.morphRangeCount = static_cast<u32>(morphRanges.size());
    pushConstants.restScalarStride = __hidden_deformer_system::s_DeformableVertexScalarStride;
    pushConstants.deformedScalarStride = __hidden_deformer_system::s_DeformableVertexScalarStride;
    pushConstants.skinCount = static_cast<u32>(skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(jointMatrices.size());
    pushConstants.displacementAmplitude = hasDisplacement ? resolvedDisplacement.amplitude : 0.0f;
    pushConstants.displacementMode = hasDisplacement ? resolvedDisplacement.mode : DeformableDisplacementMode::None;
    pushConstants.displacementBias = hasDisplacement ? resolvedDisplacement.bias : 0.0f;
    pushConstants.displacementUvScaleX = hasDisplacement ? resolvedDisplacement.uvScale.x : 1.0f;
    pushConstants.displacementUvScaleY = hasDisplacement ? resolvedDisplacement.uvScale.y : 1.0f;
    pushConstants.displacementUvOffsetX = hasDisplacement ? resolvedDisplacement.uvOffset.x : 0.0f;
    pushConstants.displacementUvOffsetY = hasDisplacement ? resolvedDisplacement.uvOffset.y : 0.0f;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(__hidden_deformer_system::DispatchGroupCount(pushConstants.vertexCount), 1, 1);

    commandList.setBufferState(instance.deformedVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::DeformerInputDirty
    );
    return true;
}

bool DeformerSystem::copyRestToDeformed(Core::ICommandList& commandList, DeformableRuntimeMeshInstance& instance){
    usize copyBytes = 0;
    if(!__hidden_deformer_system::BufferPayloadBytes(
        instance.restVertices.size(),
        sizeof(DeformableVertexRest),
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
        instance.dirtyFlags & ~RuntimeMeshDirtyFlag::DeformerInputDirty
    );
    return true;
}

bool DeformerSystem::ensureRuntimeResources(
    DeformableRuntimeMeshInstance& instance,
    const RuntimePayloadViews& payloadViews,
    const DeformableDisplacement& displacement,
    const bool hasDisplacement,
    const usize morphSignature,
    RuntimeResources*& outResources)
{
    outResources = nullptr;
    if((payloadViews.morphRangeCount == 0u) != (payloadViews.morphDeltaCount == 0u)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' has mismatched morph range/delta payloads"),
            instance.handle.value
        );
        return false;
    }
    if((payloadViews.skinInfluenceCount == 0u) != (payloadViews.jointPaletteCount == 0u)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' has mismatched skin influence/joint payloads"),
            instance.handle.value
        );
        return false;
    }

    const bool hasActiveMorphs = payloadViews.hasActiveMorphs();
    const bool hasActiveSkin = payloadViews.hasActiveSkin();
    if(!hasActiveMorphs && !hasActiveSkin && !hasDisplacement)
        return false;
    if(hasActiveMorphs && payloadViews.morphRangeCount != instance.restVertices.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' has {} morph ranges for {} vertices"),
            instance.handle.value,
            payloadViews.morphRangeCount,
            instance.restVertices.size()
        );
        return false;
    }
    const bool hasTextureDisplacement = hasDisplacement
        && __hidden_deformer_system::DisplacementModeUsesTexture(displacement.mode)
    ;
    const Name displacementTextureName = hasTextureDisplacement ? displacement.texture.name() : NAME_NONE;

    if((hasActiveMorphs && (!payloadViews.morphRanges || !payloadViews.morphDeltas))
        || (hasActiveSkin && (!payloadViews.skinInfluences || !payloadViews.jointPalette))
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' has null active deformer payloads"),
            instance.handle.value
        );
        return false;
    }
    if(!ensureDefaultDeformerBuffers() || !ensureDefaultDisplacementResources())
        return false;

    if(payloadViews.morphRangeCount > static_cast<usize>(Limit<u32>::s_Max)
        || payloadViews.morphDeltaCount > static_cast<usize>(Limit<u32>::s_Max)
        || payloadViews.skinInfluenceCount > static_cast<usize>(Limit<u32>::s_Max)
        || payloadViews.jointPaletteCount > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' deformer payload exceeds u32 limits"),
            instance.handle.value
        );
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
        || resources.morphSignature != morphSignature
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

    const Name rangeBufferName = DeriveName(
        instance.source.name(),
        StringFormat(":runtime_{}_revision_{}_deformer_ranges", instance.handle.value, instance.editRevision)
    );
    const Name deltaBufferName = DeriveName(
        instance.source.name(),
        StringFormat(":runtime_{}_revision_{}_deformer_deltas", instance.handle.value, instance.editRevision)
    );
    const Name skinBufferName = DeriveName(
        instance.source.name(),
        StringFormat(":runtime_{}_revision_{}_deformer_skin", instance.handle.value, instance.editRevision)
    );
    const Name jointPaletteBufferName = DeriveName(
        instance.source.name(),
        StringFormat(":runtime_{}_revision_{}_deformer_joints", instance.handle.value, instance.editRevision)
    );
    if((hasActiveMorphs && (!rangeBufferName || !deltaBufferName))
        || (hasActiveSkin && (!skinBufferName || !jointPaletteBufferName))
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to derive deformer buffer names for runtime mesh '{}'"),
            instance.handle.value
        );
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
        ? __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            rangeBufferName,
            payloadViews.morphRanges,
            payloadViews.morphRangeCount,
            NWB_TEXT("morph range")
        )
        : m_defaultMorphRangeBuffer
    ;
    if(!rebuilt.morphRangeBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to create morph range buffer for runtime mesh '{}'"),
            instance.handle.value
        );
        return false;
    }

    rebuilt.morphDeltaBuffer = hasActiveMorphs
        ? __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            deltaBufferName,
            payloadViews.morphDeltas,
            payloadViews.morphDeltaCount,
            NWB_TEXT("morph delta")
        )
        : m_defaultMorphDeltaBuffer
    ;
    if(!rebuilt.morphDeltaBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to create morph delta buffer for runtime mesh '{}'"),
            instance.handle.value
        );
        return false;
    }

    rebuilt.skinBuffer = hasActiveSkin
        ? __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            skinBufferName,
            payloadViews.skinInfluences,
            payloadViews.skinInfluenceCount,
            NWB_TEXT("skin influence")
        )
        : m_defaultSkinBuffer
    ;
    if(!rebuilt.skinBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to create skin buffer for runtime mesh '{}'"),
            instance.handle.value
        );
        return false;
    }

    rebuilt.jointPaletteBuffer = hasActiveSkin
        ? __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            jointPaletteBufferName,
            payloadViews.jointPalette,
            payloadViews.jointPaletteCount,
            NWB_TEXT("joint palette")
        )
        : m_defaultJointPaletteBuffer
    ;
    if(!rebuilt.jointPaletteBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to create joint palette buffer for runtime mesh '{}'"),
            instance.handle.value
        );
        return false;
    }

    if(hasTextureDisplacement){
        if(!ensureDisplacementTexture(displacement, rebuilt.displacementTexture)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: failed to load displacement texture '{}' for runtime mesh '{}'"),
                StringConvert(displacement.texture.name().c_str()),
                instance.handle.value
            );
            return false;
        }
    }
    else{
        rebuilt.displacementTexture = m_defaultDisplacementTexture;
    }
    if(!rebuilt.displacementTexture){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to resolve displacement texture resource for runtime mesh '{}'"),
            instance.handle.value
        );
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
    rebuilt.bindingSet = device->createBindingSet(bindingSetDesc, m_bindingLayout.get());
    if(!rebuilt.bindingSet){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to create binding set for runtime mesh '{}'"),
            instance.handle.value
        );
        return false;
    }

    resources = Move(rebuilt);
    outResources = &resources;
    return true;
}

bool DeformerSystem::ensureDefaultDeformerBuffers(){
    if(m_defaultMorphRangeBuffer
        && m_defaultMorphDeltaBuffer
        && m_defaultSkinBuffer
        && m_defaultJointPaletteBuffer
    )
        return true;

    const DeformerVertexMorphRangeGpu defaultRange{};
    const DeformerBlendedMorphDeltaGpu defaultDelta{};
    const DeformerSkinInfluenceGpu defaultSkin{};
    const DeformableJointMatrix defaultJoint{};

    if(!m_defaultMorphRangeBuffer){
        m_defaultMorphRangeBuffer = __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/deformer_default_morph_range"),
            &defaultRange,
            1u,
            NWB_TEXT("default morph range")
        );
        if(!m_defaultMorphRangeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create default morph range buffer"));
            return false;
        }
    }

    if(!m_defaultMorphDeltaBuffer){
        m_defaultMorphDeltaBuffer = __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/deformer_default_morph_delta"),
            &defaultDelta,
            1u,
            NWB_TEXT("default morph delta")
        );
        if(!m_defaultMorphDeltaBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create default morph delta buffer"));
            return false;
        }
    }

    if(!m_defaultSkinBuffer){
        m_defaultSkinBuffer = __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/deformer_default_skin"),
            &defaultSkin,
            1u,
            NWB_TEXT("default skin")
        );
        if(!m_defaultSkinBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create default skin buffer"));
            return false;
        }
    }

    if(!m_defaultJointPaletteBuffer){
        m_defaultJointPaletteBuffer = __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            Name("engine/graphics/deformer_default_joint"),
            &defaultJoint,
            1u,
            NWB_TEXT("default joint")
        );
        if(!m_defaultJointPaletteBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create default joint palette buffer"));
            return false;
        }
    }

    return true;
}

bool DeformerSystem::ensureDefaultDisplacementResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_displacementSampler){
        Core::SamplerDesc samplerDesc;
        samplerDesc
            .setAllFilters(false)
            .setAllAddressModes(Core::SamplerAddressMode::Clamp)
        ;
        m_displacementSampler = device->createSampler(samplerDesc);
        if(!m_displacementSampler){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create displacement sampler"));
            return false;
        }
    }

    if(!m_defaultDisplacementTexture){
        const Float4U defaultTexel(0.0f, 0.0f, 0.0f, 0.0f);
        m_defaultDisplacementTexture = __hidden_deformer_system::SetupDisplacementTexture(
            m_graphics,
            Name("engine/graphics/deformer_default_displacement_texture"),
            1u,
            1u,
            &defaultTexel,
            1u
        );
        if(!m_defaultDisplacementTexture){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create default displacement texture"));
            return false;
        }
    }

    return true;
}

bool DeformerSystem::ensureDisplacementTexture(
    const DeformableDisplacement& displacement,
    Core::TextureHandle& outTexture)
{
    outTexture = nullptr;
    if(!__hidden_deformer_system::DisplacementModeUsesTexture(displacement.mode)){
        outTexture = m_defaultDisplacementTexture;
        return static_cast<bool>(outTexture);
    }
    if(!displacement.texture.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: displacement texture asset ref is empty"));
        return false;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(DeformableDisplacementTexture::AssetTypeName(), displacement.texture.name(), loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: failed to load displacement texture asset '{}'"),
            StringConvert(displacement.texture.name().c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != DeformableDisplacementTexture::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: asset '{}' is not a deformable displacement texture"),
            StringConvert(displacement.texture.name().c_str())
        );
        return false;
    }

    const DeformableDisplacementTexture& textureAsset = static_cast<const DeformableDisplacementTexture&>(*loadedAsset);
    if(!textureAsset.validatePayload())
        return false;

    outTexture = __hidden_deformer_system::SetupDisplacementTexture(
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

