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
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_DEFAULT_COLOR_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 0, 0, 0],
    [1, 1, 1, 1, 0],
    [2, 2, 2, 2, 0],
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
struct AssetsGraphicsMeshSources{
    static constexpr AStringView s_MinimalMeshMeta =
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
    static constexpr AStringView s_DefaultColorMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    R"(asset.colors = [
    [1.0, 1.0, 1.0, 1.0],
    ];

    )"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_DEFAULT_COLOR_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
    #if defined(NWB_FINAL)
    static constexpr AStringView s_TriangleNormalField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS;
    static constexpr AStringView s_TriangleTangentField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS;
    static constexpr AStringView s_TriangleVertexRefsField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_VERTEX_REFS;
    static constexpr AStringView s_TriangleMissingNormalVertexRefsField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_NORMAL_VERTEX_REFS;
    static constexpr AStringView s_TriangleMissingTangentVertexRefsField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_TANGENT_VERTEX_REFS;
    static constexpr AStringView s_EmptyNormalListField = R"(asset.normals = [];

    )";
    static constexpr AStringView s_EmptyNormalMapField = R"(asset.normals = {};

    )";
    static constexpr AStringView s_EmptyTangentListField = R"(asset.tangents = [];

    )";
    static constexpr AStringView s_EmptyTangentMapField = R"(asset.tangents = {};

    )";
    static constexpr AStringView s_UnsupportedMeshFieldsMeta = R"(mesh asset;

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
    static constexpr AStringView s_MismatchedMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    ];

    )" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
    #endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

