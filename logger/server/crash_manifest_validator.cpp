// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_manifest_validator.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace __hidden_crash_manifest_validator{

inline constexpr usize s_GeneratedJsonNeedleReserveSlack = ::NWB::Log::CrashValues::s_CrashIngestGeneratedJsonNeedleReserveSlack;

[[nodiscard]] static Expected<usize> FindGeneratedJsonValueCursor(Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    AString<Core::Alloc::ScratchArena> needle{scratchArena};
    needle.reserve(key.size() + s_GeneratedJsonNeedleReserveSlack);
    needle += '"';
    needle += key;
    needle += "\": ";

    const usize cursor = manifest.find(AStringView(needle.data(), needle.size()));
    if(cursor == AStringView::npos)
        return MakeUnexpected(Failure{});
    return cursor + needle.size();
}

template<typename ArenaT>
[[nodiscard]] static Expected<AString<ArenaT>> FindGeneratedJsonStringValue(ArenaT& arena, Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(scratchArena, manifest, key);
    if(!valueCursor)
        return MakeUnexpected(Failure{});
    usize cursor = *valueCursor;
    if(cursor >= manifest.size() || manifest[cursor] != '"')
        return MakeUnexpected(Failure{});
    ++cursor;

    usize reserveEnd = cursor;
    for(; reserveEnd < manifest.size(); ++reserveEnd){
        if(manifest[reserveEnd] == '\\' && reserveEnd + 1u < manifest.size()){
            ++reserveEnd;
            continue;
        }
        if(manifest[reserveEnd] == '"')
            break;
    }
    AString<ArenaT> parsedValue{arena};
    parsedValue.reserve(reserveEnd - cursor);
    for(; cursor < manifest.size(); ++cursor){
        const char ch = manifest[cursor];
        if(ch == '"'){
            return parsedValue;
        }

        if(ch != '\\'){
            parsedValue.push_back(ch);
            continue;
        }

        ++cursor;
        if(cursor >= manifest.size())
            return MakeUnexpected(Failure{});

        switch(manifest[cursor]){
        case '"':
        case '\\':
            parsedValue.push_back(manifest[cursor]);
            break;
        case 'n':
            parsedValue.push_back('\n');
            break;
        case 'r':
            parsedValue.push_back('\r');
            break;
        case 't':
            parsedValue.push_back('\t');
            break;
        default:
            return MakeUnexpected(Failure{});
        }
    }

    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<u64> FindGeneratedJsonUnsignedValue(Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(scratchArena, manifest, key);
    if(!valueCursor)
        return MakeUnexpected(Failure{});
    usize cursor = *valueCursor;

    const usize begin = cursor;
    while(cursor < manifest.size() && IsAsciiDigit(manifest[cursor]))
        ++cursor;
    if(cursor == begin)
        return MakeUnexpected(Failure{});

    return ParseU64(AStringView(manifest.data() + begin, cursor - begin));
}

[[nodiscard]] static Expected<b8> FindGeneratedJsonBoolValue(Core::Alloc::ScratchArena& scratchArena, const AStringView manifest, const AStringView key){
    const auto valueCursor = FindGeneratedJsonValueCursor(scratchArena, manifest, key);
    if(!valueCursor)
        return MakeUnexpected(Failure{});
    usize cursor = *valueCursor;

    if(cursor + s_TrueText.size() <= manifest.size() && AStringView(manifest.data() + cursor, s_TrueText.size()) == s_TrueText){
        return true;
    }
    if(cursor + s_FalseText.size() <= manifest.size() && AStringView(manifest.data() + cursor, s_FalseText.size()) == s_FalseText){
        return false;
    }
    return MakeUnexpected(Failure{});
}


};


