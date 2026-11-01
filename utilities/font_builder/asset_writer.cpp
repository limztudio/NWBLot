// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"

#include <core/common/log.h>

#include <global/filesystem/file_staging.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_asset_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PublicationDiagnosticTag = "font_builder_publication";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 WriteOutputs(const BakeOptions& options, const Span<const u8> sourceFontBytes, const Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch){
    auto fontBinary = BuildPreparedFont(sourceFontBytes, payload, scratch);
    MetadataString metadata(scratch);
    if(!fontBinary){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: prepared font failed: {}"), ConvertString(fontBinary.error()));
        return false;
    }
    BuildFontMetadata(payload, metadata);

    const StagedFilePairPaths<Core::Alloc::ScratchArena> paths(
        scratch,
        StagedFilePairPaths<Core::Alloc::ScratchArena>::MakeReplacedExtensionPath(scratch, options.output, Impl::s_PreparedFontExtension),
        options.output
    );
    const StagedFilePairResult preValidation = ValidateStagedFilePairPaths(paths, options.overwrite, true);
    if(!preValidation){
        if(
            preValidation.failureStage == StagedFilePairFailureStage::QueryWorkPath
            || preValidation.failureStage == StagedFilePairFailureStage::WorkPathAlreadyExists
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: publication work paths unavailable: {}"), ConvertString(preValidation.error.message()));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: output pair unavailable; use --overwrite for complete regular .nwb and .font files: {}"), ConvertString(preValidation.error.message()));
        }
        return false;
    }
    const ScratchPath directory = paths.secondary.parentPath();
    if(!directory.empty()){
        const auto ensured = EnsureDirectories(directory);
        if(!ensured){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to prepare output directory '{}': {}"), PathToString<tchar>(directory), ConvertString(ensured.error().message()));
            return false;
        }
    }

    const Span<const u8> fontBytes(fontBinary->data(), fontBinary->size());
    const AStringView metadataText(metadata.data(), metadata.size());
    const StagedFilePairResult published = StageAndPublishFilePair(
        paths,
        fontBytes,
        metadataText,
        options.overwrite,
        true,
        true,
        __hidden_font_builder_asset_writer::s_PublicationDiagnosticTag
    );
    const Array<NotNull<const ScratchPath*>, s_StagedFilePairCount> outputs = { MakeNotNull(&paths.primary), MakeNotNull(&paths.secondary) };
    const Array<NotNull<const ScratchPath*>, s_StagedFilePairCount> backups = { MakeNotNull(&paths.primaryBackup), MakeNotNull(&paths.secondaryBackup) };

    if(!published){
        for(u32 index = 0u; index < s_StagedFilePairCount; ++index){
            if(published.removalErrors[index])
                NWB_LOGGER_ERROR(NWB_TEXT("font_builder: could not remove failed new '{}': {}"), PathToString<tchar>(*outputs[index]), ConvertString(published.removalErrors[index].message()));
            if(published.restorationErrors[index])
                NWB_LOGGER_ERROR(NWB_TEXT("font_builder: could not restore previous '{}': {}"), PathToString<tchar>(*outputs[index]), ConvertString(published.restorationErrors[index].message()));
        }
        switch(published.failureStage){
        case StagedFilePairFailureStage::StagePrimary:
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to stage font temporary: {}"), ConvertString(published.error.message()));
            break;
        case StagedFilePairFailureStage::StageSecondary:
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to stage metadata temporary: {}"), ConvertString(published.error.message()));
            break;
        case StagedFilePairFailureStage::VerifyPrimary:
        case StagedFilePairFailureStage::VerifySecondary:
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: staging or verification failed; previous pair remains in place"));
            break;
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: pair publication failed: {}"), ConvertString(published.error.message()));
            break;
        }
        return false;
    }

    for(u32 index = 0u; index < s_StagedFilePairCount; ++index){
        if(published.backupCleanupErrors[index])
            NWB_LOGGER_WARNING(NWB_TEXT("font_builder: published pair but could not remove backup '{}': {}"), PathToString<tchar>(*backups[index]), ConvertString(published.backupCleanupErrors[index].message()));
        else if(published.backupCleanupMissing[index])
            NWB_LOGGER_WARNING(NWB_TEXT("font_builder: published pair but previous backup was already absent: '{}'"), PathToString<tchar>(*backups[index]));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

