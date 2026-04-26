// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_gltf_importer.h"

#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/geometry/tangent_frame_rebuild.h>

#include <global/binary.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deformable_gltf_importer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_GlbMagic = 0x46546C67u;
static constexpr u32 s_GlbVersion = 2u;
static constexpr u32 s_GlbJsonChunkType = 0x4E4F534Au;
static constexpr u32 s_GlbBinChunkType = 0x004E4942u;
static constexpr f32 s_MorphEpsilon = 0.000001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace JsonValueType{
    enum Enum : u8{
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };
};

struct JsonValue{
    JsonValueType::Enum type = JsonValueType::Null;
    bool boolValue = false;
    f64 numberValue = 0.0;
    AString stringValue;
    Vector<JsonValue> arrayValue;
    Vector<AString> objectKeys;
    Vector<JsonValue> objectValues;

    [[nodiscard]] bool isNull()const{ return type == JsonValueType::Null; }
    [[nodiscard]] bool isBool()const{ return type == JsonValueType::Bool; }
    [[nodiscard]] bool isNumber()const{ return type == JsonValueType::Number; }
    [[nodiscard]] bool isString()const{ return type == JsonValueType::String; }
    [[nodiscard]] bool isArray()const{ return type == JsonValueType::Array; }
    [[nodiscard]] bool isObject()const{ return type == JsonValueType::Object; }
};

class JsonParser final{
public:
    explicit JsonParser(const AStringView text)
        : m_text(text)
    {}

    [[nodiscard]] bool parse(JsonValue& outValue){
        outValue = JsonValue{};
        skipWhitespace();
        if(!parseValue(outValue))
            return false;
        skipWhitespace();
        if(m_cursor != m_text.size())
            return fail("unexpected trailing JSON content");
        return true;
    }

    [[nodiscard]] const AString& error()const{ return m_error; }

private:
    [[nodiscard]] bool fail(const AStringView message){
        m_error = StringFormat("{} at byte {}", message, m_cursor);
        return false;
    }

    [[nodiscard]] bool consume(const char expected){
        if(m_cursor >= m_text.size() || m_text[m_cursor] != expected)
            return false;
        ++m_cursor;
        return true;
    }

    void skipWhitespace(){
        while(m_cursor < m_text.size()){
            const char c = m_text[m_cursor];
            if(c != ' ' && c != '\t' && c != '\r' && c != '\n')
                break;
            ++m_cursor;
        }
    }

    [[nodiscard]] bool parseValue(JsonValue& outValue){
        if(m_cursor >= m_text.size())
            return fail("unexpected end of JSON");

        const char c = m_text[m_cursor];
        if(c == '{')
            return parseObject(outValue);
        if(c == '[')
            return parseArray(outValue);
        if(c == '"'){
            outValue.type = JsonValueType::String;
            return parseString(outValue.stringValue);
        }
        if(c == '-' || (c >= '0' && c <= '9'))
            return parseNumber(outValue);
        if(matchLiteral("true")){
            outValue.type = JsonValueType::Bool;
            outValue.boolValue = true;
            return true;
        }
        if(matchLiteral("false")){
            outValue.type = JsonValueType::Bool;
            outValue.boolValue = false;
            return true;
        }
        if(matchLiteral("null")){
            outValue.type = JsonValueType::Null;
            return true;
        }

        return fail("invalid JSON value");
    }

    [[nodiscard]] bool matchLiteral(const AStringView literal){
        if(m_text.size() - m_cursor < literal.size())
            return false;
        if(m_text.substr(m_cursor, literal.size()) != literal)
            return false;
        m_cursor += literal.size();
        return true;
    }

    [[nodiscard]] static bool HexValue(const char c, u32& outValue){
        if(c >= '0' && c <= '9'){
            outValue = static_cast<u32>(c - '0');
            return true;
        }
        if(c >= 'a' && c <= 'f'){
            outValue = static_cast<u32>(c - 'a' + 10);
            return true;
        }
        if(c >= 'A' && c <= 'F'){
            outValue = static_cast<u32>(c - 'A' + 10);
            return true;
        }
        return false;
    }

    [[nodiscard]] static bool AppendUtf8Codepoint(const u32 codepoint, AString& outString){
        if(codepoint <= 0x7Fu){
            outString.push_back(static_cast<char>(codepoint));
            return true;
        }
        if(codepoint <= 0x7FFu){
            outString.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
            outString.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
            return true;
        }
        if(codepoint <= 0xFFFFu){
            outString.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
            outString.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            outString.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
            return true;
        }
        return false;
    }

    [[nodiscard]] bool parseString(AString& outString){
        outString.clear();
        if(!consume('"'))
            return fail("expected JSON string");

        while(m_cursor < m_text.size()){
            const char c = m_text[m_cursor++];
            if(c == '"')
                return true;
            if(static_cast<unsigned char>(c) < 0x20u)
                return fail("control character in JSON string");
            if(c != '\\'){
                outString.push_back(c);
                continue;
            }

            if(m_cursor >= m_text.size())
                return fail("unterminated JSON escape");

            const char escaped = m_text[m_cursor++];
            switch(escaped){
            case '"':
            case '\\':
            case '/':
                outString.push_back(escaped);
                break;
            case 'b':
                outString.push_back('\b');
                break;
            case 'f':
                outString.push_back('\f');
                break;
            case 'n':
                outString.push_back('\n');
                break;
            case 'r':
                outString.push_back('\r');
                break;
            case 't':
                outString.push_back('\t');
                break;
            case 'u':
            {
                if(m_text.size() - m_cursor < 4u)
                    return fail("short JSON unicode escape");
                u32 codepoint = 0;
                for(u32 i = 0u; i < 4u; ++i){
                    u32 nibble = 0;
                    if(!HexValue(m_text[m_cursor + i], nibble))
                        return fail("invalid JSON unicode escape");
                    codepoint = (codepoint << 4u) | nibble;
                }
                m_cursor += 4u;
                if(!AppendUtf8Codepoint(codepoint, outString))
                    return fail("unsupported JSON unicode codepoint");
                break;
            }
            default:
                return fail("invalid JSON escape");
            }
        }

        return fail("unterminated JSON string");
    }

    [[nodiscard]] bool parseNumber(JsonValue& outValue){
        const usize begin = m_cursor;

        if(m_text[m_cursor] == '-')
            ++m_cursor;

        if(m_cursor >= m_text.size())
            return fail("unterminated JSON number");
        if(m_text[m_cursor] == '0')
            ++m_cursor;
        else if(m_text[m_cursor] >= '1' && m_text[m_cursor] <= '9'){
            while(m_cursor < m_text.size() && m_text[m_cursor] >= '0' && m_text[m_cursor] <= '9')
                ++m_cursor;
        }
        else
            return fail("invalid JSON number");

        if(m_cursor < m_text.size() && m_text[m_cursor] == '.'){
            ++m_cursor;
            if(m_cursor >= m_text.size() || m_text[m_cursor] < '0' || m_text[m_cursor] > '9')
                return fail("invalid JSON fraction");
            while(m_cursor < m_text.size() && m_text[m_cursor] >= '0' && m_text[m_cursor] <= '9')
                ++m_cursor;
        }

        if(m_cursor < m_text.size() && (m_text[m_cursor] == 'e' || m_text[m_cursor] == 'E')){
            ++m_cursor;
            if(m_cursor < m_text.size() && (m_text[m_cursor] == '+' || m_text[m_cursor] == '-'))
                ++m_cursor;
            if(m_cursor >= m_text.size() || m_text[m_cursor] < '0' || m_text[m_cursor] > '9')
                return fail("invalid JSON exponent");
            while(m_cursor < m_text.size() && m_text[m_cursor] >= '0' && m_text[m_cursor] <= '9')
                ++m_cursor;
        }

        f64 parsed = 0.0;
        if(!ParseF64FromChars(m_text.data() + begin, m_text.data() + m_cursor, parsed) || !IsFinite(parsed))
            return fail("invalid JSON number");

        outValue.type = JsonValueType::Number;
        outValue.numberValue = parsed;
        return true;
    }

