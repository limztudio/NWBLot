// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font_atlas/asset.h>

#include <core/common/application_entry.h>

#include <logger/client/logger.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_package_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PackageArena("tests/integration/font_builder/package");
inline constexpr auto s_LoggerAppName = NWB_TEXT("font_builder_package_probe");
inline constexpr auto s_LoggerInitFailureText = NWB_TEXT("[font_builder_package_probe] logger.init() failed");
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
    ErrorCode error;
    if(!EnsureDirectories(directory, error))
        return false;
    for(usize index = 0u; index < payload.groups.size(); ++index){
        const auto name = StringFormat(directory.arena(), "group_{}.rgba", index);
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

[[nodiscard]] static bool DecodePackage(const char* source, const char* destination){
    NWB::Core::Assets::AssetArena arena(s_PackageArena);
    const NWB::Path sourcePath(arena, source);
    const NWB::Path destinationPath(arena, destination);
    NWB::Core::Assets::AssetBytes binary(arena);
    ErrorCode error;
    if(!ReadBinaryFile(sourcePath, binary, error))
        return false;
    NWB::Impl::FontAtlasPayload payload(arena);
    if(!NWB::Impl::DeserializeFontAtlasPayload(binary, payload))
        return false;
    return ExportPayload(destinationPath, payload);
}

int Run(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(s_LoggerAppName)){
        NWB_TCERR << s_LoggerInitFailureText << NWB_TEXT("\n");
        return s_EntryFailure;
    }
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Log::BreakPolicy::BreakOnFatal);
    if(argc != 3){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder_package_probe: expected source .atlas and decoded output directory"));
        return s_EntryFailure;
    }
    if(!DecodePackage(argv[1], argv[2])){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder_package_probe: cannot decode or export atlas package"));
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

