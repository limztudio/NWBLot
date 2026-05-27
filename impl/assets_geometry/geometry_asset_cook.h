// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_asset.h"
#include "geometry_asset.h"

#include <core/alloc/scratch.h>
#include <core/geometry/geometry_class.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GeometryCookEntry{
    Name virtualPath = NAME_NONE;
    Core::Assets::AssetVector<Float3U> positions;
    Core::Assets::AssetVector<Half4U> normals;
    Core::Assets::AssetVector<Half4U> tangents;
    Core::Assets::AssetVector<Float2U> uv0;
    Core::Assets::AssetVector<Half4U> colors;
    Core::Assets::AssetVector<GeometryVertexRef> vertexRefs;
    Core::Assets::AssetVector<GeometryMeshletDesc> meshlets;
    Core::Assets::AssetVector<GeometryMeshletBounds> meshletBounds;
    Core::Assets::AssetVector<u32> meshletVertexRefs;
    Core::Assets::AssetVector<u8> meshletPrimitiveIndices;

    explicit GeometryCookEntry(Core::Assets::AssetArena& arena)
        : positions(arena)
        , normals(arena)
        , tangents(arena)
        , uv0(arena)
        , colors(arena)
        , vertexRefs(arena)
        , meshlets(arena)
        , meshletBounds(arena)
        , meshletVertexRefs(arena)
        , meshletPrimitiveIndices(arena)
    {}
};

struct SkinnedGeometryCookEntry{
    Name virtualPath = NAME_NONE;
    u32 geometryClass = Core::Geometry::GeometryClass::Skinned;
    Core::Assets::AssetVector<Float3U> positions;
    Core::Assets::AssetVector<Half4U> normals;
    Core::Assets::AssetVector<Half4U> tangents;
    Core::Assets::AssetVector<Float2U> uv0;
    Core::Assets::AssetVector<Half4U> colors;
    Core::Assets::AssetVector<SkinInfluence4> skin;
    u32 skeletonJointCount = 0u;
    Core::Assets::AssetVector<SkinnedGeometryJointMatrix> inverseBindMatrices;
    Core::Assets::AssetVector<GeometryVertexRef> vertexRefs;
    Core::Assets::AssetVector<GeometryMeshletDesc> meshlets;
    Core::Assets::AssetVector<GeometryMeshletBounds> meshletBounds;
    Core::Assets::AssetVector<u32> meshletVertexRefs;
    Core::Assets::AssetVector<u8> meshletPrimitiveIndices;

    explicit SkinnedGeometryCookEntry(Core::Assets::AssetArena& arena)
        : positions(arena)
        , normals(arena)
        , tangents(arena)
        , uv0(arena)
        , colors(arena)
        , skin(arena)
        , inverseBindMatrices(arena)
        , vertexRefs(arena)
        , meshlets(arena)
        , meshletBounds(arena)
        , meshletVertexRefs(arena)
        , meshletPrimitiveIndices(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseGeometryCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    GeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool ParseSkinnedGeometryCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinnedGeometryCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry);
[[nodiscard]] bool BuildSkinnedGeometryAsset(SkinnedGeometryCookEntry& geometryEntry, SkinnedGeometry& outGeometry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

