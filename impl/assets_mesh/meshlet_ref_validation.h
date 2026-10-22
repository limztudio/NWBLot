// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"
#include "meshlet_payload_packing.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshletRefValidationExceptionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename MeshletContainer, typename LocalVertexRefContainer, typename AttributeSkinContainer, typename SkinResolver, typename ConflictHandler, typename UnreferencedHandler>
consteval bool AttributeSkinsIsNothrow()noexcept{
    using Result = decltype(DeclVal<SkinResolver&>()(DeclVal<usize&>(), DeclVal<const MeshletDesc&>(), DeclVal<usize&>(), DeclVal<const u32&>()));
    using SkinIndex = Decay_T<Result>;
    return
        IsNothrowDestructible_V<SkinResolver> && IsNothrowDestructible_V<ConflictHandler> && IsNothrowDestructible_V<UnreferencedHandler>
        && noexcept(DeclVal<AttributeSkinContainer&>().clear())
        && noexcept(DeclVal<AttributeSkinContainer&>().resize(DeclVal<const usize&>(), s_MeshMissingStreamIndex))
        && noexcept(static_cast<bool>(DeclVal<usize&>() < DeclVal<const MeshletContainer&>().size()))
        && noexcept(DeclVal<void(*)(const MeshletDesc&)noexcept>()(DeclVal<const MeshletContainer&>()[DeclVal<usize&>()]))
        && noexcept(DeclVal<void(*)(const MeshletLocalVertexRef&)noexcept>()(DeclVal<const LocalVertexRefContainer&>()[u32{}]))
        && noexcept(DeclVal<SkinResolver&>()(DeclVal<usize&>(), DeclVal<const MeshletDesc&>(), DeclVal<usize&>(), DeclVal<const u32&>()))
        && (!IsReference_V<Result> || IsNothrowConstructible_V<SkinIndex, Result>)
        && IsNothrowDestructible_V<SkinIndex>
        && noexcept(static_cast<bool>(!DeclVal<const SkinIndex&>()))
        && noexcept(DeclVal<void(*)(u32&)noexcept>()(DeclVal<AttributeSkinContainer&>()[DeclVal<const usize&>()]))
        && noexcept(DeclVal<u32&>() = *DeclVal<const SkinIndex&>())
        && noexcept(static_cast<bool>(DeclVal<u32&>() == *DeclVal<const SkinIndex&>()))
        && noexcept(DeclVal<void(*)(bool)noexcept>()(DeclVal<ConflictHandler&>()(DeclVal<usize&>(), DeclVal<const usize&>(), DeclVal<u32&>(), *DeclVal<const SkinIndex&>())))
        && noexcept(static_cast<bool>(DeclVal<usize&>() < DeclVal<AttributeSkinContainer&>().size()))
        && noexcept(static_cast<bool>(DeclVal<AttributeSkinContainer&>()[DeclVal<usize&>()] != s_MeshMissingStreamIndex))
        && noexcept(DeclVal<void(*)(bool)noexcept>()(DeclVal<UnreferencedHandler&>()(DeclVal<usize&>())))
    ;
}


