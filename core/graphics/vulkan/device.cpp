// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "physical_queue_registrar.h"
#include "amd_breadcrumb_ring.h"
#include "arena_names.h"
#include "vulkan_device_feature_setup.h"
#include "device_detail.h"
#include "physical_queue_registration_policy.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>

#include <global/exception.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_device{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Timeline values live one logical-device lifetime. Indices come from the Device registry; generation rejects old tokens after recreate.
static VulkanDetail::DeviceGenerationAllocator s_DeviceGenerationAllocator;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u16 AllocateDeviceGeneration()noexcept{
    const auto generation = s_DeviceGenerationAllocator.allocate();
    if(!generation)
        FOUNDATION_FATAL_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Device-generation identity space is exhausted."));
    return *generation;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device::Device(const DeviceDesc& desc)
    : RefCounter<GraphicsResource>(desc.cpuScheduler)
    , m_submissionOperationState(0u)
    , m_nextTimerQueryIncarnation(0u)
    , m_deviceGeneration(__hidden_vulkan_device::AllocateDeviceGeneration())
    , m_gpuCrashDiagnostics(desc.allocator.getObjectArena(), desc.gpuCrashDiagnosticsEnabled)
    , m_context(desc.allocator, desc.cpuScheduler, desc.instance, desc.physicalDevice, desc.device, desc.getInstanceProcAddr, desc.instanceDispatch, desc.deviceDispatch, desc.allocationCallbacks, m_deviceGeneration)
    , m_allocator(m_context)
    , m_descriptorBufferManager(*this, m_context, m_allocator)
    , m_gpuDescriptorHeap(*this)
    , m_pipelineCacheDirectory(m_context.objectArena, desc.pipelineCacheDirectory)
    , m_filesystemFactory(desc.filesystemFactory)
    , m_nativeQueueStates(m_context.objectArena)
    , m_physicalQueues(m_context.objectArena)
    , m_physicalQueueInfos(m_context.objectArena)
    , m_uploadManager(*this, s_DefaultUploadChunkSize, 0, UploadManager::ChunkKind::Upload)
    , m_scratchManager(*this, s_DefaultScratchChunkSize, s_ScratchMemoryLimit, UploadManager::ChunkKind::Scratch)
    , m_accelStructInputManager(*this, s_DefaultAccelStructInputChunkSize, s_AccelStructInputMemoryLimit, UploadManager::ChunkKind::AccelStructBuildInput)
{
    // A constructor failure cannot invoke Device::~Device; no work has been published to callers yet.
    const i32 initialExceptionCount = UncaughtExceptionCount();
    ScopeExit releaseUnpublishedResources([this, initialExceptionCount]()noexcept{
        if(UncaughtExceptionCount() > initialExceptionCount)
            releaseOwnedResources();
    });

    VkResult res = VK_SUCCESS;

    m_context.descriptorBufferManager = &m_descriptorBufferManager;
    if(!desc.nativeQueues || desc.nativeQueueCount == 0u){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Device creation requires a non-empty canonical native queue registry."));
        return;
    }
    if(m_context.physicalDevice == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Canonical native queue validation requires a physical device."));
        return;
    }
    Alloc::ScratchArena queueFamilyQueryArena(VulkanArenaScope::s_QueueFamilyQueryArena);
    u32 physicalQueueFamilyCount = 0u;
    m_context.instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(m_context.physicalDevice, &physicalQueueFamilyCount, nullptr);
    if(physicalQueueFamilyCount == 0u){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Physical device exposes no queue families."));
        return;
    }
    Vector<VkQueueFamilyProperties, Alloc::ScratchArena> physicalQueueFamilies(physicalQueueFamilyCount, queueFamilyQueryArena);
    m_context.instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(m_context.physicalDevice, &physicalQueueFamilyCount, physicalQueueFamilies.data());
    if(physicalQueueFamilyCount == 0u || physicalQueueFamilyCount > physicalQueueFamilies.size()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Physical-device queue-family enumeration returned an invalid count."));
        return;
    }
    const auto canonicalQueueValidation = PhysicalQueueRegistrationPolicy::ValidateCanonical(
        { desc.nativeQueues, desc.nativeQueueCount },
        { physicalQueueFamilies.data(), physicalQueueFamilyCount }
    );
    if(canonicalQueueValidation == PhysicalQueueRegistrationPolicy::Result::Invalid){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Refusing invalid canonical native queue registry entry."));
        return;
    }
    if(canonicalQueueValidation == PhysicalQueueRegistrationPolicy::Result::DuplicateQueue){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Refusing conflicting canonical native queue registry identity."));
        return;
    }
    m_nativeQueueStates.reserve(desc.nativeQueueCount);
    for(usize nativeQueueIndex = 0u; nativeQueueIndex < desc.nativeQueueCount; ++nativeQueueIndex){
        const VulkanNativeQueueDesc& nativeQueue = desc.nativeQueues[nativeQueueIndex];
        NativeQueueState* const nativeQueueState = NewArenaObject<NativeQueueState>(m_context.objectArena, nativeQueue.queue, nativeQueue.familyIndex, nativeQueue.queueIndex);
        if(!nativeQueueState){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to allocate canonical native queue state."));
            return;
        }
        m_nativeQueueStates.push_back(MakeNotNull(nativeQueueState));
    }
    PhysicalQueueRegistrar queueRegistrar(*this);
    for(usize queueIndex = 0u; queueIndex < desc.physicalQueueCount; ++queueIndex){
        const VulkanPhysicalQueueDesc& physicalQueue = desc.physicalQueues[queueIndex];
        if(physicalQueue.nativeQueueIndex >= m_nativeQueueStates.size() || !queueRegistrar.registerQueue(physicalQueue, *m_nativeQueueStates[physicalQueue.nativeQueueIndex])){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to register a native physical queue."));
            return;
        }
    }
    for(u32 queueClassIndex = 0u; queueClassIndex < static_cast<u32>(CommandQueue::kCount); ++queueClassIndex){
        if(m_primaryQueues[queueClassIndex] && !m_explicitPrimaryQueues[queueClassIndex]){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Every registered physical queue class requires one explicit primary."));
            return;
        }
    }
    if(!m_explicitPrimaryQueues[static_cast<u32>(CommandQueue::Graphics)]){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Device creation requires a primary Graphics queue."));
        return;
    }
    queueRegistrar.configureSharing();
    m_queueRegistryReady = true;

    m_context.instanceDispatch.vkGetPhysicalDeviceProperties(m_context.physicalDevice, &m_context.physicalDeviceProperties);
    m_context.instanceDispatch.vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &m_context.memoryProperties);
    m_pipelineCacheVolumeName = VulkanDetail::s_PipelineCacheVolumeName;

    VulkanDeviceFeatureSetup::Initialize(m_context, desc, m_gpuCrashDiagnostics.enabled());

    // Resolve every ASTC and BC sampled-image path up front.
    // Texture loading consults this cache, so it can select ASTC, BC, or an uncompressed fallback before allocating data.
    probeCompressedTextureFormats();

    const VulkanDetail::DescriptorBufferStartupPrerequisites descriptorBufferPrerequisites = VulkanDetail::QueryDescriptorBufferStartupPrerequisites(m_context);
    const b8 descriptorBufferStartupReady = VulkanDetail::HasDescriptorBufferStartupPrerequisites(descriptorBufferPrerequisites);

    // Descriptor-buffer startup prerequisites are mandatory for this device.
    if(!descriptorBufferStartupReady){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Required descriptor-buffer startup prerequisites are unavailable."));
    }

    if(!m_allocator.initialize())
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to initialize VMA allocator"));

    if(m_gpuCrashDiagnostics.enabled() && m_context.extensions.amdBufferMarker && !m_amdBreadcrumb.initialize(m_allocator, getPhysicalQueueTopology()))
        m_context.extensions.amdBufferMarker = false;


    if(descriptorBufferStartupReady){
        if(!m_descriptorBufferManager.initialize()){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Required descriptor-buffer manager initialization failed."));
        }
    }

    if(VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        GpuDescriptorHeapDesc heapDesc;
        heapDesc.setBindlessHeapAbi(desc.bindlessHeapAbi);
        if(!m_gpuDescriptorHeap.initialize(heapDesc))
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Required global GpuDescriptorHeap initialization failed."));
    }
    else{
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Required global GpuDescriptorHeap is unavailable."));
    }

    const auto pipelineCacheInitialData = loadPipelineCacheData();
    if(!pipelineCacheInitialData)
        NWB_LOGGER_INFO(FOUNDATION_TEXT("Vulkan: No usable pipeline cache found; starting with an empty cache."));

    auto cacheInfo = VulkanDetail::MakeVkStruct<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    if(pipelineCacheInitialData && !pipelineCacheInitialData->empty()){
        cacheInfo.initialDataSize = pipelineCacheInitialData->size();
        cacheInfo.pInitialData = pipelineCacheInitialData->data();
    }
    res = m_context.deviceDispatch.vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    if(res != VK_SUCCESS && pipelineCacheInitialData && !pipelineCacheInitialData->empty()){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to create pipeline cache from runtime volume '{}'. Retrying empty cache. {}"), ConvertString(m_pipelineCacheVolumeName), ResultToString(res));
        cacheInfo.initialDataSize = 0;
        cacheInfo.pInitialData = nullptr;
        res = m_context.deviceDispatch.vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    }
    if(res != VK_SUCCESS){
        m_context.pipelineCache = VK_NULL_HANDLE;
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to create pipeline cache. {}"), ResultToString(res));
    }
}
Device::~Device()noexcept{
    const b8 lifecycleDestructionPrepared = m_lifecycleDestructionPrepared.load(MemoryOrder::acquire);
    const VkResult nativeIdleResult = lifecycleDestructionPrepared ? VK_SUCCESS : waitForNativeIdle();
    const b8 nativeTeardownSafe = nativeIdleResult == VK_SUCCESS || nativeIdleResult == VK_ERROR_DEVICE_LOST;
    FOUNDATION_FATAL_ASSERT_MSG(nativeTeardownSafe, "Vulkan Device destruction requires a completed native idle join or terminal device loss");
    if(nativeIdleResult == VK_ERROR_DEVICE_LOST)
        markDeviceLost();

    releaseOwnedResources();
}

void Device::releaseOwnedResources()noexcept{
    m_uploadManager.clear();
    m_scratchManager.clear();
    m_accelStructInputManager.clear();

    m_gpuDescriptorHeap.shutdownForDeviceTeardown();
    m_descriptorBufferManager.shutdownForDeviceTeardown();

    for(const auto queue : m_physicalQueues){

        DestroyArenaObjectNoexcept(m_context.objectArena, queue.get());
    }
    m_physicalQueues.clear();
    m_physicalQueueInfos.clear();

    for(const auto nativeQueueState : m_nativeQueueStates){

        DestroyArenaObjectNoexcept(m_context.objectArena, nativeQueueState.get());
    }
    m_nativeQueueStates.clear();

    m_amdBreadcrumb.release(m_allocator);

    if(m_context.pipelineCache){
        m_context.deviceDispatch.vkDestroyPipelineCache(m_context.device, m_context.pipelineCache, m_context.allocationCallbacks);
        m_context.pipelineCache = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