    [[nodiscard]] bool parseArray(JsonValue& outValue){
        outValue = JsonValue{};
        outValue.type = JsonValueType::Array;

        if(!consume('['))
            return fail("expected JSON array");
        skipWhitespace();
        if(consume(']'))
            return true;

        while(true){
            JsonValue element;
            if(!parseValue(element))
                return false;
            outValue.arrayValue.push_back(Move(element));

            skipWhitespace();
            if(consume(']'))
                return true;
            if(!consume(','))
                return fail("expected ',' or ']' in JSON array");
            skipWhitespace();
        }
    }

    [[nodiscard]] bool parseObject(JsonValue& outValue){
        outValue = JsonValue{};
        outValue.type = JsonValueType::Object;

        if(!consume('{'))
            return fail("expected JSON object");
        skipWhitespace();
        if(consume('}'))
            return true;

        while(true){
            AString key;
            if(!parseString(key))
                return false;
            skipWhitespace();
            if(!consume(':'))
                return fail("expected ':' in JSON object");
            skipWhitespace();

            JsonValue value;
            if(!parseValue(value))
                return false;
            for(const AString& existingKey : outValue.objectKeys){
                if(existingKey == key)
                    return fail("duplicate JSON object key");
            }
            outValue.objectKeys.push_back(Move(key));
            outValue.objectValues.push_back(Move(value));

            skipWhitespace();
            if(consume('}'))
                return true;
            if(!consume(','))
                return fail("expected ',' or '}' in JSON object");
            skipWhitespace();
        }
    }

private:
    AStringView m_text;
    usize m_cursor = 0;
    AString m_error;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GltfBuffer{
    Vector<u8> data;
};

struct GltfBufferView{
    u32 buffer = 0;
    usize byteOffset = 0;
    usize byteLength = 0;
    usize byteStride = 0;
};

struct GltfAccessor{
    u32 bufferView = 0;
    usize byteOffset = 0;
    u32 componentType = 0;
    u32 componentCount = 0;
    usize count = 0;
    bool normalized = false;
};

struct GltfDocument{
    Path sourcePath;
    JsonValue root;
    Vector<GltfBuffer> buffers;
    Vector<GltfBufferView> bufferViews;
    Vector<GltfAccessor> accessors;
    Vector<u8> glbBinaryChunk;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const JsonValue* FindMember(const JsonValue& object, const AStringView memberName){
    if(!object.isObject())
        return nullptr;

    for(usize i = 0u; i < object.objectKeys.size(); ++i){
        if(object.objectKeys[i] == memberName)
            return &object.objectValues[i];
    }
    return nullptr;
}

[[nodiscard]] const JsonValue* FindRequiredMember(
    const Path& sourcePath,
    const JsonValue& object,
    const AStringView memberName,
    const JsonValueType::Enum type)
{
    const JsonValue* member = FindMember(object, memberName);
    if(member && member->type == type)
        return member;

    NWB_LOGGER_ERROR(
        NWB_TEXT("Deformable glTF import '{}': required JSON member '{}' is missing or has the wrong type"),
        PathToString<tchar>(sourcePath),
        StringConvert(memberName)
    );
    return nullptr;
}

[[nodiscard]] bool ReadU32FromJsonNumber(
    const Path& sourcePath,
    const JsonValue& value,
    const AStringView label,
    u32& outValue)
{
    if(!value.isNumber()
        || !IsFinite(value.numberValue)
        || value.numberValue < 0.0
        || value.numberValue != Floor(value.numberValue)
        || value.numberValue > static_cast<f64>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': '{}' must be a non-negative u32"),
            PathToString<tchar>(sourcePath),
            StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<u32>(value.numberValue);
    return true;
}

[[nodiscard]] bool ReadSizeFromJsonNumber(
    const Path& sourcePath,
    const JsonValue& value,
    const AStringView label,
    usize& outValue)
{
    u32 parsed = 0;
    if(!ReadU32FromJsonNumber(sourcePath, value, label, parsed))
        return false;

    outValue = static_cast<usize>(parsed);
    return true;
}

[[nodiscard]] bool ReadOptionalU32Member(
    const Path& sourcePath,
    const JsonValue& object,
    const AStringView memberName,
    u32& inOutValue)
{
    const JsonValue* member = FindMember(object, memberName);
    if(!member)
        return true;

    return ReadU32FromJsonNumber(sourcePath, *member, memberName, inOutValue);
}

[[nodiscard]] bool ReadOptionalSizeMember(
    const Path& sourcePath,
    const JsonValue& object,
    const AStringView memberName,
    usize& inOutValue)
{
    const JsonValue* member = FindMember(object, memberName);
    if(!member)
        return true;

    return ReadSizeFromJsonNumber(sourcePath, *member, memberName, inOutValue);
}

[[nodiscard]] bool ReadRequiredU32Member(
    const Path& sourcePath,
    const JsonValue& object,
    const AStringView memberName,
    u32& outValue)
{
    const JsonValue* member = FindRequiredMember(sourcePath, object, memberName, JsonValueType::Number);
    return member && ReadU32FromJsonNumber(sourcePath, *member, memberName, outValue);
}

[[nodiscard]] bool ReadRequiredSizeMember(
    const Path& sourcePath,
    const JsonValue& object,
    const AStringView memberName,
    usize& outValue)
{
    const JsonValue* member = FindRequiredMember(sourcePath, object, memberName, JsonValueType::Number);
    return member && ReadSizeFromJsonNumber(sourcePath, *member, memberName, outValue);
}

[[nodiscard]] bool ReadOptionalBoolMember(
    const Path& sourcePath,
    const JsonValue& object,
    const AStringView memberName,
    bool& inOutValue)
{
    const JsonValue* member = FindMember(object, memberName);
    if(!member)
        return true;
    if(member->isBool()){
        inOutValue = member->boolValue;
        return true;
    }

    NWB_LOGGER_ERROR(
        NWB_TEXT("Deformable glTF import '{}': '{}' must be a bool"),
        PathToString<tchar>(sourcePath),
        StringConvert(memberName)
    );
    return false;
}

[[nodiscard]] bool ReadOptionalStringMember(
    const Path& sourcePath,
    const JsonValue& object,
    const AStringView memberName,
    AString& outValue)
{
    outValue.clear();

    const JsonValue* member = FindMember(object, memberName);
    if(!member)
        return true;
    if(member->isString()){
        outValue = member->stringValue;
        return true;
    }

    NWB_LOGGER_ERROR(
        NWB_TEXT("Deformable glTF import '{}': '{}' must be a string"),
        PathToString<tchar>(sourcePath),
        StringConvert(memberName)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool LoadJsonTextFromGltf(const Path& sourcePath, AString& outJsonText){
    outJsonText.clear();
    if(!ReadTextFile(sourcePath, outJsonText)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import: failed to read glTF JSON '{}'"),
            PathToString<tchar>(sourcePath)
        );
        return false;
    }

    StripUtf8Bom(outJsonText);
    return true;
}

[[nodiscard]] bool ReadGlbChunk(
    const Vector<u8>& bytes,
    usize& inOutCursor,
    u32& outChunkType,
    AString& outJsonText,
    Vector<u8>& outBinaryChunk)
{
    u32 chunkLength = 0;
    if(!ReadPOD(bytes, inOutCursor, chunkLength) || !ReadPOD(bytes, inOutCursor, outChunkType))
        return false;
    if(chunkLength > bytes.size() - inOutCursor)
        return false;

    if(outChunkType == s_GlbJsonChunkType){
        outJsonText.assign(
            reinterpret_cast<const char*>(bytes.data() + inOutCursor),
            static_cast<usize>(chunkLength)
        );
    }
    else if(outChunkType == s_GlbBinChunkType){
        outBinaryChunk.assign(
            bytes.begin() + static_cast<isize>(inOutCursor),
            bytes.begin() + static_cast<isize>(inOutCursor + chunkLength)
        );
    }

    inOutCursor += chunkLength;
    return true;
}

[[nodiscard]] bool LoadJsonTextFromGlb(const Path& sourcePath, AString& outJsonText, Vector<u8>& outBinaryChunk){
    outJsonText.clear();
    outBinaryChunk.clear();

    Vector<u8> bytes;
    ErrorCode errorCode;
    if(!ReadBinaryFile(sourcePath, bytes, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import: failed to read GLB '{}': {}"),
            PathToString<tchar>(sourcePath),
            StringConvert(errorCode ? errorCode.message() : AString("read failed"))
        );
        return false;
    }

    usize cursor = 0;
    u32 magic = 0;
    u32 version = 0;
    u32 length = 0;
    if(!ReadPOD(bytes, cursor, magic) || !ReadPOD(bytes, cursor, version) || !ReadPOD(bytes, cursor, length)
        || magic != s_GlbMagic
        || version != s_GlbVersion
        || length != bytes.size()
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': invalid GLB header"),
            PathToString<tchar>(sourcePath)
        );
        return false;
    }

    bool sawJson = false;
    while(cursor < bytes.size()){
        u32 chunkType = 0;
        if(!ReadGlbChunk(bytes, cursor, chunkType, outJsonText, outBinaryChunk)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': malformed GLB chunk table"),
                PathToString<tchar>(sourcePath)
            );
            return false;
        }
        if(chunkType == s_GlbJsonChunkType)
            sawJson = true;
    }

    if(!sawJson || outJsonText.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': GLB JSON chunk is missing"),
            PathToString<tchar>(sourcePath)
        );
        return false;
    }

