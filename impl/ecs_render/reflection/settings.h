// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets/graphics/raytrace/optical_transport_constants.h>


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

inline constexpr u32 s_ReflectionDefaultTemporalMaxSamples = 16u;
inline constexpr f32 s_ReflectionDefaultMaxRayDistance = 100.f;
inline constexpr f32 s_ReflectionDefaultDistanceFadeStart = 80.f;
inline constexpr f32 s_ReflectionDefaultRoughnessCutoff = 0.8f;
inline constexpr u32 s_ReflectionDefaultMaxHardwareRaysPerFrame = 262144u;
inline constexpr u32 s_ReflectionDefaultScreenMaxSteps = 96u;
inline constexpr f32 s_ReflectionDefaultScreenThickness = 0.03f;
inline constexpr f32 s_ReflectionDefaultScreenConfidenceThreshold = 0.8f;
inline constexpr f32 s_ReflectionDefaultScreenEdgeFade = 0.05f;
inline constexpr Float3U s_ReflectionDefaultEnvironmentTop = Float3U(0.15f, 0.2f, 0.3f);
inline constexpr Float3U s_ReflectionDefaultEnvironmentBottom = Float3U(0.04f, 0.04f, 0.04f);


struct ReflectionSettings{
    u32 temporalMaxSamples = s_ReflectionDefaultTemporalMaxSamples;
    u32 samplingSeed = 0u;
    u32 spatialRadius = 2u;
    // Reflected-path distance in world units, not camera-to-receiver distance.
    f32 maxRayDistance = s_ReflectionDefaultMaxRayDistance;
    f32 distanceFadeStart = s_ReflectionDefaultDistanceFadeStart;
    f32 roughnessCutoff = s_ReflectionDefaultRoughnessCutoff;
    u32 maxHardwareRaysPerFrame = s_ReflectionDefaultMaxHardwareRaysPerFrame;
    // Scene queries per admitted path, including optional initial-medium discovery.
    u32 maxOpticalQueries = NWB_OPTICAL_DEFAULT_QUERIES;
    u32 screenMaxSteps = s_ReflectionDefaultScreenMaxSteps;
    // Surface uncertainty, confidence, and edge-fade width.
    f32 screenThickness = s_ReflectionDefaultScreenThickness;
    f32 screenConfidenceThreshold = s_ReflectionDefaultScreenConfidenceThreshold;
    f32 screenEdgeFade = s_ReflectionDefaultScreenEdgeFade;
    // Linear HDR radiance for ordinary exterior misses.
    Float3U environmentTop = s_ReflectionDefaultEnvironmentTop;
    Float3U environmentBottom = s_ReflectionDefaultEnvironmentBottom;
    ReflectionTraceMode::Enum traceMode = ReflectionTraceMode::Hybrid;
    ReflectionDebugView::Enum debugView = ReflectionDebugView::None;
    bool diagnosticsEnabled = false;
    // Lets stable smooth Hybrid tiles bypass proven screen misses. Off until measured.
    bool screenFeedbackEnabled = false;
    bool temporalEnabled = true;
    bool spatialFilterEnabled = true;
};

// Settings are an external boundary; reject invalid values early.
[[nodiscard]] bool ValidateReflectionSettings(const ReflectionSettings& settings);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

