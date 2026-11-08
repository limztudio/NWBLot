// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/telemetry/frame_graph_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_FrameGraphPayloadVersion = NWB::Core::Telemetry::s_FrameGraphWirePayloadVersion;
inline constexpr u32 s_FrameGraphPayloadMagic = NWB::Core::Telemetry::s_FrameGraphWirePayloadMagic; // NWFG


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphNodeKind{
    enum Enum : u8{
        Unknown,
        Pass,
        Resource,
        External,
    };
};

namespace FrameGraphEdgeKind{
    enum Enum : u8{
        Unknown,
        Reads,
        Writes,
        DependsOn,
    };
};

namespace FrameGraphQueueClass{
    static constexpr u8 s_FrameGraphQueueClassUnknownBase = NWB::Core::Telemetry::s_FrameGraphWireQueueClassUnknownBase;
    static constexpr u8 s_FrameGraphQueueClassCountValue = NWB::Core::Telemetry::s_FrameGraphWireQueueClassCountValue;
    enum Enum : u8{
        Unknown = s_FrameGraphQueueClassUnknownBase,
        Graphics,
        Compute,
        Transfer,

        Count = s_FrameGraphQueueClassCountValue,
    };
};

namespace FrameGraphQueueAssignmentReason{
    static constexpr u8 s_FrameGraphQueueAssignmentReasonUnknownBase = NWB::Core::Telemetry::s_FrameGraphWireQueueAssignmentReasonUnknownBase;
    static constexpr u8 s_FrameGraphQueueAssignmentReasonCountValue = NWB::Core::Telemetry::s_FrameGraphWireQueueAssignmentReasonCountValue;
    enum Enum : u8{
        Unknown = s_FrameGraphQueueAssignmentReasonUnknownBase,
        RequiredGraphics,
        Conservative,
        Scored,

        Count = s_FrameGraphQueueAssignmentReasonCountValue,
    };
};

namespace FrameGraphQueueAssignmentModifier{
    static constexpr u8 s_FrameGraphQueueAssignmentModifierNoneBase = NWB::Core::Telemetry::s_FrameGraphWireQueueAssignmentModifierNoneBase;
    enum Mask : u8{
        None = s_FrameGraphQueueAssignmentModifierNoneBase,
        DirectDependencyAffinity = 1u << 0u,
        SameClassLoadBalance = 1u << 1u,
        NonPrimaryRouting = 1u << 2u,
        TimingCalibration = 1u << 4u,
        TimingFeedback = 1u << 5u,

        All = DirectDependencyAffinity
            | SameClassLoadBalance
            | NonPrimaryRouting
            | TimingCalibration
            | TimingFeedback,
    };
};

namespace FrameGraphQueueAssignmentAcceptance{
    static constexpr u8 s_FrameGraphQueueAssignmentAcceptanceNotAcceptedBase = NWB::Core::Telemetry::s_FrameGraphWireQueueAssignmentAcceptanceNotAcceptedBase;
    static constexpr u8 s_FrameGraphQueueAssignmentAcceptanceCountValue = NWB::Core::Telemetry::s_FrameGraphWireQueueAssignmentAcceptanceCountValue;
    enum Enum : u8{
        NotAccepted = s_FrameGraphQueueAssignmentAcceptanceNotAcceptedBase,
        First,
        Unchanged,
        Changed,

        Count = s_FrameGraphQueueAssignmentAcceptanceCountValue,
    };
};

namespace FrameGraphTaskPacketizationDecision{
    static constexpr u8 s_FrameGraphTaskPacketizationDecisionUnknownBase = NWB::Core::Telemetry::s_FrameGraphWireTaskPacketizationDecisionUnknownBase;
    static constexpr u8 s_FrameGraphTaskPacketizationDecisionCountValue = NWB::Core::Telemetry::s_FrameGraphWireTaskPacketizationDecisionCountValue;
    enum Enum : u8{
        Unknown = s_FrameGraphTaskPacketizationDecisionUnknownBase,
        FirstTask,
        MergeNotRequested,
        TaskForcesBoundary,
        QueueChanged,
        PrecedingTaskForcesBoundary,
        ScoredMergeIneligible,
        MergeRequiresExplicitImmediateDependency,
        CrossQueueConsumerFrontier,
        MergedExplicit,
        MergedFrontierScored,
        ScoredMergeDomainMismatch,

        Count = s_FrameGraphTaskPacketizationDecisionCountValue,
    };
};

struct FrameGraphPhysicalQueueId{
    u16 index = Limit<u16>::s_Max;
    u16 deviceGeneration = 0u;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{
        return index != Limit<u16>::s_Max && deviceGeneration != 0u;
    }
};
inline constexpr b8 operator==(const FrameGraphPhysicalQueueId& lhs, const FrameGraphPhysicalQueueId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.deviceGeneration == rhs.deviceGeneration;
}

inline constexpr b8 operator!=(const FrameGraphPhysicalQueueId& lhs, const FrameGraphPhysicalQueueId& rhs)noexcept{
    return !(lhs == rhs);
}

struct FrameGraphQueueAssignment{
    FrameGraphPhysicalQueueId initialQueue;
    FrameGraphPhysicalQueueId plannedQueue;
    FrameGraphPhysicalQueueId acceptedQueue;
    FrameGraphPhysicalQueueId previousAcceptedQueue;
    FrameGraphQueueAssignmentScore score;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    FrameGraphQueueAssignmentReason::Enum reason = FrameGraphQueueAssignmentReason::Unknown;
    FrameGraphQueueAssignmentModifier::Mask modifiers = FrameGraphQueueAssignmentModifier::None;
    FrameGraphQueueAssignmentAcceptance::Enum acceptance = FrameGraphQueueAssignmentAcceptance::NotAccepted;
    b8 dedicated = false;
    b8 present = false;
};

struct FrameGraphCompiledTask{
    u64 planGeneration = 0u;
    u32 packetIndex = Limit<u32>::s_Max;
    FrameGraphTaskPacketizationDecision::Enum packetizationDecision = FrameGraphTaskPacketizationDecision::Unknown;
    b8 present = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

