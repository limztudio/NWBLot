// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.h"

#include <core/alloc/scratch.h>


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
        CookVector<AString> values;

        explicit DefineEntry(CookArena& memoryArena)
            : values(CookAllocator<AString>(memoryArena))
        {}
        explicit DefineEntry(CookVector<AString>&& inValues)
            : values(Move(inValues))
        {}
    };


public:
    struct IncludeEntry{
        AString source;
        AString defaultVariant;
        CookMap<AString, DefineEntry> defineValues;

        explicit IncludeEntry(CookArena& memoryArena)
            : defineValues(CookAllocator<Pair<const AString, DefineEntry>>(memoryArena))
        {}
    };

    struct ShaderEntry{
        AString name;
        CompactString compiler = CompactString("glslang");
        CompactString stage;
        CompactString archiveStage;
        CompactString targetProfile;
        AString entryPoint = "main";
        AString source;
        AString defaultVariant;

        CookVector<AString> includeRoots;
        CookMap<AString, DefineEntry> defineValues;
        CookMap<AString, AString> implicitDefines;

        explicit ShaderEntry(CookArena& memoryArena)
            : includeRoots(CookAllocator<AString>(memoryArena))
            , defineValues(CookAllocator<Pair<const AString, DefineEntry>>(memoryArena))
            , implicitDefines(CookAllocator<Pair<const AString, AString>>(memoryArena))
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

    bool validateDefaultVariant(AStringView contextLabel, AStringView defaultVariant, const CookMap<AString, DefineEntry>& defineValues);

    void mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<AString, IncludeEntry>& includeMetadata);

    bool gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies);

    bool expandDefineCombinations(const CookMap<AString, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations);

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
    using ScratchDefineEntryVector = Vector<
        DefineEntryPtr<MapT>,
        Alloc::ScratchAllocator<DefineEntryPtr<MapT>>
    >;

    template<typename MapT>
    ScratchDefineEntryVector<MapT> sortedDefineEntries(const MapT& map, Alloc::ScratchArena<>& scratchArena){
        using EntryPtr = DefineEntryPtr<MapT>;
        ScratchDefineEntryVector<MapT> entries{Alloc::ScratchAllocator<EntryPtr>(scratchArena)};
        entries.resize(map.size());
        usize entryIndex = 0u;
        for(const auto& [name, value] : map)
            entries[entryIndex++] = EntryPtr{ &name, &value };
        Sort(entries.begin(), entries.end(), [](const EntryPtr& lhs, const EntryPtr& rhs){ return *lhs.key < *rhs.key; });
        return entries;
    }


private:
    CookArena& m_memoryArena;

private:
    CustomUniquePtr<IShaderCompiler> m_compiler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

