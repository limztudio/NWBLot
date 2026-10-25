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
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: {} entry {} is empty"), contextLabel, i);
            return false;
        }
        if(disallowedSelfName && entry.name() == disallowedSelfName){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: {} entry {} references its own subsystem"), contextLabel, i);
            return false;
        }
        if(!seen.insert(entry.name().hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: duplicate {} entry at index {}"), contextLabel, i);
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool ValidateAssetMap(const Subsystem::AssetMap& assets){
    for(const auto& [nickname, assetRef] : assets){
        if(!nickname){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: asset entry has an empty nickname"));
            return false;
        }
        if(!assetRef.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: asset nickname '{}' has an empty asset reference"), ConvertString(nickname.resolvedText()));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Subsystem::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Subsystem::validatePayload")))
        return false;
    if(m_subsystems.empty() && m_assets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: subsystem must declare at least one child subsystem or asset"));
        return false;
    }

    if(!__hidden_subsystem_runtime::ValidateUniqueAssetRefs(Span<const SubsystemRef>(m_subsystems), NWB_TEXT("child subsystem"), virtualPath(), scratchArena))
        return false;
    if(!__hidden_subsystem_runtime::ValidateAssetMap(m_assets))
        return false;

    return true;
}

bool Subsystem::loadBinary(const Span<const u8> binary){
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
        NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<SubsystemBinaryPayload::SubsystemAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_SubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_AssetsPayloadLabel, cursor, assetEntries))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext))
        return false;

    auto& assetArena = m_subsystems.get_allocator().arena();
    Subsystem candidate(assetArena, virtualPath());

    candidate.m_subsystems.reserve(subsystemHashes.size());
    for(const NameHash& hash : subsystemHashes){
        SubsystemRef ref;
        ref.virtualPath = Name(hash);
        candidate.m_subsystems.push_back(ref);
    }

    candidate.m_assets.reserve(assetEntries.size());
    for(usize i = 0u; i < assetEntries.size(); ++i){
        const SubsystemBinaryPayload::SubsystemAssetEntryBinary& entry = assetEntries[i];
        const Name nickname(entry.nicknameHash);
        AssetRef ref;
        ref.virtualPath = Name(entry.assetHash);
        if(!nickname || !ref.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::loadBinary failed: invalid asset entry at index {}"), i);
            return false;
        }
        if(!candidate.m_assets.emplace(nickname, ref).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::loadBinary failed: duplicate asset nickname at index {}"), i);
            return false;
        }
    }

    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

