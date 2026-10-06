// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_graph.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskSubmissionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TaskCallback>
class TaskCallbackBindings final : NoCopy{
private:
    static constexpr usize s_InlineCallbackCount = 8u;


public:
    explicit TaskCallbackBindings(Alloc::ScratchArena& scratchArena)noexcept
        : m_scratchArena(scratchArena)
    {}


public:
    // Resolve before recording or native submission. The caller retains the immutable callback array through publication.
    [[nodiscard]] bool resolve(
        const GpuTaskGraph::DeclarationReadView& declarations,
        const GpuCompiledGraph::ReadView& plan,
        const GpuSubmissionPacketRange& range,
        const TaskCallback* const callbacks,
        const usize callbackCount){
        m_callbacks = nullptr;
        m_callbackCount = 0u;
        m_generation = 0u;
        m_indices.reset();
        if(
            (callbackCount != 0u && !callbacks)
            || !plan.validFor(declarations)
            || !plan.validPacketRange(range)
            || callbackCount > declarations.taskCount()
        )
            return false;
        if(callbackCount > s_InlineCallbackCount)
            m_indices.emplace(AddSize(callbackCount, callbackCount), m_scratchArena);
        const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
        for(usize callbackIndex = 0u; callbackIndex < callbackCount; ++callbackIndex){
            const TaskCallback& callback = callbacks[callbackIndex];
            if(!callback.invoke || !declarations.validTask(callback.task))
                return false;
            const GpuCompiledTaskView task = plan.findTask(callback.task);
            if(!task.valid())
                return false;
            const GpuSubmissionPacketId packet = task.plan->packet;
            if(!packet.valid() || packet.index < range.first.index || static_cast<usize>(packet.index) >= rangeEnd)
                return false;
            if(m_indices){
                if(!m_indices->try_emplace(callback.task.index, callbackIndex).second)
                    return false;
            }
            else{
                for(usize previousIndex = 0u; previousIndex < callbackIndex; ++previousIndex){
                    if(callbacks[previousIndex].task == callback.task)
                        return false;
                }
            }
        }
        m_callbacks = callbacks;
        m_callbackCount = callbackCount;
        m_generation = declarations.generation();
        return true;
    }

    [[nodiscard]] const TaskCallback* find(const GpuTaskId task)const noexcept{
        if(m_callbackCount == 0u || !task.valid() || task.generation != m_generation)
            return nullptr;
        if(m_indices){
            const auto found = m_indices->find(task.index);
            return found == m_indices->end() ? nullptr : &m_callbacks[found.value()];
        }
        for(usize index = 0u; index < m_callbackCount; ++index){
            if(m_callbacks[index].task == task)
                return &m_callbacks[index];
        }
        return nullptr;
    }


private:
    Alloc::ScratchArena& m_scratchArena;
    const TaskCallback* m_callbacks = nullptr;
    usize m_callbackCount = 0u;
    u64 m_generation = 0u;
    Optional<HashMap<u32, usize, Alloc::ScratchArena>> m_indices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

