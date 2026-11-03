// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/alloc/scratch.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SubsystemAssetCodecAutoRegistrar, SubsystemAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ISubsystem::~ISubsystem()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SubsystemAssetRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void DeduplicateSubsystemRefs(ISubsystem::SubsystemRefVector& subsystems)noexcept{
    usize writeIndex = 0u;
    for(usize readIndex = 0u; readIndex < subsystems.size(); ++readIndex){
        const ISubsystem::SubsystemRef& candidate = subsystems[readIndex];
        b8 duplicate = false;
        for(usize prevIndex = 0u; prevIndex < writeIndex; ++prevIndex){
            if(subsystems[prevIndex].name() == candidate.name()){
                duplicate = true;
                break;
            }
        }
        if(!duplicate){
            if(writeIndex != readIndex)
                subsystems[writeIndex] = candidate;
            ++writeIndex;
        }
    }
    subsystems.resize(writeIndex);
}

b8 ValidatePayload(const ISubsystem& subsystem){
    if(!subsystem.checkVirtualPath(NWB_TEXT("ISubsystem::validatePayload")))
        return false;

    for(usize i = 0u; i < subsystem.subsystems.size(); ++i){
        const ISubsystem::SubsystemRef& ref = subsystem.subsystems[i];
        if(!ValidateSubsystemDependentRef(subsystem.virtualPath(), ref.name(), ref.valid(), i))
            return false;
    }

    const usize count = subsystem.fieldCount();
    if(count == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem must declare at least one asset field"));
        return false;
    }

    for(usize i = 0u; i < count; ++i){
        const SubsystemFieldValueDescriptor descriptor = subsystem.fieldDescriptor(i);
        const usize valueCount = subsystem.fieldValueCount(i);
        if(!ValidateSubsystemFieldValueCount(subsystem.virtualPath(), descriptor, valueCount))
            return false;

        for(usize elementIndex = 0u; elementIndex < valueCount; ++elementIndex){
            const Name value = subsystem.fieldValue(i, elementIndex);
            b8 isDuplicate = false;
            for(usize prevIndex = 0u; prevIndex < elementIndex; ++prevIndex){
                if(subsystem.fieldValue(i, prevIndex) == value){
                    isDuplicate = true;
                    break;
                }
            }
            if(!ValidateSubsystemFieldElementValue(subsystem.virtualPath(), descriptor, value, isDuplicate))
                return false;
        }
    }

    return true;
}

b8 LoadBinary(const Span<const u8> binary, ISubsystem& outSubsystem){
    Core::Alloc::ScratchArena scratchArena(s_SubsystemLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SubsystemHeaderBinary>(binary, s_SubsystemMagic, s_SubsystemLoadBinaryContext, s_SubsystemAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SubsystemHeaderBinary& header = *headerResult;
    const usize expectedFieldCount = outSubsystem.fieldCount();
    if(!ValidateSubsystemHeaderBinary(header, outSubsystem.assetType(), expectedFieldCount))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<u64, Core::Alloc::ScratchArena> fieldCounts(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);
    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, s_SubsystemLoadBinaryContext, s_SubsystemDepsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.fieldCount, s_SubsystemLoadBinaryContext, s_SubsystemFieldCountsPayloadLabel, cursor, fieldCounts))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, s_SubsystemLoadBinaryContext, s_SubsystemAssetsPayloadLabel, cursor, assetHashes))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_SubsystemLoadBinaryContext))
        return false;

    outSubsystem.subsystems.clear();
    outSubsystem.subsystems.reserve(subsystemHashes.size());
    for(const NameHash& subsystemHash : subsystemHashes){
        const Name subsystemName(subsystemHash);
        if(!subsystemName){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: subsystem reference must not be empty"));
            return false;
        }
        outSubsystem.subsystems.push_back(ISubsystem::SubsystemRef(subsystemName));
    }
    DeduplicateSubsystemRefs(outSubsystem.subsystems);

    if(!ValidateSubsystemFieldElementCountSum(Span<const u64>(fieldCounts), header.assetCount))
        return false;

    usize hashOffset = 0u;
    for(usize i = 0u; i < expectedFieldCount; ++i){
        const SubsystemFieldValueDescriptor descriptor = outSubsystem.fieldDescriptor(i);
        const u64 elementCount = fieldCounts[i];
        if(!ValidateSubsystemBinaryScalarFieldCount(outSubsystem.assetType(), descriptor, elementCount))
            return false;
        outSubsystem.clearFieldValues(i);
        for(u64 elementIndex = 0u; elementIndex < elementCount; ++elementIndex){
            const Name assetName(assetHashes[hashOffset++]);
            if(!assetName){
                NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: asset reference must not be empty"));
                return false;
            }
            outSubsystem.appendFieldValue(i, assetName);
        }
    }

    return outSubsystem.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UniquePtr<Core::Assets::IAsset>> SubsystemAssetCodec::deserialize(const Name& virtualPath, const Span<const u8> binary, Core::Assets::AssetArena& arena)const{
    return PolymorphicAssetCodecDetail::DeserializePolymorphicAsset<ISubsystem, SubsystemHeaderBinary, &SubsystemHeaderBinary::subsystemTypeHash>(
        virtualPath,
        binary,
        s_SubsystemMagic,
        s_SubsystemLoadBinaryContext,
        s_SubsystemAssetKindLabel,
        NWB_TEXT("SubsystemAssetCodec::deserialize"),
        arena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

