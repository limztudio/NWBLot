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


namespace Metascript{
class Document;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderCook{
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
    struct IncludeEntry{
        AString source;
        AString defaultVariant;
        CookMap<Name, DefineEntry> defineValues;

        explicit IncludeEntry(CookArena& memoryArena)
            : defineValues(CookAllocator<Pair<const Name, DefineEntry>>(memoryArena))
        {}
    };

    struct ShaderEntry{
        AString name;
        AString compiler = "glslang";
        AString stage;
        AString targetProfile;
        AString entryPoint = "main";
        AString source;
        AString defaultVariant;

        CookVector<AString> includeRoots;
        CookMap<Name, DefineEntry> defineValues;

        explicit ShaderEntry(CookArena& memoryArena)
            : includeRoots(CookAllocator<AString>(memoryArena))
            , defineValues(CookAllocator<Pair<const Name, DefineEntry>>(memoryArena))
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
    inline bool compileVariant(const ShaderCompilerRequest& request, Vector<u8>& outBytecode){ return m_compiler->compileVariant(request, outBytecode); }

public:
    bool parseDocument(const Path& nwbFilePath, Metascript::Document& outDoc);
    bool parseShaderMeta(const Path& nwbFilePath, const Metascript::Document& doc, ShaderEntry& outEntry);
    bool parseShaderMeta(const Path& nwbFilePath, ShaderEntry& outEntry);
    bool parseIncludeMeta(const Path& nwbFilePath, const Metascript::Document& doc, IncludeEntry& outEntry);
    bool parseIncludeMeta(const Path& nwbFilePath, IncludeEntry& outEntry);

    bool validateDefaultVariant(AStringView contextLabel, AStringView defaultVariant, const CookMap<Name, DefineEntry>& defineValues);

    void mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<AString, IncludeEntry>& includeMetadata);

    bool gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies);

    void expandDefineCombinations(const CookMap<Name, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations);

    AString buildVariantName(const DefineCombo& combo);
    bool canonicalizeVariantSignature(AStringView variantSignature, AString& outCanonical);

    bool computeDependencyChecksum(const CookVector<Path>& dependencies, u64& outChecksum);
    bool computeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, u64 dependencyChecksum, u64& outChecksum);


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

