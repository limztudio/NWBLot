// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_archive.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_archive{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_SerializeIndexArena("core/graphics/shader_archive_serialize");


static constexpr char s_IndexMagic[8] = { 'N', 'W', 'B', 'S', 'D', 'X', '1', '\0' };


struct IndexHeaderDisk{
    char magic[8];
    u32 recordCount = 0;
};

struct RecordHeaderDisk{
    NameHash shaderName;
    NameHash stage;
    NameHash virtualPathHash;
    u64 sourceChecksum = 0;
    u64 bytecodeChecksum = 0;
};


bool LessRecord(const ShaderArchive::Record& lhs, const ShaderArchive::Record& rhs){
    const NameHash& lhsShaderHash = lhs.shaderName.hash();
    const NameHash& rhsShaderHash = rhs.shaderName.hash();
    if(lhsShaderHash != rhsShaderHash)
        return LessNameHash(lhsShaderHash, rhsShaderHash);

    if(lhs.variantName != rhs.variantName)
        return lhs.variantName < rhs.variantName;

    const NameHash& lhsStageHash = lhs.stage.hash();
    const NameHash& rhsStageHash = rhs.stage.hash();
    if(lhsStageHash != rhsStageHash)
        return LessNameHash(lhsStageHash, rhsStageHash);

    return LessNameHash(lhs.virtualPathHash, rhs.virtualPathHash);
}

bool LessRecordPointer(const ShaderArchive::Record* lhs, const ShaderArchive::Record* rhs){
    NWB_ASSERT(lhs != nullptr);
    NWB_ASSERT(rhs != nullptr);
    return LessRecord(*lhs, *rhs);
}


bool SameShaderVariantStage(const ShaderArchive::Record& lhs, const ShaderArchive::Record& rhs){
    return lhs.shaderName == rhs.shaderName && lhs.variantName == rhs.variantName && lhs.stage == rhs.stage;
}


bool ValidateRecord(const ShaderArchive::Record& record){
    if(!record.shaderName || record.variantName.empty() || !record.stage){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: record has empty mandatory field"));
        return false;
    }
    if(record.virtualPathHash == NameHash{}){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: record has empty virtual path hash"));
        return false;
    }

    return true;
}


u64 UpdateFnv64NameLane(u64 hash, const NameHash& nameHash, const u32 lane){
    NWB_ASSERT_MSG(lane < NameDetail::s_HashLaneCount, NWB_TEXT("ShaderArchive: invalid hash lane"));

    const u64 laneValue = nameHash.qwords[lane];
    for(u32 byteIndex = 0; byteIndex < sizeof(laneValue); ++byteIndex){
        hash ^= static_cast<u8>((laneValue >> (byteIndex * 8u)) & 0xFFu);
        hash *= FNV64_PRIME;
    }

    return hash;
}


