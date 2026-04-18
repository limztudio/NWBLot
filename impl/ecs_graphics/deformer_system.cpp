// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformer_system.h"

#include "renderer_system.h"

#include <core/graphics/shader_archive.h>
#include <impl/assets_graphics/shader_asset.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformer_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_DeformerGroupSize = 64u;
static constexpr u32 s_DeformableVertexScalarStride = sizeof(DeformableVertexRest) / sizeof(f32);
static constexpr f32 s_ActiveMorphWeightEpsilon = 0.000001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;

struct DeformerPushConstants{
    u32 vertexCount = 0;
    u32 morphCount = 0;
    u32 restScalarStride = 0;
    u32 deformedScalarStride = 0;
    u32 skinCount = 0;
    u32 jointCount = 0;
    f32 displacementAmplitude = 0.0f;
    u32 displacementMode = DeformableDisplacementMode::None;
};
static_assert(sizeof(DeformerPushConstants) == 32, "Deformer push constants layout must stay shader-compatible");

static const Name& DeformerComputeShaderName(){
    static const Name s("engine/graphics/deformer_cs");
    return s;
}

static const Name& StageNameFromShaderType(const Core::ShaderType::Mask shaderType){
    switch(shaderType){
        case Core::ShaderType::Compute: { static const Name s("cs"); return s; }
        default: return NAME_NONE;
    }
}

static bool ActiveWeight(const f32 weight){
    return weight > s_ActiveMorphWeightEpsilon || weight < -s_ActiveMorphWeightEpsilon;
}

static bool ActiveDisplacement(const DeformableDisplacement& displacement){
    return displacement.mode != DeformableDisplacementMode::None && ActiveWeight(displacement.amplitude);
}

static bool NearlyOne(const f32 value){
    const f32 difference = value > 1.0f ? value - 1.0f : 1.0f - value;
    return difference <= s_SkinWeightSumEpsilon;
}

static bool ResolveDisplacement(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableDisplacementComponent* component,
    DeformableDisplacement& outDisplacement)
{
    outDisplacement = instance.displacement;
    if(outDisplacement.mode == DeformableDisplacementMode::None)
        return true;
    if(component && !component->enabled){
        outDisplacement = DeformableDisplacement{};
        return true;
    }

    const f32 scale = component ? component->amplitudeScale : 1.0f;
    if(!IsFinite(scale)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' displacement amplitude scale is invalid"),
            instance.handle.value
        );
        return false;
    }

    outDisplacement.amplitude *= scale;
    if(!IsFinite(outDisplacement.amplitude)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' effective displacement amplitude is invalid"),
            instance.handle.value
        );
        return false;
    }
    if(!ActiveWeight(outDisplacement.amplitude))
        outDisplacement = DeformableDisplacement{};
    return true;
}

static bool IsFiniteFloat4Data(const Float4Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

static bool IsFiniteFloat3Data(const Float3Data& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

static bool IsAffineJointMatrix(const DeformableJointMatrix& matrix){
    return IsFiniteFloat4Data(matrix.column0)
        && IsFiniteFloat4Data(matrix.column1)
        && IsFiniteFloat4Data(matrix.column2)
        && IsFiniteFloat4Data(matrix.column3)
        && !ActiveWeight(matrix.column0.w)
        && !ActiveWeight(matrix.column1.w)
        && !ActiveWeight(matrix.column2.w)
        && !ActiveWeight(matrix.column3.w - 1.0f)
    ;
}

static u32 DispatchGroupCount(const u32 vertexCount){
    return vertexCount == 0
        ? 0
        : ((vertexCount - 1u) / s_DeformerGroupSize) + 1u
    ;
}

static void HashCombine(usize& seed, const usize value){
    seed ^= value
        + static_cast<usize>(0x9e3779b97f4a7c15ull)
        + (seed << 6)
        + (seed >> 2)
    ;
}

static bool ResolveMorphWeight(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* weights,
    const Name& morphName,
    f32& outWeight)
{
    outWeight = 0.0f;
    if(!weights || !morphName)
        return true;

    for(const DeformableMorphWeight& weight : weights->weights){
        if(weight.morph != morphName)
            continue;
        if(!IsFinite(weight.weight)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' morph '{}' weight is invalid"),
                instance.handle.value,
                StringConvert(morphName.c_str())
            );
            return false;
        }

        outWeight += weight.weight;
        if(!IsFinite(outWeight)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' morph '{}' accumulated weight is invalid"),
                instance.handle.value,
                StringConvert(morphName.c_str())
            );
            return false;
        }
    }
    return true;
}

