// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../global.h"

#include <core/alloc/scratch.h>
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

    struct ShaderMacroDefinition{
        AStringView name;
        AStringView value;
    };

    struct ShaderCompilerRequest{
        AStringView shaderName;
        AStringView stage;
        AStringView targetProfile;
        AStringView entryPoint;
        AStringView variantName;
        const ShaderMacroDefinition* defines = nullptr;
        u32 defineCount = 0;
        const CookVector<Path>& includeDirectories;
        const Path& sourcePath;
        const Path& outputPath;
    };

    class IShaderCompiler : NoCopy{
    public:
        IShaderCompiler(CookArena& memoryArena)
            : m_memoryArena(memoryArena)
        {}
        virtual ~IShaderCompiler() = default;


    public:
        virtual bool compileVariant(const ShaderCompilerRequest& request, CookVector<u8>& outBytecode) = 0;


    protected:
        CookArena& m_memoryArena;
    };

    using ShaderCompilerFactory = Core::GlobalUniquePtr<IShaderCompiler> (*)(CookArena& memoryArena);

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


public:
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
        ACompactString targetProfile;
        CookString entryPoint;
        CookString source;

        CookVector<CookString> includeRoots;
        CookMap<CookString, DefineEntry> defineValues;
        CookMap<CookString, CookString> implicitDefines;

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
        {}
    };


public:
    ShaderCook(CookArena& memoryArena, ShaderCompilerFactory compilerFactory = nullptr);


public:
    inline bool compileVariant(const ShaderCompilerRequest& request, CookVector<u8>& outBytecode){ return m_compiler->compileVariant(request, outBytecode); }

public:
    bool parseDocument(const Path& nwbFilePath, Core::Metascript::Document& outDoc);
    bool parseShaderMeta(
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        ShaderEntry& outEntry,
        Core::Alloc::ScratchArena& scratchArena
    );
    bool parseShaderMeta(
        const Path& nwbFilePath,
        ShaderEntry& outEntry,
        Core::Alloc::ScratchArena& scratchArena
    );
    bool parseIncludeMeta(
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        IncludeEntry& outEntry,
        Core::Alloc::ScratchArena& scratchArena
    );
    bool parseIncludeMeta(
        const Path& nwbFilePath,
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
    bool canonicalizeVariantSignature(
        AStringView variantSignature,
        CookString& outCanonical,
        Core::Alloc::ScratchArena& scratchArena
    );

    bool computeDependencyChecksum(
        const CookVector<Path>& dependencies,
        InitializerList<DependencyRootAlias> dependencyRootAliases,
        u64& outChecksum,
        Core::Alloc::ScratchArena& scratchArena
    );
    bool computeSourceChecksum(
        const ShaderEntry& entry,
        const AStringView variantSignature,
        u64 dependencyChecksum,
        u64& outChecksum,
        Core::Alloc::ScratchArena& scratchArena
    );


private:
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

private:
    Core::GlobalUniquePtr<IShaderCompiler> m_compiler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

