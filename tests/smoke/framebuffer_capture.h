// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/project_entry.h>

#include "smoke_environment.h"

#include <tests/smoke/presentation_types.adhd.h>
#include <tests/smoke/scene_types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>
#include <core/graphics/rhi/resource.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/presentation_contributor.h>
#include <core/task/gpu/task_graph.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_bitmap_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_BitmapFileHeaderSize = Tests::Smoke::s_BitmapFileHeaderSize;
inline constexpr usize s_BitmapInfoHeaderSize = Tests::Smoke::s_BitmapInfoHeaderSize;
inline constexpr usize s_BitmapPixelOffset = s_BitmapFileHeaderSize + s_BitmapInfoHeaderSize;
inline constexpr usize s_BitmapBytesPerPixel = Tests::Smoke::s_BitmapBytesPerPixel;
inline constexpr usize s_SourceBytesPerPixel = Tests::Smoke::s_BitmapSourceBytesPerPixel;
inline constexpr u32 s_ByteMask = Tests::Smoke::s_BitmapByteMask;
inline constexpr u32 s_Byte1Shift = Tests::Smoke::s_BitmapByte1Shift;
inline constexpr u32 s_Byte2Shift = Tests::Smoke::s_BitmapByte2Shift;
inline constexpr u32 s_Byte3Shift = Tests::Smoke::s_BitmapByte3Shift;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void WriteU16LE(const u16 value, const NotNull<u8*> destination)noexcept{
    destination.get()[0] = static_cast<u8>(value & s_ByteMask);
    destination.get()[1] = static_cast<u8>((value >> s_Byte1Shift) & s_ByteMask);
}

inline void WriteU32LE(const u32 value, const NotNull<u8*> destination)noexcept{
    destination.get()[0] = static_cast<u8>(value & s_ByteMask);
    destination.get()[1] = static_cast<u8>((value >> s_Byte1Shift) & s_ByteMask);
    destination.get()[2] = static_cast<u8>((value >> s_Byte2Shift) & s_ByteMask);
    destination.get()[3] = static_cast<u8>((value >> s_Byte3Shift) & s_ByteMask);
}