static Float4Data ExpandFloat3Delta(const Float3Data& value){
    return Float4Data(value.x, value.y, value.z, 0.0f);
}

static bool BuildMorphPayload(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* morphWeights,
    Vector<DeformerSystem::DeformerMorphRangeGpu>& outRanges,
    Vector<DeformerSystem::DeformerMorphDeltaGpu>& outDeltas,
    usize& outSignature)
{
    outRanges.clear();
    outDeltas.clear();
    outSignature = 0;

    for(const DeformableMorph& morph : instance.morphs){
        f32 weight = 0.0f;
        if(!ResolveMorphWeight(instance, morphWeights, morph.name, weight))
            return false;
        if(!ActiveWeight(weight))
            continue;
        if(morph.deltas.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: active morph '{}' on runtime mesh '{}' has no deltas"),
                StringConvert(morph.name.c_str()),
                instance.handle.value
            );
            return false;
        }
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)
            || outDeltas.size() > static_cast<usize>(Limit<u32>::s_Max) - morph.deltas.size()
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: morph '{}' on runtime mesh '{}' exceeds u32 delta limits"),
                StringConvert(morph.name.c_str()),
                instance.handle.value
            );
            return false;
        }

        DeformerSystem::DeformerMorphRangeGpu range;
        range.firstDelta = static_cast<u32>(outDeltas.size());
        range.deltaCount = static_cast<u32>(morph.deltas.size());
        range.weight = weight;
        outRanges.push_back(range);

        HashCombine(outSignature, Hasher<Name>{}(morph.name));
        HashCombine(outSignature, static_cast<usize>(range.deltaCount));

        for(const DeformableMorphDelta& delta : morph.deltas){
            if(delta.vertexId >= instance.restVertices.size()
                || !IsFiniteFloat3Data(delta.deltaPosition)
                || !IsFiniteFloat3Data(delta.deltaNormal)
                || !IsFiniteFloat4Data(delta.deltaTangent)
            ){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformerSystem: morph '{}' on runtime mesh '{}' contains an invalid delta"),
                    StringConvert(morph.name.c_str()),
                    instance.handle.value
                );
                return false;
            }

            DeformerSystem::DeformerMorphDeltaGpu gpuDelta;
            gpuDelta.vertexId = delta.vertexId;
            gpuDelta.deltaPosition = ExpandFloat3Delta(delta.deltaPosition);
            gpuDelta.deltaNormal = ExpandFloat3Delta(delta.deltaNormal);
            gpuDelta.deltaTangent = delta.deltaTangent;
            outDeltas.push_back(gpuDelta);
        }
    }

    return true;
}

