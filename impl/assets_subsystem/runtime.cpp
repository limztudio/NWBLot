// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SubsystemAssetCodecAutoRegistrar, SubsystemAssetCodec);
#define NWB_REGISTER_TYPED_SUBSYSTEM_ASSET_CODEC(Type) NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_##Type##AssetCodecAutoRegistrar, Type##AssetCodec);
NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_REGISTER_TYPED_SUBSYSTEM_ASSET_CODEC)
#undef NWB_REGISTER_TYPED_SUBSYSTEM_ASSET_CODEC

using NameHashSet = HashSet<NameHash, Core::Alloc::ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


template<typename TAsset>
[[nodiscard]] static bool ValidateUniqueAssetRefs(
    const Span<const Core::Assets::AssetRef<TAsset>> references,
    const TStringView contextLabel,
    const Name disallowedSelfName,
    Core::Alloc::ScratchArena& scratchArena
){
    NameHashSet seen(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    seen.reserve(references.size());
    for(usize i = 0u; i < references.size(); ++i){
        const Core::Assets::AssetRef<TAsset>& entry = references[i];
        if(!entry.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: {} entry {} is empty"), contextLabel, i);
            return false;
        }
        if(disallowedSelfName && entry.name() == disallowedSelfName){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: {} entry {} references its own subsystem"), contextLabel, i);
            return false;
        }
        if(!seen.insert(entry.name().hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: duplicate {} entry at index {}"), contextLabel, i);
            return false;
        }
    }
    return true;
}

[[nodiscard]] static UniquePtr<ISubsystem> CreateSubsystemByTypeHash(
    Core::Assets::AssetArena& arena,
    const Name& virtualPath,
    const NameHash& subsystemTypeHash
){
#define NWB_CREATE_SUBSYSTEM_IF_MATCH(Type) \
    if(subsystemTypeHash == Type::AssetTypeName().hash()) \
        return MakeUnique<Type>(arena, virtualPath);
    NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_CREATE_SUBSYSTEM_IF_MATCH)
#undef NWB_CREATE_SUBSYSTEM_IF_MATCH
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ISubsystem::~ISubsystem()noexcept = default;


bool IsSubsystemAssetType(const Name& assetType)noexcept{
    if(!assetType)
        return false;
#define NWB_CHECK_SUBSYSTEM_TYPE(Type) \
    if(assetType == Type::AssetTypeName()) \
        return true;
    NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_CHECK_SUBSYSTEM_TYPE)
#undef NWB_CHECK_SUBSYSTEM_TYPE
    return false;
}

const ISubsystem* CastSubsystem(const Core::Assets::IAsset* const asset)noexcept{
    if(!asset || !IsSubsystemAssetType(asset->assetType()))
        return nullptr;
    return checked_cast<const ISubsystem*>(asset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
SubsystemFieldValueDescriptor TypedSubsystem<SubsystemT>::fieldDescriptor(const usize index)const noexcept{
    const auto descriptors = FieldDescriptors();
    NWB_ASSERT(index < descriptors.size());
    return SubsystemFieldValueDescriptor{
        descriptors[index].fieldName,
        descriptors[index].expectedAssetTypeText
    };
}

template<typename SubsystemT>
Name TypedSubsystem<SubsystemT>::fieldValue(const usize index)const noexcept{
    const auto descriptors = FieldDescriptors();
    NWB_ASSERT(index < descriptors.size());
    return descriptors[index].get(*static_cast<const SubsystemT*>(this));
}

template<typename SubsystemT>
void TypedSubsystem<SubsystemT>::setFieldValue(const usize index, const Name& value)noexcept{
    const auto descriptors = FieldDescriptors();
    NWB_ASSERT(index < descriptors.size());
    descriptors[index].set(*static_cast<SubsystemT*>(this), value);
}

template<typename SubsystemT>
void TypedSubsystem<SubsystemT>::collectAssets(Vector<AssetRef, Core::Alloc::GlobalArena>& outAssets)const{
    const auto descriptors = FieldDescriptors();
    const SubsystemT& derived = *static_cast<const SubsystemT*>(this);
    for(const SubsystemFieldDescriptor<SubsystemT>& descriptor : descriptors){
        const Name value = descriptor.get(derived);
        if(value)
            outAssets.push_back(AssetRef(value));
    }
}

template<typename SubsystemT>
bool TypedSubsystem<SubsystemT>::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("ISubsystem::validatePayload")))
        return false;

    if constexpr(!SubsystemT::s_SupportsChildSubsystems){
        if(!subsystems().empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' of type '{}' does not support child subsystems"),
                ConvertString(virtualPath().resolvedText()),
                ConvertString(SubsystemT::s_AssetTypeText)
            );
            return false;
        }
    }

    if constexpr(SubsystemT::s_FieldCount == 0u){
        if(subsystems().empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::validatePayload failed: subsystem must declare at least one child subsystem or asset"));
            return false;
        }
    }

    if(!__hidden_subsystem_runtime::ValidateUniqueAssetRefs(Span<const SubsystemRef>(subsystems()), NWB_TEXT("child subsystem"), virtualPath(), scratchArena))
        return false;

    const auto descriptors = FieldDescriptors();
    const SubsystemT& derived = *static_cast<const SubsystemT*>(this);
    for(const SubsystemFieldDescriptor<SubsystemT>& descriptor : descriptors){
        const Name value = descriptor.get(derived);
        if(!value){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ISubsystem::validatePayload failed: subsystem '{}' field '{}' must not be empty"),
                ConvertString(virtualPath().resolvedText()),
                ConvertString(descriptor.fieldName)
            );
            return false;
        }
    }

    return true;
}

