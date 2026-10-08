// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/alloc/scratch.h>
#include <core/metascript/parser.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderOptimizationLevel{
    enum Enum : u8{
        None,
        Default,
        High,
        Maximal,

        kCount,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderCook : NoCopy{
public:
    using CookArena = Core::Alloc::GlobalArena;
    using CookString = AString<CookArena>;
    template<typename T>
    using CookVector = Vector<T, CookArena>;

    template<typename T, typename V>
    using CookMap = HashMap<T, V, CookArena, Hasher<T>, EqualTo<T>>;

    struct ShaderMacroDefinition{
        AStringView name;
        AStringView value;
    };

    struct ShaderCompilerRequest{
        AStringView shaderName;
        AStringView stage;
        AStringView entryPoint;
        AStringView variantName;
        const ShaderMacroDefinition* defines = nullptr;
        const CookVector<Path>& includeDirectories;
        const CookVector<Path>& dependencies;
        Span<const AStringView> externallyPlannedMacroIncludes;
        const Path& sourcePath;
        const Path& outputPath;
        u32 defineCount = 0;
        ShaderOptimizationLevel::Enum optimizationLevel = ShaderOptimizationLevel::Default;
        bool rayQuery = false;
        bool compilerInputsHaveBom = false;
    };

    struct DependencyRootAlias{
        Path root;
        AStringView key;

        DependencyRootAlias(const Path& rootPath, const AStringView rootKey)
            : root(rootPath)
            , key(rootKey)
        {}
    };

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


    struct IncludeEntry{
        CookString source;
        CookMap<CookString, DefineEntry> defineValues;

        explicit IncludeEntry(CookArena& memoryArena)
            : source(memoryArena)
            , defineValues(0, Hasher<CookString>(), EqualTo<CookString>(), memoryArena)
        {}
    };

    struct ShaderEntry{
        CookString name;
        ACompactString stage;
        ACompactString archiveStage;
        CookString entryPoint;
        CookString source;

        CookVector<CookString> includeRoots;
        CookMap<CookString, DefineEntry> defineValues;
        CookMap<CookString, CookString> implicitDefines;
        ShaderOptimizationLevel::Enum optimizationLevel = ShaderOptimizationLevel::Default;
        bool rayQuery = false;
        bool emitMeshComputeShadow = true;

        explicit ShaderEntry(CookArena& memoryArena)
            : name(memoryArena)
            , entryPoint("main", memoryArena)
            , source(memoryArena)
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
            , path(arena)
        {}
    };

    template<typename MapT>
    struct DefineEntryPtr{
        const typename MapT::key_type* key;
        const typename MapT::mapped_type* value;
    };
    template<typename MapT>
    using ScratchDefineEntryVector = Vector<
        DefineEntryPtr<MapT>,
        Core::Alloc::ScratchArena
    >;


public:
    explicit ShaderCook(CookArena& memoryArena)noexcept
        : m_memoryArena(memoryArena)
    {}


public:
    bool parseShaderMeta(
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        ShaderEntry& outEntry,
        Core::Alloc::ScratchArena& scratchArena
    );
    bool parseIncludeMeta(
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        IncludeEntry& outEntry,
        Core::Alloc::ScratchArena& scratchArena
    );

    bool validateVariantSignature(
        AStringView contextLabel,
        AStringView variantSignature,
        const CookMap<CookString, DefineEntry>& defineValues,
        Core::Alloc::ScratchArena& scratchArena
    );

    void mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata);

    bool gatherShaderDependencies(
        const Path& sourcePath,
        const CookVector<Path>& includeDirectories,
        Span<const AStringView> externallyPlannedMacroIncludes,
        CookVector<Path>& outDependencies,
        Core::Alloc::ScratchArena& scratchArena
    );

    bool expandDefineCombinations(
        const CookMap<CookString, DefineEntry>& defineValues,
        CookVector<DefineCombo>& outCombinations,
        Core::Alloc::ScratchArena& scratchArena
    );

    CookString buildVariantName(
        const DefineCombo& combo,
        Core::Alloc::ScratchArena& scratchArena
    );

    bool computeDependencyChecksum(
        const CookVector<Path>& dependencies,
        InitializerList<DependencyRootAlias> dependencyRootAliases,
        u64& outChecksum,
        bool& outCompilerInputsHaveBom,
        Core::Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] u64 computeSourceChecksum(
        const ShaderEntry& entry,
        const AStringView variantSignature,
        u64 dependencyChecksum,
        u64 compilerFingerprint,
        Core::Alloc::ScratchArena& scratchArena
    );


private:
    template<typename MapT>
    ScratchDefineEntryVector<MapT> sortedDefineEntries(const MapT& map, Core::Alloc::ScratchArena& scratchArena){
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

