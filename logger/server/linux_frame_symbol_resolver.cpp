// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_frame_symbol_resolver.h"
#include "crash_symbolicate_internal.h"

#include <global/overflow.h>
#include <global/process_execution.h>
#include <global/process_capture.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
namespace __hidden_linux_frame_symbol_resolver{


inline constexpr AStringView s_LinuxUnknownSymbolText = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLinuxUnknownSymbolText;
inline constexpr AStringView s_LinuxUnknownLocationPrefix = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLinuxUnknownLocationPrefix;
inline constexpr AStringView s_LinuxSymbolFrameSeparator = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLinuxSymbolFrameSeparator;
inline constexpr AStringView s_LinuxSymbolLocationSeparator = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLinuxSymbolLocationSeparator;
inline constexpr AStringView s_LinuxSymbolLocationBarePrefix = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLinuxSymbolLocationBarePrefix;
inline constexpr AStringView s_LlvmSymbolizerTool = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLlvmSymbolizerTool;
inline constexpr AStringView s_Addr2LineTool = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxAddr2LineTool;
inline constexpr AStringView s_LlvmSymbolizerObjectPrefix = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLlvmSymbolizerObjectPrefix;
inline constexpr AStringView s_LlvmSymbolizerDemangleFlag = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLlvmSymbolizerDemangleFlag;
inline constexpr AStringView s_LlvmSymbolizerFunctionsFlag = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLlvmSymbolizerFunctionsFlag;
inline constexpr AStringView s_LlvmSymbolizerInliningFlag = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxLlvmSymbolizerInliningFlag;
inline constexpr AStringView s_Addr2LineFunctionsFlag = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxAddr2LineFunctionsFlag;
inline constexpr AStringView s_Addr2LineDemangleFlag = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxAddr2LineDemangleFlag;
inline constexpr AStringView s_Addr2LineInlineFlag = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxAddr2LineInlineFlag;
inline constexpr AStringView s_Addr2LineExeFlag = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxAddr2LineExeFlag;
inline constexpr usize s_SymbolizerResultReserveMultiplier = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxSymbolizerResultReserveMultiplier;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsUnknownSymbolLine(const AStringView line)noexcept{
    const AStringView trimmed = TrimView(line);
    return trimmed.empty() || trimmed == s_LinuxUnknownSymbolText || StartsWith(trimmed, s_LinuxUnknownLocationPrefix);
}

[[nodiscard]] static Expected<AString<Core::Alloc::ScratchArena>> ExtractSymbolizerResult(const AStringView outputText, Core::Alloc::ScratchArena& arena){
    AString<Core::Alloc::ScratchArena> outSymbol(arena);
    outSymbol.reserve(AddSize(outputText.size(), SizeOf<s_SymbolizerResultReserveMultiplier>(outputText.size())));

    usize cursor = 0u;
    while(const auto function = NextTrimmedTextLine(outputText, cursor)){
        const auto location = NextTrimmedTextLine(outputText, cursor);

        const b8 hasFunction = !IsUnknownSymbolLine(*function);
        const b8 hasLocation = location && !IsUnknownSymbolLine(*location);
        if(!hasFunction && !hasLocation)
            continue;

        if(!outSymbol.empty())
            outSymbol += s_LinuxSymbolFrameSeparator;
        b8 wroteFrame = false;
        if(hasFunction){
            outSymbol.append(function->data(), function->size());
            wroteFrame = true;
        }
        if(hasLocation){
            if(wroteFrame)
                outSymbol += s_LinuxSymbolLocationSeparator;
            else
                outSymbol += s_LinuxSymbolLocationBarePrefix;
            outSymbol.append(location->data(), location->size());
        }
    }

    if(outSymbol.empty())
        return MakeUnexpected(Failure{});
    return outSymbol;
}

[[nodiscard]] static Expected<AString<Core::Alloc::ScratchArena>> RunLinuxSymbolizerCommand(const Span<const AStringView> arguments, Core::Alloc::ScratchArena& arena){
    AString<Core::Alloc::ScratchArena> output{arena};
    output.reserve(ProcessExecutionDetail::s_DefaultCaptureOutputMaxBytes);
    const auto captured = CaptureProcessOutput(output, arena, arguments);
    if(!captured){
        const ProcessCaptureFailure& failure = captured.error();
        NWB_LOGGER_INFO(FOUNDATION_TEXT("Linux symbolizer capture failed: phase {}, native {}, completion {}, exit {}, signal {}, truncated {}")
            , static_cast<u32>(failure.kind)
            , static_cast<i32>(failure.nativeError)
            , static_cast<i32>(failure.completionError)
            , failure.exitCode
            , failure.terminationSignal
            , failure.outputTruncated
        );
        return MakeUnexpected(Failure{});
    }

    return ExtractSymbolizerResult(AStringView(output.data(), output.size()), arena);
}

[[nodiscard]] static Expected<AString<Core::Alloc::ScratchArena>> TryRunLinuxSymbolizer(const AStringView toolName, const AStringView modulePathText, const u64 moduleOffset, Core::Alloc::ScratchArena& arena){
    AString<Core::Alloc::ScratchArena> addressArgument{arena};
    addressArgument.reserve(s_HexAddressPrefix.size() + ::s_HexU64DigitCount);
    addressArgument += s_HexAddressPrefix;
    AppendHexU64(moduleOffset, addressArgument);

    if(toolName == s_LlvmSymbolizerTool){
        AString<Core::Alloc::ScratchArena> objectArgument{arena};
        objectArgument.reserve(AddSize(s_LlvmSymbolizerObjectPrefix.size(), modulePathText.size()));
        objectArgument += s_LlvmSymbolizerObjectPrefix;
        objectArgument.append(modulePathText.data(), modulePathText.size());

        const Array<AStringView, 6> arguments = {{ s_LlvmSymbolizerTool, s_LlvmSymbolizerDemangleFlag, s_LlvmSymbolizerFunctionsFlag, s_LlvmSymbolizerInliningFlag, AStringView(objectArgument), AStringView(addressArgument) }};

        return RunLinuxSymbolizerCommand(arguments, arena);
    }

    AString<Core::Alloc::ScratchArena> modulePathArgument{arena};
    modulePathArgument.reserve(modulePathText.size());
    modulePathArgument.append(modulePathText.data(), modulePathText.size());

    const Array<AStringView, 7> arguments = {{ s_Addr2LineTool, s_Addr2LineFunctionsFlag, s_Addr2LineDemangleFlag, s_Addr2LineInlineFlag, s_Addr2LineExeFlag, AStringView(modulePathArgument), AStringView(addressArgument) }};

    return RunLinuxSymbolizerCommand(arguments, arena);
}

[[nodiscard]] static Expected<CrashScratchPath> FindLinuxSymbolFile(const AStringView modulePathText, const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& arena){
    if(modulePathText.empty() || modulePathText == s_AnonymousModulePath)
        return MakeUnexpected(Failure{});

    CrashScratchPath modulePath(arena, modulePathText);
    const auto regularModule = IsRegularFile(modulePath);
    if(!regularModule){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Log server: failed to inspect Linux symbol module '{}': {}"), PathToString<tchar>(modulePath), ConvertString(regularModule.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(*regularModule)
        return modulePath;

    const CrashScratchPath symbolStoreDirectory = EffectiveSymbolStoreDirectory(config, arena);
    if(symbolStoreDirectory.empty())
        return MakeUnexpected(Failure{});

    const auto moduleFileName = modulePath.filenameView();
    if(moduleFileName.empty())
        return MakeUnexpected(Failure{});

    CrashScratchPath symbolStoreCandidate = symbolStoreDirectory / moduleFileName;
    const auto regularCandidate = IsRegularFile(symbolStoreCandidate);
    if(!regularCandidate){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Log server: failed to inspect Linux symbol-store candidate '{}': {}"), PathToString<tchar>(symbolStoreCandidate), ConvertString(regularCandidate.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(*regularCandidate)
        return symbolStoreCandidate;

    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<AStringView> FindLinuxSymbolFileText(const AStringView modulePathText, const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& arena, LinuxSymbolFileCache& cache){
    for(const LinuxSymbolFileCacheEntry& entry : cache.entries){
        if(AStringView(entry.modulePath.data(), entry.modulePath.size()) != modulePathText)
            continue;

        if(!entry.found)
            return MakeUnexpected(Failure{});

        return AStringView(entry.symbolPath.data(), entry.symbolPath.size());
    }

    LinuxSymbolFileCacheEntry& entry = cache.entries.emplace_back(cache.entries.get_allocator().arena());
    entry.modulePath.assign(modulePathText.data(), modulePathText.size());

    const auto symbolPath = FindLinuxSymbolFile(modulePathText, config, arena);
    entry.found = symbolPath.has_value();
    if(!entry.found)
        return MakeUnexpected(Failure{});

    const auto symbolPathText = PathToString<char>(cache.entries.get_allocator().arena(), *symbolPath);
    entry.symbolPath.assign(symbolPathText.data(), symbolPathText.size());
    return AStringView(entry.symbolPath.data(), entry.symbolPath.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


LinuxFrameSymbolResolver::LinuxFrameSymbolResolver(const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& arena, const usize frameCount)
    : m_config(config)
    , m_arena(arena)
    , m_cache(arena)
{
    m_cache.entries.reserve(frameCount);
}

Expected<AString<Core::Alloc::ScratchArena>> LinuxFrameSymbolResolver::resolve(const AStringView modulePathText, const u64 moduleOffset){
    const CrashSymbolicationConfig& config = m_config;
    Core::Alloc::ScratchArena& arena = m_arena;
    LinuxSymbolFileCache& cache = m_cache;
    const auto symbolPathText = __hidden_linux_frame_symbol_resolver::FindLinuxSymbolFileText(modulePathText, config, arena, cache);
    if(!symbolPathText)
        return MakeUnexpected(Failure{});

    auto llvmSymbol = __hidden_linux_frame_symbol_resolver::TryRunLinuxSymbolizer(__hidden_linux_frame_symbol_resolver::s_LlvmSymbolizerTool, *symbolPathText, moduleOffset, arena);
    if(llvmSymbol)
        return Move(*llvmSymbol);
    return __hidden_linux_frame_symbol_resolver::TryRunLinuxSymbolizer(__hidden_linux_frame_symbol_resolver::s_Addr2LineTool, *symbolPathText, moduleOffset, arena);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