    StripUtf8Bom(outJsonText);
    return true;
}

[[nodiscard]] bool ParseGltfJson(const Path& sourcePath, const AStringView jsonText, JsonValue& outRoot){
    JsonParser parser(jsonText);
    if(parser.parse(outRoot) && outRoot.isObject())
        return true;

    NWB_LOGGER_ERROR(
        NWB_TEXT("Deformable glTF import '{}': JSON parse failed: {}"),
        PathToString<tchar>(sourcePath),
        StringConvert(parser.error())
    );
    return false;
}

[[nodiscard]] bool LoadGltfRoot(GltfDocument& inOutDocument){
    AString jsonText;
    const AString extension = CanonicalizeText(PathToString(inOutDocument.sourcePath.extension()));
    if(extension == ".glb"){
        if(!LoadJsonTextFromGlb(inOutDocument.sourcePath, jsonText, inOutDocument.glbBinaryChunk))
            return false;
    }
    else if(extension == ".gltf"){
        if(!LoadJsonTextFromGltf(inOutDocument.sourcePath, jsonText))
            return false;
    }
    else{
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': source extension must be .gltf or .glb"),
            PathToString<tchar>(inOutDocument.sourcePath)
        );
        return false;
    }

    return ParseGltfJson(inOutDocument.sourcePath, jsonText, inOutDocument.root);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ResolveExternalUri(const Path& sourcePath, const AStringView uri, Path& outPath){
    outPath.clear();

    if(uri.find(':') != AStringView::npos){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': URI '{}' is not a local external buffer"),
            PathToString<tchar>(sourcePath),
            StringConvert(uri)
        );
        return false;
    }

    ErrorCode errorCode;
    if(!ResolveAbsolutePath(sourcePath.parent_path(), uri, outPath, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': failed to resolve buffer URI '{}': {}"),
            PathToString<tchar>(sourcePath),
            StringConvert(uri),
            StringConvert(errorCode ? errorCode.message() : AString("invalid path"))
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool IsBase64DataUri(const AStringView uri, AStringView& outPayload){
    outPayload = {};
    static constexpr AStringView s_DataPrefix = "data:";
    static constexpr AStringView s_Base64Marker = ";base64";

    if(uri.size() < s_DataPrefix.size() || uri.substr(0u, s_DataPrefix.size()) != s_DataPrefix)
        return false;

    const usize comma = uri.find(',');
    if(comma == AStringView::npos)
        return false;

    const AStringView metadata = uri.substr(s_DataPrefix.size(), comma - s_DataPrefix.size());
    if(metadata.find(s_Base64Marker) == AStringView::npos)
        return false;

    outPayload = uri.substr(comma + 1u);
    return true;
}

[[nodiscard]] bool Base64Value(const char c, u8& outValue){
    if(c >= 'A' && c <= 'Z'){
        outValue = static_cast<u8>(c - 'A');
        return true;
    }
    if(c >= 'a' && c <= 'z'){
        outValue = static_cast<u8>(c - 'a' + 26);
        return true;
    }
    if(c >= '0' && c <= '9'){
        outValue = static_cast<u8>(c - '0' + 52);
        return true;
    }
    if(c == '+'){
        outValue = 62u;
        return true;
    }
    if(c == '/'){
        outValue = 63u;
        return true;
    }
    return false;
}

[[nodiscard]] bool DecodeBase64DataUri(const Path& sourcePath, const AStringView uri, Vector<u8>& outBytes){
    outBytes.clear();

    AStringView payload;
    if(!IsBase64DataUri(uri, payload)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': data URI buffers must be base64 encoded"),
            PathToString<tchar>(sourcePath)
        );
        return false;
    }
    if((payload.size() % 4u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': base64 data URI has invalid length"),
            PathToString<tchar>(sourcePath)
        );
        return false;
    }

    outBytes.reserve((payload.size() / 4u) * 3u);
    for(usize cursor = 0u; cursor < payload.size(); cursor += 4u){
        u8 values[4] = {};
        u32 padding = 0u;
        for(u32 i = 0u; i < 4u; ++i){
            const char c = payload[cursor + i];
            if(c == '='){
                if(i < 2u){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Deformable glTF import '{}': base64 data URI contains invalid padding"),
                        PathToString<tchar>(sourcePath)
                    );
                    return false;
                }
                values[i] = 0u;
                ++padding;
                continue;
            }
            if(padding != 0u || !Base64Value(c, values[i])){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Deformable glTF import '{}': base64 data URI contains invalid characters"),
                    PathToString<tchar>(sourcePath)
                );
                return false;
            }
        }
        if(padding > 2u || (padding != 0u && cursor + 4u != payload.size())){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': base64 data URI contains invalid padding"),
                PathToString<tchar>(sourcePath)
            );
            return false;
        }

