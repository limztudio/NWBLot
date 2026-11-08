// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#include "slang_compiler_fingerprint.h"
#include "slang_compiler_workspace.h"

#include <core/common/log.h>

#include <global/hash_utils.h>
#include <global/process_execution.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SLANGC_EXECUTABLE
#error "NWB_SLANGC_EXECUTABLE must be defined by the build configuration"
#endif


NWB_IMPL_BEGIN


namespace Alloc = Core::Alloc;


Expected<u64> SlangCompilerFingerprint::compute(const Path& temporaryRoot, Alloc::ScratchArena& scratchArena, const Span<const AStringView> commonArguments, const Span<const AStringView> baselineCapabilities, const Span<const ShaderBinaryPayload::SlangStageMapping> stageMappings){
    using namespace ShaderBinaryPayload;
    using ScratchPath = SlangCompilerWorkspace::ScratchPath;
    using ScratchString = SlangCompilerWorkspace::ScratchString;

    u64 fingerprint = s_Fnv64OffsetBasis;
    const ScratchPath compilerPath(scratchArena, NWB_SLANGC_EXECUTABLE);
    InputFileStream compilerStream(compilerPath, s_FileOpenBinary);
    if(!compilerStream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to open configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    Array<char, s_SlangFingerprintReadBufferBytes> compilerBytes{};
    for(;;){
        compilerStream.read(compilerBytes.data(), static_cast<StreamSize>(compilerBytes.size()));
        const StreamSize bytesRead = compilerStream.gcount();
        if(bytesRead > 0)
            fingerprint = UpdateFnv64TextExact(fingerprint, AStringView(compilerBytes.data(), static_cast<usize>(bytesRead)));
        if(compilerStream.eof() && !compilerStream.bad())
            break;
        if(!compilerStream.good()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
            return MakeUnexpected(Failure{});
        }
    }

    const auto workDirectoryResult = SlangCompilerWorkspace::CreateCompilerWorkDirectory(ScratchPath(scratchArena, temporaryRoot), scratchArena);
    if(!workDirectoryResult)
        return MakeUnexpected(Failure{});
    const ScratchPath& workDirectory = *workDirectoryResult;
    SlangCompilerWorkspace::ScopedDirectoryCleanupGuard workDirectoryCleanup(workDirectory);
    const ScratchPath versionPath = workDirectory / s_SlangVersionFileName;
    const auto versionPathText = SlangCompilerWorkspace::BuildCompilerPathText(versionPath, scratchArena);
    if(!versionPathText)
        return MakeUnexpected(Failure{});
    const Array<AStringView, 2u> arguments = { AStringView(NWB_SLANGC_EXECUTABLE), s_SlangVersionArgument };
    const auto exitCode = RunProcessRedirectedToFile(scratchArena, arguments, AStringView(*versionPathText));
    if(!exitCode){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to execute configured compiler '{}' for version query: process execution failure {}"), PathToString<tchar>(compilerPath), static_cast<u32>(exitCode.error()));
        return MakeUnexpected(Failure{});
    }
    if(*exitCode != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' version query failed with exit code {}"), PathToString<tchar>(compilerPath), *exitCode);
        return MakeUnexpected(Failure{});
    }
    ScratchString versionText(scratchArena);
    const auto versionRead = ReadBinaryFile(versionPath, versionText);
    if(!versionRead){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler version '{}': {}:{}"), PathToString<tchar>(versionPath), ConvertString(versionRead.error().category().name()), versionRead.error().value());
        return MakeUnexpected(Failure{});
    }
    if(TrimView(versionText).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' returned empty version text"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    // The version identifies the loaded Slang compiler library as well as its executable launcher.
    return ShaderSourceFingerprint{}.finalizeCompilerFingerprint(
        AStringView(versionText),
        commonArguments,
        baselineCapabilities,
        stageMappings,
        fingerprint
    );
}


NWB_IMPL_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