template<typename SubsystemT>
bool TypedSubsystem<SubsystemT>::loadBinary(const Span<const u8> binary){
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
    if(header.subsystemTypeHash != SubsystemT::AssetTypeName().hash()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ISubsystem::loadBinary failed: subsystem binary type does not match '{}'"),
            ConvertString(SubsystemT::s_AssetTypeText)
        );
        return false;
    }
    if(header.assetCount != SubsystemT::s_FieldCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ISubsystem::loadBinary failed: expected {} asset fields for '{}', got {}"),
            SubsystemT::s_FieldCount,
            ConvertString(SubsystemT::s_AssetTypeText),
            header.assetCount
        );
        return false;
    }

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_SubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_AssetsPayloadLabel, cursor, assetHashes))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext))
        return false;

    auto& assetArena = subsystems().get_allocator().arena();
    SubsystemT candidate(assetArena, virtualPath());

    candidate.subsystems().reserve(subsystemHashes.size());
    for(const NameHash& hash : subsystemHashes){
        SubsystemRef ref;
        ref.virtualPath = Name(hash);
        candidate.subsystems().push_back(ref);
    }

    const auto descriptors = FieldDescriptors();
    for(usize i = 0u; i < descriptors.size(); ++i){
        const Name assetName(assetHashes[i]);
        if(!assetName){
            NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: invalid asset entry at index {}"), i);
            return false;
        }
        descriptors[i].set(candidate, assetName);
    }

    if(!candidate.validatePayload(scratchArena))
        return false;
    *static_cast<SubsystemT*>(this) = Move(candidate);
    return true;
}


Expected<UniquePtr<Core::Assets::IAsset>> SubsystemAssetCodec::deserialize(
    Core::Assets::AssetArena& arena,
    const Name& virtualPath,
    const Span<const u8> binary
)const{
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
    const SubsystemBinaryPayload::SubsystemHeaderBinary& header = *headerResult;
    if(header.version != SubsystemBinaryPayload::s_SubsystemVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("ISubsystem::loadBinary failed: unsupported version {}"), header.version);
        return MakeUnexpected(Failure{});
    }

    auto subsystem = __hidden_subsystem_runtime::CreateSubsystemByTypeHash(arena, virtualPath, header.subsystemTypeHash);
    if(!subsystem){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ISubsystem::loadBinary failed: unknown concrete subsystem type hash for '{}'"),
            ConvertString(virtualPath.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }
    if(!subsystem->loadBinary(binary))
        return MakeUnexpected(Failure{});

    return UniquePtr<Core::Assets::IAsset>(Move(subsystem));
}


#define NWB_INSTANTIATE_TYPED_SUBSYSTEM(Type) template class TypedSubsystem<Type>;
NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_INSTANTIATE_TYPED_SUBSYSTEM)
#undef NWB_INSTANTIATE_TYPED_SUBSYSTEM


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

