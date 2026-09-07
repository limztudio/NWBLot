// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The opaque CSG receiver-span task consumes receiver-surface event StorageImage aliases and publishes span
// aliases for interval combine. This getter-only probe observes the graph-lowered entry states before the native
// span dispatcher can perform any compatibility transition.
struct NativePacketCsgReceiverSpanProbeTask{
    struct Payload{
        Texture* receiverEventData = nullptr;
        Texture* receiverEventCount = nullptr;
        Texture* receiverSpanData = nullptr;
        Texture* receiverSpanCount = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.receiverEventData
            || !payload.receiverEventCount
            || !payload.receiverSpanData
            || !payload.receiverSpanCount
        )
            return false;
        const auto hasUavRange = [&commandList](Texture* texture, const u32 finalArraySlice){
            return commandList.getTextureSubresourceState(texture, 0u, 0u) == ResourceStates::UnorderedAccess
                && commandList.getTextureSubresourceState(texture, finalArraySlice, 0u) == ResourceStates::UnorderedAccess
            ;
        };
        const bool ready =
            hasUavRange(payload.receiverEventData, 31u)
            && hasUavRange(payload.receiverEventCount, 0u)
            && hasUavRange(payload.receiverSpanData, 15u)
            && hasUavRange(payload.receiverSpanCount, 0u)
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// A CSG interval-combine task consumes five producer StorageImage aliases and publishes four more. This probe
// performs no native state work: it only observes the packet runtime's graph-lowered states before the equivalent
// thunk runs.
struct NativePacketCsgIntervalCombineProbeTask{
    struct Payload{
        Texture* capBackNormal = nullptr;
        Texture* intervalDepth = nullptr;
        Texture* intervalId = nullptr;
        Texture* receiverSpanData = nullptr;
        Texture* receiverSpanCount = nullptr;
        Texture* removedIntervalDepth = nullptr;
        Texture* removedIntervalCapNormal = nullptr;
        Texture* removedIntervalData = nullptr;
        Texture* removedIntervalCount = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.capBackNormal
            || !payload.intervalDepth
            || !payload.intervalId
            || !payload.receiverSpanData
            || !payload.receiverSpanCount
            || !payload.removedIntervalDepth
            || !payload.removedIntervalCapNormal
            || !payload.removedIntervalData
            || !payload.removedIntervalCount
        )
            return false;
        const auto hasUavRange = [&commandList](Texture* texture, const u32 finalArraySlice){
            return commandList.getTextureSubresourceState(texture, 0u, 0u) == ResourceStates::UnorderedAccess
                && commandList.getTextureSubresourceState(texture, finalArraySlice, 0u) == ResourceStates::UnorderedAccess
            ;
        };
        const bool ready =
            hasUavRange(payload.capBackNormal, 3u)
            && hasUavRange(payload.intervalDepth, 3u)
            && hasUavRange(payload.intervalId, 3u)
            && hasUavRange(payload.receiverSpanData, 15u)
            && hasUavRange(payload.receiverSpanCount, 0u)
            && hasUavRange(payload.removedIntervalDepth, 15u)
            && hasUavRange(payload.removedIntervalCapNormal, 15u)
            && hasUavRange(payload.removedIntervalData, 15u)
            && hasUavRange(payload.removedIntervalCount, 0u)
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// AVBOIT and opaque CSG sampling read four StorageImage aliases produced by the interval-combine task. Opaque CSG
// may also receive its exact selected mesh-source buffers through an immutable ShaderResource set. This probe performs
// no native state work: it only observes packet-runtime states before the equivalent thunk runs. Coverage is optional
// because opaque sampling has no coverage-buffer dependency.
struct NativePacketCsgIntervalSampleProbeTask{
    struct Payload{
        Buffer* coverage = nullptr;
        Buffer* materialGeometry = nullptr;
        Texture* removedIntervalDepth = nullptr;
        Texture* removedIntervalCapNormal = nullptr;
        Texture* removedIntervalData = nullptr;
        Texture* removedIntervalCount = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.removedIntervalDepth
            || !payload.removedIntervalCapNormal
            || !payload.removedIntervalData
            || !payload.removedIntervalCount
        )
            return false;
        const auto hasUavRange = [&commandList](Texture* texture, const u32 finalArraySlice){
            return commandList.getTextureSubresourceState(texture, 0u, 0u) == ResourceStates::UnorderedAccess
                && commandList.getTextureSubresourceState(texture, finalArraySlice, 0u) == ResourceStates::UnorderedAccess
            ;
        };
        const bool ready =
            (!payload.coverage || commandList.getBufferState(payload.coverage) == ResourceStates::UnorderedAccess)
            && (!payload.materialGeometry || commandList.getBufferState(payload.materialGeometry) == ResourceStates::ShaderResource)
            && hasUavRange(payload.removedIntervalDepth, 15u)
            && hasUavRange(payload.removedIntervalCapNormal, 15u)
            && hasUavRange(payload.removedIntervalData, 15u)
            && hasUavRange(payload.removedIntervalCount, 0u)
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

