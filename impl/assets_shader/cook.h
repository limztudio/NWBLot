// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include <impl/assets_shader/cook_types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/assets/cook_paths.h>
#include <core/graphics/rhi/shader.h>
#include <core/metascript/parser.h>

#include <global/not_null.h>
#include <global/span.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderCook : NoCopy{
public:
    using CookArena = Core::Assets::CookArena;
    using CookString = Core::Assets::CookString;
    template<typename T>
    using CookVector = Core::Assets::CookVector<T>;

    template<typename T, typename V>
    using CookMap = Core::Assets::CookMap<T, V>;

    // Combos borrow define text from the expanded entry's defineValues or static literals; use them only while that entry is unchanged.
    using DefineCombo = CookMap<AStringView, AStringView>;


public:
    struct ShaderCompilerRequest{
        AStringView shaderName;
        AStringView stage;
        AStringView entryPoint;
        AStringView variantName;
        Span<const ShaderBinaryPayload::ShaderMacroDefinition> defines;
        Span<const Path> includeDirectories;
        Span<const Path> dependencies;
        Span<const AStringView> externallyPlannedMacroIncludes;
        const Path& sourcePath;
        const ::Path<Core::Alloc::ScratchArena>& outputPath;
        ShaderBinaryPayload::ShaderOptimizationLevel::Enum optimizationLevel = ShaderBinaryPayload::ShaderOptimizationLevel::Default;
        b8 rayQuery = false;
        b8 compilerInputsHaveBom = false;
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
        // Stage texts borrow static ShaderStageNames or MaterialShaderStageNames literals.
        AStringView stage;
        AStringView archiveStage;
        CookString entryPoint;
        CookString source;

        CookVector<CookString> includeRoots;
        CookMap<CookString, DefineEntry> defineValues;
        CookMap<CookString, CookString> implicitDefines;
        ShaderBinaryPayload::ShaderOptimizationLevel::Enum optimizationLevel = ShaderBinaryPayload::ShaderOptimizationLevel::Default;
        b8 rayQuery = false;
        b8 emitMeshComputeShadow = true;


    public:
        explicit ShaderEntry(CookArena& memoryArena)
            : name(memoryArena)
            , entryPoint(Core::ShaderDesc::s_DefaultEntryName, memoryArena)
            , source(memoryArena)
            , includeRoots(memoryArena)
            , defineValues(memoryArena)
            , implicitDefines(memoryArena)
        {}
    };


private:
    template<typename MapT>
    struct DefineEntryPtr{
        NotNull<const typename MapT::key_type*> key;
        NotNull<const typename MapT::mapped_type*> value;
    };

    template<typename MapT>
    using ScratchDefineEntryVector = Core::Assets::ScratchVector<DefineEntryPtr<MapT>>;


public:
    [[nodiscard]] static b8 ValidateVariantSignature(AStringView contextLabel, AStringView variantSignature, const CookMap<CookString, DefineEntry>& defineValues, Core::Alloc::ScratchArena& scratchArena);

    template<typename OnAssignmentFn, typename OnErrorFn>
    [[nodiscard]] static b8 ParseVariantSignatureAssignments(
        const AStringView variantSignature,
        OnAssignmentFn&& onAssignment,
        OnErrorFn&& onError
    ){
        usize begin = 0u;
        while(begin < variantSignature.size()){
            AStringView segment;
            AStringView defineName;
            AStringView defineValue;
            ShaderBinaryPayload::VariantSignatureSegmentError::Enum error = ShaderBinaryPayload::VariantSignatureSegmentError::None;
            if(!ShaderBinaryPayload::TryParseVariantSignatureSegment(variantSignature, begin, segment, defineName, defineValue, error)){
                onError(error, segment);
                return false;
            }

            if(!onAssignment(defineName, defineValue))
                return false;
        }

        return true;
    }

    template<typename MapT, typename ArenaT>
    [[nodiscard]] static AString<ArenaT> BuildCanonicalVariantName(const MapT& combo, ArenaT& arena, Core::Alloc::ScratchArena& scratchArena){
        if(combo.empty())
            return AString<ArenaT>(ShaderBinaryPayload::s_ShaderDefaultVariantText, arena);

        if(combo.size() == 1u){
            const auto& [defineName, defineValue] = *combo.begin();
            AString<ArenaT> variantName{arena};
            variantName.reserve(defineName.size() + defineValue.size() + 1u);
            variantName += defineName;
            variantName += '=';
            variantName += defineValue;
            return variantName;
        }

        const auto entries = SortedDefineEntries(combo, scratchArena);
        usize variantNameSize = entries.size() - 1u;
        for(const auto& entry : entries)
            variantNameSize += entry.key->size() + entry.value->size() + 1u;

        AString<ArenaT> variantName{arena};
        variantName.reserve(variantNameSize);
        b8 first = true;
        for(const auto& entry : entries){
            if(!first)
                variantName += ';';
            first = false;

            variantName += *entry.key;
            variantName += '=';
            variantName += *entry.value;
        }

        return variantName;
    }

    [[nodiscard]] static Expected<ShaderBinaryPayload::DependencyChecksum> ComputeDependencyChecksum(
        Span<const Path> dependencies,
        Span<const Path> includeDirectories,
        InitializerList<DependencyRootAlias> dependencyRootAliases,
        Core::Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] static u64 ComputeSourceChecksum(const ShaderEntry& entry, AStringView variantSignature, u64 dependencyChecksum, u64 compilerFingerprint, Core::Alloc::ScratchArena& scratchArena);


private:
    template<typename MapT>
    [[nodiscard]] static ScratchDefineEntryVector<MapT> SortedDefineEntries(const MapT& map, Core::Alloc::ScratchArena& scratchArena){
        using EntryPtr = DefineEntryPtr<MapT>;
        ScratchDefineEntryVector<MapT> entries{scratchArena};
        entries.reserve(map.size());
        for(const auto& [name, value] : map)
            entries.push_back(EntryPtr{ MakeNotNull(&name), MakeNotNull(&value) });
        Sort(entries.begin(), entries.end(), [](const EntryPtr& lhs, const EntryPtr& rhs)noexcept(noexcept(*lhs.key < *rhs.key)){ return *lhs.key < *rhs.key; });
        return entries;
    }


public:
    explicit ShaderCook(CookArena& memoryArena)noexcept
        : m_memoryArena(memoryArena)
    {}


public:
    [[nodiscard]] Expected<ShaderEntry> parseShaderMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<IncludeEntry> parseIncludeMeta(const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Alloc::ScratchArena& scratchArena);

    void mergeInheritedDefines(Span<const Path> dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata, Core::Alloc::ScratchArena& scratchArena, ShaderEntry& inOutEntry);

    [[nodiscard]] b8 expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, Core::Alloc::ScratchArena& scratchArena, CookVector<DefineCombo>& outCombinations);


private:
    CookArena& m_memoryArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

