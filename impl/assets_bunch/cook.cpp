// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_bunch_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsBunchCook;
namespace Metascript = Core::Metascript;
inline constexpr Name s_AssetBunchTypeName("asset_bunch");
using ScratchString = AString<ScratchArena>;
using ScratchNameHashSet = HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView DeclarationType(const Metascript::Document::Declaration& declaration){
    return AStringView(declaration.type.data(), declaration.type.size());
}

[[nodiscard]] static AStringView DeclarationVariable(const Metascript::Document::Declaration& declaration){
    return AStringView(declaration.variable.data(), declaration.variable.size());
}

[[nodiscard]] static Metascript::MStringView DeclarationVariableMetaView(const Metascript::Document::Declaration& declaration){
    return Metascript::MStringView(declaration.variable.data(), declaration.variable.size());
}

[[nodiscard]] static bool IsAssetBunchType(const AStringView typeName){
    return ToName(typeName) == s_AssetBunchTypeName;
}

[[nodiscard]] static bool HasAssetBunchDeclaration(const Core::Metascript::Document& doc){
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        if(IsAssetBunchType(DeclarationType(declaration)))
            return true;
    }
    return false;
}

[[nodiscard]] static const Metascript::Document::Declaration* FindBunchItemDeclaration(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const Metascript::Value& item,
    const usize itemIndex
){
    if(!item.isReference()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': item {} must be a declared asset reference")
            , PathToString<tchar>(nwbFilePath)
            , itemIndex
        );
        return nullptr;
    }

    const AStringView itemReference(item.asReference().data(), item.asReference().size());
    for(const Metascript::Document::Declaration& declaration : doc.declarations()){
        if(IsAssetBunchType(DeclarationType(declaration)))
            continue;
        if(DeclarationVariable(declaration) == itemReference)
            return &declaration;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': item {} references undeclared asset variable '{}'")
        , PathToString<tchar>(nwbFilePath)
        , itemIndex
        , StringConvert(itemReference)
    );
    return nullptr;
}

[[nodiscard]] static bool BuildItemVirtualPath(
    const AStringView baseVirtualPath,
    const AStringView variableName,
    ScratchString& outVirtualPathText,
    Name& outVirtualPath,
    ScratchArena&
){
    outVirtualPath = NAME_NONE;
    outVirtualPathText.clear();

    outVirtualPathText.append(baseVirtualPath.data(), baseVirtualPath.size());
    outVirtualPathText += '/';
    outVirtualPathText.append(variableName.data(), variableName.size());

    outVirtualPath = Name(AStringView(outVirtualPathText.data(), outVirtualPathText.size()));
    return outVirtualPath != NAME_NONE;
}

[[nodiscard]] static NameHash DeclarationVariableHash(const Metascript::Document::Declaration& declaration){
    return ComputeNameHash(DeclarationVariable(declaration));
}

[[nodiscard]] static const Metascript::Document::Declaration* FindDeclarationByReference(
    const Metascript::Document& doc,
    const Metascript::MStringView reference
){
    for(const Metascript::Document::Declaration& declaration : doc.declarations()){
        if(IsAssetBunchType(DeclarationType(declaration)))
            continue;
        if(DeclarationVariable(declaration) == AStringView(reference.data(), reference.size()))
            return &declaration;
    }
    return nullptr;
}

[[nodiscard]] static bool ResolveAssetReferenceValue(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::Value& outValue,
    ScratchArena& scratchArena
);

[[nodiscard]] static bool ResolveAssetReferenceList(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::Value& outValue,
    ScratchArena& scratchArena
){
    outValue.makeList();
    for(const Metascript::Value& item : source.asList()){
        Metascript::Value resolved(outValue.arena());
        if(!ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            item,
            resolved,
            scratchArena
        ))
            return false;
        outValue.append(Move(resolved));
    }
    return true;
}

[[nodiscard]] static bool ResolveAssetReferenceMap(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::Value& outValue,
    ScratchArena& scratchArena
){
    outValue.makeMap();
    for(const auto& [key, value] : source.asMap()){
        if(!ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            value,
            outValue.field(Metascript::MStringView(key.data(), key.size())),
            scratchArena
        ))
            return false;
    }
    return true;
}

[[nodiscard]] static bool ResolveAssetReference(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::Value& outValue,
    ScratchArena& scratchArena
){
    const Metascript::MStringView reference = source.asReference();
    const Metascript::Document::Declaration* declaration = FindDeclarationByReference(doc, reference);
    if(!declaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' does not target a declared asset")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(AStringView(reference.data(), reference.size()))
        );
        return false;
    }

    const NameHash variableHash = DeclarationVariableHash(*declaration);
    if(assetVariableHashes.find(variableHash) == assetVariableHashes.end()){
        if(!resolvingVariableHashes.insert(variableHash).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': cyclic local metadata reference '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(AStringView(reference.data(), reference.size()))
            );
            return false;
        }

        const Metascript::Value* localValue = doc.findVariable(DeclarationVariableMetaView(*declaration));
        if(!localValue){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' targets a missing local variable")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(AStringView(reference.data(), reference.size()))
            );
            return false;
        }

        const bool resolved = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            *localValue,
            outValue,
            scratchArena
        );
        resolvingVariableHashes.erase(variableHash);
        return resolved;
    }

    ScratchString virtualPathText(scratchArena);
    Name virtualPath = NAME_NONE;
    if(!BuildItemVirtualPath(baseVirtualPath, DeclarationVariable(*declaration), virtualPathText, virtualPath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for reference '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(AStringView(reference.data(), reference.size()))
        );
        return false;
    }

    outValue.setString(Metascript::MStringView(virtualPathText.data(), virtualPathText.size()));
    return true;
}

