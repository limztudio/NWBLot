// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"

#include <core/assets/paths.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FontAtlasMetadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReadU32(const Path& path, const Core::Metascript::Value& object, const AStringView field, u32& outValue){
    const Core::Metascript::Value* value = Core::Metascript::FindField(object, field);
    if(!value || !value->isInteger() || value->asInteger() < 0 || static_cast<u64>(value->asInteger()) > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': '{}' must be a u32 integer"), PathToString<tchar>(path), StringConvert(field));
        return false;
    }
    outValue = static_cast<u32>(value->asInteger());
    return true;
}

bool ReadHash(const Path& path, const Core::Metascript::Value& object, const AStringView field, Sha256Digest& outValue){
    AStringView text;
    if(!Core::Assets::ReadMetadataStringField(path, object, s_DiagnosticPrefix, field, true, text) || !ParseSha256(text, outValue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': '{}' must contain 64 lowercase hexadecimal digits")
            , PathToString<tchar>(path)
            , StringConvert(field)
        );
        return false;
    }
    return true;
}

bool ReadU32List(const Path& path, const Core::Metascript::Value& object, const AStringView field, const NotNull<u32*> outValues, const usize count){
    const Core::Metascript::Value* list = Core::Metascript::FindField(object, field);
    if(!list || !list->isList() || list->asList().size() != count){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': '{}' has invalid list extent"), PathToString<tchar>(path), StringConvert(field));
        return false;
    }
    for(usize index = 0u; index < count; ++index){
        const Core::Metascript::Value& value = list->asList()[index];
        if(!value.isInteger() || value.asInteger() < 0 || static_cast<u64>(value.asInteger()) > Limit<u32>::s_Max)
            return false;
        outValues.get()[index] = static_cast<u32>(value.asInteger());
    }
    return true;
}

bool ReadFloatList(const Path& path, const Core::Metascript::Value& object, const AStringView field, const NotNull<f32*> outValues, const usize count){
    const Core::Metascript::Value* list = Core::Metascript::FindField(object, field);
    if(!list || !list->isList() || list->asList().size() != count)
        return false;
    for(usize index = 0u; index < count; ++index){
        if(!Core::Assets::ReadMetadataFiniteF32Value(path, list->asList()[index], s_DiagnosticPrefix, field, outValues.get()[index]))
            return false;
    }
    return true;
}

bool CheckToken(const Path& path, const Core::Metascript::Value& object, const AStringView field, const AStringView expected){
    AStringView value;
    if(!Core::Assets::ReadMetadataStringField(path, object, s_DiagnosticPrefix, field, true, value) || value != expected){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': '{}' must be '{}'"), PathToString<tchar>(path), StringConvert(field), StringConvert(expected));
        return false;
    }
    return true;
}

bool ReadRawPayload(const Path& path, const Core::Metascript::Value& record, const u32 limit, Core::Assets::AssetBytes& outBytes){
    AStringView fileName;
    u32 byteCount = 0u;
    if(
        !Core::Assets::ReadMetadataStringField(path, record, s_DiagnosticPrefix, "data", true, fileName)
        || !ReadU32(path, record, "byte_count", byteCount)
    )
        return false;
    if(fileName.empty() || fileName == "." || fileName == ".." || byteCount == 0u || byteCount > limit)
        return false;
    for(const char character : fileName){
        if(character == '/' || character == '\\' || character == ':' || character == '\0' || static_cast<u8>(character) < 32u){
            NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': data must be an adjacent payload basename"), PathToString<tchar>(path));
            return false;
        }
    }
    const Path sourcePath = path.parent_path() / Path(outBytes.get_allocator().arena(), fileName);
    ErrorCode error;
    if(FileSize(sourcePath, error) != byteCount || error){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': payload size mismatch or unreadable file"), PathToString<tchar>(path));
        return false;
    }
    GlobalFilesystemDetail::InputFileStream stream(sourcePath, GlobalFilesystemDetail::InputFileStream::binary);
    if(!stream.is_open())
        return false;
    outBytes.resize(byteCount);
    stream.read(reinterpret_cast<char*>(outBytes.data()), static_cast<GlobalFilesystemDetail::StreamSize>(byteCount));
    if(stream.gcount() != byteCount)
        return false;
    char extra = 0;
    stream.read(&extra, 1);
    if(stream.gcount() != 0 || !stream.eof()){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': payload size changed during read"), PathToString<tchar>(path));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

