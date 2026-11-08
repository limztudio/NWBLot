// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_capture_factory.h"
#include "smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UniquePtr<FramebufferCapture>> SmokeCaptureFactory::configure(
    const TStringView projectName,
    const u32 defaultFrameCount,
    const FramebufferCaptureOptions& options,
    Core::Alloc::ScratchArena& scratchArena
){
    const auto outputPath = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
    if(!outputPath)
        return UniquePtr<FramebufferCapture>{};

    u32 captureFrameCount = defaultFrameCount;
    const auto frameCountText = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT");
    if(frameCountText){
        const auto parsedFrameCount = ParseU64(AStringView(frameCountText->data(), frameCountText->size()));
        if(!parsedFrameCount || *parsedFrameCount == 0u || *parsedFrameCount > static_cast<u64>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: capture frame count must be a positive u32"), projectName);
            return MakeUnexpected(Failure{});
        }
        captureFrameCount = static_cast<u32>(*parsedFrameCount);
    }

    auto capture = MakeUnique<FramebufferCapture>(AStringView(outputPath->data(), outputPath->size()), captureFrameCount, m_context, options);
    if(!capture || !capture->start())
        return MakeUnexpected(Failure{});
    return capture;
}

Expected<UniquePtr<FramebufferCapture>> SmokeCaptureFactory::configure(
    const TStringView projectName,
    const u32 defaultFrameCount,
    const FramebufferCaptureOptions& options
){
    Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
    return configure(projectName, defaultFrameCount, options, scratchArena);
}

Expected<SmokeCaptureSeries> SmokeCaptureFactory::prepareSeries(const AStringView outputPath, const u32 sampleCount, const FramebufferCaptureOptions& options){
    SmokeCaptureSeries result(m_context.objectArena);
    result.paths.reserve(sampleCount);
    for(u32 sample = 0u; sample < sampleCount; ++sample){
        const auto path = sample + 1u == sampleCount ? SmokeEnvironmentString(outputPath, m_context.objectArena) : StringFormat(m_context.objectArena, "{}.{}.bmp", outputPath, sample);
        result.paths.emplace_back(m_context.objectArena, AStringView(path));
    }
    result.capture = MakeUnique<FramebufferCapture>(Move(result.paths.front()), 1u, m_context, options);
    if(!result.capture || !result.capture->start())
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

