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


namespace ShaderCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StringEqual{
    using is_transparent = void;


public:
    [[nodiscard]] bool operator()(const AStringView lhs, const AStringView rhs)const noexcept{
        return lhs == rhs;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderCook : NoCopy{
public:
    using CookArena = Core::Alloc::GlobalArena;
    using CookString = AString<CookArena>;
    template<typename T>
    using CookVector = Vector<T, CookArena>;

    template<typename T, typename V>
    using CookMap = HashMap<
        T, V, CookArena,
        Conditional_T<IsSame_V<T, CookString>, Hasher<AStringView>, Hasher<T>>,
        Conditional_T<IsSame_V<T, CookString>, ShaderCookDetail::StringEqual, EqualTo<T>>
    >;


public:
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
        const ::Path<Core::Alloc::ScratchArena>& outputPath;
        u32 defineCount = 0;
        ShaderOptimizationLevel::Enum optimizationLevel = ShaderOptimizationLevel::Default;
        bool rayQuery = false;
        bool compilerInputsHaveBom = false;
    };

    struct DependencyRootAlias{
        Path root;
        AStringView key;


    public:
        DependencyRootAlias(const Path& rootPath, const AStringView rootKey)
            : root(rootPath)
            , key(rootKey)
        {}
    };


public:
    using DefineCombo = CookMap<CookString, CookString>;


public:
    struct DefineEntry{
        CookVector<CookString> values;


    public:
        explicit DefineEntry(CookArena& memoryArena)noexcept
            : values(memoryArena)
        {}
        explicit DefineEntry(CookVector<CookString>&& inValues)noexcept
            : values(Move(inValues))
        {}
    };


    struct IncludeEntry{
        CookString source;
        CookMap<CookString, DefineEntry> defineValues;


    public:
        explicit IncludeEntry(CookArena& memoryArena)
            : source(memoryArena)
            , defineValues(memoryArena)
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


    public:
        explicit ShaderEntry(CookArena& memoryArena)
            : name(memoryArena)
            , entryPoint("main", memoryArena)
            , source(memoryArena)
            , includeRoots(memoryArena)
            , defineValues(memoryArena)
            , implicitDefines(memoryArena)
        {}
    };


private:
    template<typename MapT>
    struct DefineEntryPtr{
        const typename MapT::key_type* key;
        const typename MapT::mapped_type* value;
    };


private:
    template<typename MapT>
    using ScratchDefineEntryVector = Vector<
        DefineEntryPtr<MapT>,
        Core::Alloc::ScratchArena
    >;


public:
    struct DependencyChecksum{
        u64 checksum = 0u;
        bool compilerInputsHaveBom = false;
    };


public:
    explicit ShaderCook(CookArena& memoryArena)noexcept
        : m_memoryArena(memoryArena)
    {}


public:
    [[nodiscard]] Expected<ShaderEntry> parseShaderMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<IncludeEntry> parseIncludeMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Alloc::ScratchArena& scratchArena);

    [[nodiscard]] bool validateVariantSignature(AStringView contextLabel, AStringView variantSignature, const CookMap<CookString, DefineEntry>& defineValues, Core::Alloc::ScratchArena& scratchArena);

    void mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata, Core::Alloc::ScratchArena& scratchArena);

    [[nodiscard]] bool expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations, Core::Alloc::ScratchArena& scratchArena);

    [[nodiscard]] CookString buildVariantName(const DefineCombo& combo, Core::Alloc::ScratchArena& scratchArena);


    [[nodiscard]] Expected<DependencyChecksum> computeDependencyChecksum(const CookVector<Path>& dependencies, const CookVector<Path>& includeDirectories, InitializerList<DependencyRootAlias> dependencyRootAliases, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] u64 computeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, u64 dependencyChecksum, u64 compilerFingerprint, Core::Alloc::ScratchArena& scratchArena);


private:
    template<typename MapT>
    ScratchDefineEntryVector<MapT> sortedDefineEntries(const MapT& map, Core::Alloc::ScratchArena& scratchArena){
        using EntryPtr = DefineEntryPtr<MapT>;
        ScratchDefineEntryVector<MapT> entries{scratchArena};
        entries.reserve(map.size());
        for(const auto& [name, value] : map)
            entries.push_back(EntryPtr{ &name, &value });
        Sort(entries.begin(), entries.end(), [](const EntryPtr& lhs, const EntryPtr& rhs)noexcept(noexcept(*lhs.key < *rhs.key)){ return *lhs.key < *rhs.key; });
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

