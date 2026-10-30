// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_scene/asset.h>
#include <impl/assets_subsystem/asset.h>

#include <core/assets/manager.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::SceneAssetTestHelpers{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Assets;
using namespace NWB::Impl;

inline constexpr Name s_RendererSubsystemPath("engine/subsystems/renderer");
inline constexpr Name s_SkinningSubsystemPath("engine/subsystems/skinning");
inline constexpr Name s_TransparentSubsystemPath("engine/subsystems/transparent");
inline constexpr Name s_UiSubsystemPath("engine/subsystems/ui");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline AssetBytes MakeProjectBinary(AssetArena& arena, const Name& startScene){
    ProjectHeaderBinary header;
    header.startSceneHash = startScene.hash();

    AssetBytes binary(arena);
    binary.reserve(sizeof(header));
    AppendPOD(binary, header);
    return binary;
}

[[nodiscard]] inline AssetBytes MakeSceneBinary(AssetArena& arena, const Span<const SceneAsset::SubsystemRef> subsystems, const SceneAsset::AssetMap& assets, const Span<const SceneAsset::EntityRef> entities = {}){
    SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystems.size());
    header.entityCount = static_cast<u64>(entities.size());
    header.assetCount = static_cast<u64>(assets.size());

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + subsystems.size() * sizeof(NameHash) + entities.size() * sizeof(NameHash) + assets.size() * sizeof(SceneAssetEntryBinary));
    AppendPOD(binary, header);
    for(const SceneAsset::SubsystemRef& ref : subsystems)
        AppendPOD(binary, ref.name().hash());
    for(const SceneAsset::EntityRef& ref : entities)
        AppendPOD(binary, ref.name().hash());
    for(const auto& [nickname, ref] : assets){
        SceneAssetEntryBinary entry;
        entry.nicknameHash = nickname.hash();
        entry.assetHash = ref.name().hash();
        AppendPOD(binary, entry);
    }
    return binary;
}

[[nodiscard]] inline AssetBytes MakeEntityBinary(AssetArena& arena, const Span<const EntityAsset::ComponentRef> components){
    EntityHeaderBinary header;
    header.componentCount = static_cast<u64>(components.size());

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + components.size() * sizeof(NameHash));
    AppendPOD(binary, header);
    for(const EntityAsset::ComponentRef& ref : components)
        AppendPOD(binary, ref.name().hash());
    return binary;
}

template<typename ComponentT, typename PayloadT>
[[nodiscard]] inline AssetBytes MakeComponentBinary(AssetArena& arena, const PayloadT& payload){
    ComponentHeaderBinary header;
    header.componentTypeHash = ComponentT::AssetTypeName().hash();
    header.payloadByteCount = sizeof(PayloadT);

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + sizeof(PayloadT));
    AppendPOD(binary, header);
    AppendPOD(binary, payload);
    return binary;
}

template<typename SubsystemT>
inline void PopulateRequiredSubsystemFields(SubsystemT& subsystem){
    const usize count = subsystem.fieldCount();
    for(usize i = 0u; i < count; ++i){
        const SubsystemFieldValueDescriptor descriptor = subsystem.fieldDescriptor(i);
        if(descriptor.isArray){
            if(subsystem.fieldValueCount(i) == 0u)
                subsystem.appendFieldValue(i, Name("engine/test/default_asset"));
        }
        else if(!subsystem.fieldValue(i))
            subsystem.appendFieldValue(i, Name("engine/test/default_asset"));
    }
}

template<typename SubsystemT>
[[nodiscard]] inline AssetBytes MakeSubsystemBinary(AssetArena& arena, const SubsystemT& subsystem){
    const usize subsystemCount = subsystem.subsystems.size();
    const usize fieldCount = subsystem.fieldCount();
    u64 totalAssetCount = 0u;
    for(usize i = 0u; i < fieldCount; ++i)
        totalAssetCount += static_cast<u64>(subsystem.fieldValueCount(i));

    SubsystemHeaderBinary header;
    header.subsystemTypeHash = SubsystemT::AssetTypeName().hash();
    header.subsystemCount = static_cast<u64>(subsystemCount);
    header.fieldCount = static_cast<u64>(fieldCount);
    header.assetCount = totalAssetCount;

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + subsystemCount * sizeof(NameHash) + fieldCount * sizeof(u64) + static_cast<usize>(totalAssetCount) * sizeof(NameHash));
    AppendPOD(binary, header);
    for(const ISubsystem::SubsystemRef& ref : subsystem.subsystems)
        AppendPOD(binary, ref.name().hash());
    for(usize i = 0u; i < fieldCount; ++i){
        const u64 valueCount = static_cast<u64>(subsystem.fieldValueCount(i));
        AppendPOD(binary, valueCount);
    }
    for(usize i = 0u; i < fieldCount; ++i){
        const usize valueCount = subsystem.fieldValueCount(i);
        for(usize elementIndex = 0u; elementIndex < valueCount; ++elementIndex)
            AppendPOD(binary, subsystem.fieldValue(i, elementIndex).hash());
    }
    return binary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MapAssetBinarySource final : public IAssetBinarySource{
public:
    struct Entry{
        Name path = s_NameNone;
        AssetBytes binary;


    public:
        Entry(const Name& inPath, AssetBytes&& inBinary)noexcept
            : path(inPath)
            , binary(Move(inBinary))
        {}
    };


public:
    explicit MapAssetBinarySource(AssetArena& arena)noexcept
        : m_arena(arena)
        , m_entries(arena)
    {}


public:
    void setBinary(const Name& virtualPath, const Span<const u8> binary){
        AssetBytes copy(m_arena);
        copy.assign(binary.begin(), binary.end());
        for(Entry& entry : m_entries){
            if(entry.path == virtualPath){
                entry.binary = Move(copy);
                return;
            }
        }
        m_entries.emplace_back(virtualPath, Move(copy));
    }

    virtual b8 readAssetBinary(const Name& virtualPath, AssetBytes& outBinary)const override{
        outBinary.clear();
        for(const Entry& entry : m_entries){
            if(entry.path == virtualPath){
                outBinary.assign(entry.binary.begin(), entry.binary.end());
                return true;
            }
        }
        return false;
    }


private:
    AssetArena& m_arena;
    AssetVector<Entry> m_entries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

