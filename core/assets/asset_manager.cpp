// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_manager.h"

#include <core/alloc/scratch.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetManager::AssetManager(const AssetRegistry& registry, const IAssetBinarySource& binarySource)
    : m_registry(registry)
    , m_binarySource(binarySource)
{}


void AssetManager::setAsyncExecutor(IAssetAsyncExecutor* asyncExecutor){
    ScopedLock lock(m_mutex);
    m_asyncExecutor = asyncExecutor;
}


bool AssetManager::loadSync(
    const Name& assetType,
    const Name& virtualPath,
    UniquePtr<IAsset>& outAsset
)const{
    outAsset.reset();

    if(!assetType){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: asset type is empty"));
        return false;
    }
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: virtual path is empty"));
        return false;
    }

    AssetBytes binary;
    if(!m_binarySource.readAssetBinary(virtualPath, binary)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("AssetManager: failed to read binary for asset '{}' of type '{}'"),
            StringConvert(virtualPath.c_str()),
            StringConvert(assetType.c_str())
        );
        return false;
    }

    if(!m_registry.deserializeAsset(assetType, virtualPath, binary, outAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("AssetManager: failed to deserialize asset '{}' of type '{}'"),
            StringConvert(virtualPath.c_str()),
            StringConvert(assetType.c_str())
        );
        return false;
    }

    return true;
}


u64 AssetManager::enqueueLoad(const Name& assetType, const Name& virtualPath){
    if(!assetType || !virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: rejected async load request with empty asset type or virtual path"));
        return 0;
    }

    const u64 requestId = allocateRequestId();
    if(requestId == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetManager: async load request ids are exhausted"));
        return 0;
    }

    IAssetAsyncExecutor* asyncExecutor = nullptr;
    {
        ScopedLock lock(m_mutex);

        RequestRecord request;
        request.result.requestId = requestId;
        request.result.state = AssetLoadState::Pending;
        request.assetType = assetType;
        request.virtualPath = virtualPath;
        m_requests[requestId] = Move(request);

        asyncExecutor = m_asyncExecutor;
    }

    if(asyncExecutor != nullptr)
        dispatchAsync(requestId);

    return requestId;
}


void AssetManager::processPending(){
    Alloc::ScratchArena<> scratchArena;
    Vector<u64, Alloc::ScratchAllocator<u64>> pendingRequestIds{Alloc::ScratchAllocator<u64>(scratchArena)};
    {
        ScopedLock lock(m_mutex);
        pendingRequestIds.reserve(m_requests.size());
        for(const auto& [requestId, request] : m_requests){
            if(request.result.state == AssetLoadState::Pending)
                pendingRequestIds.push_back(requestId);
        }
    }

    for(const u64 requestId : pendingRequestIds)
        processRequest(requestId);
}


bool AssetManager::tryPopResult(const u64 requestId, AssetLoadResult& outResult){
    outResult = {};

    ScopedLock lock(m_mutex);

    auto found = m_requests.find(requestId);
    if(found == m_requests.end())
        return false;

    RequestRecord& request = found.value();
    if(request.result.state != AssetLoadState::Completed)
        return false;

    outResult = Move(request.result);

    m_requests.erase(found);
    return true;
}


void AssetManager::clear(){
    ScopedLock lock(m_mutex);
    m_requests.clear();
}


u64 AssetManager::pendingRequestCount()const{
    ScopedLock lock(m_mutex);

    u64 pendingCount = 0;
    for(const auto& [_, request] : m_requests){
        if(request.result.state == AssetLoadState::Pending || request.result.state == AssetLoadState::InFlight)
            ++pendingCount;
    }

    return pendingCount;
}


u64 AssetManager::completedRequestCount()const{
    ScopedLock lock(m_mutex);

    u64 completedCount = 0;
    for(const auto& [_, request] : m_requests){
        if(request.result.state == AssetLoadState::Completed)
            ++completedCount;
    }

    return completedCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 AssetManager::allocateRequestId(){
    u64 requestId = m_nextRequestId.load(std::memory_order_relaxed);
    for(;;){
        if(requestId == 0)
            return 0;

        const u64 nextRequestId = requestId + 1;
        if(m_nextRequestId.compare_exchange_weak(requestId, nextRequestId, std::memory_order_relaxed, std::memory_order_relaxed))
            return requestId;
    }
}


void AssetManager::dispatchAsync(const u64 requestId){
    IAssetAsyncExecutor* asyncExecutor = nullptr;
    {
        ScopedLock lock(m_mutex);
        asyncExecutor = m_asyncExecutor;
    }
    if(asyncExecutor == nullptr)
        return;

    const NotNull<IAssetAsyncExecutor*> requiredExecutor(asyncExecutor);
    requiredExecutor->enqueue([this, requestId](){
        processRequest(requestId);
    });
}


void AssetManager::processRequest(const u64 requestId){
    Name assetType = NAME_NONE;
    Name virtualPath = NAME_NONE;
    {
        ScopedLock lock(m_mutex);

        auto found = m_requests.find(requestId);
        if(found == m_requests.end())
            return;

        RequestRecord& request = found.value();
        if(request.result.state != AssetLoadState::Pending)
            return;

        request.result.state = AssetLoadState::InFlight;
        assetType = request.assetType;
        virtualPath = request.virtualPath;
    }

    UniquePtr<IAsset> loadedAsset;
    const bool success = loadSync(assetType, virtualPath, loadedAsset);

    {
        ScopedLock lock(m_mutex);

        auto found = m_requests.find(requestId);
        if(found == m_requests.end())
            return;

        RequestRecord& request = found.value();
        request.result.state = AssetLoadState::Completed;
        request.result.success = success;
        request.result.asset = Move(loadedAsset);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
