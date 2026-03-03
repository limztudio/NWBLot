// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset_registry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetLoadState{
enum Enum : u8{
    Invalid = 0,
    Pending,
    InFlight,
    Completed
};
};


class IAssetBinarySource{
public:
    virtual ~IAssetBinarySource() = default;


public:
    virtual bool readAssetBinary(AStringView virtualPath, AssetBytes& outBinary, AString& outError)const = 0;
};

class IAssetAsyncExecutor{
public:
    virtual ~IAssetAsyncExecutor() = default;


public:
    virtual void enqueue(Function<void()>&& job) = 0;
};


struct AssetLoadResult{
    u64 requestId = 0;
    AssetLoadState::Enum state = AssetLoadState::Invalid;
    bool success = false;
    UniquePtr<IAsset> asset;
    AString error;
};


class AssetManager final : NoCopy{
private:
    struct RequestRecord{
        u64 requestId = 0;
        AssetLoadState::Enum state = AssetLoadState::Invalid;
        AString assetType;
        AString virtualPath;
        bool success = false;
        UniquePtr<IAsset> asset;
        AString error;
    };

    using RequestMap = HashMap<u64, RequestRecord>;


public:
    explicit AssetManager(const AssetRegistry& registry, const IAssetBinarySource& binarySource);


public:
    void setAsyncExecutor(IAssetAsyncExecutor* asyncExecutor);

    bool loadSync(
        AStringView assetType,
        AStringView virtualPath,
        UniquePtr<IAsset>& outAsset,
        AString& outError
    )const;

    [[nodiscard]] u64 enqueueLoad(AStringView assetType, AStringView virtualPath);
    void processPending();
    bool tryPopResult(u64 requestId, AssetLoadResult& outResult);

    void clear();

    [[nodiscard]] u64 pendingRequestCount()const;
    [[nodiscard]] u64 completedRequestCount()const;


private:
    void dispatchAsync(u64 requestId);
    void processRequest(u64 requestId);


private:
    const AssetRegistry& m_registry;
    const IAssetBinarySource& m_binarySource;
    IAssetAsyncExecutor* m_asyncExecutor = nullptr;

    mutable Futex m_mutex;
    RequestMap m_requests;
    Atomic<u64> m_nextRequestId{ 1 };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