inline b8 IsBgraFormat(const Core::Format::Enum format)noexcept{
    return format == Core::Format::BGRA8_UNORM || format == Core::Format::BGRA8_UNORM_SRGB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


namespace __hidden_framebuffer_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_BitmapScratchArena = NWB::Tests::Smoke::s_SmokeNameSmokeFramebufferCaptureBitmap;
inline b8 IsSupportedSdrFormat(const Core::Format::Enum format)noexcept{
    return format == Core::Format::RGBA8_UNORM || format == Core::Format::RGBA8_UNORM_SRGB || format == Core::Format::BGRA8_UNORM || format == Core::Format::BGRA8_UNORM_SRGB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BitmapCaptureWriter{
public:
    BitmapCaptureWriter(const ::Path<Core::Alloc::GlobalArena>& outputPath, const Core::TextureDesc& captureDescription)noexcept
        : m_outputPath(outputPath)
        , m_captureDescription(captureDescription)
    {}


public:
    [[nodiscard]] b8 write(const NotNull<const u8*> sourceBytes, const usize sourceRowPitch, Core::Alloc::ScratchArena& scratchArena){
        const u32 width = m_captureDescription.width;
        const u32 height = m_captureDescription.height;
        if(width > static_cast<u32>(Limit<i32>::s_Max) || height > static_cast<u32>(Limit<i32>::s_Max))
            return false;

        const auto sourceRowBytes = TryMultiply<usize>(static_cast<usize>(width), __hidden_bitmap_capture::s_SourceBytesPerPixel);
        if(!sourceRowBytes)
            return false;
        const auto bitmapRowBytes = TryMultiply<usize>(static_cast<usize>(width), __hidden_bitmap_capture::s_BitmapBytesPerPixel);
        if(!bitmapRowBytes || sourceRowPitch < *sourceRowBytes || AddOverflows<usize>(*bitmapRowBytes, 3u))
            return false;

        const usize bitmapRowPitch = (*bitmapRowBytes + 3u) & ~usize(3u);
        const auto bitmapPixelBytes = TryMultiply<usize>(bitmapRowPitch, static_cast<usize>(height));
        if(!bitmapPixelBytes)
            return false;
        if(AddOverflows<usize>(__hidden_bitmap_capture::s_BitmapPixelOffset, *bitmapPixelBytes))
            return false;

        const usize bitmapFileBytes = __hidden_bitmap_capture::s_BitmapPixelOffset + *bitmapPixelBytes;
        if(*bitmapPixelBytes > static_cast<usize>(Limit<u32>::s_Max) || bitmapFileBytes > static_cast<usize>(Limit<u32>::s_Max))
            return false;

        Vector<u8, Core::Alloc::ScratchArena> bitmapBytes(bitmapFileBytes, 0u, scratchArena);
        bitmapBytes[0] = static_cast<u8>('B');
        bitmapBytes[1] = static_cast<u8>('M');
        __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(bitmapFileBytes), MakeNotNull(bitmapBytes.data() + 2u));
        __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(__hidden_bitmap_capture::s_BitmapPixelOffset), MakeNotNull(bitmapBytes.data() + 10u));
        __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(__hidden_bitmap_capture::s_BitmapInfoHeaderSize), MakeNotNull(bitmapBytes.data() + __hidden_bitmap_capture::s_BitmapFileHeaderSize));
        __hidden_bitmap_capture::WriteU32LE(width, MakeNotNull(bitmapBytes.data() + 18u));
        __hidden_bitmap_capture::WriteU32LE(height, MakeNotNull(bitmapBytes.data() + 22u));
        __hidden_bitmap_capture::WriteU16LE(1u, MakeNotNull(bitmapBytes.data() + 26u));
        __hidden_bitmap_capture::WriteU16LE(24u, MakeNotNull(bitmapBytes.data() + 28u));
        __hidden_bitmap_capture::WriteU32LE(static_cast<u32>(*bitmapPixelBytes), MakeNotNull(bitmapBytes.data() + 34u));

        const b8 sourceIsBgra = __hidden_bitmap_capture::IsBgraFormat(m_captureDescription.format);
        for(u32 bitmapY = 0u; bitmapY < height; ++bitmapY){
            const usize sourceY = static_cast<usize>(height - 1u - bitmapY);
            const auto sourceRowOffset = TryMultiply<usize>(sourceY, sourceRowPitch);
            if(!sourceRowOffset)
                return false;
            const auto sourceRow = MakeNotNull(sourceBytes.get() + *sourceRowOffset);
            const auto bitmapRow = MakeNotNull(bitmapBytes.data() + __hidden_bitmap_capture::s_BitmapPixelOffset + static_cast<usize>(bitmapY) * bitmapRowPitch);
            for(u32 x = 0u; x < width; ++x){
                const auto sourcePixel = MakeNotNull(sourceRow.get() + static_cast<usize>(x) * __hidden_bitmap_capture::s_SourceBytesPerPixel);
                const auto bitmapPixel = MakeNotNull(bitmapRow.get() + static_cast<usize>(x) * __hidden_bitmap_capture::s_BitmapBytesPerPixel);
                bitmapPixel.get()[0] = sourceIsBgra ? sourcePixel.get()[0] : sourcePixel.get()[2];
                bitmapPixel.get()[1] = sourcePixel.get()[1];
                bitmapPixel.get()[2] = sourceIsBgra ? sourcePixel.get()[2] : sourcePixel.get()[0];
            }
        }

        const ::Path<Core::Alloc::ScratchArena> outputPath(scratchArena, m_outputPath);
        {
            const auto outputDirectory = outputPath.parentPath();
            if(!outputDirectory.empty() && !EnsureDirectories(outputDirectory))
                return false;
        }

        ::Path<Core::Alloc::ScratchArena> partialPath(scratchArena, outputPath);
        partialPath += NWB_TEXT(".partial");
        if(!WriteBinaryFile(partialPath, bitmapBytes)){
            const auto removed = RemoveFile(partialPath);
            if(!removed)
                NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an incomplete partial capture"));
            return false;
        }
        if(!RenamePath(partialPath, outputPath)){
            const auto removed = RemoveFile(partialPath);
            if(!removed)
                NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an uncommitted partial capture"));
            return false;
        }
        return true;
    }