        const u32 triple =
            (static_cast<u32>(values[0]) << 18u)
            | (static_cast<u32>(values[1]) << 12u)
            | (static_cast<u32>(values[2]) << 6u)
            | static_cast<u32>(values[3])
        ;
        outBytes.push_back(static_cast<u8>((triple >> 16u) & 0xFFu));
        if(padding < 2u)
            outBytes.push_back(static_cast<u8>((triple >> 8u) & 0xFFu));
        if(padding < 1u)
            outBytes.push_back(static_cast<u8>(triple & 0xFFu));
    }

    return true;
}

[[nodiscard]] bool LoadGltfBuffers(GltfDocument& inOutDocument){
    const JsonValue* buffers = FindRequiredMember(
        inOutDocument.sourcePath,
        inOutDocument.root,
        "buffers",
        JsonValueType::Array
    );
    if(!buffers)
        return false;

    inOutDocument.buffers.clear();
    inOutDocument.buffers.reserve(buffers->arrayValue.size());
    for(usize i = 0u; i < buffers->arrayValue.size(); ++i){
        const JsonValue& bufferValue = buffers->arrayValue[i];
        if(!bufferValue.isObject()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': buffers[{}] must be an object"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i
            );
            return false;
        }

        usize byteLength = 0;
        if(!ReadRequiredSizeMember(inOutDocument.sourcePath, bufferValue, "byteLength", byteLength))
            return false;

        AString uri;
        if(!ReadOptionalStringMember(inOutDocument.sourcePath, bufferValue, "uri", uri))
            return false;

        GltfBuffer buffer;
        if(!uri.empty() && uri.substr(0u, Min(uri.size(), static_cast<usize>(5u))) == "data:"){
            if(!DecodeBase64DataUri(inOutDocument.sourcePath, uri, buffer.data))
                return false;
        }
        else if(!uri.empty()){
            Path bufferPath;
            if(!ResolveExternalUri(inOutDocument.sourcePath, uri, bufferPath))
                return false;

            ErrorCode errorCode;
            if(!ReadBinaryFile(bufferPath, buffer.data, errorCode)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Deformable glTF import '{}': failed to read buffer '{}': {}"),
                    PathToString<tchar>(inOutDocument.sourcePath),
                    PathToString<tchar>(bufferPath),
                    StringConvert(errorCode ? errorCode.message() : AString("read failed"))
                );
                return false;
            }
        }
        else if(i == 0u && !inOutDocument.glbBinaryChunk.empty()){
            buffer.data = inOutDocument.glbBinaryChunk;
        }
        else{
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': buffers[{}] has no URI and no GLB BIN chunk"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i
            );
            return false;
        }

        if(buffer.data.size() < byteLength){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': buffers[{}] is shorter than declared byteLength"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i
            );
            return false;
        }

        inOutDocument.buffers.push_back(Move(buffer));
    }

    return !inOutDocument.buffers.empty();
}

[[nodiscard]] bool LoadGltfBufferViews(GltfDocument& inOutDocument){
    const JsonValue* bufferViews = FindRequiredMember(
        inOutDocument.sourcePath,
        inOutDocument.root,
        "bufferViews",
        JsonValueType::Array
    );
    if(!bufferViews)
        return false;

    inOutDocument.bufferViews.clear();
    inOutDocument.bufferViews.reserve(bufferViews->arrayValue.size());
    for(usize i = 0u; i < bufferViews->arrayValue.size(); ++i){
        const JsonValue& viewValue = bufferViews->arrayValue[i];
        if(!viewValue.isObject()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': bufferViews[{}] must be an object"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i
            );
            return false;
        }

        GltfBufferView view;
        if(!ReadRequiredU32Member(inOutDocument.sourcePath, viewValue, "buffer", view.buffer)
            || !ReadRequiredSizeMember(inOutDocument.sourcePath, viewValue, "byteLength", view.byteLength)
            || !ReadOptionalSizeMember(inOutDocument.sourcePath, viewValue, "byteOffset", view.byteOffset)
            || !ReadOptionalSizeMember(inOutDocument.sourcePath, viewValue, "byteStride", view.byteStride)
        )
            return false;
        if(view.buffer >= inOutDocument.buffers.size()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': bufferViews[{}] references missing buffer {}"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i,
                view.buffer
            );
            return false;
        }

        const Vector<u8>& buffer = inOutDocument.buffers[view.buffer].data;
        if(view.byteOffset > buffer.size() || view.byteLength > buffer.size() - view.byteOffset){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': bufferViews[{}] range exceeds buffer"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i
            );
            return false;
        }

        inOutDocument.bufferViews.push_back(view);
    }

    return !inOutDocument.bufferViews.empty();
}

[[nodiscard]] u32 ComponentCountForAccessorType(const AStringView typeText){
    if(typeText == "SCALAR")
        return 1u;
    if(typeText == "VEC2")
        return 2u;
    if(typeText == "VEC3")
        return 3u;
    if(typeText == "VEC4")
        return 4u;
    return 0u;
}

[[nodiscard]] bool LoadGltfAccessors(GltfDocument& inOutDocument){
    const JsonValue* accessors = FindRequiredMember(
        inOutDocument.sourcePath,
        inOutDocument.root,
        "accessors",
        JsonValueType::Array
    );
    if(!accessors)
        return false;

    inOutDocument.accessors.clear();
    inOutDocument.accessors.reserve(accessors->arrayValue.size());
    for(usize i = 0u; i < accessors->arrayValue.size(); ++i){
        const JsonValue& accessorValue = accessors->arrayValue[i];
        if(!accessorValue.isObject()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': accessors[{}] must be an object"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i
            );
            return false;
        }
        if(FindMember(accessorValue, "sparse")){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': sparse accessors are not supported"),
                PathToString<tchar>(inOutDocument.sourcePath)
            );
            return false;
        }

        GltfAccessor accessor;
        AString typeText;
        if(!ReadRequiredU32Member(inOutDocument.sourcePath, accessorValue, "bufferView", accessor.bufferView)
            || !ReadRequiredU32Member(inOutDocument.sourcePath, accessorValue, "componentType", accessor.componentType)
            || !ReadRequiredSizeMember(inOutDocument.sourcePath, accessorValue, "count", accessor.count)
            || !ReadOptionalSizeMember(inOutDocument.sourcePath, accessorValue, "byteOffset", accessor.byteOffset)
            || !ReadOptionalBoolMember(inOutDocument.sourcePath, accessorValue, "normalized", accessor.normalized)
            || !ReadOptionalStringMember(inOutDocument.sourcePath, accessorValue, "type", typeText)
        )
            return false;
        if(typeText.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': accessor type is required"),
                PathToString<tchar>(inOutDocument.sourcePath)
            );
            return false;
        }

        accessor.componentCount = ComponentCountForAccessorType(typeText);
        if(accessor.bufferView >= inOutDocument.bufferViews.size() || accessor.componentCount == 0u || accessor.count == 0u){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': accessors[{}] is invalid"),
                PathToString<tchar>(inOutDocument.sourcePath),
                i
            );
            return false;
        }

        inOutDocument.accessors.push_back(accessor);
    }

    return !inOutDocument.accessors.empty();
}

