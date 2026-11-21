// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_command_tracking.h"
#include "backend_queue.h"


NWB_VULKAN_BEGIN


class Queue::SubmissionValidator final : NoCopy{
public:
    explicit SubmissionValidator(Queue& queue)noexcept : m_queue(queue){}


public:
    [[nodiscard]] b8 validateCommandList(CommandList* cmdList, const SubmissionCommandListIdentity& expected, usize cmdIndex, u64 batchStamp)const;
    [[nodiscard]] b8 validate(CommandList* const* ppCmd, usize numCmd, const SubmissionCommandListIdentity* expectedCommandLists, u64 batchStamp)const;


private:
    Queue& m_queue;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

