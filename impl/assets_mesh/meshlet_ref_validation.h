// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "meshlet_payload_packing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshletRefValidationExceptionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename AttributeSkinContainer, typename ConflictHandler, typename UnreferencedHandler>
consteval b8 AttributeSkinsIsNothrow()noexcept{
    return
        IsNothrowDestructible_V<ConflictHandler> && IsNothrowDestructible_V<UnreferencedHandler>
        && noexcept(DeclVal<AttributeSkinContainer&>().clear())
        && noexcept(DeclVal<AttributeSkinContainer&>().resize(DeclVal<const usize&>(), s_MeshMissingStreamIndex))
        && noexcept(DeclVal<void(*)(u32&)noexcept>()(DeclVal<AttributeSkinContainer&>()[DeclVal<const usize&>()]))
        && noexcept(DeclVal<void(*)(b8)noexcept>()(DeclVal<ConflictHandler&>()(DeclVal<usize&>(), DeclVal<const usize&>(), DeclVal<u32&>(), DeclVal<const u32&>())))
        && noexcept(static_cast<b8>(DeclVal<usize&>() < DeclVal<AttributeSkinContainer&>().size()))
        && noexcept(static_cast<b8>(DeclVal<AttributeSkinContainer&>()[DeclVal<usize&>()] != s_MeshMissingStreamIndex))
        && noexcept(DeclVal<void(*)(b8)noexcept>()(DeclVal<UnreferencedHandler&>()(DeclVal<usize&>())))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshMeshletRefValidation final : NoCopy{
public:
    [[nodiscard]] static b8 MeshletPositionRefInRange(const MeshletPositionStreamRef& ref, const usize positionCount, const usize skinCount, const b8 skinRequired)noexcept{
        return ::NWB::Impl::MeshletPositionRefInRange(ref, positionCount, skinCount, skinRequired);
    }
    [[nodiscard]] static b8 MeshletAttributeRefInRange(const MeshletAttributeStreamRef& ref, const usize normalCount, const usize tangentCount, const usize uv0Count, const usize colorCount)noexcept{
        return ::NWB::Impl::MeshletAttributeRefInRange(ref, normalCount, tangentCount, uv0Count, colorCount);
    }
    template<
        typename AttributeSkinContainer,
        typename ConflictHandler,
        typename UnreferencedHandler
    >
    [[nodiscard]] static b8 ResolveMeshletAttributeSkins(
        Span<const MeshletDesc> meshlets,
        Span<const MeshletPositionStreamRef> positionRefs,
        Span<const MeshletLocalVertexRef> localVertexRefs,
        const usize attributeCount,
        ConflictHandler onConflict,
        UnreferencedHandler onUnreferenced,
        AttributeSkinContainer& outAttributeSkins
    )noexcept(MeshletRefValidationExceptionDetail::AttributeSkinsIsNothrow<AttributeSkinContainer, ConflictHandler, UnreferencedHandler>());


public:
    MeshMeshletRefValidation() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<
    typename AttributeSkinContainer,
    typename ConflictHandler,
    typename UnreferencedHandler
>
[[nodiscard]] b8 MeshMeshletRefValidation::ResolveMeshletAttributeSkins(
    const Span<const MeshletDesc> meshlets,
    const Span<const MeshletPositionStreamRef> positionRefs,
    const Span<const MeshletLocalVertexRef> localVertexRefs,
    const usize attributeCount,
    ConflictHandler onConflict,
    UnreferencedHandler onUnreferenced,
    AttributeSkinContainer& outAttributeSkins
)noexcept(MeshletRefValidationExceptionDetail::AttributeSkinsIsNothrow<AttributeSkinContainer, ConflictHandler, UnreferencedHandler>()){
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
            const u32 skinIndex = positionRefs[meshletPositionBase + localVertexRef.localDeformedPosition].skin;

            u32& attributeSkin = outAttributeSkins[attributeIndex];
            if(attributeSkin == s_MeshMissingStreamIndex){
                attributeSkin = skinIndex;
                continue;
            }
            if(attributeSkin == skinIndex)
                continue;
            return onConflict(meshletIndex, attributeIndex, attributeSkin, skinIndex);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

