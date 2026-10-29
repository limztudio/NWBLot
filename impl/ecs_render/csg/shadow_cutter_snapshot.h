// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_csg_types.h"

#include <impl/assets/graphics/csg/ray_constants.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgShadowReceiverInput{
    Core::ECS::EntityID entity;
    CsgReceiverPass::Enum receiverPass = CsgReceiverPass::Opaque;
    // Leave false when CPU bounds cannot conservatively contain the current deformed receiver.
    b8 boundsValid = false;
    Float3U worldMin = Float3U(0.f, 0.f, 0.f);
    Float3U worldMax = Float3U(0.f, 0.f, 0.f);
};

struct CsgShadowReceiverRangeGpu{
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
    u32 flags = 0u;
    // Packed contexts assign the absolute byte offset of this receiver's cooked membership words.
    u32 solidTriangleWordOffset = 0u;
};
static_assert(sizeof(CsgShadowReceiverRangeGpu) == NWB_CSG_RAY_RANGE_BYTES);
static_assert(offsetof(CsgShadowReceiverRangeGpu, solidTriangleWordOffset) == sizeof(u32) * 3u);
static_assert(IsStandardLayout_V<CsgShadowReceiverRangeGpu>);
static_assert(IsTriviallyCopyable_V<CsgShadowReceiverRangeGpu>);
static_assert(sizeof(CsgCutterGpuData) == NWB_CSG_RAY_CUTTER_BYTES);

struct CsgShadowSnapshot{
    Vector<CsgShadowReceiverRangeGpu, Core::Alloc::GlobalArena> receiverRanges;
    // Built-in ray records use NWB_CSG_RAY_SHAPE_* in shapeType, with the existing CSG cutter layout.
    Vector<CsgCutterGpuData, Core::Alloc::GlobalArena> cutters;
    u64 identity = 0u;
    u64 contentIdentity = 0u;
    b8 hasCsg = false;


public:
    explicit CsgShadowSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : receiverRanges(arena)
        , cutters(arena)
    {}
};

// Receiver order must match the shadow instance table; an empty CSG scene leaves both vectors empty.
[[nodiscard]] b8 BuildCsgShadowSnapshot(const CsgShapeRegistry& shapeRegistry, Span<const CsgShadowReceiverInput> receivers, Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena, CsgShadowSnapshot& outSnapshot);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

