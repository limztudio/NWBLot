// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font/cook.h>
#include <impl/assets_font_atlas/cook.h>

#include <core/assets/bunch/cook.h>
#include <core/common/application_entry.h>

#include <logger/client/module.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_package_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PackageArena("tests/integration/font_builder/package");
inline constexpr TStringView s_LoggerAppName = NWB_TEXT("font_builder_package_probe");
inline constexpr TStringView s_LoggerInitFailureText = NWB_TEXT("[font_builder_package_probe] logger.init() failed");
inline constexpr int s_EntryFailure = -1;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView TableTag(const u32 tag){
    if(tag == NWB::Impl::s_FontAtlasKernTag)
        return "kern";
    if(tag == NWB::Impl::s_FontAtlasGposTag)
        return "GPOS";
    if(tag == NWB::Impl::s_FontAtlasGdefTag)
        return "GDEF";
    return {};
}

[[nodiscard]] static bool ExportPayload(const NWB::Path& directory, const NWB::Impl::FontAtlasPayload& payload){
    if(!EnsureDirectories(directory))
        return false;
    const auto binary = NWB::Impl::SerializeFontAtlasPayload(payload, payload.glyphs.get_allocator().arena());
    if(!binary || !WriteBinaryFile(directory / "atlas.bin", *binary))
        return false;
    for(usize index = 0u; index < payload.groups.size(); ++index){
        const auto name = StringFormat(directory.arena(), "group_{}.pixels", index);
        if(!WriteBinaryFile(directory / name, payload.groups[index].pixels))
            return false;
    }
    for(const auto& table : payload.positioningTables){
        const AStringView tag = TableTag(table.tag);
        if(tag.empty())
            return false;
        const auto name = StringFormat(directory.arena(), "table_{}.bin", tag);
        if(!WriteBinaryFile(directory / name, table.bytes))
            return false;
    }
    return true;
}

[[nodiscard]] static bool DecodePackage(const AStringView source, const AStringView destination){
    NWB::Core::Assets::AssetArena arena(s_PackageArena);
    const NWB::Path sourcePath(arena, source);
    const NWB::Path destinationPath(arena, destination);
    NWB::Core::Metascript::MetaArena metadataArena(Name("tests/integration/font_builder/metadata"));
    NWB::Core::Alloc::ScratchArena scratch(Name("tests/integration/font_builder/scratch"));
    NWB::Core::Metascript::MString metadata(metadataArena);
    NWB::Core::Metascript::Document document(metadataArena);
    if(!ReadTextFile(sourcePath, metadata))
        return false;
    StripUtf8Bom(metadata);
    if(!document.parse(metadata))
        return false;
    auto expanded = NWB::Core::Assets::AssetsBunchCook::ExpandAssetBunch(
        sourcePath.parentPath(), "probe", sourcePath, document, scratch
    );
    if(!expanded || expanded->size() != 2u)
        return false;

    NWB::Impl::Font font(arena);
    NWB::Impl::FontAtlas atlas(arena);
    bool foundFont = false;
    bool foundAtlas = false;
    for(const auto& asset : *expanded){
        if(asset.assetType == Name("font")){
            if(foundFont)
                return false;
            auto entryResult = NWB::Impl::ParseFontCookMetadataValue(asset.virtualPath, sourcePath, asset.value, arena);
            if(!entryResult)
                return false;
            auto assetResult = NWB::Impl::BuildFontAsset(*entryResult, arena);
            if(!assetResult)
                return false;
            font = Move(*assetResult);
            foundFont = true;
        }
        else if(asset.assetType == Name("font_atlas")){
            if(foundAtlas)
                return false;
            auto entryResult = NWB::Impl::ParseFontAtlasCookMetadataValue(asset.virtualPath, sourcePath, asset.value, arena, scratch);
            if(!entryResult)
                return false;
            auto assetResult = NWB::Impl::BuildFontAtlasAsset(*entryResult, arena);
            if(!assetResult)
                return false;
            atlas = Move(*assetResult);
            foundAtlas = true;
        }
        else
            return false;
    }
    return foundFont && foundAtlas
        && NWB::Impl::ValidateFontAtlasSourceMatch(atlas.payload(), font)
        && ExportPayload(destinationPath, atlas.payload())
        && WriteBinaryFile(destinationPath / "source.sfnt", font.fontBytes());
}

int Run(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(s_LoggerAppName)){
        NWB_TCERR << s_LoggerInitFailureText << NWB_TEXT("\n");
        return s_EntryFailure;
    }
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    if(argc != 3){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder_package_probe: expected source .nwb and decoded output directory"));
        return s_EntryFailure;
    }
    if(!DecodePackage(argv[1], argv[2])){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder_package_probe: cannot cook or export font pair"));
        return s_EntryFailure;
    }
    return 0;
}

int EntryPoint(const isize argc, char** argv, void*){
    return Run(static_cast<int>(argc), argv);
}

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_font_builder_package_probe::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

