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


using AutoMetadataParser = CookMetadataDetail::AutoMetadataParser;

class RegistrationTextPool final : NoCopy{
public:
    RegistrationTextPool()
        : m_arena(AssetsArenaScope::s_MetadataRegistrationTextArena)
        , m_values(m_arena)
    {}

    [[nodiscard]] AStringView intern(const AStringView text){
        ScopedLock lock(m_mutex);
        for(const AssetString& value : m_values){
            if(AStringView(value) == text)
                return AStringView(value);
        }
        m_values.emplace_back(text, m_arena);
        return AStringView(m_values.back());
    }


private:
    Futex m_mutex;
    AssetArena m_arena;
    Deque<AssetString, AssetArena> m_values;
};

[[nodiscard]] static AStringView StableRegistrationText(const AStringView text){
    static RegistrationTextPool s_TextPool;
    return s_TextPool.intern(text);
}


template<typename ContextT, typename FunctionT>
[[nodiscard]] static AssetMetadataParseResult::Enum TryMetadataParsersSpan(
    const Span<const AutoMetadataParser> parsers,
    FunctionT AutoMetadataParser::* const member,
    ContextT& context
){
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

template<typename ContextT, typename FunctionT>
[[nodiscard]] static AssetMetadataParseResult::Enum TryAutoCollectedMetadataParsers(FunctionT AutoMetadataParser::* const member, ContextT& context){
    Vector<AutoMetadataParser, Core::Alloc::ScratchArena> parsers{context.scratchArena};
    QueryAssetAutoRegistrationQueue<AutoMetadataParser, AssetsArenaScope::s_MetadataParserQueueArena>().copyTo(parsers);
    return TryMetadataParsersSpan(Span<const AutoMetadataParser>(parsers.data(), parsers.size()), member, context);
}

[[nodiscard]] static Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryBunchExpandersSpan(
    const Span<const NotNull<AssetBunchExpandFunction>> functions,
    const AssetBunchExpandContext& context
){
    for(const NotNull<AssetBunchExpandFunction> function : functions){
        auto result = (*function)(context);
        if(!result && result.error() == AssetBunchExpandFailure::Unsupported)
            continue;
        return result;
    }

    return MakeUnexpected(AssetBunchExpandFailure::Unsupported);
}

[[nodiscard]] static b8 ContainsNonStandaloneAssetType(const Span<const Name> types, const Name& assetType)noexcept{
    for(const Name& entry : types){
        if(entry == assetType)
            return true;
    }
    return false;
}

[[nodiscard]] static b8 MatchesNonStandaloneFirstToken(
    const Span<const AStringView> tokens,
    const Span<const AStringView> suffixes,
    const AStringView firstToken
)noexcept{
    for(const AStringView token : tokens){
        if(token == firstToken)
            return true;
    }
    for(const AStringView suffix : suffixes){
        if(firstToken.ends_with(suffix))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CopyPairedSidecarRules(Vector<AssetPairedSidecarRule, ScratchArena>& outRules){
    QueryAssetAutoRegistrationQueue<AssetPairedSidecarRule, AssetsArenaScope::s_PairedSidecarRuleQueueArena>().copyTo(outRules);
}

void CaptureMetadataRegistrySnapshot(CookMetadataRegistrySnapshot& outSnapshot){
    QueryAssetAutoRegistrationQueue<AutoMetadataParser, AssetsArenaScope::s_MetadataParserQueueArena>().copyTo(outSnapshot.metadataParsers);
    QueryAssetAutoRegistrationQueue<NotNull<AssetBunchExpandFunction>, AssetsArenaScope::s_BunchExpanderQueueArena>().copyTo(outSnapshot.bunchExpanders);
    QueryAssetAutoRegistrationQueue<Name, AssetsArenaScope::s_NonStandaloneTypeQueueArena>().copyTo(outSnapshot.nonStandaloneTypes);
    QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenQueueArena>().copyTo(outSnapshot.nonStandaloneTokens);
    QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenSuffixQueueArena>().copyTo(outSnapshot.nonStandaloneTokenSuffixes);
    QueryAssetAutoRegistrationQueue<AssetPairedSidecarRule, AssetsArenaScope::s_PairedSidecarRuleQueueArena>().copyTo(outSnapshot.pairedSidecarRules);
    QueryAssetAutoRegistrationQueue<NotNull<CookExternalSourceCollectorFunction>, AssetsArenaScope::s_ExternalSourceCollectorQueueArena>().copyTo(outSnapshot.externalSourceCollectors);
}

AssetMetadataParseResult::Enum TryDocumentMetadataParsers(const CookMetadataRegistrySnapshot& snapshot, AssetDocumentMetadataParseContext& context){
    return __hidden_cook_metadata_registry::TryMetadataParsersSpan(
        Span<const AutoMetadataParser>(snapshot.metadataParsers.data(), snapshot.metadataParsers.size()),
        &AutoMetadataParser::documentFunction,
        context
    );
}

AssetMetadataParseResult::Enum TryValueMetadataParsers(const CookMetadataRegistrySnapshot& snapshot, AssetValueMetadataParseContext& context){
    return __hidden_cook_metadata_registry::TryMetadataParsersSpan(
        Span<const AutoMetadataParser>(snapshot.metadataParsers.data(), snapshot.metadataParsers.size()),
        &AutoMetadataParser::valueFunction,
        context
    );
}

Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAssetBunchExpanders(const CookMetadataRegistrySnapshot& snapshot, const AssetBunchExpandContext& context){
    return __hidden_cook_metadata_registry::TryBunchExpandersSpan(
        Span<const NotNull<AssetBunchExpandFunction>>(snapshot.bunchExpanders.data(), snapshot.bunchExpanders.size()),
        context
    );
}

b8 IsNonStandaloneAssetType(const CookMetadataRegistrySnapshot& snapshot, const Name& assetType)noexcept{
    if(!assetType)
        return false;
    return __hidden_cook_metadata_registry::ContainsNonStandaloneAssetType(
        Span<const Name>(snapshot.nonStandaloneTypes.data(), snapshot.nonStandaloneTypes.size()),
        assetType
    );
}

b8 IsLikelyNonStandaloneFirstToken(const CookMetadataRegistrySnapshot& snapshot, const AStringView firstToken)noexcept{
    if(firstToken.empty())
        return false;
    return __hidden_cook_metadata_registry::MatchesNonStandaloneFirstToken(
        Span<const AStringView>(snapshot.nonStandaloneTokens.data(), snapshot.nonStandaloneTokens.size()),
        Span<const AStringView>(snapshot.nonStandaloneTokenSuffixes.data(), snapshot.nonStandaloneTokenSuffixes.size()),
        firstToken
    );
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

CookExternalSourceCollectorAutoRegistrar::CookExternalSourceCollectorAutoRegistrar(const CookExternalSourceCollectorFunction function){
    RegisterCookExternalSourceCollector(function);
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

    Array<char, NameDetail::s_SymbolTextBufferLength> resolvedToken = {};
    if(tokenText.empty())
        CopyFixedBuffer(resolvedToken, assetType.resolvedText());
    const AStringView effectiveToken = !tokenText.empty() ? tokenText : FixedBufferView(resolvedToken);
    if(!effectiveToken.empty()){
        QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenQueueArena>().appendUnique(
            __hidden_cook_metadata_registry::StableRegistrationText(effectiveToken),
            [](const AStringView lhs, const AStringView rhs)noexcept{ return lhs == rhs; }
        );
    }
}

void RegisterNonStandaloneFirstTokenSuffix(const AStringView tokenSuffix){
    if(tokenSuffix.empty())
        return;

    QueryAssetAutoRegistrationQueue<AStringView, AssetsArenaScope::s_NonStandaloneTokenSuffixQueueArena>().appendUnique(
        __hidden_cook_metadata_registry::StableRegistrationText(tokenSuffix),
        [](const AStringView lhs, const AStringView rhs)noexcept{ return lhs == rhs; }
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
    if(rule.sidecarExtension.empty())
        return;

    QueryAssetAutoRegistrationQueue<AssetPairedSidecarRule, AssetsArenaScope::s_PairedSidecarRuleQueueArena>().appendUnique(
        AssetPairedSidecarRule{ __hidden_cook_metadata_registry::StableRegistrationText(rule.sidecarExtension) },
        [](const AssetPairedSidecarRule& lhs, const AssetPairedSidecarRule& rhs)noexcept{
            return lhs.sidecarExtension == rhs.sidecarExtension;
        }
    );
}

void RegisterCookExternalSourceCollector(const CookExternalSourceCollectorFunction function){
    if(function == nullptr)
        return;

    QueryAssetAutoRegistrationQueue<NotNull<CookExternalSourceCollectorFunction>, AssetsArenaScope::s_ExternalSourceCollectorQueueArena>().appendUnique(
        NotNull(function),
        [](const NotNull<CookExternalSourceCollectorFunction> lhs, const NotNull<CookExternalSourceCollectorFunction> rhs)noexcept{
            return lhs.get() == rhs.get();
        }
    );
}

AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context){
    return __hidden_cook_metadata_registry::TryAutoCollectedMetadataParsers(&__hidden_cook_metadata_registry::AutoMetadataParser::documentFunction, context);
}

Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAutoCollectedAssetBunchExpanders(const AssetBunchExpandContext& context){
    Vector<NotNull<AssetBunchExpandFunction>, Core::Alloc::ScratchArena> functions{context.scratchArena};
    QueryAssetAutoRegistrationQueue<NotNull<AssetBunchExpandFunction>, AssetsArenaScope::s_BunchExpanderQueueArena>().copyTo(functions);
    return __hidden_cook_metadata_registry::TryBunchExpandersSpan(Span<const NotNull<AssetBunchExpandFunction>>(functions.data(), functions.size()), context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

