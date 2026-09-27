// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_cutter_snapshot.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_cutter_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u32 ResolveShapeKind(const CsgShapeTypeInfo& shapeType){
    if(shapeType.desc.shaderModule)
        return NWB_CSG_SHADOW_SHAPE_UNSUPPORTED;
    const Name shapeName = shapeType.desc.name;
    if(shapeName == s_CsgPlaneShapeName)
        return NWB_CSG_SHADOW_SHAPE_PLANE;
    if(shapeName == s_CsgBoxShapeName)
        return NWB_CSG_SHADOW_SHAPE_BOX;
    if(shapeName == s_CsgSphereShapeName)
        return NWB_CSG_SHADOW_SHAPE_SPHERE;
    if(shapeName == s_CsgCapsuleShapeName)
        return NWB_CSG_SHADOW_SHAPE_CAPSULE;
    return NWB_CSG_SHADOW_SHAPE_UNSUPPORTED;
}

[[nodiscard]] bool ResolveParameters(
    const CsgShapeTypeInfo& shapeType, const CsgCutterComponent& cutter,
    const u8*& parameterBytes, usize& parameterByteCount){
    if(cutter.parameterBytes.empty()){
        parameterBytes = shapeType.desc.defaultParameterBytes.data();
        parameterByteCount = shapeType.desc.defaultParameterBytes.size();
    }
    else{
        parameterBytes = cutter.parameterBytes.data();
        parameterByteCount = cutter.parameterBytes.size();
    }
    return parameterByteCount == shapeType.desc.parameterByteSize && (parameterByteCount == 0u || parameterBytes);
}

