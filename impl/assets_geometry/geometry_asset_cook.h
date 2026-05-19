// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_asset.h"
#include "geometry_asset.h"

#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GeometryCookEntry{
    Name virtualPath = NAME_NONE;
    Core::Assets::AssetVector<Float3U> positions;
    Core::Assets::AssetVector<Half4U> normals;
    Core::Assets::AssetVector<Half4U> colors;
    Core::Assets::AssetVector<u32> indices;
    bool use32BitIndices = false;

    explicit GeometryCookEntry(Core::Assets::AssetArena& arena)
        : positions(arena)
        , normals(arena)
        , colors(arena)
        , indices(arena)
    {}
};

struct SkinnedGeometryCookEntry{
    Name virtualPath = NAME_NONE;
    Core::Assets::AssetVector<SkinnedGeometryVertex> restVertices;
    Core::Assets::AssetVector<u32> indices;
    u32 geometryClass = GeometryClass::Invalid;
    Core::Assets::AssetVector<SkinInfluence4> skin;
    u32 skeletonJointCount = 0u;
    Core::Assets::AssetVector<SkinnedGeometryJointMatrix> inverseBindMatrices;
    bool use32BitIndices = true;

    explicit SkinnedGeometryCookEntry(Core::Assets::AssetArena& arena)
        : restVertices(arena)
        , indices(arena)
        , skin(arena)
        , inverseBindMatrices(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseGeometryCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    GeometryCookEntry& outEntry
);
[[nodiscard]] bool ParseSkinnedGeometryCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinnedGeometryCookEntry& outEntry
);

[[nodiscard]] bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry);
[[nodiscard]] bool BuildSkinnedGeometryAsset(SkinnedGeometryCookEntry& geometryEntry, SkinnedGeometry& outGeometry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

