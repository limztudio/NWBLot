// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_archive.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_archive{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr char s_IndexMagic[8] = { 'N', 'W', 'B', 'S', 'D', 'X', '1', '\0' };
static constexpr u32 s_IndexVersion = 1;


struct IndexHeaderDisk{
    char magic[8];
    u32 version = 0;
    u32 recordCount = 0;
};

struct RecordHeaderDisk{
    NameHash shaderName;
    NameHash variantName;
    NameHash stage;
    NameHash entryPoint;
    NameHash virtualPathHash;
    u64 sourceChecksum = 0;
    u64 bytecodeChecksum = 0;
};


bool LessRecord(const ShaderArchive::Record& lhs, const ShaderArchive::Record& rhs){
    const NameHash& lhsShaderHash = lhs.shaderName.hash();
    const NameHash& rhsShaderHash = rhs.shaderName.hash();
    if(lhsShaderHash != rhsShaderHash)
        return LessNameHash(lhsShaderHash, rhsShaderHash);

    return LessNameHash(lhs.variantName.hash(), rhs.variantName.hash());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AString ShaderArchive::buildVirtualPath(const AStringView shaderName, const AStringView variantName){
    const AString canonicalShaderName = CanonicalizeText(shaderName);

    AString canonicalVariantName = CanonicalizeText(variantName);
    if(canonicalVariantName.empty())
        canonicalVariantName = s_DefaultVariant;

    const u64 variantHash = ComputeFnv64Text(canonicalVariantName);
    const AString variantHashHex = FormatHex64(variantHash);
    return StringFormat("shader/{}/{}.spv", canonicalShaderName, variantHashHex);
}


bool ShaderArchive::serializeIndex(const Vector<Record>& records, Vector<u8>& outBinary, AString& outError){
    outBinary.clear();
    outError.clear();

    if(records.size() > Limit<u32>::s_Max){
        outError = "ShaderArchive::serializeIndex failed: record count exceeds u32 range";
        return false;
    }

    Vector<Record> sortedRecords = records;
    Sort(sortedRecords.begin(), sortedRecords.end(), __hidden_shader_archive::LessRecord);

    __hidden_shader_archive::IndexHeaderDisk header;
    NWB_MEMCPY(header.magic, sizeof(header.magic), __hidden_shader_archive::s_IndexMagic, sizeof(__hidden_shader_archive::s_IndexMagic));
    header.version = __hidden_shader_archive::s_IndexVersion;
    header.recordCount = static_cast<u32>(sortedRecords.size());

    outBinary.reserve(sizeof(header) + sortedRecords.size() * sizeof(__hidden_shader_archive::RecordHeaderDisk));
    AppendPOD(outBinary, header);

    for(const Record& record : sortedRecords){
        if(!record.shaderName || !record.variantName){
            outError = "ShaderArchive::serializeIndex failed: record has empty mandatory field";
            return false;
        }
        if(record.virtualPathHash == NameHash{}){
            outError = "ShaderArchive::serializeIndex failed: virtual path hash is empty";
            return false;
        }

        __hidden_shader_archive::RecordHeaderDisk recordHeader;
        recordHeader.shaderName = record.shaderName.hash();
        recordHeader.variantName = record.variantName.hash();
        recordHeader.stage = record.stage.hash();
        recordHeader.entryPoint = record.entryPoint.hash();
        recordHeader.virtualPathHash = record.virtualPathHash;
        recordHeader.sourceChecksum = record.sourceChecksum;
        recordHeader.bytecodeChecksum = record.bytecodeChecksum;

        AppendPOD(outBinary, recordHeader);
    }

    return true;
}


bool ShaderArchive::deserializeIndex(const Vector<u8>& binary, Vector<Record>& outRecords, AString& outError){
    outRecords.clear();
    outError.clear();

    usize cursor = 0;

    __hidden_shader_archive::IndexHeaderDisk header{};
    if(!ReadPOD(binary, cursor, header)){
        outError = "ShaderArchive::deserializeIndex failed: missing header";
        return false;
    }

    if(NWB_MEMCMP(header.magic, __hidden_shader_archive::s_IndexMagic, sizeof(header.magic)) != 0){
        outError = "ShaderArchive::deserializeIndex failed: invalid magic";
        return false;
    }
    if(header.version != __hidden_shader_archive::s_IndexVersion){
        outError = StringFormat("ShaderArchive::deserializeIndex failed: unsupported version {}", header.version);
        return false;
    }

    outRecords.reserve(header.recordCount);
    for(u32 i = 0; i < header.recordCount; ++i){
        __hidden_shader_archive::RecordHeaderDisk recordHeader{};
        if(!ReadPOD(binary, cursor, recordHeader)){
            outError = StringFormat("ShaderArchive::deserializeIndex failed: missing record header at index {}", i);
            return false;
        }

        Record record;
        record.shaderName = Name(recordHeader.shaderName);
        record.variantName = Name(recordHeader.variantName);
        record.stage = Name(recordHeader.stage);
        record.entryPoint = Name(recordHeader.entryPoint);
        record.virtualPathHash = recordHeader.virtualPathHash;
        record.sourceChecksum = recordHeader.sourceChecksum;
        record.bytecodeChecksum = recordHeader.bytecodeChecksum;
        if(record.virtualPathHash == NameHash{}){
            outError = StringFormat("ShaderArchive::deserializeIndex failed: empty virtual path hash at record {}", i);
            return false;
        }

        outRecords.push_back(Move(record));
    }

    if(cursor != binary.size()){
        outError = "ShaderArchive::deserializeIndex failed: trailing bytes detected";
        return false;
    }

    return true;
}


bool ShaderArchive::findVirtualPath(
    const Vector<Record>& records,
    const AStringView shaderName,
    const AStringView variantName,
    AString& outVirtualPath
){
    outVirtualPath.clear();

    const AString canonicalShaderName = CanonicalizeText(shaderName);
    if(canonicalShaderName.empty())
        return false;

    AString canonicalVariantName = CanonicalizeText(variantName);
    if(canonicalVariantName.empty())
        canonicalVariantName = s_DefaultVariant;

    const Name requestedShaderName(canonicalShaderName.c_str());
    const Name requestedVariantName(canonicalVariantName.c_str());
    const Name defaultVariantName(s_DefaultVariant);

    auto resolveVariant = [&](const Name& candidateVariantName, const AStringView candidateVariantText){
        for(const Record& record : records){
            if(record.shaderName != requestedShaderName)
                continue;
            if(record.variantName != candidateVariantName)
                continue;

            const AString virtualPath = buildVirtualPath(canonicalShaderName, candidateVariantText);
            const Name virtualPathName(virtualPath.c_str());
            if(virtualPathName.hash() != record.virtualPathHash)
                continue;

            outVirtualPath = virtualPath;
            return true;
        }

        return false;
    };

    if(resolveVariant(requestedVariantName, canonicalVariantName))
        return true;

    if(requestedVariantName == defaultVariantName)
        return false;

    if(resolveVariant(defaultVariantName, s_DefaultVariant))
        return true;

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