static bool BuildSkinPayload(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableJointPaletteComponent* jointPalette,
    Vector<DeformerSystem::DeformerSkinInfluenceGpu>& outSkinInfluences,
    Vector<DeformableJointMatrix>& outJointPalette)
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

    for(usize jointIndex = 0; jointIndex < jointPalette->joints.size(); ++jointIndex){
        if(!IsAffineJointMatrix(jointPalette->joints[jointIndex])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' joint palette entry {} is not a finite affine matrix"),
                instance.handle.value,
                jointIndex
            );
            return false;
        }
    }

    outSkinInfluences.reserve(instance.skin.size());
    for(usize vertexIndex = 0; vertexIndex < instance.skin.size(); ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        DeformerSystem::DeformerSkinInfluenceGpu gpuSkin;
        f32 weightSum = 0.0f;
        for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
            const u32 joint = static_cast<u32>(sourceSkin.joint[influenceIndex]);
            const f32 weight = sourceSkin.weight[influenceIndex];
            if(!IsFinite(weight) || weight < 0.0f){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} skin weight {} is invalid"),
                    instance.handle.value,
                    vertexIndex,
                    influenceIndex
                );
                return false;
            }

            weightSum += weight;
            if(!IsFinite(weightSum)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} skin weight sum is invalid"),
                    instance.handle.value,
                    vertexIndex
                );
                return false;
            }
            if(ActiveWeight(weight) && joint >= jointPalette->joints.size()){
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
            gpuSkin.weight[influenceIndex] = weight;
        }
        if(!NearlyOne(weightSum)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} skin weights sum to {}"),
                instance.handle.value,
                vertexIndex,
                weightSum
            );
            return false;
        }
        outSkinInfluences.push_back(gpuSkin);
    }

    outJointPalette = jointPalette->joints;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(DeformerSystem::DeformerMorphRangeGpu) == sizeof(f32) * 4u, "Deformer morph range GPU layout drifted");
static_assert(sizeof(DeformerSystem::DeformerMorphDeltaGpu) == sizeof(f32) * 16u, "Deformer morph delta GPU layout drifted");
static_assert(sizeof(DeformerSystem::DeformerSkinInfluenceGpu) == sizeof(f32) * 8u, "Deformer skin influence GPU layout drifted");


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
    (void)world;
    (void)delta;
}

