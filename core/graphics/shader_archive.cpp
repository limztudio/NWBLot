// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_archive.h"

#include <logger/client/logger.h>


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

    const NameHash& lhsVariantHash = lhs.variantName.hash();
    const NameHash& rhsVariantHash = rhs.variantName.hash();
    if(lhsVariantHash != rhsVariantHash)
        return LessNameHash(lhsVariantHash, rhsVariantHash);

    const NameHash& lhsStageHash = lhs.stage.hash();
    const NameHash& rhsStageHash = rhs.stage.hash();
    if(lhsStageHash != rhsStageHash)
        return LessNameHash(lhsStageHash, rhsStageHash);

    const NameHash& lhsEntryPointHash = lhs.entryPoint.hash();
    const NameHash& rhsEntryPointHash = rhs.entryPoint.hash();
    if(lhsEntryPointHash != rhsEntryPointHash)
        return LessNameHash(lhsEntryPointHash, rhsEntryPointHash);

    return LessNameHash(lhs.virtualPathHash, rhs.virtualPathHash);
}


bool SameShaderVariantStage(const ShaderArchive::Record& lhs, const ShaderArchive::Record& rhs){
    return lhs.shaderName == rhs.shaderName
        && lhs.variantName == rhs.variantName
        && lhs.stage == rhs.stage;
}


bool ValidateRecord(const ShaderArchive::Record& record){
    if(!record.shaderName || !record.variantName || !record.stage){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: record has empty mandatory field"));
        return false;
    }
    if(record.virtualPathHash == NameHash{}){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: record has empty virtual path hash"));
        return false;
    }

    return true;
}


AString NormalizeVariantName(const AStringView variantName){
    AString canonical = CanonicalizeText(variantName);
    if(canonical.empty())
        canonical = ShaderArchive::s_DefaultVariant;
    return canonical;
}


AString BuildVirtualPathFromCanonical(const AStringView canonicalShaderName, const AStringView canonicalVariantName, const AStringView canonicalStageName){
    const u64 variantHash = ComputeFnv64Text(canonicalVariantName);
    const AString variantHashHex = FormatHex64(variantHash);
    return StringFormat("shader/{}/{}/{}.spv", canonicalShaderName, canonicalStageName, variantHashHex);
}