[[nodiscard]] bool LoadGltfDocument(const Path& sourcePath, GltfDocument& outDocument){
    outDocument = GltfDocument{};
    outDocument.sourcePath = sourcePath;

    return LoadGltfRoot(outDocument)
        && LoadGltfBuffers(outDocument)
        && LoadGltfBufferViews(outDocument)
        && LoadGltfAccessors(outDocument)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] usize ComponentByteSize(const u32 componentType){
    switch(componentType){
    case 5120u: // BYTE
    case 5121u: // UNSIGNED_BYTE
        return 1u;
    case 5122u: // SHORT
    case 5123u: // UNSIGNED_SHORT
        return 2u;
    case 5125u: // UNSIGNED_INT
    case 5126u: // FLOAT
        return 4u;
    default:
        return 0u;
    }
}

[[nodiscard]] bool AccessorElementComponentOffset(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize elementIndex,
    const u32 componentIndex,
    usize& outOffset)
{
    outOffset = 0u;
    if(accessorIndex >= document.accessors.size())
        return false;

    const GltfAccessor& accessor = document.accessors[accessorIndex];
    if(componentIndex >= accessor.componentCount || elementIndex >= accessor.count)
        return false;

    const GltfBufferView& view = document.bufferViews[accessor.bufferView];
    const usize componentSize = ComponentByteSize(accessor.componentType);
    if(componentSize == 0u)
        return false;

    const usize packedElementSize = componentSize * static_cast<usize>(accessor.componentCount);
    const usize stride = view.byteStride != 0u ? view.byteStride : packedElementSize;
    if(stride < packedElementSize)
        return false;
    if(elementIndex > Limit<usize>::s_Max / stride)
        return false;

    const usize relativeOffset = accessor.byteOffset
        + (elementIndex * stride)
        + (static_cast<usize>(componentIndex) * componentSize)
    ;
    if(relativeOffset > view.byteLength || componentSize > view.byteLength - relativeOffset)
        return false;

    outOffset = view.byteOffset + relativeOffset;
    return true;
}

template<typename ValueT>
[[nodiscard]] bool ReadScalarAt(const Vector<u8>& bytes, const usize offset, ValueT& outValue){
    if(offset > bytes.size() || sizeof(ValueT) > bytes.size() - offset)
        return false;

    NWB_MEMCPY(&outValue, sizeof(ValueT), bytes.data() + offset, sizeof(ValueT));
    return true;
}

[[nodiscard]] bool ReadAccessorFloatComponent(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize elementIndex,
    const u32 componentIndex,
    f32& outValue)
{
    outValue = 0.0f;

    usize offset = 0;
    if(!AccessorElementComponentOffset(document, accessorIndex, elementIndex, componentIndex, offset))
        return false;

    const GltfAccessor& accessor = document.accessors[accessorIndex];
    const GltfBufferView& view = document.bufferViews[accessor.bufferView];
    const Vector<u8>& bytes = document.buffers[view.buffer].data;

    switch(accessor.componentType){
    case 5120u:
    {
        i8 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = accessor.normalized
            ? Max(static_cast<f32>(value) / 127.0f, -1.0f)
            : static_cast<f32>(value)
        ;
        return true;
    }
    case 5121u:
    {
        u8 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = accessor.normalized
            ? static_cast<f32>(value) / 255.0f
            : static_cast<f32>(value)
        ;
        return true;
    }
    case 5122u:
    {
        i16 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = accessor.normalized
            ? Max(static_cast<f32>(value) / 32767.0f, -1.0f)
            : static_cast<f32>(value)
        ;
        return true;
    }
    case 5123u:
    {
        u16 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = accessor.normalized
            ? static_cast<f32>(value) / 65535.0f
            : static_cast<f32>(value)
        ;
        return true;
    }
    case 5125u:
    {
        u32 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = static_cast<f32>(value);
        return true;
    }
    case 5126u:
    {
        f32 value = 0.0f;
        if(!ReadScalarAt(bytes, offset, value) || !IsFinite(value))
            return false;
        outValue = value;
        return true;
    }
    default:
        return false;
    }
}

[[nodiscard]] bool ReadAccessorU32Component(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize elementIndex,
    const u32 componentIndex,
    u32& outValue)
{
    outValue = 0u;

    usize offset = 0;
    if(!AccessorElementComponentOffset(document, accessorIndex, elementIndex, componentIndex, offset))
        return false;

    const GltfAccessor& accessor = document.accessors[accessorIndex];
    if(accessor.normalized)
        return false;

    const GltfBufferView& view = document.bufferViews[accessor.bufferView];
    const Vector<u8>& bytes = document.buffers[view.buffer].data;
    switch(accessor.componentType){
    case 5121u:
    {
        u8 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = static_cast<u32>(value);
        return true;
    }
    case 5123u:
    {
        u16 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = static_cast<u32>(value);
        return true;
    }
    case 5125u:
    {
        u32 value = 0;
        if(!ReadScalarAt(bytes, offset, value))
            return false;
        outValue = value;
        return true;
    }
    default:
        return false;
    }
}

[[nodiscard]] bool ReadAccessorFloatTuple(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize elementIndex,
    f32* outValues,
    const u32 expectedComponentCount)
{
    if(accessorIndex >= document.accessors.size())
        return false;

    const GltfAccessor& accessor = document.accessors[accessorIndex];
    if(accessor.componentCount != expectedComponentCount)
        return false;

    for(u32 i = 0u; i < expectedComponentCount; ++i){
        if(!ReadAccessorFloatComponent(document, accessorIndex, elementIndex, i, outValues[i]))
            return false;
    }
    return true;
}

[[nodiscard]] bool FindPrimitiveAttributeAccessor(
    const JsonValue& primitive,
    const AStringView attributeName,
    u32& outAccessorIndex)
{
    const JsonValue* attributes = FindMember(primitive, "attributes");
    if(!attributes || !attributes->isObject())
        return false;

    const JsonValue* accessor = FindMember(*attributes, attributeName);
    if(!accessor)
        return false;
    if(!accessor->isNumber())
        return false;

    if(accessor->numberValue < 0.0
        || accessor->numberValue != Floor(accessor->numberValue)
        || accessor->numberValue > static_cast<f64>(Limit<u32>::s_Max)
    )
        return false;

    outAccessorIndex = static_cast<u32>(accessor->numberValue);
    return true;
}

[[nodiscard]] bool FindTargetAttributeAccessor(
    const JsonValue& target,
    const AStringView attributeName,
    u32& outAccessorIndex)
{
    const JsonValue* accessor = FindMember(target, attributeName);
    if(!accessor)
        return false;
    if(!accessor->isNumber())
        return false;

    if(accessor->numberValue < 0.0
        || accessor->numberValue != Floor(accessor->numberValue)
        || accessor->numberValue > static_cast<f64>(Limit<u32>::s_Max)
    )
        return false;

    outAccessorIndex = static_cast<u32>(accessor->numberValue);
    return true;
}

