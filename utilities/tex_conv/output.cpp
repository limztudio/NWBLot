// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <global/diagnostics.h>
#include <global/error_code.h>
#include <global/scope_exit.h>

#if !defined(NWB_PLATFORM_WINDOWS)
#include <fcntl.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_output{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_NewLine = "\r\n";
inline constexpr AStringView s_NwbOutputExtension = ".nwb";
inline constexpr u32 s_OutputFileCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PublicationPaths{
    ScratchPath data;
    ScratchPath metadata;
    ScratchPath dataTemporary;
    ScratchPath metadataTemporary;
    ScratchPath dataBackup;
    ScratchPath metadataBackup;


public:
    [[nodiscard]] static ScratchPath MakeBackup(Core::Alloc::ScratchArena& scratchArena, const Path& path){
        ScratchPath backup(scratchArena, path);
        backup += s_BackupOutputSuffix;
        return backup;
    }


public:
    PublicationPaths(Core::Alloc::ScratchArena& scratchArena, const OutputPaths& paths)
        : data(scratchArena, paths.data)
        , metadata(scratchArena, paths.metadata)
        , dataTemporary(scratchArena, paths.dataTemporary)
        , metadataTemporary(scratchArena, paths.metadataTemporary)
        , dataBackup(MakeBackup(scratchArena, paths.data))
        , metadataBackup(MakeBackup(scratchArena, paths.metadata))
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
bool CheckOutputPath(const ::Path<ArenaT>& path, const bool force, const bool temporary){
    const auto exists = FileExistsNoFollow(path);
    if(!exists){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to query output path '{}': {}"), PathToString<tchar>(path), StringConvert(exists.error().message()));
        return false;
    }
    if(!*exists)
        return true;

    const auto regular = IsRegularFile(path);
    if(!regular){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to inspect output path '{}': {}"), PathToString<tchar>(path), StringConvert(regular.error().message()));
        return false;
    }
    if(!*regular){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: output path is not a regular file: '{}'"), PathToString<tchar>(path));
        return false;
    }
    if(temporary){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: publication work path already exists: '{}'"), PathToString<tchar>(path));
        return false;
    }
    if(!force){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: Output already exists: '{}'. Pass --force to replace both output files."), PathToString<tchar>(path));
        return false;
    }

    return true;
}

Expected<ScratchString> BuildMetadata(const PreparedTexture& payload, Core::Alloc::ScratchArena& scratchArena){
    const auto mipCount = TextureFormat::ComputeCompleteMipCount(payload.dimension, payload.width, payload.height, payload.depth);
    if(
        !mipCount || *mipCount != payload.mipCount || !IsValidTexturePayloadFormat(payload.format) || !IsValidTextureAlphaMode(payload.alphaMode)
        || (payload.dimension != TextureDimension::Texture3D && payload.depth != 1u)
        || (payload.dimension == TextureDimension::TextureCube && payload.width != payload.height)
        || (payload.alphaMode == TextureAlphaMode::ConstantUnorm8 && payload.alphaConstantUnorm8 > TextureFormat::s_MaxConstantAlphaUnorm8)
        || (payload.alphaMode != TextureAlphaMode::ConstantUnorm8 && payload.alphaConstantUnorm8 != TextureFormat::s_OpaqueAlphaUnorm8)
        || (payload.format == TexturePayloadFormat::UastcLdr4x4 && payload.alphaMode != TextureAlphaMode::Opaque && payload.alphaMode != TextureAlphaMode::EmbeddedLdr)
        || (IsHdrTexturePayloadFormat(payload.format) && payload.alphaMode == TextureAlphaMode::EmbeddedLdr)
    )
        return MakeUnexpected(Failure{});

    AStringView alphaMode;
    switch(payload.alphaMode){
    case TextureAlphaMode::Opaque:
        alphaMode = TextureFormat::s_OpaqueAlphaMode;
        break;
    case TextureAlphaMode::EmbeddedLdr:
        alphaMode = TextureFormat::s_EmbeddedLdrAlphaMode;
        break;
    case TextureAlphaMode::ConstantUnorm8:
        alphaMode = TextureFormat::s_ConstantUnorm8AlphaMode;
        break;
    case TextureAlphaMode::SeparateUastcLdr4x4:
        alphaMode = TextureFormat::s_SeparateUastcLdr4x4AlphaMode;
        break;
    default:
        return MakeUnexpected(Failure{});
    }
    const AStringView format = IsHdrTexturePayloadFormat(payload.format) ? TextureFormat::s_UastcHdr4x4Format : TextureFormat::s_UastcLdr4x4Format;
    const AStringView dimension = payload.dimension == TextureDimension::TextureCube ? TextureFormat::s_TextureCubeDimension : payload.dimension == TextureDimension::Texture3D ? TextureFormat::s_Texture3DDimension : TextureFormat::s_Texture2DDimension;
    ScratchStringStream output(s_FileOpenWrite, scratchArena);
    output
        << "// Generated by tex_conv. The same-stem .tex file contains raw linear UASTC blocks." << s_NewLine
        << "texture asset;" << s_NewLine
        << s_NewLine
        << "asset.format = \"" << format << "\";" << s_NewLine
        << "asset.dimension = \"" << dimension << "\";" << s_NewLine
        << "asset.resolution = [" << payload.width << ", " << payload.height
    ;
    if(payload.dimension == TextureDimension::Texture3D)
        output << ", " << payload.depth;
    output
        << "];" << s_NewLine
        << "asset.alpha_mode = \"" << alphaMode << "\";" << s_NewLine
    ;
    if(payload.alphaMode == TextureAlphaMode::ConstantUnorm8)
        output << "asset.alpha_constant_unorm8 = " << static_cast<u32>(payload.alphaConstantUnorm8) << ";" << s_NewLine;
    return Move(output).str();
}

[[nodiscard]] Expected<void, ErrorCode> StageOutput(const ScratchPath& path, const void* const data, const usize count, bool& outOwned)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(static_cast<u64>(count) > static_cast<u64>(Limit<i64>::s_Max))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    const HANDLE handle = CreateFile(path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    outOwned = true;
    ErrorCode error;
    const u8* cursor = static_cast<const u8*>(data);
    usize remaining = count;
    while(remaining > 0u){
        const DWORD requestSize = remaining > static_cast<usize>(Limit<DWORD>::s_Max) ? Limit<DWORD>::s_Max : static_cast<DWORD>(remaining);
        DWORD transferred = 0u;
        if(!WriteFile(handle, cursor, requestSize, &transferred, nullptr)){
            error = GlobalFilesystemDetail::LastSystemError();
            break;
        }
        if(transferred == 0u){
            error = MakeErrorCode(ErrorCondition::io_error);
            break;
        }
        cursor += transferred;
        remaining -= transferred;
    }
    if(!error && !FlushFileBuffers(handle))
        error = GlobalFilesystemDetail::LastSystemError();
    if(!CloseHandle(handle) && !error)
        error = GlobalFilesystemDetail::LastSystemError();
#else
    if(static_cast<u64>(count) > static_cast<u64>(Limit<off_t>::s_Max))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    constexpr u32 s_NewFileMode = 0666u;
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, s_NewFileMode);
    if(descriptor < 0)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    outOwned = true;
    ErrorCode error;
    const u8* cursor = static_cast<const u8*>(data);
    usize remaining = count;
    while(remaining > 0u){
        const usize requestSize = remaining > static_cast<usize>(Limit<ssize_t>::s_Max) ? static_cast<usize>(Limit<ssize_t>::s_Max) : remaining;
        const ssize_t transferred = write(descriptor, cursor, requestSize);
        if(transferred < 0){
            if(errno == EINTR)
                continue;
            error = GlobalFilesystemDetail::LastSystemError();
            break;
        }
        if(transferred == 0){
            error = MakeErrorCode(ErrorCondition::io_error);
            break;
        }
        cursor += static_cast<usize>(transferred);
        remaining -= static_cast<usize>(transferred);
    }
    if(!error && fsync(descriptor) != 0)
        error = GlobalFilesystemDetail::LastSystemError();
    if(close(descriptor) != 0 && !error)
        error = GlobalFilesystemDetail::LastSystemError();
#endif
    if(error)
        return MakeUnexpected(error);
    return {};
}