template<typename PositionRefContainer>
using PositionSkinResolver = Expected<u32>(*)(const usize, const MeshletDesc&, const usize, const u32)noexcept(noexcept(Expected<u32>{ DeclVal<const PositionRefContainer&>()[usize{}].skin }));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshMeshletRefValidation final : NoCopy{
public:
    [[nodiscard]] static bool MeshletPositionRefInRange(const MeshletPositionStreamRef& ref, const usize positionCount, const usize skinCount, const bool skinRequired)noexcept;
    [[nodiscard]] static bool MeshletAttributeRefInRange(const MeshletAttributeStreamRef& ref, const usize normalCount, const usize tangentCount, const usize uv0Count, const usize colorCount)noexcept;
    template<
        typename MeshletContainer,
        typename LocalVertexRefContainer,
        typename AttributeSkinContainer,
        typename SkinResolver,
        typename ConflictHandler,
        typename UnreferencedHandler
    >
    [[nodiscard]] static bool ResolveMeshletAttributeSkinsFromLocalVertices(
        const MeshletContainer& meshlets,
        const LocalVertexRefContainer& localVertexRefs,
        const usize attributeCount,
        AttributeSkinContainer& outAttributeSkins,
        SkinResolver resolveSkin,
        ConflictHandler onConflict,
        UnreferencedHandler onUnreferenced
    )noexcept(MeshletRefValidationExceptionDetail::AttributeSkinsIsNothrow<MeshletContainer, LocalVertexRefContainer, AttributeSkinContainer, SkinResolver, ConflictHandler, UnreferencedHandler>());
    template<
        typename MeshletContainer,
        typename PositionRefContainer,
        typename LocalVertexRefContainer,
        typename AttributeSkinContainer,
        typename ConflictHandler,
        typename UnreferencedHandler
    >
    [[nodiscard]] static bool ResolveMeshletAttributeSkins(
        const MeshletContainer& meshlets,
        const PositionRefContainer& positionRefs,
        const LocalVertexRefContainer& localVertexRefs,
        const usize attributeCount,
        AttributeSkinContainer& outAttributeSkins,
        ConflictHandler onConflict,
        UnreferencedHandler onUnreferenced
    )noexcept(
        IsNothrowConstructible_V<ConflictHandler, ConflictHandler&> && IsNothrowConstructible_V<UnreferencedHandler, UnreferencedHandler&>
        && MeshletRefValidationExceptionDetail::AttributeSkinsIsNothrow<MeshletContainer, LocalVertexRefContainer, AttributeSkinContainer, MeshletRefValidationExceptionDetail::PositionSkinResolver<PositionRefContainer>, ConflictHandler, UnreferencedHandler>()
    );


public:
    MeshMeshletRefValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<
    typename MeshletContainer,
    typename LocalVertexRefContainer,
    typename AttributeSkinContainer,
    typename SkinResolver,
    typename ConflictHandler,
    typename UnreferencedHandler
>
[[nodiscard]] bool MeshMeshletRefValidation::ResolveMeshletAttributeSkinsFromLocalVertices(
    const MeshletContainer& meshlets,
    const LocalVertexRefContainer& localVertexRefs,
    const usize attributeCount,
    AttributeSkinContainer& outAttributeSkins,
    SkinResolver resolveSkin,
    ConflictHandler onConflict,
    UnreferencedHandler onUnreferenced
)noexcept(MeshletRefValidationExceptionDetail::AttributeSkinsIsNothrow<MeshletContainer, LocalVertexRefContainer, AttributeSkinContainer, SkinResolver, ConflictHandler, UnreferencedHandler>()){
    outAttributeSkins.clear();
    outAttributeSkins.resize(attributeCount, s_MeshMissingStreamIndex);

    usize meshletPositionBase = 0u;
    usize meshletAttributeBase = 0u;
    for(usize meshletIndex = 0u; meshletIndex < meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = meshlets[meshletIndex];
        const u32 vertexCount = MeshletVertexCount(meshlet);
        for(u32 localVertexIndex = 0u; localVertexIndex < vertexCount; ++localVertexIndex){
            const MeshletLocalVertexRef& localVertexRef = localVertexRefs[meshlet.localVertexOffset + localVertexIndex];
            const usize attributeIndex = meshletAttributeBase + localVertexRef.localAttribute;
            const auto skinIndex = resolveSkin(meshletIndex, meshlet, meshletPositionBase, localVertexRef.localDeformedPosition);
            if(!skinIndex)
                return false;

            u32& attributeSkin = outAttributeSkins[attributeIndex];
            if(attributeSkin == s_MeshMissingStreamIndex){
                attributeSkin = *skinIndex;
                continue;
            }
            if(attributeSkin == *skinIndex)
                continue;
            return onConflict(meshletIndex, attributeIndex, attributeSkin, *skinIndex);
        }

        meshletPositionBase += MeshletPositionCount(meshlet);
        meshletAttributeBase += MeshletAttributeCount(meshlet);
    }

    for(usize attributeIndex = 0u; attributeIndex < outAttributeSkins.size(); ++attributeIndex){
        if(outAttributeSkins[attributeIndex] != s_MeshMissingStreamIndex)
            continue;
        return onUnreferenced(attributeIndex);
    }

    return true;
}


template<
    typename MeshletContainer,
    typename PositionRefContainer,
    typename LocalVertexRefContainer,
    typename AttributeSkinContainer,
    typename ConflictHandler,
    typename UnreferencedHandler
>
[[nodiscard]] bool MeshMeshletRefValidation::ResolveMeshletAttributeSkins(
    const MeshletContainer& meshlets,
    const PositionRefContainer& positionRefs,
    const LocalVertexRefContainer& localVertexRefs,
    const usize attributeCount,
    AttributeSkinContainer& outAttributeSkins,
    ConflictHandler onConflict,
    UnreferencedHandler onUnreferenced
)noexcept(
    IsNothrowConstructible_V<ConflictHandler, ConflictHandler&> && IsNothrowConstructible_V<UnreferencedHandler, UnreferencedHandler&>
    && MeshletRefValidationExceptionDetail::AttributeSkinsIsNothrow<MeshletContainer, LocalVertexRefContainer, AttributeSkinContainer, MeshletRefValidationExceptionDetail::PositionSkinResolver<PositionRefContainer>, ConflictHandler, UnreferencedHandler>()
){
    return ResolveMeshletAttributeSkinsFromLocalVertices(
        meshlets,
        localVertexRefs,
        attributeCount,
        outAttributeSkins,
        [&](const usize, const MeshletDesc&, const usize meshletPositionBase, const u32 localPositionIndex)noexcept(noexcept(Expected<u32>{ positionRefs[meshletPositionBase + localPositionIndex].skin }))->Expected<u32>{
            return positionRefs[meshletPositionBase + localPositionIndex].skin;
        },
        onConflict,
        onUnreferenced
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

