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


static constexpr char s_IndexMagic[] = "NWBSDX1";
inline constexpr usize s_IndexMagicByteCount = sizeof(s_IndexMagic);


struct IndexHeaderDisk{
    char magic[s_IndexMagicByteCount];
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

bool LessRecordPointer(NotNull<const ShaderArchive::Record*> lhs, NotNull<const ShaderArchive::Record*> rhs){
    return LessRecord(*lhs, *rhs);
}


bool SameShaderVariantStage(const ShaderArchive::Record& lhs, const ShaderArchive::Record& rhs)noexcept{
    return lhs.shaderName == rhs.shaderName && lhs.variantName == rhs.variantName && lhs.stage == rhs.stage;
}


bool ValidateRecord(const ShaderArchive::Record& record){
    if(!record.shaderName || record.variantName.empty() || !record.stage){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: record has empty mandatory field"));
        return false;
    }
    if(record.virtualPathHash == NameHash{}){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: record has empty virtual path hash"));
        return false;
    }

    return true;
}


u64 UpdateFnv64NameLane(u64 hash, const NameHash& nameHash, const u32 lane){
    NWB_ASSERT_MSG(lane < NameDetail::s_HashLaneCount, NWB_TEXT("ShaderArchive: invalid hash lane"));

    const u64 laneValue = nameHash.qwords[lane];
    for(u32 byteIndex = 0u; byteIndex < sizeof(laneValue); ++byteIndex){
        hash ^= static_cast<u8>((laneValue >> (byteIndex * NameDetail::s_NameHashByteBitCount)) & NameDetail::s_NameHashByteMask);
        hash *= s_Fnv64Prime;
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


const Name& ShaderArchive::IndexVirtualPathName()noexcept{
    return s_IndexVirtualPathName;
}

Name ShaderArchive::BuildVirtualPathName(const Name& shaderName, const AStringView variantName, const Name& stageName){
    if(!shaderName || variantName.empty() || !stageName)
        return s_NameNone;

    NameHash derivedHash = {};
    static constexpr AStringView s_VirtualPathPrefix = "nwb/shader/archive/path";
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        u64 laneHash = UpdateFnv64(
            s_Fnv64OffsetBasis,
            reinterpret_cast<const u8*>(s_VirtualPathPrefix.data()),
            s_VirtualPathPrefix.size()
        );
        laneHash = __hidden_shader_archive::UpdateFnv64NameLane(laneHash, shaderName.hash(), lane);
        laneHash = UpdateFnv64TextExact(laneHash, variantName);
        laneHash = __hidden_shader_archive::UpdateFnv64NameLane(laneHash, stageName.hash(), lane);
        derivedHash.qwords[lane] = laneHash;
    }

    return Name(derivedHash);
}

Expected<GraphicsBytes> ShaderArchive::SerializeIndex(GraphicsArena& arena, const GraphicsVector<Record>& records){
    GraphicsBytes binary(arena);

    if(records.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: record count exceeds u32 range"));
        return MakeUnexpected(Failure{});
    }

    Alloc::ScratchArena scratchArena(__hidden_shader_archive::s_SerializeIndexArena);
    Vector<const Record*, Alloc::ScratchArena> sortedRecords{scratchArena};
    sortedRecords.reserve(records.size());
    for(const Record& record : records)
        sortedRecords.push_back(&record);
    Sort(sortedRecords.begin(), sortedRecords.end(), [](const Record* lhs, const Record* rhs){ return __hidden_shader_archive::LessRecordPointer(MakeNotNull(lhs), MakeNotNull(rhs)); });

    usize variantTextBinaryBytes = 0;
    for(usize i = 0u; i < sortedRecords.size(); ++i){
        const Record& record = *sortedRecords[i];
        if(!__hidden_shader_archive::ValidateRecord(record))
            return MakeUnexpected(Failure{});
        if(record.variantName.size() > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: variant name exceeds u32 range"));
            return MakeUnexpected(Failure{});
        }
        if(record.variantName.size() > Limit<usize>::s_Max - sizeof(u32)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: variant name size overflows"));
            return MakeUnexpected(Failure{});
        }
        const usize variantRecordBytes = sizeof(u32) + record.variantName.size();
        if(variantTextBinaryBytes > Limit<usize>::s_Max - variantRecordBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: variant text size overflows"));
            return MakeUnexpected(Failure{});
        }
        variantTextBinaryBytes += variantRecordBytes;

        if(i == 0)
            continue;

        if(__hidden_shader_archive::SameShaderVariantStage(*sortedRecords[i - 1], record)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: duplicate shader+variant+stage key detected (shader='{}', variant='{}', stage='{}')")
                , StringConvert(record.shaderName.resolvedText())
                , StringConvert(AStringView(record.variantName))
                , StringConvert(record.stage.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }
    }

    if(sortedRecords.size() > (Limit<usize>::s_Max - sizeof(__hidden_shader_archive::IndexHeaderDisk)) / sizeof(__hidden_shader_archive::RecordHeaderDisk)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: output binary size overflow"));
        return MakeUnexpected(Failure{});
    }

    __hidden_shader_archive::IndexHeaderDisk header;
    NWB_MEMCPY(header.magic, sizeof(header.magic), __hidden_shader_archive::s_IndexMagic, sizeof(__hidden_shader_archive::s_IndexMagic));
    header.recordCount = static_cast<u32>(sortedRecords.size());

    const usize headerAndRecordBytes = sizeof(header) + sortedRecords.size() * sizeof(__hidden_shader_archive::RecordHeaderDisk);
    if(headerAndRecordBytes > Limit<usize>::s_Max - variantTextBinaryBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: output binary size overflow"));
        return MakeUnexpected(Failure{});
    }

    binary.reserve(headerAndRecordBytes + variantTextBinaryBytes);
    AppendPOD(binary, header);

    for(const Record* sortedRecord : sortedRecords){
        const Record& record = *sortedRecord;
        __hidden_shader_archive::RecordHeaderDisk recordHeader;
        recordHeader.shaderName = record.shaderName.hash();
        recordHeader.stage = record.stage.hash();
        recordHeader.virtualPathHash = record.virtualPathHash;
        recordHeader.sourceChecksum = record.sourceChecksum;
        recordHeader.bytecodeChecksum = record.bytecodeChecksum;

        AppendPOD(binary, recordHeader);
        if(!AppendString(binary, AStringView(record.variantName))){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::SerializeIndex failed: variant name append failed"));
            return MakeUnexpected(Failure{});
        }
    }

    return binary;
}

Expected<GraphicsVector<ShaderArchive::Record>> ShaderArchive::DeserializeIndex(GraphicsArena& arena, const GraphicsBytes& binary){

    usize cursor = 0;

    const auto decodedHeader = ReadPOD<__hidden_shader_archive::IndexHeaderDisk>(binary, cursor);
    if(!decodedHeader){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: missing header"));
        return MakeUnexpected(Failure{});
    }

    const auto& header = *decodedHeader;
    if(NWB_MEMCMP(header.magic, __hidden_shader_archive::s_IndexMagic, sizeof(header.magic)) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: invalid magic"));
        return MakeUnexpected(Failure{});
    }
    if(header.recordCount > (binary.size() - cursor) / sizeof(__hidden_shader_archive::RecordHeaderDisk)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: record count {} exceeds available data"), header.recordCount);
        return MakeUnexpected(Failure{});
    }

    GraphicsVector<Record> parsedRecords(arena);
    parsedRecords.reserve(header.recordCount);
    const Record* previousRecord = nullptr;
    for(u32 i = 0u; i < header.recordCount; ++i){
        const auto decodedRecordHeader = ReadPOD<__hidden_shader_archive::RecordHeaderDisk>(binary, cursor);
        if(!decodedRecordHeader){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: missing record header at index {}"), i);
            return MakeUnexpected(Failure{});
        }

        const auto& recordHeader = *decodedRecordHeader;
        Record record(arena);
        record.shaderName = Name(recordHeader.shaderName);
        record.stage = Name(recordHeader.stage);
        record.virtualPathHash = recordHeader.virtualPathHash;
        record.sourceChecksum = recordHeader.sourceChecksum;
        record.bytecodeChecksum = recordHeader.bytecodeChecksum;
        const auto variantName = ReadString(binary, cursor);
        if(!variantName){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: missing variant name at record {}"), i);
            return MakeUnexpected(Failure{});
        }
        record.variantName.assign(variantName->data(), variantName->size());
        if(record.variantName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: empty variant name at record {}"), i);
            return MakeUnexpected(Failure{});
        }
        if(record.virtualPathHash == NameHash{}){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: empty virtual path hash at record {}"), i);
            return MakeUnexpected(Failure{});
        }
        if(previousRecord){
            if(__hidden_shader_archive::LessRecord(record, *previousRecord)){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: records are out of order at index {}"), i);
                return MakeUnexpected(Failure{});
            }
            if(__hidden_shader_archive::SameShaderVariantStage(*previousRecord, record)){
                NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: duplicate shader+variant+stage key at record {}"), i);
                return MakeUnexpected(Failure{});
            }
        }

        parsedRecords.push_back(Move(record));
        previousRecord = &parsedRecords.back();
    }

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderArchive::DeserializeIndex failed: trailing bytes detected"));
        return MakeUnexpected(Failure{});
    }

    return parsedRecords;
}

Expected<Name> ShaderArchive::FindVirtualPath(const GraphicsVector<Record>& records, const Name& shaderName, const AStringView variantName, const Name& stageName){
    if(!shaderName || variantName.empty() || !stageName)
        return MakeUnexpected(Failure{});

    if(const Record* record = __hidden_shader_archive::FindRecord(records, shaderName, variantName, stageName))
        return Name(record->virtualPathHash);

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

