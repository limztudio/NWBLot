// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "framebuffer_capture.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>
#include <core/graphics/task_graph/compiled_graph.h>
#include <core/graphics/task_graph/task_graph.h>
#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_framebuffer_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Name s_BitmapScratchArena("tests/smoke/framebuffer_capture/bitmap");
static constexpr usize s_BitmapFileHeaderSize = 14u;
static constexpr usize s_BitmapInfoHeaderSize = 40u;
static constexpr usize s_BitmapPixelOffset = s_BitmapFileHeaderSize + s_BitmapInfoHeaderSize;
static constexpr usize s_BitmapBytesPerPixel = 3u;
static constexpr usize s_SourceBytesPerPixel = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void WriteU16LE(u8* const destination, const u16 value){
    destination[0] = static_cast<u8>(value & 0xffu);
    destination[1] = static_cast<u8>((value >> 8u) & 0xffu);
}

static void WriteU32LE(u8* const destination, const u32 value){
    destination[0] = static_cast<u8>(value & 0xffu);
    destination[1] = static_cast<u8>((value >> 8u) & 0xffu);
    destination[2] = static_cast<u8>((value >> 16u) & 0xffu);
    destination[3] = static_cast<u8>((value >> 24u) & 0xffu);
}

static bool IsSupportedSdrFormat(const Core::Format::Enum format){
    return
        format == Core::Format::RGBA8_UNORM
        || format == Core::Format::RGBA8_UNORM_SRGB
        || format == Core::Format::BGRA8_UNORM
        || format == Core::Format::BGRA8_UNORM_SRGB
    ;
}

