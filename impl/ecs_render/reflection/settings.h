// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ReflectionTraceMode{
    enum Enum : u8{
        Disabled,
        ScreenSpace,
        Hardware,
        Hybrid,
    };
};

namespace ReflectionDebugView{
    enum Enum : u8{
        None,
        TraceSource,
        Confidence,
    };
};

struct ReflectionSettings{
    ReflectionTraceMode::Enum traceMode = ReflectionTraceMode::Hybrid;
    ReflectionDebugView::Enum debugView = ReflectionDebugView::None;
    bool diagnosticsEnabled = false;
    // Distance limits the reflected path in world units, not the camera-to-receiver distance.
    f32 maxRayDistance = 100.f;
    f32 distanceFadeStart = 80.f;
    f32 roughnessCutoff = 0.8f;
    u32 maxHardwareRaysPerFrame = 262144u;
    u32 screenMaxSteps = 96u;
    // World-space surface uncertainty, acceptance confidence and normalized viewport-edge fade width.
    f32 screenThickness = 0.03f;
    f32 screenConfidenceThreshold = 0.8f;
    f32 screenEdgeFade = 0.05f;
    // Linear HDR radiance for the analytic sky fallback. A missing trace is never a black validity hole.
    Float3U environmentTop = Float3U(0.15f, 0.2f, 0.3f);
    Float3U environmentBottom = Float3U(0.04f, 0.04f, 0.04f);
};

// Settings are an external control boundary. Reject invalid values before they reach shaders or history state.
[[nodiscard]] bool ValidateReflectionSettings(const ReflectionSettings& settings);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

