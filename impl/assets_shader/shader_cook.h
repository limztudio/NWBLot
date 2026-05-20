// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../global.h"

#include <core/alloc/scratch.h>
#include <core/graphics/common.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderCook{
public:
    using CookArena = Core::Alloc::GlobalArena;
    using CookString = AString<CookArena>;

public:
    template<typename T>
    using CookVector = Vector<T, CookArena>;

    template<typename T, typename V>
    using CookMap = HashMap<T, V, Hasher<T>, EqualTo<T>, CookArena>;

    template<typename T>
    using CookHashSet = HashSet<T, Hasher<T>, EqualTo<T>, CookArena>;

    using DefineCombo = CookMap<CookString, CookString>;

    struct DefineEntry{
        CookVector<CookString> values;

        explicit DefineEntry(CookArena& memoryArena)
            : values(memoryArena)
        {}
        explicit DefineEntry(CookVector<CookString>&& inValues)
            : values(Move(inValues))
        {}
    };


public:
    struct IncludeEntry{
        CookString source;
        CookString defaultVariant;
        CookMap<CookString, DefineEntry> defineValues;

        explicit IncludeEntry(CookArena& memoryArena)
            : source(memoryArena)
            , defaultVariant(memoryArena)
            , defineValues(0, Hasher<CookString>(), EqualTo<CookString>(), memoryArena)
        {}
    };

    struct ShaderEntry{
        CookString name;
        CompactString compiler = CompactString("glslang");
        CompactString stage;
        CompactString archiveStage;
        CompactString targetProfile;
        CookString entryPoint;
        CookString source;
        CookString defaultVariant;

        CookVector<CookString> includeRoots;
        CookMap<CookString, DefineEntry> defineValues;
        CookMap<CookString, CookString> implicitDefines;

        explicit ShaderEntry(CookArena& memoryArena)
            : name(memoryArena)
            , entryPoint("main", memoryArena)
            , source(memoryArena)
            , defaultVariant(memoryArena)
            , includeRoots(memoryArena)
            , defineValues(0, Hasher<CookString>(), EqualTo<CookString>(), memoryArena)
            , implicitDefines(0, Hasher<CookString>(), EqualTo<CookString>(), memoryArena)
        {}
    };


private:
    struct SortedDependencyItem{
        CookString canonicalPath;
        Path path;

        explicit SortedDependencyItem(CookArena& arena)
            : canonicalPath(arena)
        {}
    };


public:
    ShaderCook(CookArena& memoryArena, Core::ShaderCompilerFactory compilerFactory = nullptr);


public:
    inline bool compileVariant(const Core::ShaderCompilerRequest& request, Core::GraphicsBytes& outBytecode){ return m_compiler->compileVariant(request, outBytecode); }

public:
    bool parseDocument(const Path& nwbFilePath, Core::Metascript::Document& outDoc);
    bool parseShaderMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, ShaderEntry& outEntry);
    bool parseShaderMeta(const Path& nwbFilePath, ShaderEntry& outEntry);
    bool parseIncludeMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, IncludeEntry& outEntry);
    bool parseIncludeMeta(const Path& nwbFilePath, IncludeEntry& outEntry);

    bool validateDefaultVariant(AStringView contextLabel, AStringView defaultVariant, const CookMap<CookString, DefineEntry>& defineValues);

    void mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata);

    bool gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies);

    bool expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations);

    CookString buildVariantName(const DefineCombo& combo);
    bool canonicalizeVariantSignature(AStringView variantSignature, CookString& outCanonical);

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
        Core::Alloc::ScratchArena<>
    >;

    template<typename MapT>
    ScratchDefineEntryVector<MapT> sortedDefineEntries(const MapT& map, Core::Alloc::ScratchArena<>& scratchArena){
        using EntryPtr = DefineEntryPtr<MapT>;
        ScratchDefineEntryVector<MapT> entries{scratchArena};
        entries.reserve(map.size());
        for(const auto& [name, value] : map)
            entries.push_back(EntryPtr{ &name, &value });
        Sort(entries.begin(), entries.end(), [](const EntryPtr& lhs, const EntryPtr& rhs){ return *lhs.key < *rhs.key; });
        return entries;
    }


private:
    CookArena& m_memoryArena;

private:
    Core::GlobalUniquePtr<Core::IShaderCompiler> m_compiler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

