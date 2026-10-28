// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/allocation_size.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_bunch_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsBunchCook;
namespace Metascript = Core::Metascript;
inline constexpr Name s_AssetBunchTypeName("asset_bunch");
inline constexpr Name s_MetadataDeclarationTypeName("metadata");
inline constexpr Name s_EntityAssetTypeName("entity");
inline constexpr Name s_SceneAssetTypeName("scene");
inline constexpr TStringView s_AssetBunchDiagnosticPrefix = NWB_TEXT("Asset bunch");
inline constexpr TStringView s_CompositeAssetDiagnosticPrefix = NWB_TEXT("Composite asset");
using ScratchString = AString<ScratchArena>;
using ScratchNameHashSet = HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<bool, NameTextError::Enum> IsAssetBunchType(const AStringView typeName){
    const auto name = ToName(typeName);
    if(!name)
        return MakeUnexpected(name.error());
    return *name && **name == s_AssetBunchTypeName;
}

[[nodiscard]] static Expected<bool, NameTextError::Enum> IsCompositeRootAssetType(const AStringView typeName){
    const auto name = ToName(typeName);
    if(!name)
        return MakeUnexpected(name.error());
    return *name && (**name == s_EntityAssetTypeName || **name == s_SceneAssetTypeName || typeName.ends_with("_subsystem"));
}

[[nodiscard]] static Expected<bool, NameTextError::Enum> HasAssetBunchDeclaration(const Core::Metascript::Document& doc){
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const auto isBunch = IsAssetBunchType(declaration.type);
        if(!isBunch)
            return MakeUnexpected(isBunch.error());
        if(*isBunch)
            return true;
    }
    return false;
}

// Tiny documents keep inline lookups; larger ones share one exact-text index.
class DeclarationLookup final : NoCopy{
private:
    using Declaration = Metascript::Document::Declaration;
    using LookupTable = HashMap<AStringView, NotNull<const Declaration*>, ScratchArena>;


private:
    static constexpr usize s_InlineCapacity = 16u;


public:
    [[nodiscard]] static Expected<DeclarationLookup, NameTextError::Enum> Create(const Metascript::Document& doc, ScratchArena& scratchArena){
        DeclarationLookup declarations;
        if(doc.declarations().size() > s_InlineCapacity){
            declarations.m_table.emplace(AddSize(doc.declarations().size(), doc.declarations().size()), Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena);
        }
        for(const Declaration& declaration : doc.declarations()){
            const auto isBunch = IsAssetBunchType(declaration.type);
            if(!isBunch)
                return MakeUnexpected(isBunch.error());
            if(*isBunch)
                continue;
            if(declarations.m_table){
                if(!declarations.m_table->try_emplace(declaration.variable, MakeNotNull(&declaration)).second){
                    NWB_ASSERT(false);
                    continue;
                }
            }
            else
                declarations.m_inline[declarations.m_inlineCount++] = &declaration;
        }
        return declarations;
    }


private:
    DeclarationLookup()noexcept = default;


public:
    [[nodiscard]] const Declaration* find(const AStringView reference)const noexcept{
        if(m_table){
            const auto found = m_table->find(reference);
            return found == m_table->end() ? nullptr : found.value().get();
        }
        for(usize index = 0u; index < m_inlineCount; ++index){
            if(m_inline[index]->variable == reference)
                return m_inline[index];
        }
        return nullptr;
    }


private:
    Array<const Declaration*, s_InlineCapacity> m_inline{};
    usize m_inlineCount = 0u;
    Optional<LookupTable> m_table;
};


[[nodiscard]] static Expected<NotNull<const Metascript::Document::Declaration*>> FindBunchItemDeclaration(const Path& nwbFilePath, const DeclarationLookup& declarations, const Metascript::Value& item, const usize itemIndex){
    if(!item.isReference()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': item {} must be a declared asset reference"), PathToString<tchar>(nwbFilePath), itemIndex);
        return MakeUnexpected(Failure{});
    }

    const AStringView itemReference(item.asReference().data(), item.asReference().size());
    if(const Metascript::Document::Declaration* declaration = declarations.find(itemReference))
        return MakeNotNull(declaration);

    NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': item {} references undeclared asset variable '{}'"), PathToString<tchar>(nwbFilePath), itemIndex, ConvertString(itemReference));
    return MakeUnexpected(Failure{});
}

struct ItemVirtualPath{
    Name identity;
    ScratchString text;
};