[[nodiscard]] bool ReadAccessorIndexList(
    const GltfDocument& document,
    const u32 accessorIndex,
    Vector<u32>& outIndices)
{
    outIndices.clear();
    if(accessorIndex >= document.accessors.size())
        return false;

    const GltfAccessor& accessor = document.accessors[accessorIndex];
    if(accessor.componentCount != 1u || accessor.count == 0u)
        return false;

    outIndices.reserve(accessor.count);
    for(usize i = 0u; i < accessor.count; ++i){
        u32 index = 0;
        if(!ReadAccessorU32Component(document, accessorIndex, i, 0u, index))
            return false;
        outIndices.push_back(index);
    }
    return true;
}

[[nodiscard]] bool AccessorHasVertexCount(const GltfDocument& document, const u32 accessorIndex, const usize vertexCount){
    return accessorIndex < document.accessors.size()
        && document.accessors[accessorIndex].count == vertexCount
    ;
}

[[nodiscard]] bool ReadFloat3Stream(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize vertexCount,
    Vector<Float3U>& outValues)
{
    outValues.clear();
    if(!AccessorHasVertexCount(document, accessorIndex, vertexCount))
        return false;

    outValues.reserve(vertexCount);
    for(usize i = 0u; i < vertexCount; ++i){
        f32 tuple[3] = {};
        if(!ReadAccessorFloatTuple(document, accessorIndex, i, tuple, 3u))
            return false;
        outValues.push_back(Float3U(tuple[0], tuple[1], tuple[2]));
    }
    return true;
}

[[nodiscard]] bool ReadFloat2Stream(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize vertexCount,
    Vector<Float2U>& outValues)
{
    outValues.clear();
    if(!AccessorHasVertexCount(document, accessorIndex, vertexCount))
        return false;

    outValues.reserve(vertexCount);
    for(usize i = 0u; i < vertexCount; ++i){
        f32 tuple[2] = {};
        if(!ReadAccessorFloatTuple(document, accessorIndex, i, tuple, 2u))
            return false;
        outValues.push_back(Float2U(tuple[0], tuple[1]));
    }
    return true;
}

[[nodiscard]] bool ReadFloat4Stream(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize vertexCount,
    Vector<Float4U>& outValues)
{
    outValues.clear();
    if(!AccessorHasVertexCount(document, accessorIndex, vertexCount))
        return false;

    outValues.reserve(vertexCount);
    for(usize i = 0u; i < vertexCount; ++i){
        f32 tuple[4] = {};
        if(!ReadAccessorFloatTuple(document, accessorIndex, i, tuple, 4u))
            return false;
        outValues.push_back(Float4U(tuple[0], tuple[1], tuple[2], tuple[3]));
    }
    return true;
}

[[nodiscard]] bool ReadColorStream(
    const GltfDocument& document,
    const u32 accessorIndex,
    const usize vertexCount,
    Vector<Float4U>& outValues)
{
    outValues.clear();
    if(!AccessorHasVertexCount(document, accessorIndex, vertexCount))
        return false;

    const GltfAccessor& accessor = document.accessors[accessorIndex];
    if(accessor.componentCount != 3u && accessor.componentCount != 4u)
        return false;

    outValues.reserve(vertexCount);
    for(usize i = 0u; i < vertexCount; ++i){
        f32 tuple[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        for(u32 component = 0u; component < accessor.componentCount; ++component){
            if(!ReadAccessorFloatComponent(document, accessorIndex, i, component, tuple[component]))
                return false;
        }
        outValues.push_back(Float4U(tuple[0], tuple[1], tuple[2], tuple[3]));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AssignDefaultUv0(const usize vertexCount, Vector<Float2U>& outUv0){
    outUv0.resize(vertexCount);
    for(Float2U& uv : outUv0)
        uv = Float2U(0.0f, 0.0f);
}

void AssignDefaultColors(const usize vertexCount, Vector<Float4U>& outColors){
    outColors.resize(vertexCount);
    for(Float4U& color : outColors)
        color = Float4U(1.0f, 1.0f, 1.0f, 1.0f);
}

void AssignPlaceholderFrames(Vector<DeformableVertexRest>& vertices){
    for(DeformableVertexRest& vertex : vertices){
        vertex.normal = Float3U(0.0f, 0.0f, 1.0f);
        vertex.tangent = Float4U(1.0f, 0.0f, 0.0f, 1.0f);
    }
}

[[nodiscard]] bool RebuildImportedTangentFrames(Vector<DeformableVertexRest>& vertices, const Vector<u32>& indices){
    Vector<Core::Geometry::TangentFrameRebuildVertex> rebuildVertices;
    rebuildVertices.resize(vertices.size());
    for(usize i = 0u; i < vertices.size(); ++i){
        rebuildVertices[i].position = vertices[i].position;
        rebuildVertices[i].uv0 = vertices[i].uv0;
        rebuildVertices[i].normal = vertices[i].normal;
        rebuildVertices[i].tangent = vertices[i].tangent;
    }

    if(!Core::Geometry::RebuildTangentFrames(rebuildVertices, indices))
        return false;

    for(usize i = 0u; i < vertices.size(); ++i){
        vertices[i].normal = rebuildVertices[i].normal;
        vertices[i].tangent = rebuildVertices[i].tangent;
    }
    return true;
}

[[nodiscard]] bool BuildRestVerticesFromPrimitive(
    const GltfDocument& document,
    const JsonValue& primitive,
    const Vector<u32>& indices,
    Vector<DeformableVertexRest>& outVertices)
{
    outVertices.clear();

    u32 positionAccessor = 0;
    if(!FindPrimitiveAttributeAccessor(primitive, "POSITION", positionAccessor) || positionAccessor >= document.accessors.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': primitive requires POSITION attribute"),
            PathToString<tchar>(document.sourcePath)
        );
        return false;
    }

    const usize vertexCount = document.accessors[positionAccessor].count;
    Vector<Float3U> positions;
    if(!ReadFloat3Stream(document, positionAccessor, vertexCount, positions)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': failed to read POSITION stream"),
            PathToString<tchar>(document.sourcePath)
        );
        return false;
    }

    Vector<Float2U> uv0;
    u32 uvAccessor = 0;
    if(FindPrimitiveAttributeAccessor(primitive, "TEXCOORD_0", uvAccessor)){
        if(!ReadFloat2Stream(document, uvAccessor, vertexCount, uv0)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': failed to read TEXCOORD_0 stream"),
                PathToString<tchar>(document.sourcePath)
            );
            return false;
        }
    }
    else
        AssignDefaultUv0(vertexCount, uv0);

    Vector<Float4U> colors;
    u32 colorAccessor = 0;
    if(FindPrimitiveAttributeAccessor(primitive, "COLOR_0", colorAccessor)){
        if(!ReadColorStream(document, colorAccessor, vertexCount, colors)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': failed to read COLOR_0 stream"),
                PathToString<tchar>(document.sourcePath)
            );
            return false;
        }
    }
    else
        AssignDefaultColors(vertexCount, colors);

    outVertices.resize(vertexCount);
    for(usize i = 0u; i < vertexCount; ++i){
        outVertices[i].position = positions[i];
        outVertices[i].uv0 = uv0[i];
        outVertices[i].color0 = colors[i];
    }

    bool hasCompleteFrame = false;
    u32 normalAccessor = 0;
    u32 tangentAccessor = 0;
    if(FindPrimitiveAttributeAccessor(primitive, "NORMAL", normalAccessor)
        && FindPrimitiveAttributeAccessor(primitive, "TANGENT", tangentAccessor)
    ){
        Vector<Float3U> normals;
        Vector<Float4U> tangents;
        if(!ReadFloat3Stream(document, normalAccessor, vertexCount, normals)
            || !ReadFloat4Stream(document, tangentAccessor, vertexCount, tangents)
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': failed to read NORMAL/TANGENT streams"),
                PathToString<tchar>(document.sourcePath)
            );
            return false;
        }

        for(usize i = 0u; i < vertexCount; ++i){
            outVertices[i].normal = normals[i];
            outVertices[i].tangent = tangents[i];
        }
        hasCompleteFrame = true;
    }
    else
        AssignPlaceholderFrames(outVertices);

    if(!hasCompleteFrame && !RebuildImportedTangentFrames(outVertices, indices)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': failed to generate missing normal/tangent frames"),
            PathToString<tchar>(document.sourcePath)
        );
        return false;
    }

    return true;
}