private:
    const ::Path<Core::Alloc::GlobalArena>& m_outputPath;
    const Core::TextureDesc& m_captureDescription;
};


struct FramebufferCaptureOptions{
    // Borrowed predicate context must outlive stop.
    b8 (*shouldCapture)(void*, u64) = nullptr;
    void* predicateContext = nullptr;
    u32 requiredWidth = 0u;
    u32 requiredHeight = 0u;
    b8 quitWhenReady = true;
};

// Ref-count completion state so teardown can finish after the project stops the observer.
class FramebufferCapture final
    : public Core::IRenderPass
    , public Core::IGpuTaskGraphPresentationContributor
{
private:
    struct CompletionStateData{
        Core::QueueSubmissionToken acceptedToken;
        u64 graphicsFrameIndex = Limit<u64>::s_Max;
        u64 generation = 1u;
    };


private:
    using CompletionState = RefCounter<CompletionStateData>;


private:
    struct ReadbackTask{
        static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::Transfer };


    public:
        struct Payload{
            Core::TextureHandle source;
            Core::TextureSlice sourceSlice;
            Core::StagingTextureHandle destination;
            Core::TextureSlice destinationSlice;
            RefCountPtr<CompletionState> completionState;
            u64 graphicsFrameIndex = 0u;
            u64 generation = 0u;
        };


    public:
        [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
            if(!payload.source || !payload.destination || !payload.completionState || context.commandIrCapture)
                return false;

            commandList.copyTexture(*payload.destination, payload.destinationSlice, *payload.source, payload.sourceSlice);
            return !commandList.commandRecordingFailed();
        }

        static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
            if(payload.completionState && payload.generation == payload.completionState->generation){
                payload.completionState->acceptedToken = token;
                payload.completionState->graphicsFrameIndex = payload.graphicsFrameIndex;
            }
        }

        static void Discarded(Payload& payload)noexcept{
            if(payload.completionState && payload.generation == payload.completionState->generation)
                payload.completionState->acceptedToken = {};
        }
    };


public:
    FramebufferCapture(const AStringView outputPath, const u32 captureFrameCount, ProjectRuntimeContext& context, const FramebufferCaptureOptions options = {})
        : FramebufferCapture(::Path<Core::Alloc::GlobalArena>(context.objectArena, outputPath), captureFrameCount, context, options)
    {}

    FramebufferCapture(::Path<Core::Alloc::GlobalArena>&& outputPath, const u32 captureFrameCount, ProjectRuntimeContext& context, const FramebufferCaptureOptions options = {})
        : Core::IRenderPass(context.graphics)
        , m_context(context)
        , m_outputPath(Move(outputPath))
        , m_options(options)
        , m_completionState(MakeRefCount<CompletionState>())
        , m_captureFrameCount(captureFrameCount)
    {
        if(m_outputPath.empty())
            markFailed(NWB_TEXT("capture output path is empty"));
        if(m_captureFrameCount == 0u)
            markFailed(NWB_TEXT("capture frame count must be greater than zero"));
    }

    virtual ~FramebufferCapture()noexcept override{
        stop();
    }


