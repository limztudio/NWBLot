// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "optical_scene.h"

#include <core/alloc/global.h>
#include <core/graphics/rhi/command.h>
#include <core/graphics/rhi/resource.h>

#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Preflight preserves this identity only after comparing every header and instance byte.
struct RayTracingOpticalSceneUpload{
    Vector<u8, Core::Alloc::GlobalArena> bytes;
    u32 instanceCount = 0u;


public:
    RayTracingOpticalSceneUpload(Core::Alloc::GlobalArena& arena, const RayTracingOpticalSceneGather& gather);
};
using RayTracingOpticalSceneUploadControl = RefCounter<RayTracingOpticalSceneUpload>;
using RayTracingOpticalSceneUploadHandle = RefCountPtr<RayTracingOpticalSceneUploadControl, ArenaRefDeleter<RayTracingOpticalSceneUploadControl, Core::Alloc::GlobalArena>>;

class RayTracingOpticalUploadState;

struct RayTracingOpticalUploadPlan{
    Core::BufferHandle buffer;
    RayTracingOpticalSceneUploadHandle upload;
    Core::QueueSubmissionToken acceptedToken;
    // Identity only; a reservation retains the control whenever callbacks can dereference it.
    const RayTracingOpticalUploadState* owner = nullptr;
    u64 sequence = 0u;
    u64 acceptedSequence = 0u;
    Core::GpuPhysicalQueueId queue;
    b8 reused = false;
    b8 available = false;


public:
    [[nodiscard]] b8 valid()const noexcept{ return available && buffer && upload && owner && sequence != 0u && queue.valid(); }
};

// One physical buffer, one primary Graphics writer domain.
// Only accepted writes replace residency.
// Renderer graph admission is serial; this does not admit independently submitted overlapping read graphs.
class RayTracingOpticalUploadState : NoCopy{
public:
    RayTracingOpticalUploadState(Core::BufferHandle buffer, Core::GpuPhysicalQueueId primaryQueue)noexcept;


public:
    void invalidate()noexcept;
    [[nodiscard]] RayTracingOpticalUploadPlan plan(const RayTracingOpticalSceneUploadHandle& upload)noexcept;
    [[nodiscard]] b8 reserve(const RayTracingOpticalUploadPlan& plan)noexcept;
    [[nodiscard]] b8 isReserved(const RayTracingOpticalUploadPlan& plan)const noexcept;
    void discard(const RayTracingOpticalUploadPlan& plan)noexcept;
    [[nodiscard]] b8 accept(const RayTracingOpticalUploadPlan& plan, const Core::QueueSubmissionToken& token)noexcept;

private:
    [[nodiscard]] b8 matchesBuffer(const RayTracingOpticalUploadPlan& plan)const noexcept;


private:
    Core::BufferHandle m_buffer;
    RayTracingOpticalSceneUploadHandle m_acceptedUpload;
    RayTracingOpticalSceneUploadHandle m_reservedUpload;
    Core::QueueSubmissionToken m_acceptedToken;
    u64 m_nextSequence = 1u;
    u64 m_acceptedSequence = 0u;
    u64 m_reservedSequence = 0u;
    Core::GpuPhysicalQueueId m_primaryQueue;
    mutable Futex m_mutex;
    b8 m_retired = false;
    b8 m_quarantined = false;
};

using RayTracingOpticalUploadControl = RefCounter<RayTracingOpticalUploadState>;
using RayTracingOpticalUploadControlHandle = RefCountPtr<RayTracingOpticalUploadControl, ArenaRefDeleter<RayTracingOpticalUploadControl, Core::Alloc::GlobalArena>>;

[[nodiscard]] Expected<RayTracingOpticalUploadControlHandle> CreateRayTracingOpticalUploadControl(Core::Alloc::GlobalArena& arena, const Core::BufferHandle& buffer, Core::GpuPhysicalQueueId primaryQueue)noexcept;

class RayTracingOpticalUploadReservation final : NoCopy{
public:
    RayTracingOpticalUploadReservation(RayTracingOpticalUploadControlHandle control, const RayTracingOpticalUploadPlan& plan)noexcept;
    RayTracingOpticalUploadReservation(RayTracingOpticalUploadReservation&& other)noexcept;
    ~RayTracingOpticalUploadReservation()noexcept;


public:
    RayTracingOpticalUploadReservation& operator=(RayTracingOpticalUploadReservation&& other)noexcept;


public:
    [[nodiscard]] b8 valid()const noexcept;
    [[nodiscard]] b8 accept(const Core::QueueSubmissionToken& token)noexcept;
    void discard()noexcept;


private:
    RayTracingOpticalUploadControlHandle m_control;
    RayTracingOpticalUploadPlan m_plan;
    b8 m_reserved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