[[nodiscard]] static bool ResolveAssetReferenceValue(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::Value& outValue,
    ScratchArena& scratchArena
){
    if(source.isReference())
        return ResolveAssetReference(
            nwbFilePath,
            doc,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            source,
            outValue,
            scratchArena
        );
    if(source.isList())
        return ResolveAssetReferenceList(
            nwbFilePath,
            doc,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            source,
            outValue,
            scratchArena
        );
    if(source.isMap())
        return ResolveAssetReferenceMap(
            nwbFilePath,
            doc,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            source,
            outValue,
            scratchArena
        );

    outValue = source;
    return true;
}

static Core::Assets::AssetBunchExpandResult ExpandAssetBunchForAssetCook(Core::Assets::AssetBunchExpandContext& context){
    if(!HasAssetBunchDeclaration(context.doc))
        return Core::Assets::AssetBunchExpandResult::Unsupported;

    ExpandedAssetVector expandedAssets(context.scratchArena);
    if(!ExpandAssetBunch(
        context.assetRoot,
        context.virtualRoot,
        context.nwbFilePath,
        context.doc,
        expandedAssets,
        context.scratchArena
    ))
        return Core::Assets::AssetBunchExpandResult::Error;

    context.outAssets.reserve(expandedAssets.size());
    for(const ExpandedAsset& expandedAsset : expandedAssets){
        context.outAssets.push_back(Core::Assets::ExpandedAssetMetadata{
            expandedAsset.assetType,
            expandedAsset.virtualPath,
            expandedAsset.value
        });
    }

    return Core::Assets::AssetBunchExpandResult::Parsed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetBunchExpanderAutoRegistrar s_AssetBunchExpanderRegistrar(&ExpandAssetBunchForAssetCook);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsBunchCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ExpandAssetBunch(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    ExpandedAssetVector& outAssets,
    ScratchArena& scratchArena
){
    using namespace __hidden_assets_bunch_cook;

    outAssets.clear();

    const Metascript::Document::Declaration* bunchDeclaration = nullptr;
    for(const Metascript::Document::Declaration& declaration : doc.declarations()){
        if(!IsAssetBunchType(DeclarationType(declaration)))
            continue;
        if(bunchDeclaration){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': multiple asset_bunch declarations are not allowed")
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        bunchDeclaration = &declaration;
    }
    if(!bunchDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': missing asset_bunch declaration")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const Metascript::Value* bunchValue = doc.findVariable(DeclarationVariableMetaView(*bunchDeclaration));
    if(!bunchValue || !bunchValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': declaration must be initialized with a list")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const auto& list = bunchValue->asList();
    if(list.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': list must contain at least one asset")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    ScratchString baseVirtualPathText(scratchArena);
    if(!Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, baseVirtualPathText))
        return false;

    ScratchNameHashSet usedVariables(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        scratchArena
    );
    usedVariables.reserve(list.size());
    outAssets.reserve(list.size());

    Vector<const Metascript::Document::Declaration*, ScratchArena> itemDeclarations(scratchArena);
    itemDeclarations.reserve(list.size());
    for(usize itemIndex = 0u; itemIndex < list.size(); ++itemIndex){
        const Metascript::Document::Declaration* itemDeclaration = FindBunchItemDeclaration(nwbFilePath, doc, list[itemIndex], itemIndex);
        if(!itemDeclaration)
            return false;

        const AStringView variableName = DeclarationVariable(*itemDeclaration);
        if(!usedVariables.insert(ComputeNameHash(variableName)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': variable '{}' is listed more than once")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(variableName)
            );
            return false;
        }

        itemDeclarations.push_back(itemDeclaration);
    }

    ScratchNameHashSet resolvingVariableHashes(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    resolvingVariableHashes.reserve(doc.declarations().size());

    for(const Metascript::Document::Declaration* itemDeclaration : itemDeclarations){
        const AStringView variableName = DeclarationVariable(*itemDeclaration);
        const Metascript::Value* assetValue = doc.findVariable(DeclarationVariableMetaView(*itemDeclaration));
        if(!assetValue){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': references missing variable '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(variableName)
            );
            return false;
        }

        Metascript::Value* resolvedAssetValue = NewArenaObject<Metascript::Value>(assetValue->arena(), assetValue->arena());
        if(!ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            AStringView(baseVirtualPathText.data(), baseVirtualPathText.size()),
            usedVariables,
            resolvingVariableHashes,
            *assetValue,
            *resolvedAssetValue,
            scratchArena
        ))
            return false;

        ScratchString virtualPathText(scratchArena);
        Name virtualPath = NAME_NONE;
        if(!BuildItemVirtualPath(
            AStringView(baseVirtualPathText.data(), baseVirtualPathText.size()),
            variableName,
            virtualPathText,
            virtualPath,
            scratchArena
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for variable '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(variableName)
            );
            return false;
        }

        outAssets.push_back(ExpandedAsset{
            ToName(DeclarationType(*itemDeclaration)),
            virtualPath,
            resolvedAssetValue
        });
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

