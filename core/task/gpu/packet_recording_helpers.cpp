// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_recording_helpers.h"

#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/exception.h>
#include <global/termination.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRecordingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PrepareCompiledTimingQueries(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    GpuTimingRecorder* const timingRecorder,
    Alloc::ScratchArena& scratchArena){
    bool recordsTiming = false;
    for(usize packetIndex = 0u; packetIndex < planAccess.packetCount(); ++packetIndex){
        const GpuCompiledPacketView packetView = planAccess.packet(planAccess.packetIdAt(packetIndex));
        if(!packetView.valid())
            return false;
        if(packetView.plan->recordsTiming){
            recordsTiming = true;
            break;
        }
    }
    if(!recordsTiming)
        return true;
    if(!timingRecorder)
        return false;

    HashMap<Name, u32, Hasher<Name>, EqualTo<Name>, Alloc::ScratchArena> scopeOccurrences(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    const usize maxScopeCount = declarationAccess.taskCount() + planAccess.packetCount();
    scopeOccurrences.reserve(maxScopeCount);
    Vector<Name, Alloc::ScratchArena> scopeOrder(scratchArena);
    scopeOrder.reserve(maxScopeCount);
    const auto countScopeOccurrence = [&](const Name& scopeName){
        if(!scopeName)
            return false;
        auto [it, inserted] = scopeOccurrences.try_emplace(scopeName, 0u);
        if(inserted)
            scopeOrder.push_back(scopeName);
        u32& occurrenceCount = it.value();
        if(occurrenceCount >= Limit<u32>::s_Max / s_MaxFramesInFlight)
            return false;
        ++occurrenceCount;
        return true;
    };

    // Task and packet scopes share the timing recorder's identity namespace. Count both together once so repeated
    // tasks and an authored task identity matching another packet's derived identity reserve their full demand.
    const GpuSubmissionPacketRange packetTimingEnvelopeRange = planAccess.packetTimingEnvelopeRange();
    for(usize packetIndex = 0u; packetIndex < planAccess.packetCount(); ++packetIndex){
        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packetID);
        if(!packetView.valid() || packetView.plan->taskCount == 0u)
            return false;
        const GpuSubmissionPacket& packet = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;

        // Scope IDs and report rows follow first registration, with each packet preceding its task scopes.
        if(packet.recordsTiming && !countScopeOccurrence(PacketTimingScopeName(declarationAccess, planAccess, packetID)))
            return false;
        bool packetRecordsTiming = false;
        for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            const GpuTaskGraphTaskView taskView = declarationAccess.taskAt(task.index);
            const GpuCompiledTaskView compiledTaskView = planAccess.findTask(task);
            const GpuCompiledTask* const compiledTask = compiledTaskView.plan;
            if(
                !compiledTaskView.valid()
                || taskView.id != task
                || compiledTask->packet != packetID
                || compiledTask->timingPolicy != taskView.timing.policy
                || compiledTask->timingPolicy >= GpuTaskTimingPolicy::kCount
            )
                return false;
            packetRecordsTiming = packetRecordsTiming || compiledTask->timingPolicy != GpuTaskTimingPolicy::None;
            if(compiledTask->timingPolicy == GpuTaskTimingPolicy::Task && !countScopeOccurrence(taskView.identity))
                return false;
        }
        const bool recordsPacketEnvelopeTiming = packetTimingEnvelopeRange.valid()
            && packetIndex >= packetTimingEnvelopeRange.first.index
            && packetIndex - packetTimingEnvelopeRange.first.index < packetTimingEnvelopeRange.packetCount
        ;
        if(packet.recordsPacketEnvelopeTiming != recordsPacketEnvelopeTiming)
            return false;
        packetRecordsTiming = packetRecordsTiming || recordsPacketEnvelopeTiming;
        if(packet.recordsTiming != packetRecordsTiming)
            return false;
    }
    for(const Name& scopeName : scopeOrder){
        const u32 occurrenceCount = scopeOccurrences.find(scopeName).value();
        // Recording runs inside render/submission: declare demand only. The frame preamble owns GPU pool
        // creation through materializeRequestedQueries(), so this path never calls device.createTimerQuery().
        if(!timingRecorder->requestScopeQueries(scopeName, occurrenceCount * s_MaxFramesInFlight))
            return false;
    }
    return true;
}


bool HasExplicitKnownInitialState(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledBarrier& barrier,
    CommandList& commandList
){
    if(!declarationAccess.validResource(barrier.resource))
        return false;

    const GpuTaskGraphResourceView resource = declarationAccess.resourceAt(barrier.resource.index);
    if(resource.id != barrier.resource || !resource.hasBackendResource)
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:{
        Texture* const texture = declarationAccess.textureForResource(barrier.resource);
        if(!texture)
            return false;

        const TextureDesc& description = texture->getCreationDescription();
        const TextureSubresourceSet subresources = barrier.range.textureSubresources.resolve(
            description,
            TextureSubresourceMipResolve::Range
        );
        const u64 mipEnd = static_cast<u64>(subresources.baseMipLevel) + subresources.numMipLevels;
        const u64 arrayEnd = static_cast<u64>(subresources.baseArraySlice) + subresources.numArraySlices;
        if(
            subresources.numMipLevels == 0u
            || subresources.numArraySlices == 0u
            || mipEnd > description.mipLevels
            || arrayEnd > description.arraySize
        )
            return false;

        for(ArraySlice arraySlice = subresources.baseArraySlice;
            static_cast<u64>(arraySlice) < arrayEnd;
            ++arraySlice
        ){
            for(MipLevel mipLevel = subresources.baseMipLevel;
                static_cast<u64>(mipLevel) < mipEnd;
                ++mipLevel
            ){
                if(
                    !commandList.hasExplicitTextureSubresourceState(texture, arraySlice, mipLevel)
                    || commandList.getTextureSubresourceState(texture, arraySlice, mipLevel) == ResourceStates::Unknown
                )
                    return false;
            }
        }
        return true;
    }
    case GpuGraphResourceType::Buffer:{
        Buffer* const buffer = declarationAccess.bufferForResource(barrier.resource);
        return buffer
            && commandList.hasExplicitBufferState(buffer, barrier.range.bufferRange, true)
        ;
    }
    case GpuGraphResourceType::AccelStruct:{
        RayTracingAccelStruct* const accelStruct = declarationAccess.accelStructForResource(barrier.resource);
        Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
        return backingBuffer
            && commandList.hasExplicitBufferState(backingBuffer)
            && commandList.getBufferState(backingBuffer) != ResourceStates::Unknown
        ;
    }
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