Expected<CrashPackageSummary, CrashIngestFailure> CrashManifestValidator::validate(LogArena& arena, Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory){
    using namespace __hidden_crash_manifest_validator;
    CrashPackageSummary outSummary(arena);
    AString<Core::Alloc::ScratchArena> manifest{scratchArena};
    const auto read = ReadTextFile(packageDirectory / Core::Crash::PackageNames::s_ManifestFileName, manifest);
    if(!read)
        return MakeUnexpected(CrashIngestFailure{ .detail = "missing manifest.json", .code = read.error(), });

    const AStringView manifestText(manifest.data(), manifest.size());
    const auto manifestFormat = FindGeneratedJsonStringValue(scratchArena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestFormatKey);
    const auto requireString = [&scratchArena, manifestText](const AStringView key){
        return FindGeneratedJsonStringValue(scratchArena, scratchArena, manifestText, key);
    };
    const auto requireUnsigned = [&scratchArena, manifestText](const AStringView key){
        return FindGeneratedJsonUnsignedValue(scratchArena, manifestText, key);
    };
    const auto requireBool = [&scratchArena, manifestText](const AStringView key){
        return FindGeneratedJsonBoolValue(scratchArena, manifestText, key);
    };
    auto crashId = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestCrashIdKey);
    auto platform = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestPlatformKey);
    auto reasonKind = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestReasonKindKey);
    auto reasonCode = FindGeneratedJsonUnsignedValue(scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestReasonCodeKey);
    auto threadId = FindGeneratedJsonUnsignedValue(scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestThreadIdKey);
    auto event = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestEventKey);
    auto triggerExpression = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestTriggerExpressionKey);
    auto triggerMessage = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestTriggerMessageKey);
    auto triggerFile = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestTriggerFileKey);
    auto triggerLine = FindGeneratedJsonUnsignedValue(scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestTriggerLineKey);
    auto artifactStrategy = FindGeneratedJsonStringValue(arena, scratchArena, manifestText, Core::Crash::PackageNames::s_ManifestArtifactStrategyKey);
    if(
        !manifestFormat
        || !crashId
        || !requireString(Core::Crash::PackageNames::s_ManifestApplicationKey)
        || !requireString(Core::Crash::PackageNames::s_ManifestVersionKey)
        || !requireString(Core::Crash::PackageNames::s_ManifestBuildIdKey)
        || !requireString(Core::Crash::PackageNames::s_ManifestAbiKey)
        || !platform
        || !reasonKind
        || !reasonCode
        || !requireUnsigned(Core::Crash::PackageNames::s_ManifestProcessIdKey)
        || !threadId
        || !requireBool(Core::Crash::PackageNames::s_ManifestHasExceptionContextKey)
        || !requireUnsigned(Core::Crash::PackageNames::s_ManifestFaultAddressKey)
        || !requireUnsigned(Core::Crash::PackageNames::s_ManifestInstructionPointerKey)
        || !requireUnsigned(Core::Crash::PackageNames::s_ManifestStackPointerKey)
        || !requireUnsigned(Core::Crash::PackageNames::s_ManifestFramePointerKey)
        || !event
        || !requireString(Core::Crash::PackageNames::s_ManifestTriggerCategoryKey)
        || !triggerExpression
        || !triggerMessage
        || !triggerFile
        || !triggerLine
        || !requireString(Core::Crash::PackageNames::s_ManifestDumpDetailModeKey)
        || !artifactStrategy
        || !requireString(Core::Crash::PackageNames::s_ManifestHandlerLifetimeKey)
    ){
        return MakeUnexpected(CrashIngestFailure{ .detail = "manifest.json is missing required fields", });
    }

    outSummary.crashId = Move(*crashId);
    outSummary.platform = Move(*platform);
    outSummary.reasonKind = Move(*reasonKind);
    outSummary.reasonCode = *reasonCode;
    outSummary.threadId = *threadId;
    outSummary.event = Move(*event);
    outSummary.triggerExpression = Move(*triggerExpression);
    outSummary.triggerMessage = Move(*triggerMessage);
    outSummary.triggerFile = Move(*triggerFile);
    outSummary.triggerLine = *triggerLine;
    outSummary.artifactStrategy = Move(*artifactStrategy);

    if(*manifestFormat != Core::Crash::PackageNames::s_ManifestFormatValue){
        return MakeUnexpected(CrashIngestFailure{ .detail = "manifest.json has unsupported crash package format", });
    }
    if(outSummary.crashId.empty() || outSummary.platform.empty() || outSummary.reasonKind.empty() || outSummary.artifactStrategy.empty() || outSummary.event.empty()){
        return MakeUnexpected(CrashIngestFailure{ .detail = "manifest.json contains empty required fields", });
    }

    return outSummary;
}


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

