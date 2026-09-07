// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTimingSampleCapture{
    GpuTimingSample samples[4u] = {};
    u32 sampleCount = 0u;


    static void Invoke(void* const context, const GpuTimingSample& sample){
        GpuTimingSampleCapture* const capture = static_cast<GpuTimingSampleCapture*>(context);
        if(!capture || capture->sampleCount >= LengthOf(capture->samples))
            return;

        capture->samples[capture->sampleCount] = sample;
        ++capture->sampleCount;
    }


    [[nodiscard]] const GpuTimingSample* find(const GpuTimingSampleAttribution attribution)const noexcept{
        for(u32 sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
            if(samples[sampleIndex].attribution == attribution)
                return &samples[sampleIndex];
        }
        return nullptr;
    }
};


class ScopedGpuTimingSampleListener final : NoCopy{
public:
    ScopedGpuTimingSampleListener(GpuTimingRecorder& timing, const GpuTimingSampleListener& listener)
        : m_timing(timing)
        , m_subscription(m_timing.subscribeSampleListener(listener))
    {}
    ScopedGpuTimingSampleListener(GpuTimingRecorder& timing, GpuTimingSampleCapture& capture)
        : ScopedGpuTimingSampleListener(timing, GpuTimingSampleListener{
            .context = &capture,
            .invoke = &GpuTimingSampleCapture::Invoke,
        })
    {}
    ~ScopedGpuTimingSampleListener(){ unsubscribe(); }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_subscription.valid(); }
    [[nodiscard]] bool setFeedbackCollectionScopes(
        const NotNull<const Name*> scopeNames,
        const usize scopeCount
    ){
        return m_timing.setFeedbackCollectionScopes(m_subscription, scopeNames, scopeCount);
    }
    [[nodiscard]] bool clearFeedbackCollectionScopes(){
        return m_timing.clearFeedbackCollectionScopes(m_subscription);
    }
    void unsubscribe()noexcept{
        const GpuTimingSampleSubscription subscription = m_subscription;
        m_subscription = {};
        m_timing.unsubscribeSampleListener(subscription);
    }


private:
    GpuTimingRecorder& m_timing;
    GpuTimingSampleSubscription m_subscription;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

