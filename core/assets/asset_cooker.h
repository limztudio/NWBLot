// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetCookOptions{
    AString repoRoot;
    Vector<AString> assetRoots;
    AString outputDirectory;
    AString cacheDirectory;
    CompactString configuration;
    CompactString assetType;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IAssetCooker{
public:
    IAssetCooker(Alloc::CustomArena& arena, const CompactString& assetTypeText)
        : m_arena(arena)
        , m_assetTypeText(assetTypeText)
        , m_assetType(assetTypeText.empty() ? NAME_NONE : Name(assetTypeText.view()))
    {}
    virtual ~IAssetCooker() = default;


public:
    [[nodiscard]] const CompactString& assetTypeText()const{ return m_assetTypeText; }
    [[nodiscard]] const Name& assetType()const{ return m_assetType; }

    virtual bool cook(const AssetCookOptions& options) = 0;


protected:
    Alloc::CustomArena& m_arena;


private:
    CompactString m_assetTypeText;
    Name m_assetType = NAME_NONE;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetCookerRegistry final{
private:
    using CookerMapAllocator = Alloc::CustomAllocator<Pair<const Name, UniquePtr<IAssetCooker>>>;
    using CookerMap = HashMap<Name, UniquePtr<IAssetCooker>, Hasher<Name>, EqualTo<Name>, CookerMapAllocator>;


public:
    explicit AssetCookerRegistry(Alloc::CustomArena& arena);

    bool registerCooker(UniquePtr<IAssetCooker>&& cooker);

    bool cook(const AssetCookOptions& options)const;


private:
    CookerMap m_assetCookers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

