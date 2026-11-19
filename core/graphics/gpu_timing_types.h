// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"
#include "gpu_timing_metric_correlator.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingSubmissionTicket;

struct GpuTimingScope{
    Name scopeName = s_NameNone;
    u64 reservation = 0u;
    TimerQueryRecordingToken timerQueryRecording;
    GpuTimingSubmissionTicket* submissionTicket = nullptr;
    usize submissionPublicationIndex = Limit<usize>::s_Max;
    u32 index = Limit<u32>::s_Max;
    u32 epoch = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept{ return scopeName != s_NameNone && index != Limit<u32>::s_Max && epoch != 0u && reservation != 0u; }
};

static_assert(IsTriviallyCopyable_V<GpuTimingScope>, "GPU timing publication must remain allocation-free after native recording begins");

struct GpuTimingScopeDefinition{
    Name identity = s_NameNone;
    AStringView markerLabel;


public:
    [[nodiscard]] static Expected<GpuTimingScopeDefinition, NameTextError::Enum> FromText(const AStringView label){
        const auto name = Name::FromText(label);
        if(!name)
            return MakeUnexpected(name.error());
        GpuTimingScopeDefinition definition;
        definition.identity = *name;
        definition.markerLabel = label;
        return definition;
    }


public:
    constexpr GpuTimingScopeDefinition()noexcept = default;
    template<usize LabelSize>
    constexpr explicit GpuTimingScopeDefinition(const char (&label)[LabelSize])noexcept
        : identity(label)
        , markerLabel(label)
    {}
    explicit GpuTimingScopeDefinition(const NameText<char> label)
        : identity(label)
        , markerLabel(label.text())
    {}


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return identity && !markerLabel.empty(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingSampleAttribution final{
    friend class GpuTimingRecorder;
    friend constexpr b8 operator==(const GpuTimingSampleAttribution& lhs, const GpuTimingSampleAttribution& rhs)noexcept;


public:
    constexpr GpuTimingSampleAttribution()noexcept = default;
private:
    constexpr explicit GpuTimingSampleAttribution(const u64 identity)noexcept
        : m_identity(identity)
    {}


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return m_identity != 0u; }


private:
    u64 m_identity = 0u;
};

inline constexpr b8 operator==(const GpuTimingSampleAttribution& lhs, const GpuTimingSampleAttribution& rhs)noexcept{
    return lhs.m_identity == rhs.m_identity;
}

inline constexpr b8 operator!=(const GpuTimingSampleAttribution& lhs, const GpuTimingSampleAttribution& rhs)noexcept{
    return !(lhs == rhs);
}

inline constexpr GpuTimingSampleAttribution s_NoGpuTimingSampleAttribution;

class GpuTimingRecordedSample final{
public:
    [[nodiscard]] GpuTimingSampleAttribution attribution()const noexcept{ return m_attribution; }
    void record(const GpuTimingSampleAttribution attribution)const noexcept{ m_attribution = attribution; }
    void reset()noexcept{ m_attribution = s_NoGpuTimingSampleAttribution; }


private:
    mutable GpuTimingSampleAttribution m_attribution;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTimingSample{
    u64 sourceFrameIndex = 0u;
    f64 durationSeconds = 0.0;
    Name scopeName = s_NameNone;
    GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution;
    GpuComparableTimestampRange comparableRange;
    GpuPhysicalQueueId physicalQueue;
    b8 published = false;
};

struct GpuTimingSampleListener{
    void* context = nullptr;
    void (*invoke)(void* context, const GpuTimingSample& sample) = nullptr;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return invoke != nullptr; }
};

template<typename Receiver>
[[nodiscard]] GpuTimingSampleListener MakeGpuTimingSampleListener(Receiver& receiver)noexcept{
    return { &receiver, [](void* context, const GpuTimingSample& sample)noexcept{
        if(context)
            static_cast<Receiver*>(context)->onSample(sample);
    } };
}

class GpuTimingSampleSubscription final{
    friend class GpuTimingRecorder;
    friend constexpr b8 operator==(const GpuTimingSampleSubscription& lhs, const GpuTimingSampleSubscription& rhs)noexcept;


public:
    constexpr GpuTimingSampleSubscription()noexcept = default;
private:
    constexpr explicit GpuTimingSampleSubscription(const u64 identity)noexcept
        : m_identity(identity)
    {}


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return m_identity != 0u; }


private:
    u64 m_identity = 0u;
};

inline constexpr b8 operator==(const GpuTimingSampleSubscription& lhs, const GpuTimingSampleSubscription& rhs)noexcept{
    return lhs.m_identity == rhs.m_identity;
}

inline constexpr b8 operator!=(const GpuTimingSampleSubscription& lhs, const GpuTimingSampleSubscription& rhs)noexcept{
    return !(lhs == rhs);
}


namespace GpuTimingScopeSkipReason{
    enum Enum : u8{
        CollectionInactive,
        QueueTimestampsUnsupported,
        ComparableTimestampsUnsupported,
        ScopeNotPrepared,
        QueryCapacityUnavailable,
        RecordingPositionUnavailable,

        Count,
    };
};


struct GpuTimingRecorderStatistics{
    u64 preparedScopeCount = 0u;
    u64 requestedQueryCount = 0u;
    u64 materializedQueryCount = 0u;
    u64 queryMaterializationFailureCount = 0u;
    u64 scopeAttemptCount = 0u;
    u64 recordedScopeCount = 0u;
    u64 acceptedScopeCount = 0u;
    u64 publishedSampleCount = 0u;
    u64 unpublishedSampleCount = 0u;
    u64 discardedScopeCount = 0u;
    u64 quarantinedScopeCount = 0u;
    u64 beginFailureCount = 0u;
    u64 sampleListenerFailureCount = 0u;
    Array<u64, GpuTimingScopeSkipReason::Count> skippedScopeCountByReason{};
    u16 deviceGeneration = 0u;
    b8 queryCollectionEnabled = false;
    b8 timingSinkEnabled = false;
    b8 feedbackCollectionEnabled = false;
    b8 collectionActive = false;
    b8 comparableTimestampsSupported = false;


public:
    [[nodiscard]] b8 valid()const noexcept{ return deviceGeneration != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

