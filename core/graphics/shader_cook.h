// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderCook{
private:
    static constexpr u64 s_DefaultSegmentSize = 16ull * 1024ull * 1024ull;
    static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;


public:
    using CookArena = Core::Alloc::CustomArena;
    template<typename T>
    using CookAllocator = Core::Alloc::CustomAllocator<T>;

public:
    template<typename T>
    using CookVector = Vector<T, CookAllocator<T>>;

    template<typename T, typename V>
    using CookMap = HashMap<T, V, Hasher<T>, EqualTo<T>, CookAllocator<Pair<const T, V>>>;

    template<typename T>
    using CookHashSet = HashSet<T, Hasher<T>, EqualTo<T>, CookAllocator<T>>;

    using DefineCombo = CookMap<AString, AString>;

    struct DefineEntry{
        AString name;
        CookVector<AString> values;
    };


public:
    struct ManifestEntry{
        AString name;
        AString compiler = "glslang";
        AString stage;
        AString targetProfile;
        AString entryPoint = "main";
        AString source;
        AString defaultVariant;

        CookMap<Name, DefineEntry> defineValues;

        explicit ManifestEntry(CookArena& memoryArena)
            : defineValues(CookAllocator<Pair<const Name, DefineEntry>>(memoryArena))
        {}
    };

    struct ManifestData{
        AString volumeName = "graphics";
        u64 segmentSize = s_DefaultSegmentSize;
        u64 metadataSize = s_DefaultMetadataSize;

        CookVector<AString> includeRoots;
        CookVector<ManifestEntry> entries;

        explicit ManifestData(CookArena& memoryArena)
            : includeRoots(CookAllocator<AString>(memoryArena))
            , entries(CookAllocator<ManifestEntry>(memoryArena))
        {}
    };


private:
    struct SortedDependencyItem{
        AString canonicalPath;
        Path path;
    };


public:
    ShaderCook(CookArena& memoryArena);


public:
    inline bool operator!()const{ return !m_compiler; }

    
public:
    inline bool compileVariant(const ShaderCompilerRequest& request, Vector<u8>& outBytecode){ return m_compiler->compileVariant(request, outBytecode); }

public:
    bool parseManifestFile(const Path& manifestPath, ManifestData& outManifest);

    bool gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies);

    void expandDefineCombinations(const CookMap<Name, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations);

    AString buildVariantName(const DefineCombo& combo);

    bool computeSourceChecksum(const ManifestEntry& entry, const AStringView variantName, const CookVector<Path>& dependencies, u64& outChecksum);


private:
    template<typename MapT>
    struct DefineEntryPtr{
        const typename MapT::key_type* key;
        const typename MapT::mapped_type* value;
    };

    template<typename MapT>
    CookVector<DefineEntryPtr<MapT>> sortedDefineEntries(const MapT& map){
        using EntryPtr = DefineEntryPtr<MapT>;
        CookVector<EntryPtr> entries{CookAllocator<EntryPtr>(m_memoryArena)};
        entries.reserve(map.size());
        for(const auto& [name, value] : map)
            entries.push_back(EntryPtr{ &name, &value });
        Sort(entries.begin(), entries.end(), [](const EntryPtr& lhs, const EntryPtr& rhs){ return *lhs.key < *rhs.key; });
        return entries;
    }


private:
    CookArena& m_memoryArena;

private:
    UniquePtr<IShaderCompiler> m_compiler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