void CaptureCleanupFailure(const ScratchPath& path, const ErrorCode& error)noexcept{
    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("tex_conv: publication cleanup failed for '{}', error {}", path.c_str(), error.value());
    CaptureDiagnosticEvent("tex_conv_publication", diagnostic.view(), __FILE__, __LINE__);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OutputPaths> ResolveOutputPaths(const ScratchPath& inputPath, const AStringView outputArgument, Core::Alloc::ScratchArena& scratchArena){
    OutputPaths paths;
    if(outputArgument.empty()){
        paths.metadata = inputPath;
        paths.metadata.replaceExtension(__hidden_output::s_NwbOutputExtension);
    }
    else{
        const ScratchPath outputBase(scratchArena, outputArgument);
        const auto extension = ToAsciiLowerCopy(PathToGenericString(scratchArena, outputBase.extension()));
        if(extension.empty())
            paths.metadata = outputBase;
        else if(extension == __hidden_output::s_NwbOutputExtension)
            paths.metadata = outputBase;
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: --output must be an output base name or a .nwb filename."));
            return MakeUnexpected(Failure{});
        }
        paths.metadata.replaceExtension(__hidden_output::s_NwbOutputExtension);
    }

    paths.data = paths.metadata;
    paths.data.replaceExtension(TextureFormat::s_TextureDataExtension);
    paths.metadataTemporary = paths.metadata;
    paths.metadataTemporary += s_TemporaryOutputSuffix;
    paths.dataTemporary = paths.data;
    paths.dataTemporary += s_TemporaryOutputSuffix;
    return paths;
}

