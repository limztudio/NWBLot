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
    static constexpr u8 s_AssetLoadStateInvalidBase = 0;
    enum Enum : u8{
        Invalid = s_AssetLoadStateInvalidBase,
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
    UniquePtr<IAsset> asset;
    AssetLoadState::Enum state = AssetLoadState::Invalid;
    bool success = false;
};


class AssetManager final : NoCopy{
private:
    struct RequestRecord{
        AssetLoadResult result;
        Name assetType = s_NameNone;
        Name virtualPath = s_NameNone;
    };

    using RequestMap = HashMap<u64, RequestRecord, Alloc::GlobalArena, Hasher<u64>, EqualTo<u64>>;


public:
    explicit AssetManager(AssetArena& arena, const AssetRegistry& registry, const IAssetBinarySource& binarySource);


public:
    void setAsyncExecutor(IAssetAsyncExecutor* asyncExecutor);

    [[nodiscard]] Expected<UniquePtr<IAsset>> loadSync(const Name& assetType, const Name& virtualPath)const;

    [[nodiscard]] u64 enqueueLoad(const Name& assetType, const Name& virtualPath);
    void processPending();
    [[nodiscard]] Expected<AssetLoadResult> tryPopResult(u64 requestId);

    void clear();

    [[nodiscard]] u64 pendingRequestCount()const;
    [[nodiscard]] u64 completedRequestCount()const;


public:
    template<typename TAsset, typename TResource>
    [[nodiscard]] static bool CheckLoaderEnter(
        const AssetRef<TAsset>& assetRef,
        const TResource& resource,
        const TStringView owner,
        const AStringView assetKindText
    ){
        if(!assetRef.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: {} asset reference is empty"), owner, StringConvert(assetKindText));
            return false;
        }
        return !resource.valid();
    }

    template<typename AssetT>
    [[nodiscard]] Expected<UniquePtr<AssetT>> loadTypedSync(
        const Name& virtualPath,
        const TStringView ownerName,
        const AStringView assetKindText
    )const{
        auto loadedAsset = loadSync(AssetT::AssetTypeName(), virtualPath);
        if(!loadedAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load {} asset '{}'")
                , ownerName
                , StringConvert(assetKindText)
                , StringConvert(virtualPath.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }
        const AssetT* typedAsset = CastAsset<AssetT>(loadedAsset->get());
        if(!typedAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: asset '{}' is not a {}")
                , ownerName
                , StringConvert(virtualPath.resolvedText())
                , StringConvert(assetKindText)
            );
            return MakeUnexpected(Failure{});
        }
        return UniquePtr<AssetT>(checked_cast<AssetT*>(loadedAsset->release()));
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