public:
    [[nodiscard]] b8 start(){
        if(m_registered)
            return true;
        if(m_failed || m_stopped)
            return false;

        Core::GraphicsRuntime& graphics = getGraphics();
        Core::IGpuTaskGraphPresentationContributor* const contributor = graphics.taskGraphPresentationContributor();
        if(contributor && contributor != this){
            markFailed(NWB_TEXT("another task-graph presentation contributor is already registered"));
            return false;
        }

        graphics.addRenderPassToBack(*this);
        if(m_failed){
            graphics.removeRenderPass(*this);
            return false;
        }
        graphics.setTaskGraphPresentationContributor(this);
        m_registered = true;
        return true;
    }

    void stop(){
        if(!m_registered)
            return;

        Core::GraphicsRuntime& graphics = getGraphics();
        graphics.clearTaskGraphPresentationContributor(*this);
        m_registered = false;
        m_stopped = true;
        graphics.removeRenderPass(*this);
        resetPendingReadback();
    }

    void update(){
        if(!m_registered)
            return;
        if(m_skipped || m_failed){
            requestTerminalQuit();
            return;
        }
        if(m_captureReady){
            if(m_options.quitWhenReady)
                requestTerminalQuit();
            return;
        }
        const Core::QueueSubmissionToken acceptedToken = m_completionState->acceptedToken;
        if(!acceptedToken.valid())
            return;
        if(!acceptedToken.hasPhysicalQueueIdentity()){
            markFailed(NWB_TEXT("accepted readback submission has no physical queue identity"));
            requestTerminalQuit();
            return;
        }

        Core::GraphicsBackend::Device& device = getGraphics().getDevice();
        if(acceptedToken.deviceGeneration != device.getDeviceGeneration()){
            markFailed(NWB_TEXT("accepted readback submission belongs to a stale graphics device"));
            requestTerminalQuit();
            return;
        }
        const Core::GpuPhysicalQueueId physicalQueue{ .index = acceptedToken.physicalQueueIndex, .deviceGeneration = acceptedToken.deviceGeneration, };
        const Expected<u64> completedValue = device.queueGetCompletedInstance(physicalQueue);
        if(!completedValue){
            markFailed(NWB_TEXT("failed to query readback submission completion"));
            requestTerminalQuit();
            return;
        }
        if(*completedValue < acceptedToken.value)
            return;
        if(!m_readback){
            markFailed(NWB_TEXT("accepted readback submission lost its staging texture"));
            requestTerminalQuit();
            return;
        }

        const auto mapping = device.mapStagingTexture(*m_readback, Core::TextureSlice{}, Core::CpuAccessMode::Read);
        if(!mapping){
            markFailed(NWB_TEXT("failed to map the completed readback staging texture"));
            requestTerminalQuit();
            return;
        }

        Core::Alloc::ScratchArena scratchArena(__hidden_framebuffer_capture::s_BitmapScratchArena);
        const b8 captureWritten = BitmapCaptureWriter(m_outputPath, m_captureDescription).write(MakeNotNull(static_cast<const u8*>(mapping->data)), mapping->rowPitch, scratchArena);
        device.unmapStagingTexture(*m_readback);
        if(!captureWritten){
            markFailed(NWB_TEXT("failed to write the framebuffer capture atomically"));
            requestTerminalQuit();
            return;
        }

        m_captureReady = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FramebufferCapture: capture ready"));
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FramebufferCapture: graphics source frame {}"), m_completionState->graphicsFrameIndex);
        if(m_options.quitWhenReady)
            requestTerminalQuit();
    }

    [[nodiscard]] b8 rearm(::Path<Core::Alloc::GlobalArena>&& outputPath)noexcept{
        if(!m_registered || !m_captureReady || m_failed || m_skipped || m_quitRequested || outputPath.empty() || m_completionState->generation == Limit<u64>::s_Max)
            return false;

        m_outputPath = Move(outputPath);
        ++m_completionState->generation;
        m_completionState->acceptedToken = {};
        m_completionState->graphicsFrameIndex = Limit<u64>::s_Max;
        m_preparedFrame = {};
        m_preparedGraphGeneration = 0u;
        m_lastCountedGraphicsFrame = Limit<u64>::s_Max;
        m_preparedFrameCount = 0u;
        m_taskGraphPresentationPrepared = false;
        m_taskGraphPresentationClaimed = false;
        m_captureReady = false;
        return true;
    }

    [[nodiscard]] b8 captureReady()const noexcept{ return m_captureReady; }

    [[nodiscard]] u64 capturedGraphicsFrameIndex()const noexcept{ return m_completionState->graphicsFrameIndex; }

    void finish(){
        NWB_ASSERT(m_captureReady);
        requestTerminalQuit();
    }


