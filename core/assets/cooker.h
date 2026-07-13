
#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetCookServices{
    Alloc::ThreadPool& threadPool;

    explicit AssetCookServices(Alloc::ThreadPool& threadPool)
        : threadPool(threadPool)
    {}
};

struct AssetCookRoot{
    AssetString path;
    ACompactString virtualRoot;

    explicit AssetCookRoot(AssetArena& arena)
        : path(arena)
    {}

    AssetCookRoot(AssetArena& arena, AStringView inPath, const ACompactString& inVirtualRoot)
        : path(inPath, arena)
        , virtualRoot(inVirtualRoot)
    {}
};

struct AssetCookOptions{
    AssetString repoRoot;
    AssetVector<AssetCookRoot> assetRoots;
    AssetString outputDirectory;
    AssetString cacheDirectory;
    ACompactString configuration;
    ACompactString assetType;
    AssetCookServices services;

    explicit AssetCookOptions(AssetArena& arena, Alloc::ThreadPool& threadPool)
        : repoRoot(arena)
        , assetRoots(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
        , services(threadPool)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IAssetCooker{
public:
    IAssetCooker(AssetArena& arena, const ACompactString& assetTypeText)
        : m_arena(arena)
        , m_assetTypeText(assetTypeText)
        , m_assetType(assetTypeText.empty() ? NAME_NONE : Name(assetTypeText.view()))
    {}
    virtual ~IAssetCooker() = default;


public:
    [[nodiscard]] const ACompactString& assetTypeText()const{ return m_assetTypeText; }
    [[nodiscard]] const Name& assetType()const{ return m_assetType; }

    virtual bool cook(const AssetCookOptions& options) = 0;


protected:
    AssetArena& m_arena;


private:
    ACompactString m_assetTypeText;
    Name m_assetType = NAME_NONE;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetCookerRegistry final{
private:
    using CookerMap = HashMap<Name, UniquePtr<IAssetCooker>, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    explicit AssetCookerRegistry(AssetArena& arena);

    bool registerCooker(UniquePtr<IAssetCooker>&& cooker);

    bool cook(const AssetCookOptions& options)const;


private:
    AssetArena& m_arena;
    CookerMap m_assetCookers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

