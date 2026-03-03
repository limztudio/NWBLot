// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_manager.h"


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
    const AStringView assetType,
    const AStringView virtualPath,
    UniquePtr<IAsset>& outAsset,
    AString& outError
)const{
    outAsset.reset();
    outError.clear();

    const AString canonicalType = ::CanonicalizeText(assetType);
    if(canonicalType.empty()){
        outError = "AssetManager: asset type is empty";
        return false;
    }
    if(virtualPath.empty()){
        outError = "AssetManager: virtual path is empty";
        return false;
    }

    AssetBytes binary;
    if(!m_binarySource.readAssetBinary(virtualPath, binary, outError))
        return false;

    return m_registry.deserializeAsset(canonicalType, virtualPath, binary, outAsset, outError);
}


u64 AssetManager::enqueueLoad(const AStringView assetType, const AStringView virtualPath){
    const AString canonicalType = ::CanonicalizeText(assetType);
    if(canonicalType.empty() || virtualPath.empty())
        return 0;

    const u64 requestId = m_nextRequestId.fetch_add(1, MemoryOrder::memory_order_relaxed);
    if(requestId == 0)
        return 0;

    IAssetAsyncExecutor* asyncExecutor = nullptr;
    {
        ScopedLock lock(m_mutex);

        RequestRecord request;
        request.result.requestId = requestId;
        request.result.state = AssetLoadState::Pending;
        request.assetType = canonicalType;
        request.virtualPath = AString(virtualPath);
        m_requests[requestId] = Move(request);

        asyncExecutor = m_asyncExecutor;
    }

    if(asyncExecutor != nullptr)
        dispatchAsync(requestId);

    return requestId;
}


void AssetManager::processPending(){
    Vector<u64> pendingRequestIds;
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


void AssetManager::dispatchAsync(const u64 requestId){
    IAssetAsyncExecutor* asyncExecutor = nullptr;
    {
        ScopedLock lock(m_mutex);
        asyncExecutor = m_asyncExecutor;
    }
    if(asyncExecutor == nullptr)
        return;

    asyncExecutor->enqueue([this, requestId](){
        processRequest(requestId);
    });
}


void AssetManager::processRequest(const u64 requestId){
    AString assetType;
    AString virtualPath;
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
    AString loadError;
    const bool success = loadSync(assetType, virtualPath, loadedAsset, loadError);

    {
        ScopedLock lock(m_mutex);

        auto found = m_requests.find(requestId);
        if(found == m_requests.end())
            return;

        RequestRecord& request = found.value();
        request.result.state = AssetLoadState::Completed;
        request.result.success = success;
        request.result.asset = Move(loadedAsset);
        request.result.error = Move(loadError);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