template <typename RecordVector>
const ShaderArchive::Record* FindRecord(const RecordVector& records, const Name& shaderName, const Name& variantName, const Name& stageName){
    const auto it = LowerBound(records.begin(), records.end(), nullptr,
        [&shaderName, &variantName, &stageName](const ShaderArchive::Record& record, std::nullptr_t){
            const NameHash& recordShader = record.shaderName.hash();
            const NameHash& targetShader = shaderName.hash();
            if(recordShader != targetShader)
                return LessNameHash(recordShader, targetShader);

            const NameHash& recordVariant = record.variantName.hash();
            const NameHash& targetVariant = variantName.hash();
            if(recordVariant != targetVariant)
                return LessNameHash(recordVariant, targetVariant);

            return LessNameHash(record.stage.hash(), stageName.hash());
        }
    );
    if(it == records.end())
        return nullptr;
    if(it->shaderName != shaderName || it->variantName != variantName || it->stage != stageName)
        return nullptr;
    return &*it;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AString ShaderArchive::buildVirtualPath(const AStringView shaderName, const AStringView variantName, const AStringView stageName){
    return __hidden_shader_archive::BuildVirtualPathFromCanonical(
        CanonicalizeText(shaderName),
        __hidden_shader_archive::NormalizeVariantName(variantName),
        CanonicalizeText(stageName)
    );
}

bool ShaderArchive::serializeIndex(const Vector<Record>& records, Vector<u8>& outBinary){
    outBinary.clear();

    if(records.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: record count exceeds u32 range"));
        return false;
    }

    Vector<Record> sortedRecords(records);
    Sort(sortedRecords.begin(), sortedRecords.end(), __hidden_shader_archive::LessRecord);

    for(usize i = 0; i < sortedRecords.size(); ++i){
        const Record& record = sortedRecords[i];
        if(!__hidden_shader_archive::ValidateRecord(record))
            return false;

        if(i == 0)
            continue;

        if(__hidden_shader_archive::SameShaderVariantStage(sortedRecords[i - 1], record)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderArchive::serializeIndex failed: duplicate shader+variant+stage key detected (shader='{}', variant='{}', stage='{}')"),
                StringConvert(record.shaderName.c_str()),
                StringConvert(record.variantName.c_str()),
                StringConvert(record.stage.c_str())
            );
            return false;
        }
    }

    if(sortedRecords.size() > (Limit<usize>::s_Max - sizeof(__hidden_shader_archive::IndexHeaderDisk)) / sizeof(__hidden_shader_archive::RecordHeaderDisk)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: output binary size overflow"));
        return false;
    }

    __hidden_shader_archive::IndexHeaderDisk header;
    NWB_MEMCPY(header.magic, sizeof(header.magic), __hidden_shader_archive::s_IndexMagic, sizeof(__hidden_shader_archive::s_IndexMagic));
    header.version = __hidden_shader_archive::s_IndexVersion;
    header.recordCount = static_cast<u32>(sortedRecords.size());

    outBinary.reserve(sizeof(header) + sortedRecords.size() * sizeof(__hidden_shader_archive::RecordHeaderDisk));
    AppendPOD(outBinary, header);

    for(const Record& record : sortedRecords){
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

bool ShaderArchive::deserializeIndex(const Vector<u8>& binary, Vector<Record>& outRecords){
    outRecords.clear();

    usize cursor = 0;

    __hidden_shader_archive::IndexHeaderDisk header{};
    if(!ReadPOD(binary, cursor, header)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: missing header"));
        return false;
    }

    if(NWB_MEMCMP(header.magic, __hidden_shader_archive::s_IndexMagic, sizeof(header.magic)) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: invalid magic"));
        return false;
    }
    if(header.version != __hidden_shader_archive::s_IndexVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: unsupported version {}"), header.version);
        return false;
    }
    if(header.recordCount > (binary.size() - cursor) / sizeof(__hidden_shader_archive::RecordHeaderDisk)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: record count {} exceeds available data"), header.recordCount);
        return false;
    }

    outRecords.reserve(header.recordCount);
    for(u32 i = 0; i < header.recordCount; ++i){
        __hidden_shader_archive::RecordHeaderDisk recordHeader{};
        if(!ReadPOD(binary, cursor, recordHeader)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: missing record header at index {}"), i);
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
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: empty virtual path hash at record {}"), i);
            return false;
        }
        if(!outRecords.empty()){
            const Record& previous = outRecords.back();
            if(__hidden_shader_archive::LessRecord(record, previous)){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: records are out of order at index {}"), i);
                return false;
            }
            if(__hidden_shader_archive::SameShaderVariantStage(previous, record)){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: duplicate shader+variant+stage key at record {}"), i);
                return false;
            }
        }

        outRecords.push_back(Move(record));
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: trailing bytes detected"));
        return false;
    }

    return true;
}

bool ShaderArchive::findVirtualPath(const Vector<Record>& records, const AStringView shaderName, const AStringView variantName, const AStringView stageName, AString& outVirtualPath){
    outVirtualPath.clear();

    const AString canonicalShaderName = CanonicalizeText(shaderName);
    if(canonicalShaderName.empty())
        return false;

    const AString canonicalVariantName = __hidden_shader_archive::NormalizeVariantName(variantName);
    const AString canonicalStageName = CanonicalizeText(stageName);
    if(canonicalStageName.empty())
        return false;

    const Name requestedShaderName(canonicalShaderName.c_str());
    const Name requestedVariantName(canonicalVariantName.c_str());
    const Name requestedStageName(canonicalStageName.c_str());
    static constexpr Name s_DefaultVariantName(s_DefaultVariant);

    if(__hidden_shader_archive::FindRecord(records, requestedShaderName, requestedVariantName, requestedStageName)){
        outVirtualPath = __hidden_shader_archive::BuildVirtualPathFromCanonical(canonicalShaderName, canonicalVariantName, canonicalStageName);
        return true;
    }

    if(requestedVariantName == s_DefaultVariantName)
        return false;

    if(__hidden_shader_archive::FindRecord(records, requestedShaderName, s_DefaultVariantName, requestedStageName)){
        outVirtualPath = __hidden_shader_archive::BuildVirtualPathFromCanonical(canonicalShaderName, s_DefaultVariant, canonicalStageName);
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

