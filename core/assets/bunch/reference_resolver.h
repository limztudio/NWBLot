// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"

#include <global/allocation_size.h>
#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsBunchCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_AssetBunchTypeName("asset_bunch");
inline constexpr Name s_MetadataDeclarationTypeName("metadata");
inline constexpr TStringView s_AssetBunchDiagnosticPrefix = NWB_TEXT("Asset bunch");
inline constexpr TStringView s_CompositeAssetDiagnosticPrefix = NWB_TEXT("Composite asset");

using ScratchString = AString<ScratchArena>;
using ScratchNameHashSet = HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<bool, NameTextError::Enum> IsAssetBunchType(AStringView typeName);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Tiny documents keep inline lookups; larger ones share one exact-text index.
class DeclarationLookup final : NoCopy{
private:
    using Declaration = Core::Metascript::Document::Declaration;
    using LookupTable = HashMap<AStringView, NotNull<const Declaration*>, ScratchArena>;


private:
    static constexpr usize s_InlineCapacity = 16u;


public:
    [[nodiscard]] static Expected<DeclarationLookup, NameTextError::Enum> Create(const Core::Metascript::Document& doc, ScratchArena& scratchArena);


private:
    DeclarationLookup()noexcept = default;


public:
    [[nodiscard]] const Declaration* find(AStringView reference)const noexcept;


private:
    Array<const Declaration*, s_InlineCapacity> m_inline{};
    usize m_inlineCount = 0u;
    Optional<LookupTable> m_table;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ItemVirtualPath{
    Name identity;
    ScratchString text;
};

[[nodiscard]] Expected<ItemVirtualPath> BuildItemVirtualPath(AStringView baseVirtualPath, AStringView variableName, ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReferenceResolutionContext final : NoCopy{
public:
    ReferenceResolutionContext(
        const Path& inNwbFilePath,
        const Core::Metascript::Document& inDoc,
        const DeclarationLookup& inDeclarations,
        AStringView inBaseVirtualPath,
        const ScratchNameHashSet& inAssetVariableHashes,
        ScratchArena& inScratchArena
    );


public:
    [[nodiscard]] Expected<Core::Metascript::Value> resolveValue(
        const Core::Metascript::Value& source,
        Core::Metascript::MetaArena& valueArena,
        ScratchNameHashSet* referencedAssetVariableHashes = nullptr
    );
    [[nodiscard]] Expected<ExpandedAssetMetadata> expandItemDeclaration(
        const Core::Metascript::Document::Declaration& itemDeclaration,
        TStringView diagnosticPrefix,
        const NameHash* activeItemHash = nullptr
    );


private:
    [[nodiscard]] Expected<Core::Metascript::Value> resolveList(
        const Core::Metascript::Value& source,
        Core::Metascript::MetaArena& valueArena,
        ScratchNameHashSet* referencedAssetVariableHashes
    );
    [[nodiscard]] Expected<Core::Metascript::Value> resolveMap(
        const Core::Metascript::Value& source,
        Core::Metascript::MetaArena& valueArena,
        ScratchNameHashSet* referencedAssetVariableHashes
    );
    [[nodiscard]] Expected<Core::Metascript::Value> resolveDeclaredVariableValue(
        const Core::Metascript::Document::Declaration& declaration,
        NameHash variableHash,
        Core::Metascript::MStringView reference,
        TStringView diagnosticPrefix,
        TStringView cycleMessage,
        Core::Metascript::MetaArena& valueArena,
        ScratchNameHashSet* referencedAssetVariableHashes
    );
    [[nodiscard]] Expected<Core::Metascript::Value> resolveReference(
        const Core::Metascript::Value& source,
        Core::Metascript::MetaArena& valueArena,
        ScratchNameHashSet* referencedAssetVariableHashes
    );


private:
    const Path& m_nwbFilePath;
    const Core::Metascript::Document& m_doc;
    const DeclarationLookup& m_declarations;
    AStringView m_baseVirtualPath;
    const ScratchNameHashSet& m_assetVariableHashes;
    ScratchArena& m_scratchArena;
    ScratchNameHashSet m_resolvingVariableHashes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