template<typename RecordVector>
const ShaderArchive::Record* FindRecord(
    const RecordVector& records,
    const Name& shaderName,
    const AStringView variantName,
    const Name& stageName
){
    const NameHash& targetShader = shaderName.hash();
    const NameHash& targetStage = stageName.hash();
    const auto it = LowerBound(
        records.begin(),
        records.end(),
        nullptr,
        [&targetShader, &variantName, &targetStage](const ShaderArchive::Record& record, decltype(nullptr)){
            const NameHash& recordShader = record.shaderName.hash();
            if(recordShader != targetShader)
                return LessNameHash(recordShader, targetShader);

            const AStringView recordVariant(record.variantName);
            if(recordVariant != variantName)
                return recordVariant < variantName;

            return LessNameHash(record.stage.hash(), targetStage);
        }
    );
    if(it == records.end())
        return nullptr;
    if(
        it->shaderName.hash() != targetShader
        || AStringView(it->variantName) != variantName
        || it->stage.hash() != targetStage
    )
        return nullptr;
    return &*it;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const Name& ShaderArchive::IndexVirtualPathName(){
    return s_IndexVirtualPathName;
}

Name ShaderArchive::buildVirtualPathName(const Name& shaderName, const AStringView variantName, const Name& stageName){
    if(!shaderName || variantName.empty() || !stageName)
        return NAME_NONE;

    NameHash derivedHash = {};
    static constexpr char s_VirtualPathPrefix[] = "nwb/shader/archive/path";
    for(u32 lane = 0; lane < NameDetail::s_HashLaneCount; ++lane){
        u64 laneHash = UpdateFnv64(
            FNV64_OFFSET_BASIS,
            reinterpret_cast<const u8*>(s_VirtualPathPrefix),
            sizeof(s_VirtualPathPrefix) - 1
        );
        laneHash = __hidden_shader_archive::UpdateFnv64NameLane(laneHash, shaderName.hash(), lane);
        laneHash = UpdateFnv64TextExact(laneHash, variantName);
        laneHash = __hidden_shader_archive::UpdateFnv64NameLane(laneHash, stageName.hash(), lane);
        derivedHash.qwords[lane] = laneHash;
    }

    return Name(derivedHash);
}

bool ShaderArchive::serializeIndex(const GraphicsVector<Record>& records, GraphicsBytes& outBinary){
    outBinary.clear();

    if(records.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: record count exceeds u32 range"));
        return false;
    }

    Alloc::ScratchArena scratchArena(__hidden_shader_archive::s_SerializeIndexArena);
    Vector<const Record*, Alloc::ScratchArena> sortedRecords{scratchArena};
    sortedRecords.reserve(records.size());
    for(const Record& record : records)
        sortedRecords.push_back(&record);
    Sort(sortedRecords.begin(), sortedRecords.end(), __hidden_shader_archive::LessRecordPointer);

    usize variantTextBinaryBytes = 0;
    for(usize i = 0; i < sortedRecords.size(); ++i){
        const Record& record = *sortedRecords[i];
        if(!__hidden_shader_archive::ValidateRecord(record))
            return false;
        if(record.variantName.size() > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: variant name exceeds u32 range"));
            return false;
        }
        if(record.variantName.size() > Limit<usize>::s_Max - sizeof(u32)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: variant name size overflows"));
            return false;
        }
        const usize variantRecordBytes = sizeof(u32) + record.variantName.size();
        if(variantTextBinaryBytes > Limit<usize>::s_Max - variantRecordBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: variant text size overflows"));
            return false;
        }
        variantTextBinaryBytes += variantRecordBytes;

        if(i == 0)
            continue;

        if(__hidden_shader_archive::SameShaderVariantStage(*sortedRecords[i - 1], record)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: duplicate shader+variant+stage key detected (shader='{}', variant='{}', stage='{}')")
                , StringConvert(record.shaderName.c_str())
                , StringConvert(AStringView(record.variantName))
                , StringConvert(record.stage.c_str())
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
    header.recordCount = static_cast<u32>(sortedRecords.size());

    const usize headerAndRecordBytes = sizeof(header) + sortedRecords.size() * sizeof(__hidden_shader_archive::RecordHeaderDisk);
    if(headerAndRecordBytes > Limit<usize>::s_Max - variantTextBinaryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: output binary size overflow"));
        return false;
    }

    outBinary.reserve(headerAndRecordBytes + variantTextBinaryBytes);
    AppendPOD(outBinary, header);

    for(const Record* sortedRecord : sortedRecords){
        const Record& record = *sortedRecord;
        __hidden_shader_archive::RecordHeaderDisk recordHeader;
        recordHeader.shaderName = record.shaderName.hash();
        recordHeader.stage = record.stage.hash();
        recordHeader.virtualPathHash = record.virtualPathHash;
        recordHeader.sourceChecksum = record.sourceChecksum;
        recordHeader.bytecodeChecksum = record.bytecodeChecksum;

        AppendPOD(outBinary, recordHeader);
        if(!AppendString(outBinary, AStringView(record.variantName))){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::serializeIndex failed: variant name append failed"));
            return false;
        }
    }

    return true;
}

bool ShaderArchive::deserializeIndex(const GraphicsBytes& binary, GraphicsVector<Record>& outRecords){
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
    if(header.recordCount > (binary.size() - cursor) / sizeof(__hidden_shader_archive::RecordHeaderDisk)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: record count {} exceeds available data"), header.recordCount);
        return false;
    }

    GraphicsVector<Record> parsedRecords(outRecords.get_allocator());
    parsedRecords.reserve(header.recordCount);
    const Record* previousRecord = nullptr;
    for(u32 i = 0; i < header.recordCount; ++i){
        __hidden_shader_archive::RecordHeaderDisk recordHeader{};
        if(!ReadPOD(binary, cursor, recordHeader)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: missing record header at index {}"), i);
            return false;
        }

        Record record(outRecords.get_allocator().arena());
        record.shaderName = Name(recordHeader.shaderName);
        record.stage = Name(recordHeader.stage);
        record.virtualPathHash = recordHeader.virtualPathHash;
        record.sourceChecksum = recordHeader.sourceChecksum;
        record.bytecodeChecksum = recordHeader.bytecodeChecksum;
        if(!ReadString(binary, cursor, record.variantName)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: missing variant name at record {}"), i);
            return false;
        }
        if(record.variantName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: empty variant name at record {}"), i);
            return false;
        }
        if(record.virtualPathHash == NameHash{}){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: empty virtual path hash at record {}"), i);
            return false;
        }
        if(previousRecord){
            if(__hidden_shader_archive::LessRecord(record, *previousRecord)){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: records are out of order at index {}"), i);
                return false;
            }
            if(__hidden_shader_archive::SameShaderVariantStage(*previousRecord, record)){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: duplicate shader+variant+stage key at record {}"), i);
                return false;
            }
        }

        parsedRecords.push_back(Move(record));
        previousRecord = &parsedRecords.back();
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::deserializeIndex failed: trailing bytes detected"));
        return false;
    }

    outRecords = Move(parsedRecords);
    return true;
}

bool ShaderArchive::findVirtualPath(const GraphicsVector<Record>& records, const Name& shaderName, const AStringView variantName, const Name& stageName, Name& outVirtualPath){
    outVirtualPath = NAME_NONE;

    if(!shaderName || variantName.empty() || !stageName)
        return false;

    if(const Record* record = __hidden_shader_archive::FindRecord(records, shaderName, variantName, stageName)){
        outVirtualPath = Name(record->virtualPathHash);
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