public:
    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }

    virtual b8 validateResources(const u32 width, const u32 height, const u32 /*sampleCount*/)override{
        if(m_skipped || m_failed || width == 0u || height == 0u)
            return true;
        if((m_options.requiredWidth != 0u && width != m_options.requiredWidth) || (m_options.requiredHeight != 0u && height != m_options.requiredHeight))
            return true;
        if(!getGraphics().isSwapChainReadbackAvailable())
            return true;

        const Core::Texture* const backbuffer = getGraphics().getBackBuffer(0u);
        if(!backbuffer){
            markFailed(NWB_TEXT("swap-chain texture is unavailable during capture resource setup"));
            return false;
        }
        return prepareReadback(backbuffer->getCreationDescription());
    }

    virtual void invalidateResources()override{
        if(!m_registered)
            return;

        if(m_captureReady){
            m_readback.reset();
            m_captureDescription = {};
        }else if(!m_skipped && !m_failed)
            resetPendingReadback();
    }

    virtual void backBufferResizing()override{
        if(m_registered)
            invalidateResources();
    }


public:
    [[nodiscard]] virtual b8 prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame)override{
        m_taskGraphPresentationPrepared = false;
        if(m_captureReady || m_skipped || m_failed || m_completionState->acceptedToken.valid())
            return true;

        if(!getGraphics().isSwapChainReadbackAvailable()){
            m_skipped = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FramebufferCapture: skipped because swap-chain transfer-source usage is unavailable"));
            return true;
        }
        if(!frame.valid()){
            markFailed(NWB_TEXT("renderer supplied an invalid acquired presentation frame"));
            return true;
        }

        const u64 graphicsFrame = getGraphics().getFrameIndex();
        if(graphicsFrame != m_lastCountedGraphicsFrame){
            m_lastCountedGraphicsFrame = graphicsFrame;
            if(m_preparedFrameCount < m_captureFrameCount)
                ++m_preparedFrameCount;
        }
        if(m_preparedFrameCount < m_captureFrameCount)
            return true;
        const Core::TextureDesc& description = frame.backBuffer.texture->getCreationDescription();
        if((m_options.requiredWidth != 0u && description.width != m_options.requiredWidth) || (m_options.requiredHeight != 0u && description.height != m_options.requiredHeight))
            return true;
        if(m_options.shouldCapture && !m_options.shouldCapture(m_options.predicateContext, graphicsFrame))
            return true;

        if(!stagingMatches(description)){
            markFailed(NWB_TEXT("framebuffer readback staging texture was not prepared for the current swap chain"));
            return true;
        }

        m_preparedFrame = frame;
        m_taskGraphPresentationPrepared = true;
        m_taskGraphPresentationClaimed = false;
        m_preparedGraphGeneration = 0u;
        return true;
    }

    [[nodiscard]] virtual b8 hasTaskGraphPresentationWork()const noexcept override{
        return m_taskGraphPresentationPrepared && m_readback && !m_completionState->acceptedToken.valid() && !m_captureReady && !m_skipped && !m_failed;
    }

    [[nodiscard]] virtual Expected<Core::GpuTaskId> declareTaskGraphPresentation(Core::GpuTaskGraph& graph, const Core::AcquiredPresentationFrame& frame, const Core::GpuGraphResourceId backbuffer, const Core::GpuTaskId previousTask)override{
        u64 graphGeneration = 0u;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            graphGeneration = declarations.generation();
        }
        if(m_taskGraphPresentationClaimed && m_preparedGraphGeneration != graphGeneration){
            m_taskGraphPresentationClaimed = false;
            m_preparedGraphGeneration = 0u;
        }
        if(!m_taskGraphPresentationPrepared || m_taskGraphPresentationClaimed || !m_readback || m_completionState->acceptedToken.valid())
            return MakeUnexpected(Failure{});
        if(
            !frame.valid()
            || !previousTask.valid()
            || !backbuffer.valid()
            || frame.backBuffer.texture != m_preparedFrame.backBuffer.texture
            || frame.backBuffer.nativeInitialState != m_preparedFrame.backBuffer.nativeInitialState
            || frame.backBuffer.index != m_preparedFrame.backBuffer.index
            || frame.framebuffer != m_preparedFrame.framebuffer
        ){
            markFailed(NWB_TEXT("presentation declaration no longer matches the prepared acquired frame"));
            return MakeUnexpected(Failure{});
        }

        Core::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Core::GpuTaskCostHint::Small;
        scheduling.overlapPreferred = false;
        scheduling.avoidQueueCrossing = true;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;

        const auto stagingHazardResult = graph.importHazardDomain(Core::GpuGraphResourceDesc{}.setIdentity(Name(NWB::Tests::Smoke::s_SmokeNameTestsSmokeFramebufferCaptureStaging))
            .setMarkerLabel("Framebuffer Capture Staging").setType(Core::GpuGraphResourceType::HazardDomain));
        if(!stagingHazardResult){
            markFailed(NWB_TEXT("failed to declare the framebuffer staging hazard domain"));
            return MakeUnexpected(Failure{});
        }

        const Core::GpuGraphResourceId stagingHazard = *stagingHazardResult;

        Core::GpuTaskDesc taskDesc;
        const Array<Core::GpuTaskResourceUse, 2> resourceUses = {
            Core::GpuTaskResourceUse{ .resource = backbuffer, .range = { .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u) }, .requiredState = Core::ResourceStates::CopySource, .access = Core::GpuTaskResourceAccess::Read, },
            Core::GpuTaskResourceUse{ .resource = stagingHazard, .range = {}, .requiredState = Core::ResourceStates::Unknown, .access = Core::GpuTaskResourceAccess::Write, },
        };
        taskDesc.setIdentity(Name(NWB::Tests::Smoke::s_SmokeNameTestsSmokeFramebufferCapture)).setMarkerLabel("Framebuffer Capture Readback").setScheduling(scheduling).setDependencies(Span(&previousTask, 1u)).setResourceUses(resourceUses);
        const auto readbackTask = graph.addTask<ReadbackTask>(
            taskDesc,
            ReadbackTask::Payload{
                .source = frame.backBuffer.texture,
                .sourceSlice = {},
                .destination = m_readback,
                .destinationSlice = {},
                .completionState = m_completionState,
                .graphicsFrameIndex = m_lastCountedGraphicsFrame,
                .generation = m_completionState->generation,
            });
        if(!readbackTask){
            markFailed(NWB_TEXT("failed to declare the framebuffer readback task"));
            return MakeUnexpected(Failure{});
        }

        m_taskGraphPresentationClaimed = true;
        m_preparedGraphGeneration = graphGeneration;
        return readbackTask;
    }


