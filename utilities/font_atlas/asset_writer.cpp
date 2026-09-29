// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"
#include "asset_metadata.h"

#include <global/blocking_io.h>
#include <logger/client/logger.h>

#if defined(NWB_PLATFORM_LINUX)
#include <fcntl.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_ATLAS_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TemporaryFile final : NoCopy{
public:
    explicit TemporaryFile(const Path& path)
        : m_path(path)
    {}
    ~TemporaryFile(){
        if(m_owned){
            ErrorCode error;
            if(!RemoveFile(m_path, error) && error)
                NWB_LOGGER_WARNING(NWB_TEXT("font_atlas: cannot remove unpublished temporary '{}'"), PathToString<tchar>(m_path));
        }
    }


public:
    [[nodiscard]] bool stage(const AStringView text){
#if defined(NWB_PLATFORM_WINDOWS)
        const HANDLE handle = CreateFile(m_path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(handle == INVALID_HANDLE_VALUE){
            NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: temporary path unavailable '{}'"), PathToString<tchar>(m_path));
            return false;
        }
        m_owned = true;
        const bool written = WriteAllWin32Handle(handle, text.data(), text.size());
        const bool flushed = written && FlushFileBuffers(handle);
        const bool closed = CloseHandle(handle);
#else
        const int descriptor = open(m_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666u);
        if(descriptor < 0){
            NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: temporary path unavailable '{}'"), PathToString<tchar>(m_path));
            return false;
        }
        m_owned = true;
        const bool written = WriteAllFileDescriptor(descriptor, text.data(), text.size());
        const bool flushed = written && fsync(descriptor) == 0;
        const bool closed = close(descriptor) == 0;
#endif
        return written && flushed && closed;
    }
    void published(){ m_owned = false; }


private:
    Path m_path;
    bool m_owned = false;
};

[[nodiscard]] static bool OutputAvailable(const BakeOptions& options){
    ErrorCode error;
    const bool exists = FileExists(options.output, error);
    if(error || (exists && (!options.overwrite || !IsRegularFile(options.output, error))) || error){
        NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: output unavailable; pass --overwrite to replace a regular file"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteOutputs(const BakeOptions& options, const Impl::FontAtlasPayload& payload){
    if(!Impl::ValidateFontAtlasPayload(payload) || !__hidden_font_atlas_writer::OutputAvailable(options))
        return false;
    MetadataString metadata(payload.glyphs.get_allocator().arena());
    if(!BuildFontAtlasMetadata(options, payload, metadata))
        return false;
    ErrorCode error;
    const Path directory = options.output.parent_path();
    if(!directory.empty() && !EnsureDirectories(directory, error)){
        NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: cannot create output directory"));
        return false;
    }
    Path temporary(options.output);
    temporary += ".tmp";
    __hidden_font_atlas_writer::TemporaryFile cleanup(temporary);
    if(!cleanup.stage(AStringView(metadata.data(), metadata.size()))){
        NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: cannot stage atlas file"));
        return false;
    }
    MetadataString staged(payload.glyphs.get_allocator().arena());
    if(!ReadTextFile(temporary, staged) || staged != metadata){
        NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: staged atlas verification failed"));
        return false;
    }
    if(!__hidden_font_atlas_writer::OutputAvailable(options))
        return false;
    if(!RenamePath(temporary, options.output, error)){
        NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: final atlas publication failed; previous file remains valid"));
        return false;
    }
    cleanup.published();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_ATLAS_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

