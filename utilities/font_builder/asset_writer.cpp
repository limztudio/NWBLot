// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"
#include "asset_metadata.h"
#include "prepared_font.h"

#include <global/blocking_io.h>
#include <logger/client/logger.h>

#if defined(GLB_PLATFORM_LINUX)
#include <fcntl.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OutputPaths{
    Path metadata;
    Path font;
    Path metadataTemporary;
    Path fontTemporary;
    Path metadataBackup;
    Path fontBackup;

    explicit OutputPaths(const Path& output)
        : metadata(output)
        , font(output)
        , metadataTemporary(output)
        , fontTemporary(output)
        , metadataBackup(output)
        , fontBackup(output)
    {
        font.replace_extension(".font");
        metadataTemporary += ".tmp";
        fontTemporary = font;
        fontTemporary += ".tmp";
        metadataBackup += ".old";
        fontBackup = font;
        fontBackup += ".old";
    }
};

class TemporaryFile final : NoCopy{
public:
    explicit TemporaryFile(const Path& path)
        : m_path(path)
    {}
    ~TemporaryFile(){
        if(m_owned){
            ErrorCode error;
            if(!RemoveFile(m_path, error) && error)
                NWB_LOGGER_WARNING(GLB_TEXT("font_builder: cannot remove unpublished temporary '{}'"), PathToString<tchar>(m_path));
        }
    }


public:
    [[nodiscard]] bool stage(const u8* data, const usize count){
#if defined(GLB_PLATFORM_WINDOWS)
        const HANDLE handle = CreateFile(m_path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(handle == INVALID_HANDLE_VALUE)
            return false;
        m_owned = true;
        const bool written = WriteAllWin32Handle(handle, data, count);
        const bool flushed = written && FlushFileBuffers(handle);
        const bool closed = CloseHandle(handle);
#else
        const int descriptor = open(m_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666u);
        if(descriptor < 0)
            return false;
        m_owned = true;
        const bool written = WriteAllFileDescriptor(descriptor, data, count);
        const bool flushed = written && fsync(descriptor) == 0;
        const bool closed = close(descriptor) == 0;
#endif
        return written && flushed && closed;
    }
    [[nodiscard]] bool verify(const u8* expected, const usize count)const{
        InputFileStream stream(m_path, InputFileStream::binary);
        if(!stream.is_open())
            return false;
        u8 buffer[4096u];
        for(usize offset = 0u; offset < count;){
            const usize remaining = count - offset;
            const usize chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            stream.read(reinterpret_cast<char*>(buffer), static_cast<StreamSize>(chunk));
            if(stream.gcount() != static_cast<StreamSize>(chunk) || GLB_MEMCMP(buffer, expected + offset, chunk) != 0)
                return false;
            offset += chunk;
        }
        char extra = 0;
        stream.read(&extra, 1);
        return stream.gcount() == 0 && stream.eof();
    }
    void published(){ m_owned = false; }


private:
    Path m_path;
    bool m_owned = false;
};

[[nodiscard]] static bool CheckOutputs(const OutputPaths& paths, const bool overwrite, bool& outPresent){
    const Path* files[] = { &paths.metadata, &paths.font };
    u32 presentCount = 0u;
    for(const Path* file : files){
        ErrorCode error;
        const bool exists = FileExists(*file, error);
        if(error || (exists && !IsRegularFile(*file, error)) || error)
            return false;
        presentCount += exists ? 1u : 0u;
    }
    if(presentCount != 0u && (presentCount != 2u || !overwrite))
        return false;
    outPresent = presentCount == 2u;
    return true;
}

[[nodiscard]] static bool CheckWorkPaths(const OutputPaths& paths){
    const Path* files[] = {
        &paths.metadataTemporary, &paths.fontTemporary,
        &paths.metadataBackup, &paths.fontBackup,
    };
    for(const Path* file : files){
        ErrorCode error;
        if(FileExists(*file, error) || error)
            return false;
    }
    return true;
}

static void RestoreBackups(const Path* const outputs[2u], const Path* const backups[2u], const u32 count){
    for(u32 index = count; index > 0u; --index){
        ErrorCode error;
        if(!RenamePath(*backups[index - 1u], *outputs[index - 1u], error))
            NWB_LOGGER_ERROR(GLB_TEXT("font_builder: could not restore previous '{}'"), PathToString<tchar>(*outputs[index - 1u]));
    }
}

[[nodiscard]] static bool Publish(const OutputPaths& paths, TemporaryFile& font, TemporaryFile& metadata, const bool overwrite){
    bool present = false;
    if(!CheckOutputs(paths, overwrite, present))
        return false;
    const Path* outputs[] = { &paths.font, &paths.metadata };
    const Path* backups[] = { &paths.fontBackup, &paths.metadataBackup };
    const Path* temporaries[] = { &paths.fontTemporary, &paths.metadataTemporary };
    TemporaryFile* staged[] = { &font, &metadata };

    u32 backedUp = 0u;
    if(present){
        for(; backedUp < 2u; ++backedUp){
            ErrorCode error;
            if(!RenamePath(*outputs[backedUp], *backups[backedUp], error)){
                RestoreBackups(outputs, backups, backedUp);
                return false;
            }
        }
    }
    u32 published = 0u;
    for(; published < 2u; ++published){
        ErrorCode error;
        if(!RenamePath(*temporaries[published], *outputs[published], error)){
            for(u32 index = published; index > 0u; --index){
                ErrorCode removalError;
                if(!RemoveFile(*outputs[index - 1u], removalError))
                    NWB_LOGGER_ERROR(GLB_TEXT("font_builder: could not remove failed new '{}'"), PathToString<tchar>(*outputs[index - 1u]));
            }
            RestoreBackups(outputs, backups, backedUp);
            return false;
        }
        staged[published]->published();
    }
    for(u32 index = 0u; index < backedUp; ++index){
        ErrorCode error;
        if(!RemoveFile(*backups[index], error))
            NWB_LOGGER_WARNING(GLB_TEXT("font_builder: published pair but could not remove backup '{}'"), PathToString<tchar>(*backups[index]));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteOutputs(const BakeOptions& options, const Impl::FontAtlasPayload& payload){
    Core::Assets::AssetArena& arena = payload.glyphs.get_allocator().arena();
    Core::Assets::AssetBytes fontBinary(arena);
    MetadataString metadata(arena);
    if(!BuildPreparedFont(options, payload, fontBinary))
        return false;
    BuildFontMetadata(payload, metadata);

    const __hidden_font_builder_writer::OutputPaths paths(options.output);
    bool present = false;
    if(!__hidden_font_builder_writer::CheckOutputs(paths, options.overwrite, present)
        || !__hidden_font_builder_writer::CheckWorkPaths(paths)){
        NWB_LOGGER_ERROR(GLB_TEXT("font_builder: output pair unavailable; use --overwrite for complete regular .nwb and .font files"));
        return false;
    }
    ErrorCode error;
    const Path directory = options.output.parent_path();
    if(!directory.empty() && !EnsureDirectories(directory, error))
        return false;

    __hidden_font_builder_writer::TemporaryFile stagedFont(paths.fontTemporary);
    __hidden_font_builder_writer::TemporaryFile stagedMetadata(paths.metadataTemporary);
    const u8* metadataBytes = reinterpret_cast<const u8*>(metadata.data());
    if(
        !stagedFont.stage(fontBinary.data(), fontBinary.size())
        || !stagedMetadata.stage(metadataBytes, metadata.size())
        || !stagedFont.verify(fontBinary.data(), fontBinary.size())
        || !stagedMetadata.verify(metadataBytes, metadata.size())
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("font_builder: staging or verification failed; previous pair remains in place"));
        return false;
    }
    if(!__hidden_font_builder_writer::Publish(paths, stagedFont, stagedMetadata, options.overwrite)){
        NWB_LOGGER_ERROR(GLB_TEXT("font_builder: pair publication failed"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

