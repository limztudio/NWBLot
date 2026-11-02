// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS R"(asset.positions = [
    [-0.5, -0.5, 0.0], [ 0.5, -0.5, 0.0], [ 0.0,  0.5, 0.0],];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS R"(asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0 R"(asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS R"(asset.colors = [
    [1.0, 1.0, 1.0, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES R"(asset.indices = [
    [0, 1, 2],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 0, 0, 0],
    [1, 1, 1, 1, 1],
    [2, 2, 2, 2, 2],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_NORMAL_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 4294967295, 0, 0, 0],
    [1, 4294967295, 1, 1, 1],
    [2, 4294967295, 2, 2, 2],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_TANGENT_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 4294967295, 0, 0],
    [1, 1, 4294967295, 1, 1],
    [2, 2, 4294967295, 2, 2],
];

)"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Triangle mesh metadata and field variants shared by the mesh cook and validation tests.
namespace AssetsGraphicsFixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_MinimalMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    R"(asset.colors = [
    [1.0, 0.0, 0.0, 1.0],
    [0.0, 1.0, 0.0, 1.0],
    [0.0, 0.0, 1.0, 1.0],
    ];

    )" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
#if defined(NWB_FINAL)
inline constexpr AStringView s_EmptyNormalListField = R"(asset.normals = [];

    )";
inline constexpr AStringView s_EmptyNormalMapField = R"(asset.normals = {};

    )";
inline constexpr AStringView s_EmptyTangentListField = R"(asset.tangents = [];

    )";
inline constexpr AStringView s_EmptyTangentMapField = R"(asset.tangents = {};

    )";
inline constexpr AStringView s_UnsupportedMeshFieldsMeta = R"(mesh asset;

    asset.vertex_stride = 24;

    asset.vertex_data = [
    [-0.5, -0.5, 0.0, 1.0, 0.0, 0.0],
    [ 0.5, -0.5, 0.0, 0.0, 1.0, 0.0],
    [ 0.0,  0.5, 0.0, 0.0, 0.0, 1.0],
    ];

    asset.index_data = [
    [0, 1, 2],
    ];
    )";
inline constexpr AStringView s_MismatchedMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    ];

    )" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