private:
    void markFailed(const TStringView reason){
        if(m_failed)
            return;

        m_failed = true;
        NWB_LOGGER_ERROR(NWB_TEXT("FramebufferCapture: {}"), reason);
    }

    void requestTerminalQuit(){
        if(m_quitRequested)
            return;

        m_quitRequested = true;
        m_context.requestQuit();
    }

    void resetPendingReadback(){
        m_completionState = MakeRefCount<CompletionState>();
        m_readback.reset();
        m_captureDescription = {};
        m_preparedFrame = {};
        m_preparedGraphGeneration = 0u;
        m_taskGraphPresentationPrepared = false;
        m_taskGraphPresentationClaimed = false;
    }

    [[nodiscard]] b8 stagingMatches(const Core::TextureDesc& description)const noexcept{
        return
            m_readback
            && m_captureDescription.width == description.width
            && m_captureDescription.height == description.height
            && m_captureDescription.depth == description.depth
            && m_captureDescription.arraySize == description.arraySize
            && m_captureDescription.mipLevels == description.mipLevels
            && m_captureDescription.sampleCount == description.sampleCount
            && m_captureDescription.sampleQuality == description.sampleQuality
            && m_captureDescription.format == description.format
            && m_captureDescription.dimension == description.dimension
            && m_captureDescription.queueSharing == description.queueSharing
        ;
    }

    [[nodiscard]] b8 prepareReadback(const Core::TextureDesc& description){
        if(!__hidden_framebuffer_capture::IsSupportedSdrFormat(description.format)){
            markFailed(NWB_TEXT("swap-chain format is not SDR RGBA8/BGRA8 UNORM or SRGB"));
            return false;
        }
        if(description.width == 0u || description.height == 0u || description.depth != 1u || description.arraySize != 1u || description.mipLevels != 1u || description.sampleCount != 1u || description.dimension != Core::TextureDimension::Texture2D){
            markFailed(NWB_TEXT("swap-chain texture shape cannot be represented by the SDR framebuffer capture"));
            return false;
        }
        if(stagingMatches(description))
            return true;

        m_readback.reset();
        m_captureDescription = description;
        auto readbackResult = getGraphics().getDevice().createStagingTexture(description, Core::CpuAccessMode::Read);
        if(readbackResult)
            m_readback = Move(*readbackResult);
        else
            m_readback.reset();
        if(!readbackResult){
            markFailed(NWB_TEXT("failed to allocate the matching framebuffer readback staging texture"));
            return false;
        }
        return true;
    }


