// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_frame_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
static void UpdateCacheHashValue(u64& inOutHash, const T& value){
    static_assert(IsTriviallyCopyable_V<T>, "CSG frame-state cache signature values must be trivially copyable");
    inOutHash = UpdateFnv64(inOutHash, reinterpret_cast<const u8*>(&value), sizeof(value));
}

static void UpdateCacheHashName(u64& inOutHash, const Name& name){
    UpdateCacheHashValue(inOutHash, name.hash());
}

static void UpdateCacheHashBytes(u64& inOutHash, const u8* bytes, const usize byteCount){
    UpdateCacheHashValue(inOutHash, byteCount);
    inOutHash = UpdateFnv64(inOutHash, bytes, byteCount);
}

static void UpdateCacheHashBool(u64& inOutHash, const bool value){
    const u8 byteValue = value ? 1u : 0u;
    UpdateCacheHashValue(inOutHash, byteValue);
}

[[nodiscard]] static bool BuildCsgFrameStateCacheSignature(
    Core::ECS::World& world,
    CsgFrameStateCacheSignature& outSignature
){
    outSignature = CsgFrameStateCacheSignature{};

    if(world.view<SkinnedCsgMeshComponent>().candidateCount() > 0u)
        return false;

    u64 contentHash = FNV64_OFFSET_BASIS;
    auto cutterView = world.view<CsgCutterComponent>();
    cutterView.each(
        [&](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
            UpdateCacheHashValue(contentHash, entity.id);
            UpdateCacheHashBool(contentHash, cutter.active);
            UpdateCacheHashName(contentHash, cutter.receiverGroup);
            UpdateCacheHashName(contentHash, cutter.shapeType);
            UpdateCacheHashValue(contentHash, cutter.worldToShape);
            UpdateCacheHashValue(contentHash, cutter.shapeToWorld);
            UpdateCacheHashBytes(contentHash, cutter.parameterBytes.data(), cutter.parameterBytes.size());
        }
    );

    auto receiverView = world.view<StaticCsgMeshComponent>();
    bool cacheable = true;
    receiverView.each(
        [&](const Core::ECS::EntityID entity, StaticCsgMeshComponent& typedReceiver){
            const CsgReceiverComponent& receiver = typedReceiver;
            UpdateCacheHashValue(contentHash, entity.id);
            UpdateCacheHashBool(contentHash, receiver.enabled);
            UpdateCacheHashBool(contentHash, receiver.affectOpaquePass);
            UpdateCacheHashBool(contentHash, receiver.affectTransparentPass);
            UpdateCacheHashName(contentHash, receiver.receiverGroup);

            const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
            UpdateCacheHashBool(contentHash, renderer != nullptr);
            if(renderer){
                UpdateCacheHashBool(contentHash, renderer->visible);
                UpdateCacheHashName(contentHash, renderer->material.name());
            }

            const MeshComponent* mesh = world.tryGetComponent<MeshComponent>(entity);
            UpdateCacheHashBool(contentHash, mesh != nullptr);
            if(mesh){
                UpdateCacheHashName(contentHash, mesh->mesh.name());
            }
            else if(receiver.enabled && renderer){
                cacheable = false;
            }

            const Scene::TransformComponent* transform = world.tryGetComponent<Scene::TransformComponent>(entity);
            UpdateCacheHashBool(contentHash, transform != nullptr);
            if(transform){
                UpdateCacheHashValue(contentHash, transform->position);
                UpdateCacheHashValue(contentHash, transform->rotation);
                UpdateCacheHashValue(contentHash, transform->scale);
            }
        }
    );
    if(!cacheable)
        return false;

    outSignature.contentHash = contentHash;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState RendererCsgSystem::buildFrameState(Core::Alloc::ScratchArena& scratchArena){
    CsgFrameStateCacheSignature signature;
    const bool cacheable = __hidden_csg_frame_state::BuildCsgFrameStateCacheSignature(world(), signature);
    if(cacheable && csgState().m_frameStateCacheValid && csgState().m_frameStateCacheSignature == signature)
        return csgState().m_frameStateCache;

    bool frameStateCacheable = cacheable;
    CsgFrameState state;
    auto finishFrameState = [&](const CsgFrameState& frameState) -> CsgFrameState{
        if(frameStateCacheable){
            csgState().m_frameStateCacheSignature = signature;
            csgState().m_frameStateCache = frameState;
            csgState().m_frameStateCacheValid = true;
        }
        else{
            csgState().m_frameStateCacheValid = false;
        }
        return frameState;
    };

    CsgFrameReceiverLookup receiverLookup(world(), scratchArena);
    if(receiverLookup.empty())
        return finishFrameState(state);

    auto* ecsMeshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!ecsMeshSystem){
        frameStateCacheable = false;
        return finishFrameState(state);
    }

    auto rendererView = world().view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        CsgReceiverKind::Enum receiverKind = CsgReceiverKind::Static;
        const CsgReceiverComponent* receiver = ResolveCsgReceiverComponent(world(), entity, receiverKind);
        if(!receiver || !receiver->enabled)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!ecsMeshSystem->resolveRenderableMesh(entity, resolvedMesh)){
            frameStateCacheable = false;
            continue;
        }

        MeshResources* mesh = nullptr;
        if(resolvedMesh.runtime){
            frameStateCacheable = false;
            if(!m_renderer.meshSystem().createRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh))
                continue;
        }
        else if(!m_renderer.meshSystem().createMeshResources(resolvedMesh.mesh, mesh)){
            frameStateCacheable = false;
            continue;
        }
        NWB_ASSERT(mesh);

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!m_renderer.materialSystem().createMaterialSurfaceInfo(renderer.material, materialInfo)){
            frameStateCacheable = false;
            continue;
        }
        NWB_ASSERT(materialInfo);

        const Scene::TransformComponent* transform = world().tryGetComponent<Scene::TransformComponent>(entity);

        auto countPassCutters = [&](const CsgReceiverPass::Enum pass) -> u32{
            CsgReceiverDrawState drawState;
            if(!receiverLookup.resolveReceiverDrawState(entity, pass, drawState) || drawState.receiverKind != receiverKind)
                return 0u;

            CsgReceiverClipDrawInfo clipInfo;
            if(!resolveCsgReceiverClipDrawInfo(receiverLookup, entity, mesh->csgLocalBounds, transform, clipInfo))
                return 0u;
            if(clipInfo.cutterCount == 0u)
                return 0u;

            return clipInfo.cutterCount;
        };

        const u32 opaqueCutterCount =
            !materialInfo->transparent && receiver->affectOpaquePass
            ? countPassCutters(CsgReceiverPass::Opaque)
            : 0u
        ;
        const u32 transparentCutterCount =
            materialInfo->transparent && receiver->affectTransparentPass
            ? countPassCutters(CsgReceiverPass::Transparent)
            : 0u
        ;
        const bool opaqueWork = opaqueCutterCount > 0u;
        const bool transparentWork = transparentCutterCount > 0u;
        if(!opaqueWork && !transparentWork)
            continue;

        AddCsgFrameReceiverWork(
            state,
            receiverKind,
            opaqueWork,
            transparentWork,
            Max(opaqueCutterCount, transparentCutterCount)
        );
    }

    FinalizeCsgFrameState(state);
    return finishFrameState(state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

