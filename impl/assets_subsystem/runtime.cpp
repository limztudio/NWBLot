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
        if(!ref.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' dependent subsystem entry {} must not be empty"), ConvertString(subsystem.virtualPath().resolvedText()), i);
            return false;
        }
        if(ref.name() == subsystem.virtualPath()){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' cannot reference itself in 'subsystems'"), ConvertString(subsystem.virtualPath().resolvedText()));
            return false;
        }
    }

    const usize count = subsystem.fieldCount();
    if(count == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem must declare at least one asset field"));
        return false;
    }

    for(usize i = 0u; i < count; ++i){
        const SubsystemFieldValueDescriptor descriptor = subsystem.fieldDescriptor(i);
        const usize valueCount = subsystem.fieldValueCount(i);
        if(descriptor.isArray){
            if(valueCount == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' array field '{}' must not be empty"), ConvertString(subsystem.virtualPath().resolvedText()), ConvertString(descriptor.fieldName));
                return false;
            }
        }
        else if(valueCount != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' field '{}' must contain exactly one value"), ConvertString(subsystem.virtualPath().resolvedText()), ConvertString(descriptor.fieldName));
            return false;
        }

        for(usize elementIndex = 0u; elementIndex < valueCount; ++elementIndex){
            const Name value = subsystem.fieldValue(i, elementIndex);
            if(!value){
                NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' field '{}' must not be empty"), ConvertString(subsystem.virtualPath().resolvedText()), ConvertString(descriptor.fieldName));
                return false;
            }
            for(usize prevIndex = 0u; prevIndex < elementIndex; ++prevIndex){
                if(subsystem.fieldValue(i, prevIndex) == value){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' array field '{}' has duplicate entry '{}'"),
                        ConvertString(subsystem.virtualPath().resolvedText()),
                        ConvertString(descriptor.fieldName),
                        ConvertString(value.resolvedText())
                    );
                    return false;
                }
            }
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
    if(header.version != s_SubsystemVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }
    if(header.subsystemTypeHash != outSubsystem.assetType().hash()){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: subsystem binary type does not match '{}'"), ConvertString(outSubsystem.assetType().resolvedText()));
        return false;
    }
    const usize expectedFieldCount = outSubsystem.fieldCount();
    if(header.fieldCount != expectedFieldCount){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: expected {} asset fields for '{}', got {}"), expectedFieldCount, ConvertString(outSubsystem.assetType().resolvedText()), header.fieldCount);
        return false;
    }

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

    u64 totalCount = 0u;
    if(!TrySumSubsystemFieldElementCounts(Span<const u64>(fieldCounts), totalCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: invalid field element count"));
        return false;
    }
    if(totalCount != header.assetCount){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: field element counts do not sum to asset count"));
        return false;
    }

    usize hashOffset = 0u;
    for(usize i = 0u; i < expectedFieldCount; ++i){
        const SubsystemFieldValueDescriptor descriptor = outSubsystem.fieldDescriptor(i);
        const u64 elementCount = fieldCounts[i];
        if(!descriptor.isArray && elementCount != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: scalar field '{}' in '{}' has element count {}"), ConvertString(descriptor.fieldName), ConvertString(outSubsystem.assetType().resolvedText()), elementCount);
            return false;
        }
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
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SubsystemHeaderBinary>(binary, s_SubsystemMagic, s_SubsystemLoadBinaryContext, s_SubsystemAssetKindLabel, cursor);
    if(!headerResult)
        return MakeUnexpected(Failure{});

    auto asset = SubsystemTypeAutoRegistrar::CreateByTypeHash(virtualPath, headerResult->subsystemTypeHash, arena);
    if(!asset){
        NWB_LOGGER_ERROR(NWB_TEXT("SubsystemAssetCodec::deserialize failed: unregistered subsystem type in '{}'"), ConvertString(virtualPath.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    if(!asset->loadBinary(binary))
        return MakeUnexpected(Failure{});

    return UniquePtr<Core::Assets::IAsset>(Move(asset));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