[[nodiscard]] bool BuildSkinFromPrimitive(
    const GltfDocument& document,
    const JsonValue& primitive,
    const usize vertexCount,
    Vector<SkinInfluence4>& outSkin)
{
    outSkin.clear();

    u32 jointsAccessor = 0;
    u32 weightsAccessor = 0;
    const bool hasJoints = FindPrimitiveAttributeAccessor(primitive, "JOINTS_0", jointsAccessor);
    const bool hasWeights = FindPrimitiveAttributeAccessor(primitive, "WEIGHTS_0", weightsAccessor);
    if(!hasJoints && !hasWeights)
        return true;
    if(!hasJoints || !hasWeights
        || !AccessorHasVertexCount(document, jointsAccessor, vertexCount)
        || !AccessorHasVertexCount(document, weightsAccessor, vertexCount)
        || document.accessors[jointsAccessor].componentCount != 4u
        || document.accessors[weightsAccessor].componentCount != 4u
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': JOINTS_0 and WEIGHTS_0 must both be VEC4 streams"),
            PathToString<tchar>(document.sourcePath)
        );
        return false;
    }

    outSkin.resize(vertexCount);
    for(usize i = 0u; i < vertexCount; ++i){
        SkinInfluence4 influence;
        f32 weightSum = 0.0f;
        for(u32 component = 0u; component < 4u; ++component){
            u32 joint = 0;
            f32 weight = 0.0f;
            if(!ReadAccessorU32Component(document, jointsAccessor, i, component, joint)
                || joint > Limit<u16>::s_Max
                || !ReadAccessorFloatComponent(document, weightsAccessor, i, component, weight)
                || !IsFinite(weight)
                || weight < 0.0f
            ){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Deformable glTF import '{}': invalid skin influence at vertex {}"),
                    PathToString<tchar>(document.sourcePath),
                    i
                );
                return false;
            }

            influence.joint[component] = static_cast<u16>(joint);
            influence.weight[component] = weight;
            weightSum += weight;
        }

        if(!IsFinite(weightSum) || weightSum <= 0.0f){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': zero skin weight sum at vertex {}"),
                PathToString<tchar>(document.sourcePath),
                i
            );
            return false;
        }

        const f32 invWeightSum = 1.0f / weightSum;
        for(f32& weight : influence.weight)
            weight *= invWeightSum;
        outSkin[i] = influence;
    }

    return true;
}

[[nodiscard]] bool BuildSourceSamples(
    const GltfDocument& document,
    const usize vertexCount,
    const Vector<u32>& indices,
    Vector<SourceSample>& outSourceSamples)
{
    outSourceSamples.clear();
    if(vertexCount > static_cast<usize>(Limit<u32>::s_Max) || (indices.size() % 3u) != 0u)
        return false;

    Vector<u8> assigned;
    assigned.resize(vertexCount, 0u);
    outSourceSamples.resize(vertexCount);

    const usize triangleCount = indices.size() / 3u;
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const usize indexBase = triangleIndex * 3u;
        for(u32 corner = 0u; corner < 3u; ++corner){
            const u32 vertexId = indices[indexBase + corner];
            if(vertexId >= vertexCount)
                return false;
            if(assigned[vertexId])
                continue;

            SourceSample& sample = outSourceSamples[vertexId];
            sample.sourceTri = static_cast<u32>(triangleIndex);
            sample.bary[0] = corner == 0u ? 1.0f : 0.0f;
            sample.bary[1] = corner == 1u ? 1.0f : 0.0f;
            sample.bary[2] = corner == 2u ? 1.0f : 0.0f;
            assigned[vertexId] = 1u;
        }
    }

    for(usize vertexIndex = 0u; vertexIndex < assigned.size(); ++vertexIndex){
        if(assigned[vertexIndex])
            continue;

        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': vertex {} is not referenced by any triangle"),
            PathToString<tchar>(document.sourcePath),
            vertexIndex
        );
        return false;
    }

    return true;
}

[[nodiscard]] bool ReadTargetNames(const JsonValue& mesh, Vector<AString>& outTargetNames){
    outTargetNames.clear();

    const JsonValue* extras = FindMember(mesh, "extras");
    if(!extras || !extras->isObject())
        return true;

    const JsonValue* targetNames = FindMember(*extras, "targetNames");
    if(!targetNames || !targetNames->isArray())
        return true;

    outTargetNames.reserve(targetNames->arrayValue.size());
    for(const JsonValue& value : targetNames->arrayValue){
        if(value.isString())
            outTargetNames.push_back(value.stringValue);
        else
            outTargetNames.push_back(AString());
    }
    return true;
}

[[nodiscard]] bool ActiveMorphDelta(const DeformableMorphDelta& delta){
    return Abs(delta.deltaPosition.x) > s_MorphEpsilon
        || Abs(delta.deltaPosition.y) > s_MorphEpsilon
        || Abs(delta.deltaPosition.z) > s_MorphEpsilon
        || Abs(delta.deltaNormal.x) > s_MorphEpsilon
        || Abs(delta.deltaNormal.y) > s_MorphEpsilon
        || Abs(delta.deltaNormal.z) > s_MorphEpsilon
        || Abs(delta.deltaTangent.x) > s_MorphEpsilon
        || Abs(delta.deltaTangent.y) > s_MorphEpsilon
        || Abs(delta.deltaTangent.z) > s_MorphEpsilon
    ;
}

[[nodiscard]] bool ReadOptionalMorphFloat3(
    const GltfDocument& document,
    const JsonValue& target,
    const AStringView attributeName,
    const usize vertexIndex,
    const usize vertexCount,
    Float3U& outDelta)
{
    outDelta = Float3U(0.0f, 0.0f, 0.0f);

    u32 accessorIndex = 0;
    if(!FindTargetAttributeAccessor(target, attributeName, accessorIndex))
        return true;
    if(!AccessorHasVertexCount(document, accessorIndex, vertexCount))
        return false;

    f32 tuple[3] = {};
    if(!ReadAccessorFloatTuple(document, accessorIndex, vertexIndex, tuple, 3u))
        return false;

    outDelta = Float3U(tuple[0], tuple[1], tuple[2]);
    return true;
}

