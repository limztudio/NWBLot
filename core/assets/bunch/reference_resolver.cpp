// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reference_resolver.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsBunchCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Metascript = Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<b8, NameTextError::Enum> IsAssetBunchType(const AStringView typeName){
    const auto name = ToName(typeName);
    if(!name)
        return MakeUnexpected(name.error());
    return *name && **name == s_AssetBunchTypeName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeclarationLookup, NameTextError::Enum> DeclarationLookup::Create(const Metascript::Document& doc, ScratchArena& scratchArena){
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

const DeclarationLookup::Declaration* DeclarationLookup::find(const AStringView reference)const noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ItemVirtualPath> BuildItemVirtualPath(const AStringView baseVirtualPath, const AStringView variableName, ScratchArena& scratchArena){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReferenceResolutionContext::ReferenceResolutionContext(
    const Path& inNwbFilePath,
    const Metascript::Document& inDoc,
    const DeclarationLookup& inDeclarations,
    const AStringView inBaseVirtualPath,
    const ScratchNameHashSet& inAssetVariableHashes,
    ScratchArena& inScratchArena
)
    : m_nwbFilePath(inNwbFilePath)
    , m_doc(inDoc)
    , m_declarations(inDeclarations)
    , m_baseVirtualPath(inBaseVirtualPath)
    , m_assetVariableHashes(inAssetVariableHashes)
    , m_scratchArena(inScratchArena)
    , m_resolvingVariableHashes(AddSize(inDoc.declarations().size(), inDoc.declarations().size()), Hasher<NameHash>(), EqualTo<NameHash>(), inScratchArena)
{}

Expected<Metascript::Value> ReferenceResolutionContext::resolveList(
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

Expected<Metascript::Value> ReferenceResolutionContext::resolveMap(
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

Expected<Metascript::Value> ReferenceResolutionContext::resolveDeclaredVariableValue(
    const Metascript::Document::Declaration& declaration,
    const NameHash variableHash,
    const Metascript::MStringView reference,
    const TStringView diagnosticPrefix,
    const TStringView cycleMessage,
    Metascript::MetaArena& valueArena,
    ScratchNameHashSet* const referencedAssetVariableHashes
){
    if(!m_resolvingVariableHashes.insert(variableHash).second){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': {} '{}'"), diagnosticPrefix, PathToString<tchar>(m_nwbFilePath), cycleMessage, ConvertString(reference));
        return MakeUnexpected(Failure{});
    }
    const Metascript::Value* const variableValue = m_doc.findVariable(declaration.variable);
    if(!variableValue){
        m_resolvingVariableHashes.erase(variableHash);
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': reference '{}' targets a missing local variable"), diagnosticPrefix, PathToString<tchar>(m_nwbFilePath), ConvertString(reference));
        return MakeUnexpected(Failure{});
    }
    auto resolved = resolveValue(*variableValue, valueArena, referencedAssetVariableHashes);
    m_resolvingVariableHashes.erase(variableHash);
    return resolved;
}

Expected<Metascript::Value> ReferenceResolutionContext::resolveReference(
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchNameHashSet* const referencedAssetVariableHashes
){
    const Metascript::MStringView reference = source.asReference();
    const Metascript::Document::Declaration* declaration = m_declarations.find(reference);
    if(!declaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference '{}' does not target a declared asset"), PathToString<tchar>(m_nwbFilePath), ConvertString(reference));
        return MakeUnexpected(Failure{});
    }

    const auto variableHashResult = ComputeNameHash(AStringView(declaration->variable));
    if(!variableHashResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': reference variable contains an embedded null"), PathToString<tchar>(m_nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    const NameHash variableHash = *variableHashResult;
    if(m_assetVariableHashes.find(variableHash) == m_assetVariableHashes.end()){
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
        if(m_resolvingVariableHashes.find(variableHash) != m_resolvingVariableHashes.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Composite asset '{}': cyclic asset reference '{}'"), PathToString<tchar>(m_nwbFilePath), ConvertString(reference));
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

    const auto virtualPath = BuildItemVirtualPath(m_baseVirtualPath, declaration->variable, m_scratchArena);
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Asset bunch '{}': failed to build virtual path for reference '{}'"), PathToString<tchar>(m_nwbFilePath), ConvertString(reference));
        return MakeUnexpected(Failure{});
    }

    Metascript::Value result(valueArena);
    result.setString(virtualPath->text);
    return result;
}

Expected<Metascript::Value> ReferenceResolutionContext::resolveValue(
    const Metascript::Value& source,
    Metascript::MetaArena& valueArena,
    ScratchNameHashSet* const referencedAssetVariableHashes
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

Expected<ExpandedAssetMetadata> ReferenceResolutionContext::expandItemDeclaration(
    const Metascript::Document::Declaration& itemDeclaration,
    const TStringView diagnosticPrefix,
    const NameHash* const activeItemHash
){
    const AStringView variableName = itemDeclaration.variable;
    const Metascript::Value* const itemValue = m_doc.findVariable(variableName);
    if(!itemValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': references missing variable '{}'"), diagnosticPrefix, PathToString<tchar>(m_nwbFilePath), ConvertString(variableName));
        return MakeUnexpected(Failure{});
    }

    if(activeItemHash)
        m_resolvingVariableHashes.insert(*activeItemHash);
    auto resolvedItemValue = resolveValue(*itemValue, itemValue->arena());
    if(activeItemHash)
        m_resolvingVariableHashes.erase(*activeItemHash);
    if(!resolvedItemValue)
        return MakeUnexpected(Failure{});

    const auto virtualPath = BuildItemVirtualPath(m_baseVirtualPath, variableName, m_scratchArena);
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to build virtual path for variable '{}'"), diagnosticPrefix, PathToString<tchar>(m_nwbFilePath), ConvertString(variableName));
        return MakeUnexpected(Failure{});
    }

    const auto assetType = ToName(itemDeclaration.type);
    if(!assetType)
        return MakeUnexpected(Failure{});
    return ExpandedAssetMetadata{ *assetType ? **assetType : s_NameNone, virtualPath->identity, Move(*resolvedItemValue) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