void AppendReceiverCutters(
    const CsgShadowReceiverInput& receiver, const CsgReceiverDrawState& drawState,
    const CsgFrameReceiverLookup& receiverLookup, const CsgShapeRegistry& shapeRegistry,
    CsgShadowSnapshot& snapshot, CsgShadowReceiverRangeGpu& range){
    const SIMDVector receiverMin = LoadFloat(receiver.worldMin);
    const SIMDVector receiverMax = LoadFloat(receiver.worldMax);
    const bool boundsCanCull = receiver.boundsValid && AabbTests::Valid(receiverMin, receiverMax);
    range.firstCutter = static_cast<u32>(snapshot.cutters.size());
    receiverLookup.forEachReceiverCutter(drawState, [&](const Core::ECS::EntityID entity, const CsgCutterComponent& cutter){
        CsgShapeTypeInfo shapeType;
        if(!shapeRegistry.findShapeType(cutter.shapeType, shapeType))
            return;
        const u8* parameterBytes = nullptr;
        usize parameterByteCount = 0u;
        if(!ResolveParameters(shapeType, cutter, parameterBytes, parameterByteCount))
            return;

        SIMDVector cutterMin;
        SIMDVector cutterMax;
        bool finiteBounds = false;
        const SIMDMatrix shapeToWorld = LoadFloat(cutter.shapeToWorld);
        if(!shapeRegistry.buildShapeBounds(
            shapeType.id, shapeToWorld, parameterBytes, parameterByteCount, cutterMin, cutterMax, finiteBounds
        ))
            return;
        if(boundsCanCull && finiteBounds && !AabbTests::Intersects(receiverMin, receiverMax, cutterMin, cutterMax))
            return;

        Fnv64AppendValue(snapshot.contentIdentity, entity.id);
        Fnv64AppendValue(snapshot.contentIdentity, cutter.shapeType.hash());
        Fnv64AppendBuffer(snapshot.contentIdentity, parameterBytes, parameterByteCount);
        Fnv64AppendValue(snapshot.identity, entity.id);
        Fnv64AppendValue(snapshot.identity, cutter.shapeType.hash());
        Fnv64AppendValue(snapshot.identity, cutter.worldToShape);
        Fnv64AppendValue(snapshot.identity, cutter.shapeToWorld);
        Fnv64AppendBuffer(snapshot.identity, parameterBytes, parameterByteCount);
        range.flags |= NWB_CSG_SHADOW_RECEIVER_ACTIVE;
        const u32 shapeKind = ResolveShapeKind(shapeType);
        const SIMDMatrix worldToShape = LoadFloat(cutter.worldToShape);
        if(
            shapeKind == NWB_CSG_SHADOW_SHAPE_UNSUPPORTED
            || parameterByteCount != sizeof(Float4)
            || MatrixIsNaN(worldToShape) || MatrixIsInfinite(worldToShape)
            || MatrixIsNaN(shapeToWorld) || MatrixIsInfinite(shapeToWorld)
            || range.cutterCount == NWB_CSG_SHADOW_MAX_CUTTERS
        ){
            range.flags |= NWB_CSG_SHADOW_RECEIVER_UNSUPPORTED;
            return;
        }
        if((range.flags & NWB_CSG_SHADOW_RECEIVER_UNSUPPORTED) != 0u)
            return;

        CsgCutterGpuData gpuCutter;
        gpuCutter.shapeType = shapeKind;
        gpuCutter.worldToShape = cutter.worldToShape;
        NWB_MEMCPY(&gpuCutter.parameter0, sizeof(Float4), parameterBytes, parameterByteCount);
        snapshot.cutters.push_back(gpuCutter);
        ++range.cutterCount;
    });
    if((range.flags & NWB_CSG_SHADOW_RECEIVER_UNSUPPORTED) != 0u){
        snapshot.cutters.resize(range.firstCutter);
        range.cutterCount = 0u;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildCsgShadowSnapshot(
    Core::ECS::World& world, const CsgShapeRegistry& shapeRegistry,
    const CsgShadowReceiverInput* const receivers, const usize receiverCount,
    Core::Alloc::ScratchArena& scratchArena, CsgShadowSnapshot& outSnapshot){
    outSnapshot.receiverRanges.clear();
    outSnapshot.cutters.clear();
    outSnapshot.identity = 0u;
    outSnapshot.contentIdentity = 0u;
    outSnapshot.hasCsg = false;
    if(receiverCount > static_cast<usize>(Limit<u32>::s_Max) || (receiverCount != 0u && !receivers))
        return false;
    if(receiverCount == 0u || !HasCsgFrameCandidates(world))
        return true;

    const CsgFrameReceiverLookup receiverLookup(world, scratchArena);
    if(receiverLookup.empty())
        return true;
    Vector<CsgReceiverDrawState, Core::Alloc::ScratchArena> drawStates(scratchArena);
    drawStates.resize(receiverCount);
    bool hasReceiver = false;
    usize cutterCapacity = 0u;
    for(usize index = 0u; index < receiverCount; ++index){
        if(!receiverLookup.resolveReceiverDrawState(receivers[index].entity, receivers[index].receiverPass, drawStates[index]))
            continue;
        hasReceiver = true;
        cutterCapacity = AddSaturating<usize>(cutterCapacity, Min<usize>(drawStates[index].cutterCount, NWB_CSG_SHADOW_MAX_CUTTERS));
    }
    if(!hasReceiver)
        return true;
    if(cutterCapacity > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outSnapshot.receiverRanges.resize(receiverCount);
    outSnapshot.cutters.reserve(cutterCapacity);
    outSnapshot.contentIdentity = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(outSnapshot.contentIdentity, shapeRegistry.revision());
    Fnv64AppendValue(outSnapshot.contentIdentity, receiverCount);
    outSnapshot.identity = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(outSnapshot.identity, shapeRegistry.revision());
    Fnv64AppendValue(outSnapshot.identity, receiverCount);
    for(usize index = 0u; index < receiverCount; ++index){
        const CsgShadowReceiverInput& receiver = receivers[index];
        Fnv64AppendValue(outSnapshot.contentIdentity, receiver.entity.id);
        Fnv64AppendValue(outSnapshot.contentIdentity, receiver.receiverPass);
        Fnv64AppendValue(outSnapshot.identity, receiver.entity.id);
        Fnv64AppendValue(outSnapshot.identity, receiver.receiverPass);
        CsgShadowReceiverRangeGpu& range = outSnapshot.receiverRanges[index];
        range = CsgShadowReceiverRangeGpu{};
        if(drawStates[index].active){
            Fnv64AppendBool(outSnapshot.identity, receiver.boundsValid);
            if(receiver.boundsValid){
                Fnv64AppendValue(outSnapshot.identity, receiver.worldMin);
                Fnv64AppendValue(outSnapshot.identity, receiver.worldMax);
            }
            __hidden_shadow_cutter_snapshot::AppendReceiverCutters(
                receiver, drawStates[index], receiverLookup, shapeRegistry, outSnapshot, range
            );
            outSnapshot.hasCsg = outSnapshot.hasCsg || (range.flags & NWB_CSG_SHADOW_RECEIVER_ACTIVE) != 0u;
        }
        Fnv64AppendValue(outSnapshot.identity, range);
        Fnv64AppendValue(outSnapshot.contentIdentity, range);
    }
    if(!outSnapshot.hasCsg){
        outSnapshot.receiverRanges.clear();
        outSnapshot.identity = 0u;
        outSnapshot.contentIdentity = 0u;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