[[nodiscard]] bool BuildMorphsFromPrimitive(
    const GltfDocument& document,
    const JsonValue& mesh,
    const JsonValue& primitive,
    const usize vertexCount,
    Vector<DeformableMorph>& outMorphs)
{
    outMorphs.clear();

    const JsonValue* targets = FindMember(primitive, "targets");
    if(!targets)
        return true;
    if(!targets->isArray()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': primitive targets must be an array"),
            PathToString<tchar>(document.sourcePath)
        );
        return false;
    }

    Vector<AString> targetNames;
    if(!ReadTargetNames(mesh, targetNames))
        return false;

    outMorphs.reserve(targets->arrayValue.size());
    for(usize targetIndex = 0u; targetIndex < targets->arrayValue.size(); ++targetIndex){
        const JsonValue& target = targets->arrayValue[targetIndex];
        if(!target.isObject()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': morph target {} must be an object"),
                PathToString<tchar>(document.sourcePath),
                targetIndex
            );
            return false;
        }

        AString morphName = targetIndex < targetNames.size() && !targetNames[targetIndex].empty()
            ? targetNames[targetIndex]
            : StringFormat("morph_{}", targetIndex)
        ;

        DeformableMorph morph;
        morph.name = Name(AStringView(morphName));
        if(!morph.name){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable glTF import '{}': morph target {} has an invalid name"),
                PathToString<tchar>(document.sourcePath),
                targetIndex
            );
            return false;
        }

        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            DeformableMorphDelta delta;
            delta.vertexId = static_cast<u32>(vertexIndex);
            if(!ReadOptionalMorphFloat3(document, target, "POSITION", vertexIndex, vertexCount, delta.deltaPosition)
                || !ReadOptionalMorphFloat3(document, target, "NORMAL", vertexIndex, vertexCount, delta.deltaNormal)
            ){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Deformable glTF import '{}': failed to read morph target {}"),
                    PathToString<tchar>(document.sourcePath),
                    targetIndex
                );
                return false;
            }

            Float3U tangentDelta;
            if(!ReadOptionalMorphFloat3(document, target, "TANGENT", vertexIndex, vertexCount, tangentDelta)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Deformable glTF import '{}': failed to read morph tangent target {}"),
                    PathToString<tchar>(document.sourcePath),
                    targetIndex
                );
                return false;
            }
            delta.deltaTangent = Float4U(tangentDelta.x, tangentDelta.y, tangentDelta.z, 0.0f);

            if(ActiveMorphDelta(delta))
                morph.deltas.push_back(delta);
        }

        if(!morph.deltas.empty())
            outMorphs.push_back(Move(morph));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const JsonValue* SelectMesh(
    const GltfDocument& document,
    const u32 meshIndex)
{
    const JsonValue* meshes = FindRequiredMember(document.sourcePath, document.root, "meshes", JsonValueType::Array);
    if(!meshes)
        return nullptr;
    if(meshIndex >= meshes->arrayValue.size() || !meshes->arrayValue[meshIndex].isObject()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': mesh index {} is invalid"),
            PathToString<tchar>(document.sourcePath),
            meshIndex
        );
        return nullptr;
    }

    return &meshes->arrayValue[meshIndex];
}

[[nodiscard]] const JsonValue* SelectPrimitive(
    const GltfDocument& document,
    const JsonValue& mesh,
    const u32 primitiveIndex)
{
    const JsonValue* primitives = FindRequiredMember(document.sourcePath, mesh, "primitives", JsonValueType::Array);
    if(!primitives)
        return nullptr;
    if(primitiveIndex >= primitives->arrayValue.size() || !primitives->arrayValue[primitiveIndex].isObject()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': primitive index {} is invalid"),
            PathToString<tchar>(document.sourcePath),
            primitiveIndex
        );
        return nullptr;
    }

    const JsonValue& primitive = primitives->arrayValue[primitiveIndex];
    u32 mode = 4u;
    if(!ReadOptionalU32Member(document.sourcePath, primitive, "mode", mode))
        return nullptr;
    if(mode != 4u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': only triangle primitives are supported"),
            PathToString<tchar>(document.sourcePath)
        );
        return nullptr;
    }

    return &primitive;
}

[[nodiscard]] bool BuildDeformableGeometryFromPrimitive(
    const DeformableGltfImportOptions& options,
    const GltfDocument& document,
    const JsonValue& mesh,
    const JsonValue& primitive,
    DeformableGeometry& outGeometry)
{
    u32 indexAccessor = 0;
    const JsonValue* indicesValue = FindRequiredMember(document.sourcePath, primitive, "indices", JsonValueType::Number);
    if(!indicesValue || !ReadU32FromJsonNumber(document.sourcePath, *indicesValue, "indices", indexAccessor)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': primitive requires indexed geometry"),
            PathToString<tchar>(document.sourcePath)
        );
        return false;
    }

    Vector<u32> indices;
    if(!ReadAccessorIndexList(document, indexAccessor, indices) || (indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable glTF import '{}': failed to read triangle indices"),
            PathToString<tchar>(document.sourcePath)
        );
        return false;
    }

    Vector<DeformableVertexRest> restVertices;
    if(!BuildRestVerticesFromPrimitive(document, primitive, indices, restVertices))
        return false;

    Vector<SkinInfluence4> skin;
    if(!BuildSkinFromPrimitive(document, primitive, restVertices.size(), skin))
        return false;

    Vector<SourceSample> sourceSamples;
    if(!BuildSourceSamples(document, restVertices.size(), indices, sourceSamples))
        return false;

    Vector<DeformableMorph> morphs;
    if(!BuildMorphsFromPrimitive(document, mesh, primitive, restVertices.size(), morphs))
        return false;

    outGeometry = DeformableGeometry(options.virtualPath);
    outGeometry.setRestVertices(Move(restVertices));
    outGeometry.setIndices(Move(indices));
    outGeometry.setSkin(Move(skin));
    outGeometry.setSourceSamples(Move(sourceSamples));
    outGeometry.setMorphs(Move(morphs));
    return outGeometry.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ImportDeformableGeometryFromGltf(
    const DeformableGltfImportOptions& options,
    DeformableGeometry& outGeometry)
{
    if(!options.virtualPath || options.sourcePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Deformable glTF import: source path and virtual path are required"));
        return false;
    }

    __hidden_deformable_gltf_importer::GltfDocument document;
    if(!__hidden_deformable_gltf_importer::LoadGltfDocument(options.sourcePath, document))
        return false;

    const __hidden_deformable_gltf_importer::JsonValue* mesh =
        __hidden_deformable_gltf_importer::SelectMesh(document, options.meshIndex)
    ;
    if(!mesh)
        return false;

    const __hidden_deformable_gltf_importer::JsonValue* primitive =
        __hidden_deformable_gltf_importer::SelectPrimitive(document, *mesh, options.primitiveIndex)
    ;
    if(!primitive)
        return false;

    return __hidden_deformable_gltf_importer::BuildDeformableGeometryFromPrimitive(
        options,
        document,
        *mesh,
        *primitive,
        outGeometry
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

