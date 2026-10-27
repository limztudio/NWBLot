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
using ScratchString = AString<ScratchArena>;
using ScratchNameHashSet = HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView DeclarationType(const Metascript::Document::Declaration& declaration)noexcept{
    return declaration.type;
}

[[nodiscard]] static AStringView DeclarationVariable(const Metascript::Document::Declaration& declaration)noexcept{
    return declaration.variable;
}

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
        const auto isBunch = IsAssetBunchType(DeclarationType(declaration));
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
            const auto isBunch = IsAssetBunchType(DeclarationType(declaration));
            if(!isBunch)
                return MakeUnexpected(isBunch.error());
            if(*isBunch)
                continue;
            if(declarations.m_table){
                if(!declarations.m_table->try_emplace(DeclarationVariable(declaration), MakeNotNull(&declaration)).second){
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
            if(DeclarationVariable(*m_inline[index]) == reference)
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

[[nodiscard]] static Expected<NameHash, NameTextError::Enum> DeclarationVariableHash(const Metascript::Document::Declaration& declaration)noexcept{
    return ComputeNameHash(DeclarationVariable(declaration));
}

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReferenceValue(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena,
    ScratchNameHashSet& resolvingVariableHashes,
    ScratchNameHashSet* referencedAssetVariableHashes = nullptr
);

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReferenceList(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena,
    ScratchNameHashSet& resolvingVariableHashes,
    ScratchNameHashSet* const referencedAssetVariableHashes
){
    Metascript::Value result(valueArena);
    result.makeList();
    result.asList().reserve(source.asList().size());
    for(const Metascript::Value& item : source.asList()){
        auto resolved = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            item,
            valueArena,
            scratchArena,
            resolvingVariableHashes,
            referencedAssetVariableHashes
        );
        if(!resolved)
            return MakeUnexpected(Failure{});
        result.append(Move(*resolved));
    }
    return result;
}

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReferenceMap(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena,
    ScratchNameHashSet& resolvingVariableHashes,
    ScratchNameHashSet* const referencedAssetVariableHashes
){
    Metascript::Value result(valueArena);
    result.makeMap();
    result.asMap().reserve(source.asMap().size());
    for(const auto& [key, value] : source.asMap()){
        Metascript::Value& field = result.field(key);
        auto resolved = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            value,
            valueArena,
            scratchArena,
            resolvingVariableHashes,
            referencedAssetVariableHashes
        );
        if(!resolved)
            return MakeUnexpected(Failure{});
        field = Move(*resolved);
    }
    return result;
}

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReference(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena,
    ScratchNameHashSet& resolvingVariableHashes,
    ScratchNameHashSet* const referencedAssetVariableHashes
){
    const Metascript::MStringView reference = source.asReference();
    const Metascript::Document::Declaration* declaration = declarations.find(reference);
    if(!declaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' does not target a declared asset"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
        return MakeUnexpected(Failure{});
    }

    const auto variableHashResult = DeclarationVariableHash(*declaration);
    if(!variableHashResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference variable contains an embedded null"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    const NameHash variableHash = *variableHashResult;
    if(assetVariableHashes.find(variableHash) == assetVariableHashes.end()){
        if(!resolvingVariableHashes.insert(variableHash).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': cyclic local metadata reference '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
            return MakeUnexpected(Failure{});
        }

        const Metascript::Value* localValue = doc.findVariable(DeclarationVariable(*declaration));
        if(!localValue){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' targets a missing local variable"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
            return MakeUnexpected(Failure{});
        }

        auto resolved = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            *localValue,
            valueArena,
            scratchArena,
            resolvingVariableHashes,
            referencedAssetVariableHashes
        );
        resolvingVariableHashes.erase(variableHash);
        return resolved;
    }

    if(referencedAssetVariableHashes){
        if(resolvingVariableHashes.find(variableHash) != resolvingVariableHashes.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': cyclic asset reference '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
            return MakeUnexpected(Failure{});
        }
        if(referencedAssetVariableHashes->insert(variableHash).second){
            const Metascript::Value* const referencedValue = doc.findVariable(DeclarationVariable(*declaration));
            if(!referencedValue){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': reference '{}' targets a missing local variable"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
                return MakeUnexpected(Failure{});
            }
            resolvingVariableHashes.insert(variableHash);
            const auto nestedResolved = ResolveAssetReferenceValue(
                nwbFilePath,
                doc,
                declarations,
                baseVirtualPath,
                assetVariableHashes,
                *referencedValue,
                valueArena,
                scratchArena,
                resolvingVariableHashes,
                referencedAssetVariableHashes
            );
            resolvingVariableHashes.erase(variableHash);
            if(!nestedResolved)
                return MakeUnexpected(Failure{});
        }
    }

    const auto virtualPath = BuildItemVirtualPath(baseVirtualPath, DeclarationVariable(*declaration), scratchArena);
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for reference '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(reference));
        return MakeUnexpected(Failure{});
    }

    Metascript::Value result(valueArena);
    result.setString(virtualPath->text);
    return result;
}

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReferenceValue(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena,
    ScratchNameHashSet& resolvingVariableHashes,
    ScratchNameHashSet* const referencedAssetVariableHashes
){
    if(source.isReference())
        return ResolveAssetReference(nwbFilePath, doc, declarations, baseVirtualPath, assetVariableHashes, source, valueArena, scratchArena, resolvingVariableHashes, referencedAssetVariableHashes);
    if(source.isList())
        return ResolveAssetReferenceList(nwbFilePath, doc, declarations, baseVirtualPath, assetVariableHashes, source, valueArena, scratchArena, resolvingVariableHashes, referencedAssetVariableHashes);
    if(source.isMap())
        return ResolveAssetReferenceMap(nwbFilePath, doc, declarations, baseVirtualPath, assetVariableHashes, source, valueArena, scratchArena, resolvingVariableHashes, referencedAssetVariableHashes);

    Metascript::Value result(valueArena);
    result = source;
    return result;
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
        const auto isBunch = IsAssetBunchType(DeclarationType(declaration));
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

    const Metascript::Value* bunchValue = doc.findVariable(DeclarationVariable(*bunchDeclaration));
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

            const AStringView variableName = DeclarationVariable(**itemDeclaration);
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

        ScratchNameHashSet resolvingVariableHashes(AddSize(doc.declarations().size(), doc.declarations().size()), Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);

        for(const auto itemDeclaration : itemDeclarations){
            const AStringView variableName = DeclarationVariable(*itemDeclaration);
            const Metascript::Value* assetValue = doc.findVariable(variableName);
            if(!assetValue){
                NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': references missing variable '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(variableName));
                return MakeUnexpected(Failure{});
            }

            auto resolvedAssetValue = ResolveAssetReferenceValue(nwbFilePath, doc, *declarations, *baseVirtualPathText, usedVariables, *assetValue, assetValue->arena(), scratchArena, resolvingVariableHashes);
            if(!resolvedAssetValue)
                return MakeUnexpected(Failure{});

            const auto virtualPath = BuildItemVirtualPath(*baseVirtualPathText, variableName, scratchArena);
            if(!virtualPath){
                NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for variable '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(variableName));
                return MakeUnexpected(Failure{});
            }

            const auto assetType = ToName(DeclarationType(*itemDeclaration));
            if(!assetType)
                return MakeUnexpected(Failure{});
            assets.push_back(ExpandedAssetMetadata{ *assetType ? **assetType : s_NameNone, virtualPath->identity, Move(*resolvedAssetValue) });
        }
    }

    return assets;
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

    return IsCompositeRootAssetType(DeclarationType(*assetDeclaration));
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
    const Metascript::Value* const rootValue = doc.findVariable(DeclarationVariable(*rootDeclaration));
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
            const auto typeName = ToName(DeclarationType(declaration));
            if(!typeName || !*typeName){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': declaration type contains an embedded null"), PathToString<tchar>(nwbFilePath));
                return MakeUnexpected(Failure{});
            }
            if(**typeName == s_MetadataDeclarationTypeName)
                continue;

            const auto variableHash = DeclarationVariableHash(declaration);
            if(!variableHash){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': declaration variable contains an embedded null"), PathToString<tchar>(nwbFilePath));
                return MakeUnexpected(Failure{});
            }
            if(!assetVariableHashes.insert(*variableHash).second){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Composite asset '{}': duplicate declaration variable '{}'"),
                    PathToString<tchar>(nwbFilePath),
                    ConvertString(DeclarationVariable(declaration))
                );
                return MakeUnexpected(Failure{});
            }
            auxiliaryDeclarations.push_back(MakeNotNull(&declaration));
        }

        ScratchNameHashSet resolvingVariableHashes(declarationCapacity, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
        ScratchNameHashSet referencedAssetVariableHashes(declarationCapacity, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);

        auto resolvedRootValue = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            *declarations,
            *baseVirtualPathText,
            assetVariableHashes,
            *rootValue,
            rootValue->arena(),
            scratchArena,
            resolvingVariableHashes,
            &referencedAssetVariableHashes
        );
        if(!resolvedRootValue)
            return MakeUnexpected(Failure{});

        assets.reserve(AddSize(auxiliaryDeclarations.size(), 1u));
        for(const auto itemDeclaration : auxiliaryDeclarations){
            const AStringView variableName = DeclarationVariable(*itemDeclaration);
            const auto variableHash = DeclarationVariableHash(*itemDeclaration);
            if(!variableHash || referencedAssetVariableHashes.find(*variableHash) == referencedAssetVariableHashes.end()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Composite asset '{}': declaration '{}' is not referenced by 'asset'"),
                    PathToString<tchar>(nwbFilePath),
                    ConvertString(variableName)
                );
                return MakeUnexpected(Failure{});
            }

            const Metascript::Value* const itemValue = doc.findVariable(variableName);
            if(!itemValue){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': references missing variable '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(variableName));
                return MakeUnexpected(Failure{});
            }

            resolvingVariableHashes.insert(*variableHash);
            auto resolvedItemValue = ResolveAssetReferenceValue(
                nwbFilePath,
                doc,
                *declarations,
                *baseVirtualPathText,
                assetVariableHashes,
                *itemValue,
                itemValue->arena(),
                scratchArena,
                resolvingVariableHashes
            );
            resolvingVariableHashes.erase(*variableHash);
            if(!resolvedItemValue)
                return MakeUnexpected(Failure{});

            const auto virtualPath = BuildItemVirtualPath(*baseVirtualPathText, variableName, scratchArena);
            if(!virtualPath){
                NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': failed to build virtual path for variable '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(variableName));
                return MakeUnexpected(Failure{});
            }

            const auto assetType = ToName(DeclarationType(*itemDeclaration));
            if(!assetType)
                return MakeUnexpected(Failure{});
            assets.push_back(ExpandedAssetMetadata{ *assetType ? **assetType : s_NameNone, virtualPath->identity, Move(*resolvedItemValue) });
        }

        const auto rootAssetType = ToName(DeclarationType(*rootDeclaration));
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

