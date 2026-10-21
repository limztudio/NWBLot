// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_meshlet_triangles.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deduplicates a meshlet's source vertices into local deformed-position and attribute stream refs.
class MeshletZippedRefBuilder final : NoCopy{
public:
    template<typename PositionRefVectorT>
    [[nodiscard]] static Expected<u16> FindMeshletPositionRef(const PositionRefVectorT& refs, const MeshletPositionStreamRef& ref)noexcept(
        noexcept(static_cast<bool>(DeclVal<usize&>() < refs.size()))
        && noexcept(static_cast<bool>(refs[DeclVal<usize&>()].position != ref.position || refs[DeclVal<usize&>()].skin != ref.skin))
    );
    template<typename AttributeRefVectorT, typename AttributeSkinVectorT>
    [[nodiscard]] static Expected<u16> FindMeshletAttributeRef(const AttributeRefVectorT& refs, const AttributeSkinVectorT& skins, const MeshletAttributeStreamRef& ref, const u32 skin)noexcept(
        noexcept(static_cast<bool>(DeclVal<usize&>() < refs.size()))
        && noexcept(DeclVal<void(*)(const MeshletAttributeStreamRef&)noexcept>()(refs[DeclVal<usize&>()]))
        && noexcept(static_cast<bool>(
            DeclVal<const MeshletAttributeStreamRef&>().normal != ref.normal
            || DeclVal<const MeshletAttributeStreamRef&>().tangent != ref.tangent
            || DeclVal<const MeshletAttributeStreamRef&>().uv0 != ref.uv0
            || DeclVal<const MeshletAttributeStreamRef&>().color != ref.color
            || skins[DeclVal<usize&>()] != skin
        ))
    );
    template<
        typename CookEntryT,
        typename LocalVertexVectorT,
        typename PositionRefVectorT,
        typename AttributeRefVectorT,
        typename AttributeSkinVectorT,
        typename LocalVertexRefVectorT
    >
    [[nodiscard]] static bool BuildZippedMeshletRefs(
        const Path& nwbFilePath,
        const TStringView metaKind,
        const CookEntryT& entry,
        const LocalVertexVectorT& sourceVertexRefs,
        PositionRefVectorT& outPositionRefs,
        AttributeRefVectorT& outAttributeRefs,
        AttributeSkinVectorT& outAttributeSkins,
        LocalVertexRefVectorT& outLocalVertexRefs
    );


public:
    MeshletZippedRefBuilder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PositionRefVectorT>
Expected<u16> MeshletZippedRefBuilder::FindMeshletPositionRef(const PositionRefVectorT& refs, const MeshletPositionStreamRef& ref)noexcept(
    noexcept(static_cast<bool>(DeclVal<usize&>() < refs.size()))
    && noexcept(static_cast<bool>(refs[DeclVal<usize&>()].position != ref.position || refs[DeclVal<usize&>()].skin != ref.skin))
){
    for(usize localIndex = 0u; localIndex < refs.size(); ++localIndex){
        if(refs[localIndex].position != ref.position || refs[localIndex].skin != ref.skin)
            continue;

        return static_cast<u16>(localIndex);
    }

    return MakeUnexpected(Failure{});
}


template<typename AttributeRefVectorT, typename AttributeSkinVectorT>
Expected<u16> MeshletZippedRefBuilder::FindMeshletAttributeRef(const AttributeRefVectorT& refs, const AttributeSkinVectorT& skins, const MeshletAttributeStreamRef& ref, const u32 skin)noexcept(
    noexcept(static_cast<bool>(DeclVal<usize&>() < refs.size()))
    && noexcept(DeclVal<void(*)(const MeshletAttributeStreamRef&)noexcept>()(refs[DeclVal<usize&>()]))
    && noexcept(static_cast<bool>(
        DeclVal<const MeshletAttributeStreamRef&>().normal != ref.normal
        || DeclVal<const MeshletAttributeStreamRef&>().tangent != ref.tangent
        || DeclVal<const MeshletAttributeStreamRef&>().uv0 != ref.uv0
        || DeclVal<const MeshletAttributeStreamRef&>().color != ref.color
        || skins[DeclVal<usize&>()] != skin
    ))
){
    for(usize localIndex = 0u; localIndex < refs.size(); ++localIndex){
        const MeshletAttributeStreamRef& existing = refs[localIndex];
        if(existing.normal != ref.normal || existing.tangent != ref.tangent || existing.uv0 != ref.uv0 || existing.color != ref.color || skins[localIndex] != skin)
            continue;

        return static_cast<u16>(localIndex);
    }

    return MakeUnexpected(Failure{});
}


template<
    typename CookEntryT,
    typename LocalVertexVectorT,
    typename PositionRefVectorT,
    typename AttributeRefVectorT,
    typename AttributeSkinVectorT,
    typename LocalVertexRefVectorT
>
[[nodiscard]] bool MeshletZippedRefBuilder::BuildZippedMeshletRefs(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const CookEntryT& entry,
    const LocalVertexVectorT& sourceVertexRefs,
    PositionRefVectorT& outPositionRefs,
    AttributeRefVectorT& outAttributeRefs,
    AttributeSkinVectorT& outAttributeSkins,
    LocalVertexRefVectorT& outLocalVertexRefs
){
    outPositionRefs.clear();
    outAttributeRefs.clear();
    outAttributeSkins.clear();
    outLocalVertexRefs.clear();
    outPositionRefs.reserve(sourceVertexRefs.size());
    outAttributeRefs.reserve(sourceVertexRefs.size());
    outAttributeSkins.reserve(sourceVertexRefs.size());
    outLocalVertexRefs.reserve(sourceVertexRefs.size());

    for(const u32 vertexRefIndex : sourceVertexRefs){
        const MeshVertexRef& source = entry.vertexRefs[vertexRefIndex];
        const MeshletPositionStreamRef positionRef{ source.position, source.skin };
        const MeshletAttributeStreamRef attributeRef{ source.normal, source.tangent, source.uv0, source.color };

        const auto foundLocalPosition = FindMeshletPositionRef(outPositionRefs, positionRef);
        u16 localPosition = foundLocalPosition ? *foundLocalPosition : 0u;
        if(!foundLocalPosition){
            if(outPositionRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet deformed positions exceed local index limits"), metaKind, PathToString<tchar>(nwbFilePath));
                return false;
            }
            localPosition = static_cast<u16>(outPositionRefs.size());
            outPositionRefs.push_back(positionRef);
        }

        const auto foundLocalAttribute = FindMeshletAttributeRef(outAttributeRefs, outAttributeSkins, attributeRef, source.skin);
        u16 localAttribute = foundLocalAttribute ? *foundLocalAttribute : 0u;
        if(!foundLocalAttribute){
            if(outAttributeRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet attributes exceed local index limits"), metaKind, PathToString<tchar>(nwbFilePath));
                return false;
            }
            localAttribute = static_cast<u16>(outAttributeRefs.size());
            outAttributeRefs.push_back(attributeRef);
            outAttributeSkins.push_back(source.skin);
        }

        outLocalVertexRefs.push_back(MeshletLocalVertexRef{ localPosition, localAttribute });
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

