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
using ScratchString = AString<ScratchArena>;
using ScratchNameHashSet = HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView DeclarationType(const Metascript::Document::Declaration& declaration)noexcept{
    return AStringView(declaration.type.data(), declaration.type.size());
}

[[nodiscard]] static AStringView DeclarationVariable(const Metascript::Document::Declaration& declaration)noexcept{
    return AStringView(declaration.variable.data(), declaration.variable.size());
}

[[nodiscard]] static Metascript::MStringView DeclarationVariableMetaView(const Metascript::Document::Declaration& declaration)noexcept{
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

// Tiny documents keep inline lookups; larger ones share one exact-text index.
class DeclarationLookup final : NoCopy{
private:
    using Declaration = Metascript::Document::Declaration;
    using LookupTable = HashMap<AStringView, const Declaration*, ScratchArena>;


private:
    static constexpr usize s_InlineCapacity = 16u;


public:
    DeclarationLookup(const Metascript::Document& doc, ScratchArena& scratchArena){
        if(doc.declarations().size() > s_InlineCapacity){
            m_table.emplace(
                AddSize(doc.declarations().size(), doc.declarations().size()),
                Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena
            );
        }
        for(const Declaration& declaration : doc.declarations()){
            if(IsAssetBunchType(DeclarationType(declaration)))
                continue;
            if(m_table)
                m_table->try_emplace(DeclarationVariable(declaration), &declaration);
            else
                m_inline[m_inlineCount++] = &declaration;
        }
    }


public:
    [[nodiscard]] const Declaration* find(const AStringView reference)const{
        if(m_table){
            const auto found = m_table->find(reference);
            return found == m_table->end() ? nullptr : found.value();
        }
        for(usize index = 0u; index < m_inlineCount; ++index){
            if(DeclarationVariable(*m_inline[index]) == reference)
                return m_inline[index];
        }
        return nullptr;
    }


private:
    Array<const Declaration*, s_InlineCapacity> m_inline;
    usize m_inlineCount = 0u;
    Optional<LookupTable> m_table;
};


[[nodiscard]] static const Metascript::Document::Declaration* FindBunchItemDeclaration(
    const Path& nwbFilePath,
    const DeclarationLookup& declarations,
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
    if(const Metascript::Document::Declaration* declaration = declarations.find(itemReference))
        return declaration;

    NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': item {} references undeclared asset variable '{}'")
        , PathToString<tchar>(nwbFilePath)
        , itemIndex
        , StringConvert(itemReference)
    );
    return nullptr;
}

struct ItemVirtualPath{
    Name identity;
    ScratchString text;
};

[[nodiscard]] static Expected<ItemVirtualPath> BuildItemVirtualPath(
    const AStringView baseVirtualPath,
    const AStringView variableName,
    ScratchArena& scratchArena
){
    ScratchString virtualPathText(scratchArena);
    virtualPathText.reserve(AddSize(AddSize(baseVirtualPath.size(), 1u), variableName.size()));

    virtualPathText.append(baseVirtualPath.data(), baseVirtualPath.size());
    virtualPathText += '/';
    virtualPathText.append(variableName.data(), variableName.size());

    const Name virtualPath(AStringView(virtualPathText.data(), virtualPathText.size()));
    if(virtualPath == s_NameNone)
        return MakeUnexpected(Failure{});
    return ItemVirtualPath{ virtualPath, Move(virtualPathText) };
}

[[nodiscard]] static NameHash DeclarationVariableHash(const Metascript::Document::Declaration& declaration)noexcept{
    return ComputeNameHash(DeclarationVariable(declaration));
}

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReferenceValue(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena
);

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReferenceList(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena
){
    Metascript::Value result(valueArena);
    result.makeList();
    for(const Metascript::Value& item : source.asList()){
        auto resolved = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            item,
            valueArena,
            scratchArena
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
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena
){
    Metascript::Value result(valueArena);
    result.makeMap();
    for(const auto& [key, value] : source.asMap()){
        Metascript::Value& field = result.field(Metascript::MStringView(key.data(), key.size()));
        auto resolved = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            value,
            valueArena,
            scratchArena
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
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena
){
    const Metascript::MStringView reference = source.asReference();
    const Metascript::Document::Declaration* declaration = declarations.find(reference);
    if(!declaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' does not target a declared asset")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(AStringView(reference.data(), reference.size()))
        );
        return MakeUnexpected(Failure{});
    }

    const NameHash variableHash = DeclarationVariableHash(*declaration);
    if(assetVariableHashes.find(variableHash) == assetVariableHashes.end()){
        if(!resolvingVariableHashes.insert(variableHash).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': cyclic local metadata reference '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(AStringView(reference.data(), reference.size()))
            );
            return MakeUnexpected(Failure{});
        }

        const Metascript::Value* localValue = doc.findVariable(DeclarationVariableMetaView(*declaration));
        if(!localValue){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' targets a missing local variable")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(AStringView(reference.data(), reference.size()))
            );
            return MakeUnexpected(Failure{});
        }

        auto resolved = ResolveAssetReferenceValue(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            *localValue,
            valueArena,
            scratchArena
        );
        resolvingVariableHashes.erase(variableHash);
        return resolved;
    }

    const auto virtualPath = BuildItemVirtualPath(baseVirtualPath, DeclarationVariable(*declaration), scratchArena);
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for reference '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(AStringView(reference.data(), reference.size()))
        );
        return MakeUnexpected(Failure{});
    }

    Metascript::Value result(valueArena);
    result.setString(Metascript::MStringView(virtualPath->text.data(), virtualPath->text.size()));
    return result;
}

