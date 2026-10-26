// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/alloc/scratch.h>
#include <core/metascript/parser.h>

#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookArena = Core::Alloc::GlobalArena;
template<typename T, typename V>
using CookDependencyMap = HashMap<T, V, CookArena, Hasher<T>, EqualTo<T>>;
template<typename T>
using CookDependencySet = HashSet<T, CookArena, Hasher<T>, EqualTo<T>>;
using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CookDependencyItemContext{
    const Path& assetRoot;
    AStringView virtualRoot;
    const Path& nwbFilePath;
    Name assetType = s_NameNone;
    Name virtualPath = s_NameNone;
    const Core::Metascript::Value& asset;
    const Core::Metascript::Document* document = nullptr;
    AssetArena& arena;
    ScratchArena& scratchArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ICookDependencyCollector : NoCopy{
public:
    virtual ~ICookDependencyCollector()noexcept = default;


public:
    [[nodiscard]] virtual bool addAssetDependency(const Name& virtualPath, const Name& expectedAssetType = s_NameNone) = 0;
    [[nodiscard]] virtual bool addSubsystemDependency(const Name& subsystemVirtualPath) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookDependencyExtractFunction = bool (*)(const CookDependencyItemContext& context, ICookDependencyCollector& collector);


class CookDependencyRegistry : NoCopy{
private:
    using ExtractorLookup = CookDependencyMap<Name, NotNull<CookDependencyExtractFunction>>;
    using SubsystemTypeSet = CookDependencySet<Name>;


public:
    explicit CookDependencyRegistry(CookArena& arena)noexcept
        : m_lookup(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , m_subsystemTypes(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    bool registerType(const Name& assetType, CookDependencyExtractFunction extractDependencies);
    bool registerSubsystemType(const Name& assetType, CookDependencyExtractFunction extractDependencies);

    [[nodiscard]] CookDependencyExtractFunction find(const Name& assetType)const noexcept;
    [[nodiscard]] bool has(const Name& assetType)const noexcept{ return find(assetType) != nullptr; }
    [[nodiscard]] bool isSubsystemType(const Name& assetType)const noexcept;
    [[nodiscard]] bool extractDependencies(const CookDependencyItemContext& context, ICookDependencyCollector& collector)const;


private:
    ExtractorLookup m_lookup;
    SubsystemTypeSet m_subsystemTypes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookDependencyRegistrationFunction = bool (*)(CookDependencyRegistry& registry);

class CookDependencyAutoRegistrar final{
public:
    explicit CookDependencyAutoRegistrar(CookDependencyRegistrationFunction function);
};

#define NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(registrarVariable, registerFunction) \
    Core::Assets::CookDependencyAutoRegistrar registrarVariable(&registerFunction)

[[nodiscard]] bool RegisterAutoCollectedCookDependencyTypes(CookDependencyRegistry& registry, ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

