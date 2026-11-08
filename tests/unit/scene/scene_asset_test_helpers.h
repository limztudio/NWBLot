// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/unit/scene/name_cases.adhd.h>

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

inline constexpr Name s_RendererSubsystemPath = NWB::Tests::SceneAssetCases::s_SceneAssetNameEngineSubsystemsRenderer;
inline constexpr Name s_SkinningSubsystemPath = NWB::Tests::SceneAssetCases::s_SceneAssetNameEngineSubsystemsSkinning;
inline constexpr Name s_TransparentSubsystemPath = NWB::Tests::SceneAssetCases::s_SceneAssetNameEngineSubsystemsTransparent;
inline constexpr Name s_UiSubsystemPath = NWB::Tests::SceneAssetCases::s_SceneAssetNameEngineSubsystemsUi;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] AssetBytes MakeProjectBinary(const Name& startScene, AssetArena& arena);
[[nodiscard]] AssetBytes MakeSceneBinary(Span<const SceneAsset::SubsystemRef> subsystems, const SceneAsset::AssetMap& assets, Span<const SceneAsset::EntityRef> entities, AssetArena& arena);
[[nodiscard]] AssetBytes MakeEntityBinary(Span<const EntityAsset::ComponentRef> components, AssetArena& arena);

template<typename ComponentT, typename PayloadT>
[[nodiscard]] inline AssetBytes MakeComponentBinary(const PayloadT& payload, AssetArena& arena){
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
                subsystem.appendFieldValue(i, Name(NWB::Tests::SceneAssetCases::s_SceneAssetNameEngineTestDefaultAsset));
        }
        else if(!subsystem.fieldValue(i))
            subsystem.appendFieldValue(i, Name(NWB::Tests::SceneAssetCases::s_SceneAssetNameEngineTestDefaultAsset));
    }
}

template<typename SubsystemT>
[[nodiscard]] inline AssetBytes MakeSubsystemBinary(const SubsystemT& subsystem, AssetArena& arena){
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
        AssetBytes binary;
        Name path = s_NameNone;


    public:
        Entry(const Name& inPath, AssetBytes&& inBinary)noexcept
            : binary(Move(inBinary))
            , path(inPath)
        {}
    };


public:
    explicit MapAssetBinarySource(AssetArena& arena)noexcept
        : m_arena(arena)
        , m_entries(arena)
    {}


public:
    void setBinary(const Name& virtualPath, Span<const u8> binary);
    virtual b8 readAssetBinary(const Name& virtualPath, AssetBytes& outBinary)const override;


private:
    AssetArena& m_arena;
    AssetVector<Entry> m_entries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

