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
    Vector<GeometryVertex> vertices;
    Vector<u32> indices;
    bool use32BitIndices = false;
};

struct SkinnedGeometryCookEntry{
    Name virtualPath = NAME_NONE;
    Vector<SkinnedGeometryVertex> restVertices;
    Vector<u32> indices;
    u32 geometryClass = GeometryClass::Invalid;
    Vector<SkinInfluence4> skin;
    u32 skeletonJointCount = 0u;
    Vector<SkinnedGeometryJointMatrix> inverseBindMatrices;
    Vector<SourceSample> sourceSamples;
    Vector<SkinnedGeometryEditMaskFlags> editMaskPerTriangle;
    SkinnedGeometryDisplacement displacement;
    CompactString displacementTextureVirtualPathText;
    Vector<SkinnedGeometryMorph> morphs;
    bool use32BitIndices = true;
};

struct SkinnedGeometryDisplacementTextureCookEntry{
    Name virtualPath = NAME_NONE;
    u32 width = 0u;
    u32 height = 0u;
    Vector<Float4U> texels;
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
[[nodiscard]] bool ParseSkinnedGeometryDisplacementTextureCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinnedGeometryDisplacementTextureCookEntry& outEntry
);

[[nodiscard]] bool BuildGeometryAsset(GeometryCookEntry& geometryEntry, Geometry& outGeometry);
[[nodiscard]] bool BuildSkinnedGeometryAsset(SkinnedGeometryCookEntry& geometryEntry, SkinnedGeometry& outGeometry);
[[nodiscard]] bool BuildSkinnedGeometryDisplacementTextureAsset(
    SkinnedGeometryDisplacementTextureCookEntry& textureEntry,
    SkinnedGeometryDisplacementTexture& outTexture
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

