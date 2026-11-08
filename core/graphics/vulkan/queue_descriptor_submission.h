// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_command_tracking.h"
#include "backend_queue.h"


NWB_VULKAN_BEGIN


class Queue::DescriptorSubmission final : NoCopy{
public:
    explicit DescriptorSubmission(Queue& queue)noexcept;


public:
    [[nodiscard]] b8 prepare(CommandList* const* ppCmd, usize numCmd);
    [[nodiscard]] b8 validateHeapUses(TrackedCommandBuffer& tracked, const QueueSubmissionToken& submissionToken);
    void releaseLocks()noexcept;
    void commitAndRelease(const QueueSubmissionToken& submissionToken)noexcept;


private:
    Queue& m_queue;
    DescriptorBufferManager* const m_descriptorBufferManager;
    GpuDescriptorHeap* m_submissionDescriptorHeap = nullptr;
    UniqueLock<Futex> m_descriptorHeapLock;
    UniqueLock<Futex> m_descriptorBufferLifecycleLock;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
