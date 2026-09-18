// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "registry.h"
#include "ref.h"

#include <core/common/log.h>


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
    virtual bool readAssetBinary(const Name& virtualPath, AssetBytes& outBinary)const = 0;
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
};


class AssetManager final : NoCopy{
private:
    struct RequestRecord{
        AssetLoadResult result;
        Name assetType = NAME_NONE;
        Name virtualPath = NAME_NONE;
    };

    using RequestMap = HashMap<u64, RequestRecord, Hasher<u64>, EqualTo<u64>, Alloc::GlobalArena>;


public:
    explicit AssetManager(AssetArena& arena, const AssetRegistry& registry, const IAssetBinarySource& binarySource);


public:
    void setAsyncExecutor(IAssetAsyncExecutor* asyncExecutor);

    bool loadSync(const Name& assetType, const Name& virtualPath, UniquePtr<IAsset>& outAsset)const;

    [[nodiscard]] u64 enqueueLoad(const Name& assetType, const Name& virtualPath);
    void processPending();
    bool tryPopResult(u64 requestId, AssetLoadResult& outResult);

    void clear();

    [[nodiscard]] u64 pendingRequestCount()const;
    [[nodiscard]] u64 completedRequestCount()const;


public:
    // Shared Load() prologue for typed GPU-resource loaders: rejects empty refs and already-valid resources.
    template<typename TAsset, typename TResource>
    [[nodiscard]] static bool CheckLoaderEnter(
        const AssetRef<TAsset>& assetRef,
        const TResource& resource,
        const NotNull<const tchar*> owner,
        const NotNull<const char*> assetKindText
    ){
        if(!assetRef.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: {} asset reference is empty"), owner, assetKindText);
            return false;
        }
        return !resource.valid();
    }

    // Shared loadSync + asset-type check used by typed asset loaders. Returns the typed asset on success.
    template<typename AssetT>
    [[nodiscard]] const AssetT* loadTypedSync(
        const Name& virtualPath,
        UniquePtr<IAsset>& outLoadedAsset,
        const NotNull<const tchar*>& failureContext,
        const NotNull<const tchar*> ownerName,
        const NotNull<const char*> assetKindText
    )const{
        static_cast<void>(failureContext);
        if(!loadSync(AssetT::AssetTypeName(), virtualPath, outLoadedAsset)){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load {} asset '{}'")
                , ownerName
                , assetKindText
                , StringConvert(virtualPath.c_str())
            );
            return nullptr;
        }
        const AssetT* typedAsset = CastAsset<AssetT>(outLoadedAsset.get());
        if(!typedAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: asset '{}' is not a {}")
                , ownerName
                , StringConvert(virtualPath.c_str())
                , assetKindText
            );
            return nullptr;
        }
        return typedAsset;
    }


private:
    [[nodiscard]] u64 allocateRequestId();
    void dispatchAsync(u64 requestId, IAssetAsyncExecutor& asyncExecutor);
    void processRequest(u64 requestId);


private:
    const AssetRegistry& m_registry;
    const IAssetBinarySource& m_binarySource;
    IAssetAsyncExecutor* m_asyncExecutor = nullptr;

    mutable Futex m_mutex;
    AssetArena& m_arena;
    RequestMap m_requests;
    Atomic<u64> m_nextRequestId{ 1 };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

