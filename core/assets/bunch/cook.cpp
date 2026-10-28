// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "reference_resolver.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_bunch_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsBunchCook;
namespace Metascript = Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<bool, NameTextError::Enum> IsCompositeRootType(const AStringView typeName){
    const auto name = ToName(typeName);
    if(!name)
        return MakeUnexpected(name.error());
    return *name && Core::Assets::IsCompositeRootAssetType(**name);
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

[[nodiscard]] static Expected<NotNull<const Metascript::Document::Declaration*>> FindBunchItemDeclaration(
    const Path& nwbFilePath,
    const DeclarationLookup& declarations,
    const Metascript::Value& item,
    const usize itemIndex
){
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

    return IsCompositeRootType(assetDeclaration->type);
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