[[nodiscard]] static Expected<ItemVirtualPath> BuildItemVirtualPath(const AStringView baseVirtualPath, const AStringView variableName, ScratchArena& scratchArena){
    ScratchString virtualPathText(scratchArena);
    virtualPathText.reserve(AddSize(AddSize(baseVirtualPath.size(), 1u), variableName.size()));

    virtualPathText.append(baseVirtualPath.data(), baseVirtualPath.size());
    virtualPathText += '/';
    virtualPathText.append(variableName.data(), variableName.size());

    const auto virtualPath = Name::FromText(AStringView(virtualPathText));
    if(!virtualPath)
        return MakeUnexpected(Failure{});
    return ItemVirtualPath{ *virtualPath, Move(virtualPathText) };
}

struct ReferenceResolutionContext{
    const Path& nwbFilePath;
    const Metascript::Document& doc;
    const DeclarationLookup& declarations;
    AStringView baseVirtualPath;
    const ScratchNameHashSet& assetVariableHashes;
    ScratchArena& scratchArena;
    ScratchNameHashSet resolvingVariableHashes;


public:
    ReferenceResolutionContext(
        const Path& inNwbFilePath,
        const Metascript::Document& inDoc,
        const DeclarationLookup& inDeclarations,
        const AStringView inBaseVirtualPath,
        const ScratchNameHashSet& inAssetVariableHashes,
        ScratchArena& inScratchArena
    )
        : nwbFilePath(inNwbFilePath)
        , doc(inDoc)
        , declarations(inDeclarations)
        , baseVirtualPath(inBaseVirtualPath)
        , assetVariableHashes(inAssetVariableHashes)
        , scratchArena(inScratchArena)
        , resolvingVariableHashes(AddSize(inDoc.declarations().size(), inDoc.declarations().size()), Hasher<NameHash>(), EqualTo<NameHash>(), inScratchArena)
    {}

    [[nodiscard]] Expected<Metascript::Value> resolveList(
        const Metascript::Value& source,
        Metascript::MetaArena& valueArena,
        ScratchNameHashSet* const referencedAssetVariableHashes
    ){
        Metascript::Value result(valueArena);
        result.makeList();
        result.asList().reserve(source.asList().size());
        for(const Metascript::Value& item : source.asList()){
            auto resolved = resolveValue(item, valueArena, referencedAssetVariableHashes);
            if(!resolved)
                return MakeUnexpected(Failure{});
            result.append(Move(*resolved));
        }
        return result;
    }

    [[nodiscard]] Expected<Metascript::Value> resolveMap(
        const Metascript::Value& source,
        Metascript::MetaArena& valueArena,
        ScratchNameHashSet* const referencedAssetVariableHashes
    ){
        Metascript::Value result(valueArena);
        result.makeMap();
        result.asMap().reserve(source.asMap().size());
        for(const auto& [key, value] : source.asMap()){
            Metascript::Value& field = result.field(key);
            auto resolved = resolveValue(value, valueArena, referencedAssetVariableHashes);
            if(!resolved)
                return MakeUnexpected(Failure{});
            field = Move(*resolved);
        }
        return result;
    }

    [[nodiscard]] Expected<Metascript::Value> resolveDeclaredVariableValue(
        const Metascript::Document::Declaration& declaration,
        const NameHash variableHash,
        const Metascript::MStringView reference,
        const TStringView diagnosticPrefix,
        const TStringView cycleMessage,
        Metascript::MetaArena& valueArena,
        ScratchNameHashSet* const referencedAssetVariableHashes
    ){
        if(!resolvingVariableHashes.insert(variableHash).second){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': {} '{}'"), diagnosticPrefix, PathToString<tchar>(nwbFilePath), cycleMessage, ConvertString(reference));
            return MakeUnexpected(Failure{});
        }
        const Metascript::Value* const variableValue = doc.findVariable(declaration.variable);
        if(!variableValue){
            resolvingVariableHashes.erase(variableHash);
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': reference '{}' targets a missing local variable"), diagnosticPrefix, PathToString<tchar>(nwbFilePath), ConvertString(reference));
            return MakeUnexpected(Failure{});
        }
        auto resolved = resolveValue(*variableValue, valueArena, referencedAssetVariableHashes);
        resolvingVariableHashes.erase(variableHash);
        return resolved;
    }

