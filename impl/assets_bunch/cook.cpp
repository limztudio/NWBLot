// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_bunch_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace AssetsBunchCook;
namespace Metascript = Core::Metascript;
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
        if(AssetsBunchCook::IsAssetBunchType(DeclarationType(declaration)))
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
    const Name baseVirtualPath,
    const AStringView variableName,
    Name& outVirtualPath,
    ScratchArena& scratchArena
){
    outVirtualPath = NAME_NONE;

    ScratchString virtualPath(scratchArena);
    virtualPath += baseVirtualPath.c_str();
    virtualPath += '/';
    virtualPath.append(variableName.data(), variableName.size());

    outVirtualPath = Name(AStringView(virtualPath.data(), virtualPath.size()));
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
    const Name baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::Value& outValue,
    ScratchArena& scratchArena
);

[[nodiscard]] static bool ResolveAssetReferenceList(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const Name baseVirtualPath,
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
    const Name baseVirtualPath,
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
    const Name baseVirtualPath,
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

    Name virtualPath = NAME_NONE;
    if(!BuildItemVirtualPath(baseVirtualPath, DeclarationVariable(*declaration), virtualPath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for reference '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(AStringView(reference.data(), reference.size()))
        );
        return false;
    }

    outValue.setString(Metascript::MStringView(virtualPath.c_str(), NWB_STRLEN(virtualPath.c_str())));
    return true;
}

[[nodiscard]] static bool ResolveAssetReferenceValue(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const Name baseVirtualPath,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsBunchCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsAssetBunchType(const AStringView typeName){
    return ToName(typeName) == s_AssetTypeName;
}

usize AssetBunchDeclarationCount(const Core::Metascript::Document& doc){
    using namespace __hidden_assets_bunch_cook;

    usize count = 0u;
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        if(IsAssetBunchType(DeclarationType(declaration)))
            ++count;
    }
    return count;
}

usize NonAssetBunchDeclarationCount(const Core::Metascript::Document& doc){
    using namespace __hidden_assets_bunch_cook;

    usize count = 0u;
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        if(!IsAssetBunchType(DeclarationType(declaration)))
            ++count;
    }
    return count;
}

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

    Name baseVirtualPath = NAME_NONE;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, baseVirtualPath, scratchArena))
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
            baseVirtualPath,
            usedVariables,
            resolvingVariableHashes,
            *assetValue,
            *resolvedAssetValue,
            scratchArena
        ))
            return false;

        Name virtualPath = NAME_NONE;
        if(!BuildItemVirtualPath(baseVirtualPath, variableName, virtualPath, scratchArena)){
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