private:
    ProjectRuntimeContext& m_context;
    ::Path<Core::Alloc::GlobalArena> m_outputPath;
    FramebufferCaptureOptions m_options;
    RefCountPtr<CompletionState> m_completionState;
    Core::StagingTextureHandle m_readback;
    Core::TextureDesc m_captureDescription;
    Core::AcquiredPresentationFrame m_preparedFrame;
    u64 m_preparedGraphGeneration = 0u;
    u64 m_lastCountedGraphicsFrame = Limit<u64>::s_Max;
    u32 m_captureFrameCount = 0u;
    u32 m_preparedFrameCount = 0u;
    b8 m_registered = false;
    b8 m_taskGraphPresentationPrepared = false;
    b8 m_taskGraphPresentationClaimed = false;
    b8 m_captureReady = false;
    b8 m_skipped = false;
    b8 m_failed = false;
    b8 m_quitRequested = false;
    b8 m_stopped = false;
};

[[nodiscard]] inline b8 TickSmokeWorld(const f32 delta, Core::ECS::World& world){
    const auto updated = world.tick(delta);
    if(!updated){
        NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
        return false;
    }
    return true;
}

[[nodiscard]] inline b8 TickSmokeWorldWithCapture(const f32 delta, Core::ECS::World& world, UniquePtr<FramebufferCapture>& capture){
    if(capture)
        capture->update();
    return TickSmokeWorld(delta, world);
}


struct SmokeCaptureSeries{
    Vector<::Path<Core::Alloc::GlobalArena>, Core::Alloc::GlobalArena> paths;
    UniquePtr<FramebufferCapture> capture;


public:
    explicit SmokeCaptureSeries(Core::Alloc::GlobalArena& arena)noexcept
        : paths(arena)
    {}
};


class SmokeCaptureFactory{
public:
    explicit SmokeCaptureFactory(ProjectRuntimeContext& context)noexcept
        : m_context(context)
    {}


public:
    [[nodiscard]] Expected<UniquePtr<FramebufferCapture>> configure(
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

    [[nodiscard]] Expected<UniquePtr<FramebufferCapture>> configure(
        const TStringView projectName,
        const u32 defaultFrameCount,
        const FramebufferCaptureOptions& options
    ){
        Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
        return configure(projectName, defaultFrameCount, options, scratchArena);
    }

    [[nodiscard]] Expected<SmokeCaptureSeries> prepareSeries(const AStringView outputPath, const u32 sampleCount, const FramebufferCaptureOptions& options){
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


private:
    ProjectRuntimeContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