    [[nodiscard]] Expected<Metascript::Value> resolveReference(
        const Metascript::Value& source,
        Metascript::MetaArena& valueArena,
        ScratchNameHashSet* const referencedAssetVariableHashes
    ){
        const Metascript::MStringView reference = source.asReference();
        const Metascript::Document::Declaration* declaration = declarations.find(reference);
        if(!declaration){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' does not target a declared asset"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
            return MakeUnexpected(Failure{});
        }

        const auto variableHashResult = ComputeNameHash(AStringView(declaration->variable));
        if(!variableHashResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference variable contains an embedded null"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        const NameHash variableHash = *variableHashResult;
        if(assetVariableHashes.find(variableHash) == assetVariableHashes.end()){
            return resolveDeclaredVariableValue(
                *declaration,
                variableHash,
                reference,
                s_AssetBunchDiagnosticPrefix,
                NWB_TEXT("cyclic local metadata reference"),
                valueArena,
                referencedAssetVariableHashes
            );
        }

        if(referencedAssetVariableHashes){
            if(resolvingVariableHashes.find(variableHash) != resolvingVariableHashes.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': cyclic asset reference '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
                return MakeUnexpected(Failure{});
            }
            if(referencedAssetVariableHashes->insert(variableHash).second){
                const auto nestedResolved = resolveDeclaredVariableValue(
                    *declaration,
                    variableHash,
                    reference,
                    s_CompositeAssetDiagnosticPrefix,
                    NWB_TEXT("cyclic asset reference"),
                    valueArena,
                    referencedAssetVariableHashes
                );
                if(!nestedResolved)
                    return MakeUnexpected(Failure{});
            }
        }

        const auto virtualPath = BuildItemVirtualPath(baseVirtualPath, declaration->variable, scratchArena);
        if(!virtualPath){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for reference '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
            return MakeUnexpected(Failure{});
        }

        Metascript::Value result(valueArena);
        result.setString(virtualPath->text);
        return result;
    }

    [[nodiscard]] Expected<Metascript::Value> resolveValue(
        const Metascript::Value& source,
        Metascript::MetaArena& valueArena,
        ScratchNameHashSet* const referencedAssetVariableHashes = nullptr
    ){
        if(source.isReference())
            return resolveReference(source, valueArena, referencedAssetVariableHashes);
        if(source.isList())
            return resolveList(source, valueArena, referencedAssetVariableHashes);
        if(source.isMap())
            return resolveMap(source, valueArena, referencedAssetVariableHashes);

        Metascript::Value result(valueArena);
        result = source;
        return result;
    }

    [[nodiscard]] Expected<ExpandedAssetMetadata> expandItemDeclaration(
        const Metascript::Document::Declaration& itemDeclaration,
        const TStringView diagnosticPrefix,
        const NameHash* const activeItemHash = nullptr
    ){
        const AStringView variableName = itemDeclaration.variable;
        const Metascript::Value* const itemValue = doc.findVariable(variableName);
        if(!itemValue){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': references missing variable '{}'"), diagnosticPrefix, PathToString<tchar>(nwbFilePath), ConvertString(variableName));
            return MakeUnexpected(Failure{});
        }

        if(activeItemHash)
            resolvingVariableHashes.insert(*activeItemHash);
        auto resolvedItemValue = resolveValue(*itemValue, itemValue->arena());
        if(activeItemHash)
            resolvingVariableHashes.erase(*activeItemHash);
        if(!resolvedItemValue)
            return MakeUnexpected(Failure{});

        const auto virtualPath = BuildItemVirtualPath(baseVirtualPath, variableName, scratchArena);
        if(!virtualPath){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to build virtual path for variable '{}'"), diagnosticPrefix, PathToString<tchar>(nwbFilePath), ConvertString(variableName));
            return MakeUnexpected(Failure{});
        }

        const auto assetType = ToName(itemDeclaration.type);
        if(!assetType)
            return MakeUnexpected(Failure{});
        return ExpandedAssetMetadata{ *assetType ? **assetType : s_NameNone, virtualPath->identity, Move(*resolvedItemValue) };
    }
};

static Expected<ExpandedAssetMetadataVector, Core::Assets::AssetBunchExpandFailure::Enum> ExpandAssetBunchForAssetCook(const Core::Assets::AssetBunchExpandContext& context){
    const auto hasBunch = HasAssetBunchDeclaration(context.doc);
    if(!hasBunch)
        return MakeUnexpected(Core::Assets::AssetBunchExpandFailure::Error);
    if(*hasBunch){
        auto assets = ExpandAssetBunch(context.assetRoot, context.virtualRoot, context.nwbFilePath, context.doc, context.scratchArena);
        if(!assets)
            return MakeUnexpected(Core::Assets::AssetBunchExpandFailure::Error);
        return Move(*assets);
    }

    const auto hasComposite = HasCompositeAssetDeclarations(context.doc);
    if(!hasComposite)
        return MakeUnexpected(Core::Assets::AssetBunchExpandFailure::Error);
    if(!*hasComposite)
        return MakeUnexpected(Core::Assets::AssetBunchExpandFailure::Unsupported);

    auto assets = ExpandCompositeAsset(context.assetRoot, context.virtualRoot, context.nwbFilePath, context.doc, context.scratchArena);
    if(!assets)
        return MakeUnexpected(Core::Assets::AssetBunchExpandFailure::Error);
    return Move(*assets);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::AssetBunchExpanderAutoRegistrar s_AssetBunchExpanderRegistrar(&ExpandAssetBunchForAssetCook);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsBunchCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ExpandedAssetMetadataVector> ExpandAssetBunch(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, ScratchArena& scratchArena){
    using namespace __hidden_assets_bunch_cook;

    ExpandedAssetMetadataVector assets(scratchArena);

    const Metascript::Document::Declaration* bunchDeclaration = nullptr;
    for(const Metascript::Document::Declaration& declaration : doc.declarations()){
        const auto isBunch = IsAssetBunchType(declaration.type);
        if(!isBunch){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': declaration type contains an embedded null"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        if(!*isBunch)
            continue;
        if(bunchDeclaration){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': multiple asset_bunch declarations are not allowed"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        bunchDeclaration = &declaration;
    }
    if(!bunchDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': missing asset_bunch declaration"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    const Metascript::Value* bunchValue = doc.findVariable(bunchDeclaration->variable);
    if(!bunchValue || !bunchValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': declaration must be initialized with a list"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    const auto& list = bunchValue->asList();
    if(list.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': list must contain at least one asset"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    assets.reserve(list.size());

    // Debug iterator proxies allocate during moves; release temporaries before transferring the result.
    {
        auto baseVirtualPathText = Core::Assets::BuildDerivedAssetVirtualPath(scratchArena, assetRoot, virtualRoot, nwbFilePath, scratchArena);
        if(!baseVirtualPathText)
            return MakeUnexpected(Failure{});

        ScratchNameHashSet usedVariables(AddSize(list.size(), list.size()), Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);

        const auto declarations = DeclarationLookup::Create(doc, scratchArena);
        if(!declarations)
            return MakeUnexpected(Failure{});
        Vector<NotNull<const Metascript::Document::Declaration*>, ScratchArena> itemDeclarations(scratchArena);
        itemDeclarations.reserve(list.size());
        for(usize itemIndex = 0u; itemIndex < list.size(); ++itemIndex){
            const auto itemDeclaration = FindBunchItemDeclaration(nwbFilePath, *declarations, list[itemIndex], itemIndex);
            if(!itemDeclaration)
                return MakeUnexpected(itemDeclaration.error());

            const AStringView variableName = (*itemDeclaration)->variable;
            const auto variableHash = ComputeNameHash(variableName);
            if(!variableHash){
                NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': item variable contains an embedded null"), PathToString<tchar>(nwbFilePath));
                return MakeUnexpected(Failure{});
            }
            if(!usedVariables.insert(*variableHash).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': variable '{}' is listed more than once"), PathToString<tchar>(nwbFilePath), ConvertString(variableName));
                return MakeUnexpected(Failure{});
            }

            itemDeclarations.push_back(MakeNotNull(&**itemDeclaration));
        }

        ReferenceResolutionContext resolutionContext(
            nwbFilePath,
            doc,
            *declarations,
            *baseVirtualPathText,
            usedVariables,
            scratchArena
        );

        for(const auto itemDeclaration : itemDeclarations){
            auto expandedItem = resolutionContext.expandItemDeclaration(*itemDeclaration, s_AssetBunchDiagnosticPrefix);
            if(!expandedItem)
                return MakeUnexpected(Failure{});
            assets.push_back(Move(*expandedItem));
        }
    }

    return assets;
}

Expected<bool, NameTextError::Enum> HasAssetBunchDeclaration(const Core::Metascript::Document& doc){
    return __hidden_assets_bunch_cook::HasAssetBunchDeclaration(doc);
}

Expected<bool, NameTextError::Enum> HasCompositeAssetDeclarations(const Core::Metascript::Document& doc){
    using namespace __hidden_assets_bunch_cook;

    const auto hasBunch = HasAssetBunchDeclaration(doc);
    if(!hasBunch)
        return MakeUnexpected(hasBunch.error());
    if(*hasBunch || doc.declarations().size() <= 1u)
        return false;

    const auto* const assetDeclaration = Core::Assets::FindMetadataAssetDeclaration(doc);
    if(!assetDeclaration)
        return false;

    return IsCompositeRootAssetType(assetDeclaration->type);
}

Expected<ExpandedAssetMetadataVector> ExpandCompositeAsset(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    ScratchArena& scratchArena
){
    using namespace __hidden_assets_bunch_cook;

    ExpandedAssetMetadataVector assets(scratchArena);

    const auto hasComposite = HasCompositeAssetDeclarations(doc);
    if(!hasComposite){
        NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': declaration type contains an embedded null"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    if(!*hasComposite){
        NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': missing composite root declaration"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    const Metascript::Document::Declaration* const rootDeclaration = Core::Assets::FindMetadataAssetDeclaration(doc);
    if(!rootDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': missing 'asset' declaration"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    const Metascript::Value* const rootValue = doc.findVariable(rootDeclaration->variable);
    if(!rootValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': missing 'asset' variable value"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    {
        auto baseVirtualPathText = Core::Assets::BuildDerivedAssetVirtualPath(scratchArena, assetRoot, virtualRoot, nwbFilePath, scratchArena);
        if(!baseVirtualPathText)
            return MakeUnexpected(Failure{});
        const auto baseVirtualPath = Name::FromText(AStringView(*baseVirtualPathText));
        if(!baseVirtualPath || !*baseVirtualPath)
            return MakeUnexpected(Failure{});

        const auto declarations = DeclarationLookup::Create(doc, scratchArena);
        if(!declarations)
            return MakeUnexpected(Failure{});

        const usize declarationCapacity = AddSize(doc.declarations().size(), doc.declarations().size());
        ScratchNameHashSet assetVariableHashes(declarationCapacity, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
        Vector<NotNull<const Metascript::Document::Declaration*>, ScratchArena> auxiliaryDeclarations(scratchArena);
        auxiliaryDeclarations.reserve(doc.declarations().size());

        for(const Metascript::Document::Declaration& declaration : doc.declarations()){
            if(&declaration == rootDeclaration)
                continue;
            const auto typeName = ToName(declaration.type);
            if(!typeName || !*typeName){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': declaration type contains an embedded null"), PathToString<tchar>(nwbFilePath));
                return MakeUnexpected(Failure{});
            }
            if(**typeName == s_MetadataDeclarationTypeName)
                continue;

            const auto variableHash = ComputeNameHash(AStringView(declaration.variable));
            if(!variableHash){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': declaration variable contains an embedded null"), PathToString<tchar>(nwbFilePath));
                return MakeUnexpected(Failure{});
            }
            if(!assetVariableHashes.insert(*variableHash).second){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Composite asset '{}': duplicate declaration variable '{}'"),
                    PathToString<tchar>(nwbFilePath),
                    ConvertString(declaration.variable)
                );
                return MakeUnexpected(Failure{});
            }
            auxiliaryDeclarations.push_back(MakeNotNull(&declaration));
        }

        ScratchNameHashSet referencedAssetVariableHashes(declarationCapacity, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
        ReferenceResolutionContext resolutionContext(
            nwbFilePath,
            doc,
            *declarations,
            *baseVirtualPathText,
            assetVariableHashes,
            scratchArena
        );

        auto resolvedRootValue = resolutionContext.resolveValue(*rootValue, rootValue->arena(), &referencedAssetVariableHashes);
        if(!resolvedRootValue)
            return MakeUnexpected(Failure{});

        assets.reserve(AddSize(auxiliaryDeclarations.size(), 1u));
        for(const auto itemDeclaration : auxiliaryDeclarations){
            const AStringView variableName = itemDeclaration->variable;
            const auto variableHash = ComputeNameHash(variableName);
            if(!variableHash || referencedAssetVariableHashes.find(*variableHash) == referencedAssetVariableHashes.end()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Composite asset '{}': declaration '{}' is not referenced by 'asset'"),
                    PathToString<tchar>(nwbFilePath),
                    ConvertString(variableName)
                );
                return MakeUnexpected(Failure{});
            }

            auto expandedItem = resolutionContext.expandItemDeclaration(*itemDeclaration, s_CompositeAssetDiagnosticPrefix, &*variableHash);
            if(!expandedItem)
                return MakeUnexpected(Failure{});
            assets.push_back(Move(*expandedItem));
        }

        const auto rootAssetType = ToName(rootDeclaration->type);
        if(!rootAssetType)
            return MakeUnexpected(Failure{});
        assets.push_back(ExpandedAssetMetadata{ *rootAssetType ? **rootAssetType : s_NameNone, *baseVirtualPath, Move(*resolvedRootValue) });
    }

    return assets;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