bool ValidateOutputPaths(const OutputPaths& outputPaths, const bool force, Core::Alloc::ScratchArena& scratchArena){
    const ScratchPath dataBackup = __hidden_output::PublicationPaths::MakeBackup(scratchArena, outputPaths.data);
    const ScratchPath metadataBackup = __hidden_output::PublicationPaths::MakeBackup(scratchArena, outputPaths.metadata);
    return
        __hidden_output::CheckOutputPath(outputPaths.metadata, force, false)
        && __hidden_output::CheckOutputPath(outputPaths.data, force, false)
        && __hidden_output::CheckOutputPath(outputPaths.metadataTemporary, false, true)
        && __hidden_output::CheckOutputPath(outputPaths.dataTemporary, false, true)
        && __hidden_output::CheckOutputPath(dataBackup, false, true)
        && __hidden_output::CheckOutputPath(metadataBackup, false, true)
    ;
}

bool WriteOutputs(const OutputPaths& outputPaths, const PreparedTexture& payload, const bool force, Core::Alloc::ScratchArena& scratchArena){
    if(payload.bytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: cannot write an empty prepared texture."));
        return false;
    }
    const auto metadata = __hidden_output::BuildMetadata(payload, scratchArena);
    if(!metadata){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: cannot describe an invalid prepared texture."));
        return false;
    }
    if(!ValidateOutputPaths(outputPaths, force, scratchArena))
        return false;

    const __hidden_output::PublicationPaths paths(scratchArena, outputPaths);
    const ScratchPath* backups[] = { &paths.dataBackup, &paths.metadataBackup };
    const ScratchPath* outputs[] = { &paths.data, &paths.metadata };
    const ScratchPath* temporaries[] = { &paths.dataTemporary, &paths.metadataTemporary };
    bool ownedTemporary[__hidden_output::s_OutputFileCount] = {};
    bool backedUp[__hidden_output::s_OutputFileCount] = {};
    bool published[__hidden_output::s_OutputFileCount] = {};
    ErrorCode removalErrors[__hidden_output::s_OutputFileCount];
    ErrorCode restorationErrors[__hidden_output::s_OutputFileCount];
    ErrorCode temporaryErrors[__hidden_output::s_OutputFileCount];

    // Cleanup never allocates or logs through the throwing logger while an unexpected exception unwinds.
    const auto rollback = [&]()noexcept{
        for(u32 index = __hidden_output::s_OutputFileCount; index > 0u; --index){
            const u32 current = index - 1u;
            if(published[current]){
                const auto removed = RemoveFile(*outputs[current]);
                if(!removed){
                    removalErrors[current] = removed.error();
                    __hidden_output::CaptureCleanupFailure(*outputs[current], removalErrors[current]);
                }
            }
            if(backedUp[current] && !removalErrors[current]){
                const auto restored = RenamePath(*backups[current], *outputs[current]);
                if(!restored){
                    restorationErrors[current] = restored.error();
                    __hidden_output::CaptureCleanupFailure(*backups[current], restorationErrors[current]);
                }
            }
            if(ownedTemporary[current]){
                const auto removed = RemoveFile(*temporaries[current]);
                if(!removed){
                    temporaryErrors[current] = removed.error();
                    __hidden_output::CaptureCleanupFailure(*temporaries[current], temporaryErrors[current]);
                }
            }
        }
    };
    ScopeExit restoreOnFailure(rollback);

    const auto publish = [&](){
        const auto dataStaged = __hidden_output::StageOutput(paths.dataTemporary, payload.bytes.data(), payload.bytes.size(), ownedTemporary[0u]);
        if(!dataStaged){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to stage output data file '{}': {}"), PathToString<tchar>(outputPaths.dataTemporary), StringConvert(dataStaged.error().message()));
            return false;
        }
        const auto metadataStaged = __hidden_output::StageOutput(paths.metadataTemporary, metadata->data(), metadata->size(), ownedTemporary[1u]);
        if(!metadataStaged){
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to stage output metadata file '{}': {}"), PathToString<tchar>(outputPaths.metadataTemporary), StringConvert(metadataStaged.error().message()));
            return false;
        }

        for(u32 index = 0u; index < __hidden_output::s_OutputFileCount; ++index){
            const auto present = FileExistsNoFollow(*outputs[index]);
            if(!present){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to inspect existing output '{}': {}"), PathToString<tchar>(*outputs[index]), StringConvert(present.error().message()));
                return false;
            }
            if(!*present)
                continue;
            if(!__hidden_output::CheckOutputPath(*outputs[index], force, false))
                return false;
            if(!__hidden_output::CheckOutputPath(*backups[index], false, true))
                return false;
            const auto preserved = RenamePath(*outputs[index], *backups[index]);
            if(!preserved){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to preserve existing output '{}': {}"), PathToString<tchar>(*outputs[index]), StringConvert(preserved.error().message()));
                return false;
            }
            backedUp[index] = true;
        }
        for(u32 index = 0u; index < __hidden_output::s_OutputFileCount; ++index){
            const auto finalized = RenamePath(*temporaries[index], *outputs[index]);
            if(!finalized){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to finalize output '{}': {}"), PathToString<tchar>(*outputs[index]), StringConvert(finalized.error().message()));
                return false;
            }
            published[index] = true;
            ownedTemporary[index] = false;
        }
        return true;
    };
    if(!publish()){
        rollback();
        restoreOnFailure.release();
        for(u32 index = 0u; index < __hidden_output::s_OutputFileCount; ++index){
            if(backedUp[index] && (removalErrors[index] || restorationErrors[index])){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to restore '{}'; previous output retained at '{}' (remove {}, restore {})")
                    , PathToString<tchar>(*outputs[index])
                    , PathToString<tchar>(*backups[index])
                    , removalErrors[index].value()
                    , restorationErrors[index].value()
                );
            }
            if(!backedUp[index] && removalErrors[index]){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to remove newly published output '{}': {}"), PathToString<tchar>(*outputs[index]), StringConvert(removalErrors[index].message()));
            }
            if(temporaryErrors[index]){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("tex_conv: failed to remove unpublished temporary '{}': {}"), PathToString<tchar>(*temporaries[index]), StringConvert(temporaryErrors[index].message()));
            }
        }
        return false;
    }

    restoreOnFailure.release();
    for(u32 index = 0u; index < __hidden_output::s_OutputFileCount; ++index){
        if(!backedUp[index])
            continue;
        const auto removed = RemoveFile(*backups[index]);
        if(!removed){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("tex_conv: published output pair but could not remove previous backup '{}': {}"), PathToString<tchar>(*backups[index]), StringConvert(removed.error().message()));
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

