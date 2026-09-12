// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_scene_upload.h"

#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalSceneUpload::RayTracingOpticalSceneUpload(Core::Alloc::GlobalArena& arena, const RayTracingOpticalSceneGather& gather)
    : bytes(arena)
    , instanceCount(static_cast<u32>(gather.instances.size()))
{
    const usize instanceBytes = gather.instances.size() * sizeof(RayTracingOpticalInstanceGpu);
    bytes.resize(sizeof(gather.header) + instanceBytes);
    NWB_MEMCPY(bytes.data(), bytes.size(), &gather.header, sizeof(gather.header));
    if(instanceBytes != 0u)
        NWB_MEMCPY(bytes.data() + sizeof(gather.header), instanceBytes, gather.instances.data(), instanceBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalUploadState::RayTracingOpticalUploadState(Core::BufferHandle buffer, const Core::GpuPhysicalQueueId primaryQueue)
    : m_buffer(Move(buffer))
    , m_primaryQueue(primaryQueue)
{}

void RayTracingOpticalUploadState::invalidate()noexcept{
    NothrowScopedLock lock(m_mutex);

    m_retired = true;
    m_reservedSequence = 0u;
    m_reservedUpload = nullptr;
    m_acceptedUpload = nullptr;
    m_acceptedToken = {};
}

RayTracingOpticalUploadPlan RayTracingOpticalUploadState::plan(const RayTracingOpticalSceneUploadHandle& upload)noexcept{
    NothrowScopedLock lock(m_mutex);

    RayTracingOpticalUploadPlan result;
    if(
        m_retired || m_quarantined || m_reservedSequence != 0u || !m_buffer || !upload || upload->bytes.empty()
        || !m_primaryQueue.valid() || m_primaryQueue.deviceGeneration != m_buffer->getDeviceGeneration()
        || m_nextSequence == Limit<u64>::s_Max
    )
        return result;

    result.buffer = m_buffer;
    result.upload = upload;
    result.acceptedToken = m_acceptedToken;
    result.owner = this;
    result.sequence = m_nextSequence++;
    result.acceptedSequence = m_acceptedSequence;
    result.queue = m_primaryQueue;
    result.reused = m_acceptedUpload.get() == upload.get() && m_acceptedToken.valid();
    result.available = true;
    return result;
}

bool RayTracingOpticalUploadState::reserve(const RayTracingOpticalUploadPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(
        !matchesBuffer(plan) || plan.reused || m_reservedSequence != 0u
        || plan.acceptedSequence != m_acceptedSequence || plan.sequence >= m_nextSequence
    )
        return false;

    m_reservedSequence = plan.sequence;
    m_reservedUpload = plan.upload;
    return true;
}

bool RayTracingOpticalUploadState::isReserved(const RayTracingOpticalUploadPlan& plan)const noexcept{
    NothrowScopedLock lock(m_mutex);

    return matchesBuffer(plan) && plan.sequence == m_reservedSequence && plan.upload.get() == m_reservedUpload.get();
}

void RayTracingOpticalUploadState::discard(const RayTracingOpticalUploadPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(
        plan.owner == this && plan.buffer.get() == m_buffer.get()
        && plan.sequence == m_reservedSequence && plan.upload.get() == m_reservedUpload.get()
    ){
        m_reservedSequence = 0u;
        m_reservedUpload = nullptr;
    }
}

bool RayTracingOpticalUploadState::accept(const RayTracingOpticalUploadPlan& plan, const Core::QueueSubmissionToken& token)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(!matchesBuffer(plan) || plan.sequence != m_reservedSequence || plan.upload.get() != m_reservedUpload.get())
        return false;
    m_reservedSequence = 0u;
    m_reservedUpload = nullptr;
    if(
        !token.valid() || token.queue != Core::CommandQueue::Graphics
        || !token.matchesPhysicalQueue(m_primaryQueue.index, m_primaryQueue.deviceGeneration)
        || (m_acceptedToken.valid() && token.value <= m_acceptedToken.value)
    ){
        // This callback follows native acceptance. Discard cannot prove the old bytes survived an unknown write.
        m_quarantined = true;
        return false;
    }

    m_acceptedUpload = plan.upload;
    m_acceptedSequence = plan.sequence;
    m_acceptedToken = token;
    return true;
}

bool RayTracingOpticalUploadState::matchesBuffer(const RayTracingOpticalUploadPlan& plan)const noexcept{
    return !m_retired && !m_quarantined && plan.valid() && plan.owner == this
        && plan.buffer.get() == m_buffer.get() && plan.queue == m_primaryQueue && m_buffer && m_primaryQueue.valid()
        && m_primaryQueue.deviceGeneration == m_buffer->getDeviceGeneration();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalUploadControlHandle CreateRayTracingOpticalUploadControl(
    Core::Alloc::GlobalArena& arena,
    const Core::BufferHandle& buffer,
    const Core::GpuPhysicalQueueId primaryQueue){
    if(!buffer || !primaryQueue.valid() || primaryQueue.deviceGeneration != buffer->getDeviceGeneration())
        return {};
    return RayTracingOpticalUploadControlHandle(
        NewArenaObject<RayTracingOpticalUploadControl>(arena, buffer, primaryQueue),
        ArenaRefDeleter<RayTracingOpticalUploadControl, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalUploadReservation::RayTracingOpticalUploadReservation(
    RayTracingOpticalUploadControlHandle control,
    const RayTracingOpticalUploadPlan& plan)
    : m_control(Move(control))
    , m_plan(plan)
{
    m_reserved = m_control && !m_plan.reused && m_control->reserve(m_plan);
}

RayTracingOpticalUploadReservation::RayTracingOpticalUploadReservation(RayTracingOpticalUploadReservation&& other)noexcept
    : m_control(Move(other.m_control))
    , m_plan(Move(other.m_plan))
    , m_reserved(other.m_reserved)
{
    other.m_reserved = false;
}

RayTracingOpticalUploadReservation::~RayTracingOpticalUploadReservation()noexcept{
    discard();
}

RayTracingOpticalUploadReservation& RayTracingOpticalUploadReservation::operator=(RayTracingOpticalUploadReservation&& other)noexcept{
    if(this != &other){
        discard();
        m_control = Move(other.m_control);
        m_plan = Move(other.m_plan);
        m_reserved = other.m_reserved;
        other.m_reserved = false;
    }
    return *this;
}

bool RayTracingOpticalUploadReservation::valid()const noexcept{
    return m_reserved && m_control && m_control->isReserved(m_plan);
}

bool RayTracingOpticalUploadReservation::accept(const Core::QueueSubmissionToken& token)noexcept{
    const bool accepted = m_reserved && m_control && m_control->accept(m_plan, token);
    if(m_reserved && m_control && !accepted)
        m_control->discard(m_plan);
    m_reserved = false;
    m_control = nullptr;
    return accepted;
}

void RayTracingOpticalUploadReservation::discard()noexcept{
    if(m_reserved && m_control)
        m_control->discard(m_plan);
    m_reserved = false;
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