void DeformerSystem::render(Core::IFramebuffer* framebuffer){
    (void)framebuffer;

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<
        Core::ECS::EntityID,
        Core::Alloc::ScratchAllocator<Core::ECS::EntityID>
    > candidates{Core::Alloc::ScratchAllocator<Core::ECS::EntityID>(scratchArena)};
    HashSet<
        u64,
        Hasher<u64>,
        EqualTo<u64>,
        Core::Alloc::ScratchAllocator<u64>
    > liveHandles(
        0,
        Hasher<u64>(),
        EqualTo<u64>(),
        Core::Alloc::ScratchAllocator<u64>(scratchArena)
    );

    m_world.view<DeformableRendererComponent>().each(
        [&](Core::ECS::EntityID entity, DeformableRendererComponent& renderer){
            if(!renderer.runtimeMesh.valid())
                return;

            DeformableRuntimeMeshInstance* instance = m_rendererSystem.findDeformableRuntimeMesh(renderer.runtimeMesh);
            if(!instance || !instance->valid())
                return;

            liveHandles.insert(renderer.runtimeMesh.value);
            if(renderer.visible)
                candidates.push_back(entity);
        }
    );

    Vector<u64, Core::Alloc::ScratchAllocator<u64>> staleResources{
        Core::Alloc::ScratchAllocator<u64>(scratchArena)
    };
    for(const auto& [handle, resources] : m_runtimeResources){
        const bool live = liveHandles.find(handle) != liveHandles.end();
        const DeformableRuntimeMeshInstance* instance = live
            ? m_rendererSystem.findDeformableRuntimeMesh(resources.handle)
            : nullptr
        ;
        if(!live || !instance || instance->editRevision != resources.editRevision)
            staleResources.push_back(handle);
    }
    for(const u64 handle : staleResources)
        m_runtimeResources.erase(handle);

    if(candidates.empty())
        return;

    Core::IDevice* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: failed to create command list"));
        return;
    }

    bool submittedWork = false;
    commandList->open();
    for(const Core::ECS::EntityID entity : candidates){
        DeformableRendererComponent* renderer = m_world.tryGetComponent<DeformableRendererComponent>(entity);
        if(!renderer || !renderer->visible || !renderer->runtimeMesh.valid())
            continue;

        DeformableRuntimeMeshInstance* instance = m_rendererSystem.findDeformableRuntimeMesh(renderer->runtimeMesh);
        if(!instance || !instance->valid())
            continue;

        const DeformableMorphWeightsComponent* morphWeights =
            m_world.tryGetComponent<DeformableMorphWeightsComponent>(entity)
        ;
        const DeformableJointPaletteComponent* jointPalette =
            m_world.tryGetComponent<DeformableJointPaletteComponent>(entity)
        ;
        const DeformableDisplacementComponent* displacement =
            m_world.tryGetComponent<DeformableDisplacementComponent>(entity)
        ;
        if(dispatchRuntimeMesh(*commandList, *instance, morphWeights, jointPalette, displacement))
            submittedWork = true;
    }
    commandList->close();

    if(submittedWork)
        device->executeCommandList(commandList.get());
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
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_deformer_system::DeformerPushConstants)));

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

    const Name& stageName = __hidden_deformer_system::StageNameFromShaderType(shaderType);
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

    Vector<DeformerMorphRangeGpu> morphRanges;
    Vector<DeformerMorphDeltaGpu> morphDeltas;
    usize morphSignature = 0;
    if(!__hidden_deformer_system::BuildMorphPayload(instance, morphWeights, morphRanges, morphDeltas, morphSignature))
        return false;

    Vector<DeformerSkinInfluenceGpu> skinInfluences;
    Vector<DeformableJointMatrix> jointMatrices;
    if(!__hidden_deformer_system::BuildSkinPayload(instance, jointPalette, skinInfluences, jointMatrices))
        return false;

    DeformableDisplacement resolvedDisplacement;
    if(!__hidden_deformer_system::ResolveDisplacement(instance, displacement, resolvedDisplacement))
        return false;

    const bool hasActiveMorphs = !morphRanges.empty();
    const bool hasActiveSkin = !skinInfluences.empty() && !jointMatrices.empty();
    const bool hasDisplacement = __hidden_deformer_system::ActiveDisplacement(resolvedDisplacement);
    if(!hasActiveMorphs && !hasActiveSkin && !hasDisplacement){
        const bool deformerInputDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::DeformerInputDirty) != 0u;
        const auto foundResources = m_runtimeResources.find(instance.handle.value);
        if(foundResources == m_runtimeResources.end() && !deformerInputDirty)
            return false;

        if(foundResources != m_runtimeResources.end())
            m_runtimeResources.erase(instance.handle.value);
        return copyRestToDeformed(commandList, instance);
    }
    if(!ensurePipeline())
        return false;
    if(!m_computePipeline)
        return false;

    RuntimeResources* resources = nullptr;
    if(!ensureRuntimeResources(
        instance,
        morphRanges,
        morphDeltas,
        skinInfluences,
        jointMatrices,
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
    )
        return false;

    usize rangeBytes = 0;
    usize jointPaletteBytes = 0;
    if(hasActiveMorphs){
        if(!__hidden_deformer_system::BufferPayloadBytes(
            morphRanges.size(),
            sizeof(DeformerMorphRangeGpu),
            rangeBytes,
            NWB_TEXT("morph range")
        ))
            return false;

        commandList.setBufferState(resources->morphRangeBuffer.get(), Core::ResourceStates::CopyDest);
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
        if(hasActiveMorphs)
            commandList.writeBuffer(resources->morphRangeBuffer.get(), morphRanges.data(), rangeBytes);
        if(hasActiveSkin)
            commandList.writeBuffer(resources->jointPaletteBuffer.get(), jointMatrices.data(), jointPaletteBytes);
    }

    commandList.setBufferState(instance.restVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.deformedVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(resources->morphRangeBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->morphDeltaBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->skinBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_computePipeline.get());
    computeState.addBindingSet(resources->bindingSet.get());
    commandList.setComputeState(computeState);

    __hidden_deformer_system::DeformerPushConstants pushConstants;
    pushConstants.vertexCount = static_cast<u32>(instance.restVertices.size());
    pushConstants.morphCount = static_cast<u32>(morphRanges.size());
    pushConstants.restScalarStride = __hidden_deformer_system::s_DeformableVertexScalarStride;
    pushConstants.deformedScalarStride = __hidden_deformer_system::s_DeformableVertexScalarStride;
    pushConstants.skinCount = static_cast<u32>(skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(jointMatrices.size());
    pushConstants.displacementAmplitude = hasDisplacement ? resolvedDisplacement.amplitude : 0.0f;
    pushConstants.displacementMode = hasDisplacement ? resolvedDisplacement.mode : DeformableDisplacementMode::None;
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
    const Vector<DeformerMorphRangeGpu>& morphRanges,
    const Vector<DeformerMorphDeltaGpu>& morphDeltas,
    const Vector<DeformerSkinInfluenceGpu>& skinInfluences,
    const Vector<DeformableJointMatrix>& jointPalette,
    const bool hasDisplacement,
    const usize morphSignature,
    RuntimeResources*& outResources)
{
    outResources = nullptr;
    const bool hasActiveMorphs = !morphRanges.empty() && !morphDeltas.empty();
    const bool hasActiveSkin = !skinInfluences.empty() && !jointPalette.empty();
    if(!hasActiveMorphs && !hasActiveSkin && !hasDisplacement)
        return false;
    if(morphRanges.empty() != morphDeltas.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' has mismatched morph range/delta payloads"),
            instance.handle.value
        );
        return false;
    }
    if(!ensureDefaultDeformerBuffers())
        return false;

    if(morphRanges.size() > static_cast<usize>(Limit<u32>::s_Max)
        || morphDeltas.size() > static_cast<usize>(Limit<u32>::s_Max)
        || skinInfluences.size() > static_cast<usize>(Limit<u32>::s_Max)
        || jointPalette.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' deformer payload exceeds u32 limits"),
            instance.handle.value
        );
        return false;
    }

    auto [it, inserted] = m_runtimeResources.emplace(instance.handle.value, RuntimeResources{});
    RuntimeResources& resources = it.value();
    const bool rebuild =
        inserted
        || resources.editRevision != instance.editRevision
        || resources.vertexCount != static_cast<u32>(instance.restVertices.size())
        || resources.morphCount != static_cast<u32>(morphRanges.size())
        || resources.deltaCount != static_cast<u32>(morphDeltas.size())
        || resources.skinCount != static_cast<u32>(skinInfluences.size())
        || resources.jointCount != static_cast<u32>(jointPalette.size())
        || resources.morphSignature != morphSignature
        || !resources.morphRangeBuffer
        || !resources.morphDeltaBuffer
        || !resources.skinBuffer
        || !resources.jointPaletteBuffer
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
    rebuilt.morphCount = static_cast<u32>(morphRanges.size());
    rebuilt.deltaCount = static_cast<u32>(morphDeltas.size());
    rebuilt.skinCount = static_cast<u32>(skinInfluences.size());
    rebuilt.jointCount = static_cast<u32>(jointPalette.size());
    rebuilt.morphSignature = morphSignature;

    rebuilt.morphRangeBuffer = hasActiveMorphs
        ? __hidden_deformer_system::SetupStructuredBuffer(
            m_graphics,
            rangeBufferName,
            morphRanges.data(),
            morphRanges.size(),
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
            morphDeltas.data(),
            morphDeltas.size(),
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
            skinInfluences.data(),
            skinInfluences.size(),
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
            jointPalette.data(),
            jointPalette.size(),
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

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, instance.restVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, instance.deformedVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, rebuilt.morphRangeBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, rebuilt.morphDeltaBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(4, rebuilt.skinBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, rebuilt.jointPaletteBuffer.get()));

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

    const DeformerMorphRangeGpu defaultRange{};
    const DeformerMorphDeltaGpu defaultDelta{};
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
