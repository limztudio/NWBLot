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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Subsystem::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("Subsystem::validatePayload")))
        return false;
    if(m_subsystems.empty() && m_assets.empty() && m_features.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: subsystem must declare at least one child subsystem, asset, or feature"));
        return false;
    }

    if(!__hidden_subsystem_runtime::ValidateUniqueAssetRefs(Span<const SubsystemRef>(m_subsystems), NWB_TEXT("child subsystem"), virtualPath(), scratchArena))
        return false;
    if(!__hidden_subsystem_runtime::ValidateUniqueAssetRefs(Span<const AssetRef>(m_assets), NWB_TEXT("asset"), s_NameNone, scratchArena))
        return false;

    __hidden_subsystem_runtime::NameHashSet seenFeatures(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    seenFeatures.reserve(m_features.size());
    for(usize i = 0u; i < m_features.size(); ++i){
        const SubsystemFeature& feature = m_features[i];
        if(!feature.name){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: feature {} has an empty name"), i);
            return false;
        }
        if(!seenFeatures.insert(feature.name.hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: feature {} name is duplicated"), i);
            return false;
        }
        if(feature.assets.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::validatePayload failed: feature {} must list at least one asset"), i);
            return false;
        }
        if(!__hidden_subsystem_runtime::ValidateUniqueAssetRefs(Span<const SubsystemFeature::AssetRef>(feature.assets), NWB_TEXT("feature asset"), s_NameNone, scratchArena))
            return false;
    }

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
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);
    Vector<SubsystemBinaryPayload::SubsystemFeatureBinary, Core::Alloc::ScratchArena> featureBinaries(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> featureAssetHashes(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_SubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_AssetsPayloadLabel, cursor, assetHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.featureCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_FeaturesPayloadLabel, cursor, featureBinaries))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.featureAssetCount, SubsystemBinaryPayload::s_SubsystemLoadBinaryContext, SubsystemBinaryPayload::s_FeatureAssetsPayloadLabel, cursor, featureAssetHashes))
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

    candidate.m_assets.reserve(assetHashes.size());
    for(const NameHash& hash : assetHashes){
        AssetRef ref;
        ref.virtualPath = Name(hash);
        candidate.m_assets.push_back(ref);
    }

    candidate.m_features.reserve(featureBinaries.size());
    u64 expectedOffset = 0u;
    for(const SubsystemBinaryPayload::SubsystemFeatureBinary& featureBinary : featureBinaries){
        if(featureBinary.assetOffset != expectedOffset || featureBinary.assetCount > featureAssetHashes.size() - expectedOffset){
            NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::loadBinary failed: malformed feature asset range"));
            return false;
        }
        SubsystemFeature feature(assetArena);
        feature.name = Name(featureBinary.featureNameHash);
        feature.assets.reserve(static_cast<usize>(featureBinary.assetCount));
        for(u64 index = 0u; index < featureBinary.assetCount; ++index){
            AssetRef ref;
            ref.virtualPath = Name(featureAssetHashes[static_cast<usize>(featureBinary.assetOffset + index)]);
            feature.assets.push_back(ref);
        }
        expectedOffset += featureBinary.assetCount;
        candidate.m_features.push_back(Move(feature));
    }
    if(expectedOffset != featureAssetHashes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Subsystem::loadBinary failed: unreferenced feature asset entries"));
        return false;
    }

    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