[[nodiscard]] static Expected<Metascript::Value> ResolveAssetReferenceValue(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    const DeclarationLookup& declarations,
    const AStringView baseVirtualPath,
    const ScratchNameHashSet& assetVariableHashes,
    ScratchNameHashSet& resolvingVariableHashes,
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchArena& scratchArena
){
    if(source.isReference())
        return ResolveAssetReference(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            source,
            valueArena,
            scratchArena
        );
    if(source.isList())
        return ResolveAssetReferenceList(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            source,
            valueArena,
            scratchArena
        );
    if(source.isMap())
        return ResolveAssetReferenceMap(
            nwbFilePath,
            doc,
            declarations,
            baseVirtualPath,
            assetVariableHashes,
            resolvingVariableHashes,
            source,
            valueArena,
            scratchArena
        );

    Metascript::Value result(valueArena);
    result = source;
    return result;
}

static Expected<ExpandedAssetMetadataVector, Core::Assets::AssetBunchExpandFailure::Enum> ExpandAssetBunchForAssetCook(
    const Core::Assets::AssetBunchExpandContext& context
){
    if(!HasAssetBunchDeclaration(context.doc))
        return MakeUnexpected(Core::Assets::AssetBunchExpandFailure::Unsupported);

    auto assets = ExpandAssetBunch(
        context.assetRoot,
        context.virtualRoot,
        context.nwbFilePath,
        context.doc,
        context.scratchArena
    );
    if(!assets)
        return MakeUnexpected(Core::Assets::AssetBunchExpandFailure::Error);
    return Move(*assets);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetBunchExpanderAutoRegistrar s_AssetBunchExpanderRegistrar(&ExpandAssetBunchForAssetCook);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsBunchCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ExpandedAssetMetadataVector> ExpandAssetBunch(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    ScratchArena& scratchArena
){
    using namespace __hidden_assets_bunch_cook;

    ExpandedAssetMetadataVector assets(scratchArena);

    const Metascript::Document::Declaration* bunchDeclaration = nullptr;
    for(const Metascript::Document::Declaration& declaration : doc.declarations()){
        if(!IsAssetBunchType(DeclarationType(declaration)))
            continue;
        if(bunchDeclaration){
            NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': multiple asset_bunch declarations are not allowed")
                , PathToString<tchar>(nwbFilePath)
            );
            return MakeUnexpected(Failure{});
        }
        bunchDeclaration = &declaration;
    }
    if(!bunchDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': missing asset_bunch declaration")
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    const Metascript::Value* bunchValue = doc.findVariable(DeclarationVariableMetaView(*bunchDeclaration));
    if(!bunchValue || !bunchValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': declaration must be initialized with a list")
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    const auto& list = bunchValue->asList();
    if(list.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': list must contain at least one asset")
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    assets.reserve(list.size());

    // Debug iterator proxies allocate during moves; release temporaries before transferring the result.
    {
        auto baseVirtualPathText = Core::Assets::BuildDerivedAssetVirtualPath(scratchArena, assetRoot, virtualRoot, nwbFilePath);
        if(!baseVirtualPathText)
            return MakeUnexpected(Failure{});

        ScratchNameHashSet usedVariables(
            AddSize(list.size(), list.size()),
            Hasher<NameHash>(),
            EqualTo<NameHash>(),
            scratchArena
        );

        const DeclarationLookup declarations(doc, scratchArena);
        Vector<const Metascript::Document::Declaration*, ScratchArena> itemDeclarations(scratchArena);
        itemDeclarations.reserve(list.size());
        for(usize itemIndex = 0u; itemIndex < list.size(); ++itemIndex){
            const Metascript::Document::Declaration* itemDeclaration = FindBunchItemDeclaration(
                nwbFilePath, declarations, list[itemIndex], itemIndex
            );
            if(!itemDeclaration)
                return MakeUnexpected(Failure{});

            const AStringView variableName = DeclarationVariable(*itemDeclaration);
            if(!usedVariables.insert(ComputeNameHash(variableName)).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': variable '{}' is listed more than once")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(variableName)
                );
                return MakeUnexpected(Failure{});
            }

            itemDeclarations.push_back(itemDeclaration);
        }

        ScratchNameHashSet resolvingVariableHashes(
            AddSize(doc.declarations().size(), doc.declarations().size()),
            Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena
        );

        for(const Metascript::Document::Declaration* itemDeclaration : itemDeclarations){
            const AStringView variableName = DeclarationVariable(*itemDeclaration);
            const Metascript::Value* assetValue = doc.findVariable(DeclarationVariableMetaView(*itemDeclaration));
            if(!assetValue){
                NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': references missing variable '{}'")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(variableName)
                );
                return MakeUnexpected(Failure{});
            }

            auto resolvedAssetValue = ResolveAssetReferenceValue(
                nwbFilePath,
                doc,
                declarations,
                AStringView(baseVirtualPathText->data(), baseVirtualPathText->size()),
                usedVariables,
                resolvingVariableHashes,
                *assetValue,
                assetValue->arena(),
                scratchArena
            );
            if(!resolvedAssetValue)
                return MakeUnexpected(Failure{});

            const auto virtualPath = BuildItemVirtualPath(
                AStringView(baseVirtualPathText->data(), baseVirtualPathText->size()),
                variableName,
                scratchArena
            );
            if(!virtualPath){
                NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for variable '{}'")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(variableName)
                );
                return MakeUnexpected(Failure{});
            }

            assets.push_back(ExpandedAssetMetadata{
                ToName(DeclarationType(*itemDeclaration)),
                virtualPath->identity,
                Move(*resolvedAssetValue)
            });
        }
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