static bool IsBgraFormat(const Core::Format::Enum format){
    return format == Core::Format::BGRA8_UNORM || format == Core::Format::BGRA8_UNORM_SRGB;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FramebufferCapture::ReadbackTask{
    struct Payload{
        Core::TextureHandle source;
        Core::TextureSlice sourceSlice;
        Core::StagingTextureHandle destination;
        Core::TextureSlice destinationSlice;
        RefCountPtr<CompletionState> completionState;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        if(!payload.source || !payload.destination || !payload.completionState || context.commandIrCapture)
            return false;

        commandList.copyTexture(
            payload.destination.get(),
            payload.destinationSlice,
            payload.source.get(),
            payload.sourceSlice
        );
        return !commandList.commandRecordingFailed();
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.completionState)
            payload.completionState->acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.completionState)
            payload.completionState->acceptedToken = {};
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FramebufferCapture::FramebufferCapture(
    ProjectRuntimeContext& context,
    const AStringView outputPath,
    const u32 captureFrameCount
)
    : Core::IRenderPass(context.graphics)
    , m_context(context)
    , m_outputPath(context.objectArena, outputPath)
    , m_completionState(MakeRefCount<CompletionState>())
    , m_captureFrameCount(captureFrameCount)
{
    if(m_outputPath.empty())
        markFailed(NWB_TEXT("capture output path is empty"));
    if(m_captureFrameCount == 0u)
        markFailed(NWB_TEXT("capture frame count must be greater than zero"));
}

FramebufferCapture::~FramebufferCapture(){
    stop();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FramebufferCapture::start(){
    if(m_registered)
        return true;
    if(m_failed || m_stopped)
        return false;

    Core::Graphics& graphics = getGraphics();
    Core::IGpuTaskGraphPresentationContributor* const contributor = graphics.taskGraphPresentationContributor();
    if(contributor && contributor != this){
        markFailed(NWB_TEXT("another task-graph presentation contributor is already registered"));
        return false;
    }

    graphics.addRenderPassToBack(*this);
    graphics.setTaskGraphPresentationContributor(this);
    m_registered = true;
    return true;
}

void FramebufferCapture::stop(){
    if(!m_registered)
        return;

    Core::Graphics& graphics = getGraphics();
    graphics.clearTaskGraphPresentationContributor(*this);
    m_registered = false;
    m_stopped = true;
    graphics.removeRenderPass(*this);
    resetPendingReadback();
}

void FramebufferCapture::update(){
    if(!m_registered)
        return;
    if(m_captureReady || m_skipped || m_failed){
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
    const Core::GpuPhysicalQueueId physicalQueue{
        acceptedToken.physicalQueueIndex,
        acceptedToken.deviceGeneration,
    };
    if(device.queueGetCompletedInstance(physicalQueue) < acceptedToken.value)
        return;
    if(!m_readback){
        markFailed(NWB_TEXT("accepted readback submission lost its staging texture"));
        requestTerminalQuit();
        return;
    }

    usize rowPitch = 0u;
    const auto* const sourceBytes = static_cast<const u8*>(device.mapStagingTexture(
        m_readback.get(),
        Core::TextureSlice{},
        Core::CpuAccessMode::Read,
        &rowPitch
    ));
    if(!sourceBytes){
        markFailed(NWB_TEXT("failed to map the completed readback staging texture"));
        requestTerminalQuit();
        return;
    }

    Core::Alloc::ScratchArena scratchArena(__hidden_framebuffer_capture::s_BitmapScratchArena);
    const bool captureWritten = writeCapture(sourceBytes, rowPitch, scratchArena);
    device.unmapStagingTexture(m_readback.get());
    if(!captureWritten){
        markFailed(NWB_TEXT("failed to write the framebuffer capture atomically"));
        requestTerminalQuit();
        return;
    }

    m_captureReady = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FramebufferCapture: capture ready"));
    requestTerminalQuit();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FramebufferCapture::shouldRenderUnfocused(){
    return true;
}

void FramebufferCapture::invalidateResources(){
    if(!m_registered)
        return;

    if(!m_captureReady && !m_skipped && !m_failed)
        resetPendingReadback();
}

void FramebufferCapture::backBufferResizing(){
    if(m_registered && !m_captureReady && !m_skipped && !m_failed)
        resetPendingReadback();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool FramebufferCapture::prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame){
    m_taskGraphPresentationPrepared = false;
    if(m_captureReady || m_skipped || m_failed || m_completionState->acceptedToken.valid())
        return true;

    if(!getGraphics().isSwapChainReadbackAvailable()){
        m_skipped = true;
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("FramebufferCapture: skipped because swap-chain transfer-source usage is unavailable")
        );
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
    if(!prepareReadback(description))
        return true;

    m_preparedFrame = frame;
    m_taskGraphPresentationPrepared = true;
    m_taskGraphPresentationClaimed = false;
    m_preparedGraphGeneration = 0u;
    return true;
}

bool FramebufferCapture::hasTaskGraphPresentationWork()const{
    return
        m_taskGraphPresentationPrepared
        && m_readback
        && !m_completionState->acceptedToken.valid()
        && !m_captureReady
        && !m_skipped
        && !m_failed
    ;
}

Core::GpuTaskId FramebufferCapture::declareTaskGraphPresentation(
    Core::GpuTaskGraph& graph,
    const Core::AcquiredPresentationFrame& frame,
    const Core::GpuGraphResourceId backbuffer,
    const Core::GpuTaskId previousTask
){
    if(m_taskGraphPresentationClaimed && m_preparedGraphGeneration != graph.generation()){
        m_taskGraphPresentationClaimed = false;
        m_preparedGraphGeneration = 0u;
    }
    if(
        !m_taskGraphPresentationPrepared
        || m_taskGraphPresentationClaimed
        || !m_readback
        || m_completionState->acceptedToken.valid()
    )
        return {};
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
        return {};
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const Core::GpuGraphResourceId stagingHazard = graph.importHazardDomain(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name("tests.smoke.framebuffer_capture.staging"))
            .setMarkerLabel("Framebuffer Capture Staging")
            .setType(Core::GpuGraphResourceType::HazardDomain)
    );
    if(!stagingHazard.valid()){
        markFailed(NWB_TEXT("failed to declare the framebuffer staging hazard domain"));
        return {};
    }

    Core::GpuTaskDesc taskDesc;
    const Core::GpuTaskResourceUse resourceUses[] = {
        {
            .resource = backbuffer,
            .range = { .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u) },
            .requiredState = Core::ResourceStates::CopySource,
            .access = Core::GpuTaskResourceAccess::Read,
        },
        {
            .resource = stagingHazard,
            .range = {},
            .requiredState = Core::ResourceStates::Unknown,
            .access = Core::GpuTaskResourceAccess::Write,
        },
    };
    taskDesc
        .setIdentity(Name("tests.smoke.framebuffer_capture"))
        .setMarkerLabel("Framebuffer Capture Readback")
        .setQueue(Core::GpuQueueRequest{
            Core::GpuQueueCapability::Transfer | Core::GpuQueueCapability::Graphics,
            Core::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setDependencies(&previousTask, 1u)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    const Core::GpuTaskId readbackTask = graph.addTask<ReadbackTask>(
        taskDesc,
        ReadbackTask::Payload{
            .source = frame.backBuffer.texture,
            .sourceSlice = {},
            .destination = m_readback,
            .destinationSlice = {},
            .completionState = m_completionState,
        }
    );
    if(!readbackTask.valid()){
        markFailed(NWB_TEXT("failed to declare the framebuffer readback task"));
        return {};
    }

    m_taskGraphPresentationClaimed = true;
    m_preparedGraphGeneration = graph.generation();
    return readbackTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FramebufferCapture::markFailed(const tchar* const reason){
    if(m_failed)
        return;

    m_failed = true;
    NWB_LOGGER_ERROR(NWB_TEXT("FramebufferCapture: {}"), reason);
}

void FramebufferCapture::requestTerminalQuit(){
    if(m_quitRequested)
        return;

    m_quitRequested = true;
    m_context.requestQuit();
}

void FramebufferCapture::resetPendingReadback(){
    m_completionState = MakeRefCount<CompletionState>();
    m_readback.reset();
    m_captureDescription = {};
    m_preparedFrame = {};
    m_preparedGraphGeneration = 0u;
    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationClaimed = false;
}

bool FramebufferCapture::stagingMatches(const Core::TextureDesc& description)const{
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

bool FramebufferCapture::prepareReadback(const Core::TextureDesc& description){
    if(!__hidden_framebuffer_capture::IsSupportedSdrFormat(description.format)){
        markFailed(NWB_TEXT("swap-chain format is not SDR RGBA8/BGRA8 UNORM or SRGB"));
        return false;
    }
    if(
        description.width == 0u
        || description.height == 0u
        || description.depth != 1u
        || description.arraySize != 1u
        || description.mipLevels != 1u
        || description.sampleCount != 1u
        || description.dimension != Core::TextureDimension::Texture2D
    ){
        markFailed(NWB_TEXT("swap-chain texture shape cannot be represented by the SDR framebuffer capture"));
        return false;
    }
    if(stagingMatches(description))
        return true;

    m_readback.reset();
    m_captureDescription = description;
    m_readback = getGraphics().getDevice().createStagingTexture(description, Core::CpuAccessMode::Read);
    if(!m_readback){
        markFailed(NWB_TEXT("failed to allocate the matching framebuffer readback staging texture"));
        return false;
    }
    return true;
}

bool FramebufferCapture::writeCapture(
    const u8* const sourceBytes,
    const usize sourceRowPitch,
    Core::Alloc::ScratchArena& scratchArena
){
    const u32 width = m_captureDescription.width;
    const u32 height = m_captureDescription.height;
    if(width > static_cast<u32>(Limit<i32>::s_Max) || height > static_cast<u32>(Limit<i32>::s_Max))
        return false;

    usize sourceRowBytes = 0u;
    usize bitmapRowBytes = 0u;
    if(
        !TryMultiply<usize>(static_cast<usize>(width), __hidden_framebuffer_capture::s_SourceBytesPerPixel, sourceRowBytes)
        || !TryMultiply<usize>(static_cast<usize>(width), __hidden_framebuffer_capture::s_BitmapBytesPerPixel, bitmapRowBytes)
        || sourceRowPitch < sourceRowBytes
        || AddOverflows<usize>(bitmapRowBytes, 3u)
    )
        return false;

    const usize bitmapRowPitch = (bitmapRowBytes + 3u) & ~usize(3u);
    usize bitmapPixelBytes = 0u;
    if(!TryMultiply<usize>(bitmapRowPitch, static_cast<usize>(height), bitmapPixelBytes))
        return false;
    if(AddOverflows<usize>(__hidden_framebuffer_capture::s_BitmapPixelOffset, bitmapPixelBytes))
        return false;

    const usize bitmapFileBytes = __hidden_framebuffer_capture::s_BitmapPixelOffset + bitmapPixelBytes;
    if(bitmapPixelBytes > static_cast<usize>(Limit<u32>::s_Max) || bitmapFileBytes > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    Vector<u8, Core::Alloc::ScratchArena> bitmapBytes(bitmapFileBytes, 0u, scratchArena);
    bitmapBytes[0] = static_cast<u8>('B');
    bitmapBytes[1] = static_cast<u8>('M');
    __hidden_framebuffer_capture::WriteU32LE(bitmapBytes.data() + 2u, static_cast<u32>(bitmapFileBytes));
    __hidden_framebuffer_capture::WriteU32LE(
        bitmapBytes.data() + 10u,
        static_cast<u32>(__hidden_framebuffer_capture::s_BitmapPixelOffset)
    );
    __hidden_framebuffer_capture::WriteU32LE(
        bitmapBytes.data() + __hidden_framebuffer_capture::s_BitmapFileHeaderSize,
        static_cast<u32>(__hidden_framebuffer_capture::s_BitmapInfoHeaderSize)
    );
    __hidden_framebuffer_capture::WriteU32LE(bitmapBytes.data() + 18u, width);
    __hidden_framebuffer_capture::WriteU32LE(bitmapBytes.data() + 22u, height);
    __hidden_framebuffer_capture::WriteU16LE(bitmapBytes.data() + 26u, 1u);
    __hidden_framebuffer_capture::WriteU16LE(bitmapBytes.data() + 28u, 24u);
    __hidden_framebuffer_capture::WriteU32LE(bitmapBytes.data() + 34u, static_cast<u32>(bitmapPixelBytes));

    const bool sourceIsBgra = __hidden_framebuffer_capture::IsBgraFormat(m_captureDescription.format);
    for(u32 bitmapY = 0u; bitmapY < height; ++bitmapY){
        const usize sourceY = static_cast<usize>(height - 1u - bitmapY);
        usize sourceRowOffset = 0u;
        if(!TryMultiply<usize>(sourceY, sourceRowPitch, sourceRowOffset))
            return false;
        const u8* const sourceRow = sourceBytes + sourceRowOffset;
        u8* const bitmapRow = bitmapBytes.data()
            + __hidden_framebuffer_capture::s_BitmapPixelOffset
            + static_cast<usize>(bitmapY) * bitmapRowPitch
        ;
        for(u32 x = 0u; x < width; ++x){
            const u8* const sourcePixel = sourceRow + static_cast<usize>(x) * __hidden_framebuffer_capture::s_SourceBytesPerPixel;
            u8* const bitmapPixel = bitmapRow + static_cast<usize>(x) * __hidden_framebuffer_capture::s_BitmapBytesPerPixel;
            bitmapPixel[0] = sourceIsBgra ? sourcePixel[0] : sourcePixel[2];
            bitmapPixel[1] = sourcePixel[1];
            bitmapPixel[2] = sourceIsBgra ? sourcePixel[2] : sourcePixel[0];
        }
    }

    const ::Path<Core::Alloc::GlobalArena> outputDirectory = m_outputPath.parent_path();
    ErrorCode filesystemError;
    if(!outputDirectory.empty() && !EnsureDirectories(outputDirectory, filesystemError))
        return false;

    ::Path<Core::Alloc::GlobalArena> partialPath(m_outputPath);
    partialPath += NWB_TEXT(".partial");
    if(!WriteBinaryFile(partialPath, bitmapBytes)){
        ErrorCode cleanupError;
        const bool removed = RemoveFile(partialPath, cleanupError);
        if(!removed && cleanupError)
            NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an incomplete partial capture"));
        return false;
    }
    if(!RenamePath(partialPath, m_outputPath, filesystemError)){
        ErrorCode cleanupError;
        const bool removed = RemoveFile(partialPath, cleanupError);
        if(!removed && cleanupError)
            NWB_LOGGER_WARNING(NWB_TEXT("FramebufferCapture: failed to remove an uncommitted partial capture"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

