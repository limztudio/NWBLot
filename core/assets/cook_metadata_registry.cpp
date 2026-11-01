// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_internal.h"
#include "arena_names.h"

#include <global/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_metadata_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoMetadataParser{
    AssetDocumentMetadataParseFunction documentFunction = nullptr;
    AssetValueMetadataParseFunction valueFunction = nullptr;
};


template<typename ContextT, typename FunctionT>
[[nodiscard]] static AssetMetadataParseResult::Enum TryAutoCollectedMetadataParsers(FunctionT AutoMetadataParser::* const member, ContextT& context){
    Vector<AutoMetadataParser, Core::Alloc::ScratchArena> parsers{context.scratchArena};
    QueryAssetAutoRegistrationQueue<AutoMetadataParser, AssetsArenaScope::s_MetadataParserQueueArena>().copyTo(parsers);

    for(const AutoMetadataParser& parser : parsers){
        const FunctionT function = parser.*member;
        if(function == nullptr)
            continue;

        const AssetMetadataParseResult::Enum result = function(context);
        if(result == AssetMetadataParseResult::Unsupported)
            continue;
        return result;
    }

    return AssetMetadataParseResult::Unsupported;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CopyPairedSidecarRules(Vector<AssetPairedSidecarRule, ScratchArena>& outRules){
    QueryAssetAutoRegistrationQueue<AssetPairedSidecarRule, AssetsArenaScope::s_PairedSidecarRuleQueueArena>().copyTo(outRules);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetMetadataParserAutoRegistrar::AssetMetadataParserAutoRegistrar(const AssetDocumentMetadataParseFunction documentFunction, const AssetValueMetadataParseFunction valueFunction){
    if(documentFunction == nullptr && valueFunction == nullptr)
        return;

    auto& queue = QueryAssetAutoRegistrationQueue<__hidden_cook_metadata_registry::AutoMetadataParser, AssetsArenaScope::s_MetadataParserQueueArena>();
    queue.appendUnique(
        __hidden_cook_metadata_registry::AutoMetadataParser{ documentFunction, valueFunction },
        [](const __hidden_cook_metadata_registry::AutoMetadataParser& lhs, const __hidden_cook_metadata_registry::AutoMetadataParser& rhs)noexcept{
            return lhs.documentFunction == rhs.documentFunction && lhs.valueFunction == rhs.valueFunction;
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetBunchExpanderAutoRegistrar::AssetBunchExpanderAutoRegistrar(const AssetBunchExpandFunction function){
    ::RegisterAutoFactory(QueryAssetAutoRegistrationQueue<NotNull<AssetBunchExpandFunction>, AssetsArenaScope::s_BunchExpanderQueueArena>(), function);
}

CompositeRootAssetTypeAutoRegistrar::CompositeRootAssetTypeAutoRegistrar(const Name& assetType){
    RegisterCompositeRootAssetType(assetType);
}

NonStandaloneAssetTypeAutoRegistrar::NonStandaloneAssetTypeAutoRegistrar(const Name& assetType, const AStringView tokenText){
    RegisterNonStandaloneAssetType(assetType, tokenText);
}

NonStandaloneFirstTokenSuffixAutoRegistrar::NonStandaloneFirstTokenSuffixAutoRegistrar(const AStringView tokenSuffix){
    RegisterNonStandaloneFirstTokenSuffix(tokenSuffix);
}

AssetPairedSidecarRuleAutoRegistrar::AssetPairedSidecarRuleAutoRegistrar(const AssetPairedSidecarRule& rule){
    RegisterAssetPairedSidecarRule(rule);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegisterCompositeRootAssetType(const Name& assetType){
    if(!assetType)
        return;

    QueryAssetAutoRegistrationQueue<Name, AssetsArenaScope::s_CompositeRootTypeQueueArena>().appendUnique(
        assetType,
        [](const Name& lhs, const Name& rhs)noexcept{ return lhs == rhs; }
    );
}

b8 IsCompositeRootAssetType(const Name& assetType){
    if(!assetType)
        return false;

    return QueryAssetAutoRegistrationQueue<Name, AssetsArenaScope::s_CompositeRootTypeQueueArena>().contains(
        assetType,
        [](const Name& lhs, const Name& rhs)noexcept{ return lhs == rhs; }
    );
}

void RegisterNonStandaloneAssetType(const Name& assetType, const AStringView tokenText){
    if(assetType){
        QueryAssetAutoRegistrationQueue<Name, AssetsArenaScope::s_NonStandaloneTypeQueueArena>().appendUnique(
            assetType,
            [](const Name& lhs, const Name& rhs)noexcept{ return lhs == rhs; }
        );
    }

    const AStringView effectiveToken = !tokenText.empty() ? tokenText : assetType.resolvedText();
    if(!effectiveToken.empty()){
        QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenQueueArena>().appendUnique(
            effectiveToken,
            [](const AStringView lhs, const AStringView rhs)noexcept{ return lhs == rhs; }
        );
    }
}

void RegisterNonStandaloneFirstTokenSuffix(const AStringView tokenSuffix){
    if(tokenSuffix.empty())
        return;

    QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenSuffixQueueArena>().appendUnique(
        tokenSuffix,
        [](const AStringView lhs, const AStringView rhs)noexcept{ return lhs == rhs; }
    );
}

b8 IsNonStandaloneAssetType(const Name& assetType){
    if(!assetType)
        return false;

    return QueryAssetAutoRegistrationQueue<Name, AssetsArenaScope::s_NonStandaloneTypeQueueArena>().contains(
        assetType,
        [](const Name& lhs, const Name& rhs)noexcept{ return lhs == rhs; }
    );
}

b8 IsLikelyNonStandaloneFirstToken(const AStringView firstToken){
    if(firstToken.empty())
        return false;

    if(QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenQueueArena>().contains(
        firstToken,
        [](const AStringView lhs, const AStringView rhs)noexcept{ return lhs == rhs; }
    ))
        return true;

    return QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenSuffixQueueArena>().contains(
        firstToken,
        [](const AStringView suffix, const AStringView token)noexcept{ return token.ends_with(suffix); }
    );
}

void RegisterAssetPairedSidecarRule(const AssetPairedSidecarRule& rule){
    if(rule.sidecarExtension.empty() || (rule.firstToken.empty() && rule.bunchScanKeyword.empty()))
        return;

    QueryAssetAutoRegistrationQueue<AssetPairedSidecarRule, AssetsArenaScope::s_PairedSidecarRuleQueueArena>().appendUnique(
        rule,
        [](const AssetPairedSidecarRule& lhs, const AssetPairedSidecarRule& rhs)noexcept{
            return lhs.firstToken == rhs.firstToken && lhs.sidecarExtension == rhs.sidecarExtension && lhs.bunchScanKeyword == rhs.bunchScanKeyword;
        }
    );
}

AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context){
    return __hidden_cook_metadata_registry::TryAutoCollectedMetadataParsers(&__hidden_cook_metadata_registry::AutoMetadataParser::documentFunction, context);
}

AssetMetadataParseResult::Enum TryAutoCollectedValueMetadataParsers(AssetValueMetadataParseContext& context){
    return __hidden_cook_metadata_registry::TryAutoCollectedMetadataParsers(&__hidden_cook_metadata_registry::AutoMetadataParser::valueFunction, context);
}

Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAutoCollectedAssetBunchExpanders(const AssetBunchExpandContext& context){
    Vector<NotNull<AssetBunchExpandFunction>, Core::Alloc::ScratchArena> functions{context.scratchArena};
    QueryAssetAutoRegistrationQueue<NotNull<AssetBunchExpandFunction>, AssetsArenaScope::s_BunchExpanderQueueArena>().copyTo(functions);

    for(const NotNull<AssetBunchExpandFunction> function : functions){
        auto result = (*function)(context);
        if(!result && result.error() == AssetBunchExpandFailure::Unsupported)
            continue;
        return result;
    }

    return MakeUnexpected(AssetBunchExpandFailure::Unsupported);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

