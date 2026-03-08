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


CompactString NormalizeVariantName(const AStringView variantName){
    CompactString canonical;
    if(variantName.empty()){
        const bool assigned = canonical.assign(ShaderArchive::s_DefaultVariant);
        NWB_ASSERT_MSG(assigned, NWB_TEXT("ShaderArchive: default variant name exceeded CompactString capacity"));
        if(!assigned)
            canonical.clear();
        return canonical;
    }

    const bool assigned = canonical.assign(variantName);
    NWB_ASSERT_MSG(assigned, NWB_TEXT("ShaderArchive: canonical variant name exceeded CompactString capacity"));
    if(!assigned)
        canonical.clear();
    return canonical;
}

u64 UpdateFnv64NameLane(u64 hash, const NameHash& nameHash, const u32 lane){
    NWB_ASSERT_MSG(lane < __hidden_name::s_HashLaneCount, NWB_TEXT("ShaderArchive: invalid hash lane"));

    const u64 laneValue = nameHash.qwords[lane];
    for(u32 byteIndex = 0; byteIndex < sizeof(laneValue); ++byteIndex){
        hash ^= static_cast<u8>((laneValue >> (byteIndex * 8u)) & 0xFFu);
        hash *= FNV64_PRIME;
    }

    return hash;
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


const Name& ShaderArchive::IndexVirtualPathName(){
    static const Name s_IndexVirtualPathName(s_IndexVirtualPath);
    return s_IndexVirtualPathName;
}

Name ShaderArchive::buildVirtualPathName(const Name& shaderName, const CompactString& variantName, const Name& stageName){
    if(!shaderName || !stageName)
        return NAME_NONE;

    const CompactString canonicalVariantName = __hidden_shader_archive::NormalizeVariantName(variantName.view());
    if(canonicalVariantName.empty())
        return NAME_NONE;

    const Name canonicalVariantNameId(canonicalVariantName.view());
    if(!canonicalVariantNameId)
        return NAME_NONE;

    NameHash derivedHash = {};
    static constexpr char s_VirtualPathPrefix[] = "nwb/shader/archive/path";
    for(u32 lane = 0; lane < __hidden_name::s_HashLaneCount; ++lane){
        u64 laneHash = UpdateFnv64(
            FNV64_OFFSET_BASIS,
            reinterpret_cast<const u8*>(s_VirtualPathPrefix),
            sizeof(s_VirtualPathPrefix) - 1
        );
        laneHash = __hidden_shader_archive::UpdateFnv64NameLane(laneHash, shaderName.hash(), lane);
        laneHash = __hidden_shader_archive::UpdateFnv64NameLane(laneHash, canonicalVariantNameId.hash(), lane);
        laneHash = __hidden_shader_archive::UpdateFnv64NameLane(laneHash, stageName.hash(), lane);
        derivedHash.qwords[lane] = laneHash;
    }

    return Name(derivedHash);
}

bool ShaderArchive::serializeIndex(const Vector<Record>& records, Vector<u8>& outBinary){
    outBinary.clear();

    if(records.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: record count exceeds u32 range"));
        return false;
    }

    Alloc::ScratchArena<> scratchArena;
    Vector<Record, Alloc::ScratchAllocator<Record>> sortedRecords{Alloc::ScratchAllocator<Record>(scratchArena)};
    sortedRecords.reserve(records.size());
    sortedRecords.insert(sortedRecords.end(), records.begin(), records.end());
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

bool ShaderArchive::findVirtualPath(const Vector<Record>& records, const Name& shaderName, const CompactString& variantName, const Name& stageName, Name& outVirtualPath){
    outVirtualPath = NAME_NONE;

    if(!shaderName || !stageName)
        return false;

    const CompactString canonicalVariantName = __hidden_shader_archive::NormalizeVariantName(variantName.view());
    if(canonicalVariantName.empty())
        return false;
    const Name requestedVariantName(canonicalVariantName.view());
    static constexpr Name s_DefaultVariantName(s_DefaultVariant);

    if(const Record* record = __hidden_shader_archive::FindRecord(records, shaderName, requestedVariantName, stageName)){
        outVirtualPath = Name(record->virtualPathHash);
        return true;
    }

    if(requestedVariantName == s_DefaultVariantName)
        return false;

    if(const Record* record = __hidden_shader_archive::FindRecord(records, shaderName, s_DefaultVariantName, stageName)){
        outVirtualPath = Name(record->virtualPathHash);
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

