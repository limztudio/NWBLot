// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_csg_types.h"

#include <impl/assets/graphics/shadow/csg_shadow_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgShadowReceiverInput{
    Core::ECS::EntityID entity;
    CsgReceiverPass::Enum receiverPass = CsgReceiverPass::Opaque;
    Float3U worldMin = Float3U(0.f, 0.f, 0.f);
    Float3U worldMax = Float3U(0.f, 0.f, 0.f);
    // Leave false when CPU bounds cannot conservatively contain the current deformed receiver.
    bool boundsValid = false;
};

struct CsgShadowReceiverRangeGpu{
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
    u32 flags = 0u;
    u32 padding = 0u;
};
static_assert(sizeof(CsgShadowReceiverRangeGpu) == NWB_CSG_SHADOW_RANGE_BYTES);
static_assert(IsStandardLayout_V<CsgShadowReceiverRangeGpu>);
static_assert(IsTriviallyCopyable_V<CsgShadowReceiverRangeGpu>);
static_assert(sizeof(CsgCutterGpuData) == NWB_CSG_SHADOW_CUTTER_BYTES);

struct CsgShadowSnapshot{
    Vector<CsgShadowReceiverRangeGpu, Core::Alloc::GlobalArena> receiverRanges;
    // Shadow records use NWB_CSG_SHADOW_SHAPE_* in shapeType, with the existing CSG cutter layout.
    Vector<CsgCutterGpuData, Core::Alloc::GlobalArena> cutters;
    u64 identity = 0u;
    u64 contentIdentity = 0u;
    bool hasCsg = false;


public:
    explicit CsgShadowSnapshot(Core::Alloc::GlobalArena& arena)
        : receiverRanges(arena)
        , cutters(arena)
    {}
};

// Receiver order must match the shadow instance table; an empty CSG scene leaves both vectors empty.
[[nodiscard]] bool BuildCsgShadowSnapshot(
    Core::ECS::World& world, const CsgShapeRegistry& shapeRegistry,
    const CsgShadowReceiverInput* receivers, usize receiverCount,
    Core::Alloc::ScratchArena& scratchArena, CsgShadowSnapshot& outSnapshot
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

