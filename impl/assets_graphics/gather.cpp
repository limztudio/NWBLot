// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gather.h"

#include <core/alloc/scratch.h>
#include <core/graphics/shader_archive.h>
#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_gather{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateRecordIdentity(const Core::ShaderArchive::Record& record){
    const Name expectedPath = Core::ShaderArchive::buildVirtualPathName(record.shaderName, record.variantName, record.stage);
    if(expectedPath && expectedPath.hash() == record.virtualPathHash)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: shader index record has an invalid runtime identity"));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MergeGatheredGraphicsAsset(
    const Name& virtualPath,
    Core::Assets::AssetBytes& existingPayload,
    const void* incomingPayload,
    const usize incomingSize){
    if(virtualPath != Core::ShaderArchive::IndexVirtualPathName())
        return false;
    if(incomingSize > 0u && incomingPayload == nullptr)
        return false;

    Core::Assets::AssetArena& arena = existingPayload.get_allocator().arena();
    Core::Alloc::ScratchArena scratchArena(Name("assets_graphics/gather"));
    Core::GraphicsBytes incomingBytes(arena);
    incomingBytes.reserve(incomingSize);
    BinaryDetail::AppendBytesNoReserveUnchecked(incomingBytes, incomingPayload, incomingSize);

    Core::GraphicsVector<Core::ShaderArchive::Record> records(arena);
    Core::GraphicsVector<Core::ShaderArchive::Record> incomingRecords(arena);
    if(!Core::ShaderArchive::deserializeIndex(existingPayload, records)
        || !Core::ShaderArchive::deserializeIndex(incomingBytes, incomingRecords))
        return false;
    if(AddOverflows<usize>(records.size(), incomingRecords.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: merged shader index size overflows"));
        return false;
    }

    HashMap<NameHash, usize, Hasher<NameHash>, EqualTo<NameHash>, Core::Alloc::ScratchArena> recordIndices(scratchArena);
    recordIndices.reserve(records.size() + incomingRecords.size());
    records.reserve(records.size() + incomingRecords.size());
    for(usize i = 0u; i < records.size(); ++i){
        if(!__hidden_graphics_gather::ValidateRecordIdentity(records[i]))
            return false;
        if(!recordIndices.emplace(records[i].virtualPathHash, i).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: duplicate shader index identity"));
            return false;
        }
    }

    for(auto& record : incomingRecords){
        if(!__hidden_graphics_gather::ValidateRecordIdentity(record))
            return false;
        const auto found = recordIndices.find(record.virtualPathHash);
        if(found == recordIndices.end()){
            if(!recordIndices.emplace(record.virtualPathHash, records.size()).second)
                return false;
            records.push_back(Move(record));
            continue;
        }

        const auto& previous = records[found.value()];
        if(previous.shaderName != record.shaderName || previous.stage != record.stage
            || previous.variantName != record.variantName || previous.sourceChecksum != record.sourceChecksum
            || previous.bytecodeChecksum != record.bytecodeChecksum){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetGatherer: conflicting shader index record '{}' variant '{}' stage '{}'")
                , StringConvert(record.shaderName.c_str())
                , StringConvert(record.variantName)
                , StringConvert(record.stage.c_str())
            );
            return false;
        }
    }

    Core::GraphicsBytes merged(arena);
    if(!Core::ShaderArchive::serializeIndex(records, merged))
        return false;
    existingPayload = Move(merged);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

