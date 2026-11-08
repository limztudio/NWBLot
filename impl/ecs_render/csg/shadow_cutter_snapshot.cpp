// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_cutter_snapshot.h"

#include <impl/ecs_render/csg/csg_snapshot_identity_builder.adhd.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_cutter_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendReceiverCutters(const CsgShadowReceiverInput& receiver, const CsgReceiverDrawState& drawState, const CsgFrameReceiverLookup& receiverLookup, const CsgShapeRegistry& shapeRegistry, CsgShadowSnapshot& snapshot, CsgShadowReceiverRangeGpu& range){
    const SIMDVectorSingle receiverMin = LoadFloat(receiver.worldMin);
    const SIMDVectorSingle receiverMax = LoadFloat(receiver.worldMax);
    const b8 boundsCanCull = receiver.boundsValid && AabbTests::Valid(receiverMin, receiverMax);
    range.firstCutter = static_cast<u32>(snapshot.cutters.size());
    receiverLookup.forEachReceiverCutter(drawState, [&](const Core::ECS::EntityID entity, const CsgCutterComponent& cutter){
        const auto shapeType = shapeRegistry.findShapeType(cutter.shapeType);
        if(!shapeType)
            return;
        const auto parameters = TryResolveCsgCutterParameterBytes(shapeType->desc.parameterByteSize, shapeType->desc.defaultParameterBytes, cutter.parameterBytes);
        if(!parameters)
            return;

        const SIMDMatrix shapeToWorld = LoadFloat(cutter.shapeToWorld);
        const auto bounds = shapeRegistry.buildShapeBounds(shapeType->id, shapeToWorld, *parameters);
        if(!bounds)
            return;
        if(boundsCanCull && bounds->finiteBounds && !AabbTests::Intersects(receiverMin, receiverMax, bounds->minBounds, bounds->maxBounds))
            return;

        const SIMDMatrix worldToShape = LoadFloat(cutter.worldToShape);
        CsgSnapshotIdentityBuilder().updateShadowCutterSnapshotHashes(
            entity,
            cutter.shapeType,
            worldToShape,
            shapeToWorld,
            *parameters,
            snapshot.contentIdentity,
            snapshot.identity
        );
        AppendShadowCutterRecord(
            static_cast<b8>(shapeType->desc.shaderModule),
            shapeType->desc.name,
            worldToShape,
            shapeToWorld,
            *parameters,
            range,
            snapshot.cutters
        );
    });
    FinalizeShadowReceiverCuttersRange(range, snapshot.cutters);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildCsgShadowSnapshot(const CsgShapeRegistry& shapeRegistry, const Span<const CsgShadowReceiverInput> receivers, Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena, CsgShadowSnapshot& outSnapshot){
    outSnapshot.receiverRanges.clear();
    outSnapshot.cutters.clear();
    outSnapshot.identity = 0u;
    outSnapshot.contentIdentity = 0u;
    outSnapshot.hasCsg = false;
    const usize receiverCount = receivers.size();
    if(receiverCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(receiverCount == 0u || !HasCsgFrameCandidates(world))
        return true;

    const CsgFrameReceiverLookup receiverLookup(world, scratchArena);
    if(receiverLookup.empty())
        return true;
    Vector<CsgReceiverDrawState, Core::Alloc::ScratchArena> drawStates(scratchArena);
    drawStates.resize(receiverCount);
    b8 hasReceiver = false;
    usize cutterCapacity = 0u;
    for(usize index = 0u; index < receiverCount; ++index){
        const auto drawState = receiverLookup.resolveReceiverDrawState(receivers[index].entity, receivers[index].receiverPass);
        if(!drawState)
            continue;
        drawStates[index] = *drawState;
        hasReceiver = true;
        cutterCapacity = AddSaturating<usize>(cutterCapacity, Min<usize>(drawStates[index].cutterCount, NWB_CSG_RAY_MAX_CUTTERS));
    }
    if(!hasReceiver)
        return true;
    if(cutterCapacity > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outSnapshot.receiverRanges.resize(receiverCount);
    outSnapshot.cutters.reserve(cutterCapacity);
    CsgSnapshotIdentityBuilder().initializeShadowCutterSnapshotHashes(shapeRegistry.revision(), receiverCount, outSnapshot.contentIdentity, outSnapshot.identity);
    for(usize index = 0u; index < receiverCount; ++index){
        const CsgShadowReceiverInput& receiver = receivers[index];
        CsgSnapshotIdentityBuilder().updateShadowReceiverSnapshotHeaderHashes(receiver, drawStates[index].active, outSnapshot.contentIdentity, outSnapshot.identity);
        CsgShadowReceiverRangeGpu& range = outSnapshot.receiverRanges[index];
        range = CsgShadowReceiverRangeGpu{};
        if(drawStates[index].active){
            __hidden_shadow_cutter_snapshot::AppendReceiverCutters(receiver, drawStates[index], receiverLookup, shapeRegistry, outSnapshot, range);
            outSnapshot.hasCsg = outSnapshot.hasCsg || (range.flags & NWB_CSG_RAY_RECEIVER_ACTIVE) != 0u;
        }
        CsgSnapshotIdentityBuilder().updateShadowReceiverSnapshotRangeHashes(range, outSnapshot.contentIdentity, outSnapshot.identity);
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
