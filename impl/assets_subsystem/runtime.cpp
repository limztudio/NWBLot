// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SubsystemAssetCodecAutoRegistrar, SubsystemAssetCodec);

static const SubsystemTypeAutoRegistrar* s_RegisteredSubsystemTypesHead = nullptr;


[[nodiscard]] static UniquePtr<ISubsystem> CreateSubsystemByTypeHash(Core::Assets::AssetArena& arena, const Name& virtualPath, const NameHash& subsystemTypeHash){
    for(const SubsystemTypeAutoRegistrar* node = s_RegisteredSubsystemTypesHead; node != nullptr; node = node->next()){
        if(node->assetType().hash() == subsystemTypeHash && node->create() != nullptr)
            return node->create()(arena, virtualPath);
    }
    return nullptr;
}

static void DeduplicateSubsystemRefs(ISubsystem::SubsystemRefVector& subsystems)noexcept{
    usize writeIndex = 0u;
    for(usize readIndex = 0u; readIndex < subsystems.size(); ++readIndex){
        const ISubsystem::SubsystemRef& candidate = subsystems[readIndex];
        bool duplicate = false;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ISubsystem::~ISubsystem()noexcept = default;


SubsystemTypeAutoRegistrar::SubsystemTypeAutoRegistrar(const Name& assetType, const SubsystemInstanceFactory create)noexcept
    : m_assetType(assetType)
    , m_create(create)
    , m_next(__hidden_subsystem_runtime::s_RegisteredSubsystemTypesHead)
{
    __hidden_subsystem_runtime::s_RegisteredSubsystemTypesHead = this;
}


bool IsSubsystemAssetType(const Name& assetType)noexcept{
    if(!assetType)
        return false;
    for(const SubsystemTypeAutoRegistrar* node = __hidden_subsystem_runtime::s_RegisteredSubsystemTypesHead; node != nullptr; node = node->next()){
        if(node->assetType() == assetType)
            return true;
    }
    return false;
}

const ISubsystem* CastSubsystem(const Core::Assets::IAsset* const asset)noexcept{
    if(!asset || !IsSubsystemAssetType(asset->assetType()))
        return nullptr;
    return checked_cast<const ISubsystem*>(asset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SubsystemAssetRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidatePayload(const ISubsystem& subsystem){
    if(!subsystem.checkVirtualPath(NWB_TEXT("ISubsystem::validatePayload")))
        return false;

    for(usize i = 0u; i < subsystem.subsystems.size(); ++i){
        const ISubsystem::SubsystemRef& ref = subsystem.subsystems[i];
        if(!ref.valid()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' dependent subsystem entry {} must not be empty"),
                ConvertString(subsystem.virtualPath().resolvedText()),
                i
            );
            return false;
        }
        if(ref.name() == subsystem.virtualPath()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' cannot reference itself in 'subsystems'"),
                ConvertString(subsystem.virtualPath().resolvedText())
            );
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
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' array field '{}' must not be empty"),
                    ConvertString(subsystem.virtualPath().resolvedText()),
                    ConvertString(descriptor.fieldName)
                );
                return false;
            }
        }
        else if(valueCount != 1u){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' field '{}' must contain exactly one value"),
                ConvertString(subsystem.virtualPath().resolvedText()),
                ConvertString(descriptor.fieldName)
            );
            return false;
        }

        for(usize elementIndex = 0u; elementIndex < valueCount; ++elementIndex){
            const Name value = subsystem.fieldValue(i, elementIndex);
            if(!value){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' field '{}' must not be empty"),
                    ConvertString(subsystem.virtualPath().resolvedText()),
                    ConvertString(descriptor.fieldName)
                );
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

bool LoadBinary(ISubsystem& subsystem, const Span<const u8> binary){
    Core::Alloc::ScratchArena scratchArena(AssetsSubsystemArenaScope::s_LoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SubsystemBinaryPayload::SubsystemHeaderBinary>(
        binary,
        SubsystemBinaryPayload::s_SubsystemMagic,
        SubsystemBinaryPayload::s_SubsystemLoadBinaryContext,
        SubsystemBinaryPayload::s_SubsystemAssetKindLabel,
        cursor
    );
    if(!headerResult)
        return false;
    const SubsystemBinaryPayload::SubsystemHeaderBinary& header = *headerResult;
    if(header.version != SubsystemBinaryPayload::s_SubsystemVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }
    if(header.subsystemTypeHash != subsystem.assetType().hash()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ISubsystem::loadBinary failed: subsystem binary type does not match '{}'"),
            ConvertString(subsystem.assetType().resolvedText())
        );
        return false;
    }
    const usize expectedFieldCount = subsystem.fieldCount();
    if(header.fieldCount != expectedFieldCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ISubsystem::loadBinary failed: expected {} asset fields for '{}', got {}"),
            expectedFieldCount,
            ConvertString(subsystem.assetType().resolvedText()),
            header.fieldCount
        );
        return false;
    }

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<u64, Core::Alloc::ScratchArena> fieldCounts(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);
    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_SubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.fieldCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_FieldCountsPayloadLabel, cursor, fieldCounts))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_AssetsPayloadLabel, cursor, assetHashes))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext))
        return false;

    subsystem.subsystems.clear();
    subsystem.subsystems.reserve(subsystemHashes.size());
    for(const NameHash& subsystemHash : subsystemHashes){
        const Name subsystemName(subsystemHash);
        if(!subsystemName){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: subsystem reference must not be empty"));
            return false;
        }
        subsystem.subsystems.push_back(ISubsystem::SubsystemRef(subsystemName));
    }
    __hidden_subsystem_runtime::DeduplicateSubsystemRefs(subsystem.subsystems);

    u64 totalCount = 0u;
    for(const u64 count : fieldCounts){
        if(count == 0u || count > Limit<u64>::s_Max - totalCount){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: invalid field element count"));
            return false;
        }
        totalCount += count;
    }
    if(totalCount != header.assetCount){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: field element counts do not sum to asset count"));
        return false;
    }

    usize hashOffset = 0u;
    for(usize i = 0u; i < expectedFieldCount; ++i){
        const SubsystemFieldValueDescriptor descriptor = subsystem.fieldDescriptor(i);
        const u64 elementCount = fieldCounts[i];
        if(!descriptor.isArray && elementCount != 1u){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ISubsystem::loadBinary failed: scalar field '{}' in '{}' has element count {}"),
                ConvertString(descriptor.fieldName),
                ConvertString(subsystem.assetType().resolvedText()),
                elementCount
            );
            return false;
        }
        subsystem.clearFieldValues(i);
        for(u64 elementIndex = 0u; elementIndex < elementCount; ++elementIndex){
            const Name assetName(assetHashes[hashOffset++]);
            if(!assetName){
                NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: asset reference must not be empty"));
                return false;
            }
            subsystem.appendFieldValue(i, assetName);
        }
    }

    return subsystem.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UniquePtr<Core::Assets::IAsset>> SubsystemAssetCodec::deserialize(Core::Assets::AssetArena& arena, const Name& virtualPath, const Span<const u8> binary)const{
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SubsystemBinaryPayload::SubsystemHeaderBinary>(
        binary,
        SubsystemBinaryPayload::s_SubsystemMagic,
        SubsystemBinaryPayload::s_SubsystemLoadBinaryContext,
        SubsystemBinaryPayload::s_SubsystemAssetKindLabel,
        cursor
    );
    if(!headerResult)
        return MakeUnexpected(Failure{});

    auto asset = __hidden_subsystem_runtime::CreateSubsystemByTypeHash(arena, virtualPath, headerResult->subsystemTypeHash);
    if(!asset){
        NWB_LOGGER_ERROR(
            NWB_TEXT("SubsystemAssetCodec::deserialize failed: unregistered subsystem type in '{}'"),
            ConvertString(virtualPath.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }
    if(!asset->loadBinary(binary))
        return MakeUnexpected(Failure{});

    return UniquePtr<Core::Assets::IAsset>(Move(asset));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

