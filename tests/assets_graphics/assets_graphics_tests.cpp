// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/deformable_geometry_asset.h>
#include <impl/assets_graphics/geometry_asset.h>
#include <impl/assets_graphics/graphics_asset_cooker.h>

#include <tests/test_context.h>

#include <core/common/common.h>
#include <core/filesystem/filesystem.h>
#include <core/graphics/common.h>

#include <global/binary.h>
#include <global/compile.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;


#define NWB_ASSETS_GRAPHICS_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


static void* AssetsGraphicsTestAlloc(usize size){
    return NWB::Core::Alloc::CoreAlloc(size, "NWB::Tests::AssetsGraphics::Alloc");
}

static void AssetsGraphicsTestFree(void* ptr){
    NWB::Core::Alloc::CoreFree(ptr, "NWB::Tests::AssetsGraphics::Free");
}

static void* AssetsGraphicsTestAllocAligned(usize size, usize align){
    return NWB::Core::Alloc::CoreAllocAligned(size, align, "NWB::Tests::AssetsGraphics::AllocAligned");
}

static void AssetsGraphicsTestFreeAligned(void* ptr){
    NWB::Core::Alloc::CoreFreeAligned(ptr, "NWB::Tests::AssetsGraphics::FreeAligned");
}

struct TestArena{
    NWB::Core::Alloc::CustomArena arena;

    TestArena()
        : arena(
            &AssetsGraphicsTestAlloc,
            &AssetsGraphicsTestFree,
            &AssetsGraphicsTestAllocAligned,
            &AssetsGraphicsTestFreeAligned
        )
    {}
};


static constexpr AStringView s_MinimalGeometryMeta = R"(geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.colors = [
    [1.0, 0.0, 0.0, 1.0],
    [0.0, 1.0, 0.0, 1.0],
    [0.0, 0.0, 1.0, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_DefaultColorGeometryMeta = R"(geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";

#if defined(NWB_FINAL)
static constexpr AStringView s_UnsupportedGeometryFieldsMeta = R"(geometry asset;

asset.vertex_stride = 24;
asset.index_type = "u16";

asset.vertex_data = [
    [-0.5, -0.5, 0.0, 1.0, 0.0, 0.0],
    [ 0.5, -0.5, 0.0, 0.0, 1.0, 0.0],
    [ 0.0,  0.5, 0.0, 0.0, 0.0, 1.0],
];

asset.index_data = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_MismatchedGeometryMeta = R"(geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";
#endif


static constexpr AStringView s_MinimalDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.source_samples = {};
asset.skin = {};
asset.morphs = {};
)";

static constexpr AStringView s_GeneratedFrameDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.skin = {};
asset.morphs = {};
)";

static constexpr AStringView s_U32IndexTypeDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u32";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.skin = {};
asset.morphs = {};
)";

static constexpr AStringView s_EmptyListOptionalDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.colors = [];
asset.source_samples = [];
asset.skin = [];
asset.morphs = [];
)";

static constexpr AStringView s_EmptyMapOptionalDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.colors = {};
asset.source_samples = {};
asset.skin = {};
asset.morphs = {};
)";

static constexpr AStringView s_NativeCharacterMockDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.5,  0.5, 0.0],
    [-0.5,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [1.0, 1.0],
    [0.0, 1.0],
];

asset.colors = [
    [1.0, 0.0, 0.0, 1.0],
    [0.0, 1.0, 0.0, 1.0],
    [0.0, 0.0, 1.0, 1.0],
    [1.0, 1.0, 1.0, 0.5],
];

asset.indices = [
    [0, 1, 2],
    [0, 2, 3],
];

asset.edit_masks = [1, 2];

asset.source_samples = {
    "source_tri": [0, 0, 0, 1],
    "bary": [
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0],
        [0.0, 0.0, 1.0],
    ],
};

asset.skeleton_joint_count = 2;

asset.inverse_bind_matrices = [
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ],
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [-0.25, 0.0, 0.0, 1.0],
    ],
];

asset.skin = {
    "joints0": [
        [0, 0, 0, 0],
        [0, 1, 0, 0],
        [1, 0, 0, 0],
        [1, 0, 0, 0],
    ],
    "weights0": [
        [1.0, 0.0, 0.0, 0.0],
        [0.75, 0.25, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
    ],
};

asset.displacement = {
    "space": "tangent",
    "mode": "scalar",
    "field": "uv_ramp",
    "amplitude": 0.125,
};

asset.morphs = {
    "lift": {
        "vertex_ids": [1, 2],
        "delta_position": [
            [0.0, 0.0, 0.25],
            [0.0, 0.0, 0.5],
        ],
        "delta_normal": [
            [0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
        ],
        "delta_tangent": [
            [0.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 0.0],
        ],
    },
};
)";

static constexpr AStringView s_NonnormalizedSkinDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.skeleton_joint_count = 2;

asset.skin = {
    "joints0": [
        [0, 0, 0, 0],
        [0, 1, 0, 0],
        [0, 1, 0, 0],
    ],
    "weights0": [
        [2.0, 0.0, 0.0, 0.0],
        [3.0, 1.0, 0.0, 0.0],
        [0.0, 4.0, 0.0, 0.0],
    ],
};
)";

#if defined(NWB_FINAL)
static constexpr AStringView s_MismatchedDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_MissingIndexTypeDeformableMeta = R"(deformable_geometry asset;

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_MismatchedSkinDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.skin = {
    "joints0": [
        [0, 0, 0, 0],
        [0, 0, 0, 0],
        [0, 0, 0, 0],
    ],
    "weights0": [
        [1.0, 0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
    ],
};
)";

static constexpr AStringView s_MismatchedSourceSamplesDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.source_samples = {
    "source_tri": [0, 0, 0],
    "bary": [
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
    ],
};
)";

static constexpr AStringView s_UnreferencedVertexGeneratedSourceSampleDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
    [ 2.0,  2.0, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
    [1.0, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_MismatchedMorphDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.morphs = {
    "lift": {
        "vertex_ids": [1, 2],
        "delta_position": [
            [0.0, 0.0, 0.25],
        ],
        "delta_normal": [
            [0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
        ],
        "delta_tangent": [
            [0.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 0.0],
        ],
    },
};
)";

static constexpr AStringView s_MissingMorphTangentDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.morphs = {
    "lift": {
        "vertex_ids": [1, 2],
        "delta_position": [
            [0.0, 0.0, 0.25],
            [0.0, 0.0, 0.5],
        ],
        "delta_normal": [
            [0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
        ],
    },
};
)";

static constexpr AStringView s_SourceImportDeformableMeta = R"(deformable_geometry asset;

asset.source = {
    "format": "external",
    "path": "mesh.bin",
};
)";

static constexpr AStringView s_MismatchedEditMaskDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.edit_masks = [1, 1];
)";
#endif


static bool PrepareCleanDirectory(const Path& directory){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(directory, errorCode))
        return false;
    errorCode.clear();
    return EnsureDirectories(directory, errorCode);
}

static bool WriteTextFile(const Path& filePath, const AStringView text){
    ErrorCode errorCode;
    if(!EnsureDirectories(filePath.parent_path(), errorCode))
        return false;

    GlobalFilesystemDetail::OutputFileStream file(
        filePath,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!file)
        return false;

    file.write(text.data(), static_cast<GlobalFilesystemDetail::StreamSize>(text.size()));
    return static_cast<bool>(file);
}

static AString BuildTextureDisplacementDeformableMeta(
    const AStringView space,
    const AStringView mode,
    const AStringView texturePath
){
    AString meta(s_MinimalDeformableMeta.data(), s_MinimalDeformableMeta.size());
    const auto append = [&](const AStringView text){ meta.append(text.data(), text.size()); };
    append(R"(

asset.displacement = {
    "space": ")");
    append(space);
    append(R"(",
    "mode": ")");
    append(mode);
    append(R"(",
    "field": "texture",
    "texture": ")");
    append(texturePath);
    append(R"(",
    "amplitude": 0.5,
    "bias": -0.25,
    "uv_scale": [2.0, 3.0],
    "uv_offset": [0.25, 0.5],
};
)");
    return meta;
}

static const char* AssetsGraphicsTestConfigurationName(){
#if defined(NWB_DEBUG)
    return "dbg";
#elif defined(NWB_FINAL)
    return "fin";
#else
    return "opt";
#endif
}

static bool CookSingleDeformableMeta(
    const AStringView metaText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    outRoot = Path("__build_obj") / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / AString(caseName);
    outOutputDirectory = outRoot / "cooked";

    if(!PrepareCleanDirectory(outRoot))
        return false;

    const Path assetRoot = outRoot / "assets";
    const Path metaPath = assetRoot / "characters" / "minimal_deformable.nwb";
    if(!WriteTextFile(metaPath, metaText))
        return false;

    NWB::Core::Assets::AssetCookOptions options;
    options.repoRoot = ".";
    options.assetRoots.push_back(PathToString(assetRoot));
    options.outputDirectory = PathToString(outOutputDirectory);
    options.cacheDirectory = PathToString(outRoot / "cache");
    if(!options.configuration.assign("tests") || !options.assetType.assign("graphics"))
        return false;

    NWB::Impl::GraphicsAssetCooker cooker(testArena.arena);
    return cooker.cook(options);
}

static bool CookSingleGeometryMeta(
    const AStringView metaText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    outRoot = Path("__build_obj") / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / AString(caseName);
    outOutputDirectory = outRoot / "cooked";

    if(!PrepareCleanDirectory(outRoot))
        return false;

    const Path assetRoot = outRoot / "assets";
    const Path metaPath = assetRoot / "meshes" / "minimal_geometry.nwb";
    if(!WriteTextFile(metaPath, metaText))
        return false;

    NWB::Core::Assets::AssetCookOptions options;
    options.repoRoot = ".";
    options.assetRoots.push_back(PathToString(assetRoot));
    options.outputDirectory = PathToString(outOutputDirectory);
    options.cacheDirectory = PathToString(outRoot / "cache");
    if(!options.configuration.assign("tests") || !options.assetType.assign("graphics"))
        return false;

    NWB::Impl::GraphicsAssetCooker cooker(testArena.arena);
    return cooker.cook(options);
}

static bool LoadCookedMinimalDeformable(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
    const bool loadedVolume = volumeSession.load("graphics", outputDirectory);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedVolume);
    if(!loadedVolume)
        return false;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.fileCount() == 2u);

    NWB::Core::Assets::AssetBytes binary;
    const bool loadedBinary = volumeSession.loadData(Name("project/characters/minimal_deformable"), binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedBinary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());
    if(!loadedBinary || binary.empty())
        return false;

    NWB::Impl::DeformableGeometryAssetCodec codec;
    const bool deserialized = codec.deserialize(Name("project/characters/minimal_deformable"), binary, outLoadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, deserialized);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(outLoadedAsset));
    return deserialized && static_cast<bool>(outLoadedAsset);
}

static bool LoadCookedMinimalGeometry(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
    const bool loadedVolume = volumeSession.load("graphics", outputDirectory);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedVolume);
    if(!loadedVolume)
        return false;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.fileCount() == 2u);

    NWB::Core::Assets::AssetBytes binary;
    const bool loadedBinary = volumeSession.loadData(Name("project/meshes/minimal_geometry"), binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedBinary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());
    if(!loadedBinary || binary.empty())
        return false;

    NWB::Impl::GeometryAssetCodec codec;
    const bool deserialized = codec.deserialize(Name("project/meshes/minimal_geometry"), binary, outLoadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, deserialized);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(outLoadedAsset));
    return deserialized && static_cast<bool>(outLoadedAsset);
}

static bool CookAndLoadMinimalDeformable(
    TestContext& context,
    TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    Path outputDirectory;
    const bool cooked = CookSingleDeformableMeta(
        metaText,
        caseName,
        testArena,
        outRoot,
        outputDirectory
    );

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked){
        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
        return false;
    }

    if(LoadCookedMinimalDeformable(context, testArena, outputDirectory, outLoadedAsset))
        return true;

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
    return false;
}

static bool CookAndLoadMinimalGeometry(
    TestContext& context,
    TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    Path outputDirectory;
    const bool cooked = CookSingleGeometryMeta(
        metaText,
        caseName,
        testArena,
        outRoot,
        outputDirectory
    );

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked){
        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
        return false;
    }

    if(LoadCookedMinimalGeometry(context, testArena, outputDirectory, outLoadedAsset))
        return true;

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
    return false;
}

static NWB::Impl::DeformableVertexRest MakeRestVertex(const f32 x, const f32 y, const f32 u, const f32 v){
    NWB::Impl::DeformableVertexRest vertex;
    vertex.position = Float3U(x, y, 0.f);
    vertex.normal = Float3U(0.f, 0.f, 1.f);
    vertex.tangent = Float4U(1.f, 0.f, 0.f, 1.f);
    vertex.uv0 = Float2U(u, v);
    vertex.color0 = Float4U(1.f, 1.f, 1.f, 1.f);
    return vertex;
}

static NWB::Impl::SkinInfluence4 MakeRootSkin(){
    NWB::Impl::SkinInfluence4 skin;
    skin.joint[0] = 0u;
    skin.weight[0] = 1.f;
    return skin;
}

static NWB::Impl::DeformableJointMatrix MakeJointMatrix(const f32 tx, const f32 ty, const f32 tz){
    NWB::Impl::DeformableJointMatrix matrix = NWB::Impl::MakeIdentityDeformableJointMatrix();
    matrix.rows[3] = Float4(tx, ty, tz, 1.0f);
    return matrix;
}

static NWB::Impl::SourceSample MakeSourceSample(const u32 sourceTri, const f32 a, const f32 b, const f32 c){
    NWB::Impl::SourceSample sample;
    sample.sourceTri = sourceTri;
    sample.bary[0] = a;
    sample.bary[1] = b;
    sample.bary[2] = c;
    return sample;
}

static NWB::Impl::DeformableMorphDelta MakeMorphDelta(const u32 vertexId, const f32 zDelta){
    NWB::Impl::DeformableMorphDelta delta;
    delta.vertexId = vertexId;
    delta.deltaPosition = Float3U(0.f, 0.f, zDelta);
    delta.deltaNormal = Float3U(0.f, 0.f, 0.f);
    delta.deltaTangent = Float4U(0.f, 0.f, 0.f, 0.f);
    return delta;
}

#if defined(NWB_FINAL)
static bool OverwriteU32(NWB::Core::Assets::AssetBytes& binary, const usize offset, const u32 value){
    if(offset > binary.size() || sizeof(value) > binary.size() - offset)
        return false;

    NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
    return true;
}

static bool OverwriteU64(NWB::Core::Assets::AssetBytes& binary, const usize offset, const u64 value){
    if(offset > binary.size() || sizeof(value) > binary.size() - offset)
        return false;

    NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
    return true;
}

static usize DeformableHeaderCountOffset(const usize countIndex){
    return (sizeof(u32) * 2u) + (sizeof(u64) * countIndex);
}

static usize DeformableMorphDeltaCountOffset(const NWB::Impl::DeformableGeometry& geometry){
    return (sizeof(u32) * 2u)
        + (sizeof(u64) * 9u)
        + (geometry.restVertices().size() * sizeof(NWB::Impl::DeformableVertexRest))
        + (geometry.indices().size() * sizeof(u32))
        + (geometry.skin().size() * sizeof(NWB::Impl::SkinInfluence4))
        + (geometry.inverseBindMatrices().size() * sizeof(NWB::Impl::DeformableJointMatrix))
        + (geometry.sourceSamples().size() * sizeof(NWB::Impl::SourceSample))
        + (geometry.editMaskPerTriangle().size() * sizeof(NWB::Impl::DeformableEditMaskFlags))
        + (sizeof(u32) * 2u)
    ;
}
#endif

static NWB::Impl::DeformableGeometry BuildValidDeformableGeometry(){
    NWB::Impl::DeformableGeometry geometry(Name("tests/characters/proxy_deformable"));

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, 0.5f, 1.f, 1.f));
    vertices.push_back(MakeRestVertex(-0.5f, 0.5f, 0.f, 1.f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(0u);
    indices.push_back(2u);
    indices.push_back(3u);

    Vector<NWB::Impl::SkinInfluence4> skin;
    skin.resize(vertices.size());
    for(NWB::Impl::SkinInfluence4& influence : skin)
        influence = MakeRootSkin();

    Vector<NWB::Impl::DeformableJointMatrix> inverseBindMatrices;
    inverseBindMatrices.push_back(MakeJointMatrix(-0.25f, 0.0f, 0.0f));

    Vector<NWB::Impl::SourceSample> sourceSamples;
    sourceSamples.push_back(MakeSourceSample(0u, 1.f, 0.f, 0.f));
    sourceSamples.push_back(MakeSourceSample(0u, 0.f, 1.f, 0.f));
    sourceSamples.push_back(MakeSourceSample(0u, 0.f, 0.f, 1.f));
    sourceSamples.push_back(MakeSourceSample(1u, 0.f, 0.f, 1.f));

    Vector<NWB::Impl::DeformableEditMaskFlags> editMasks;
    editMasks.push_back(NWB::Impl::DeformableEditMaskFlag::Editable);
    editMasks.push_back(NWB::Impl::DeformableEditMaskFlag::Restricted);

    Vector<NWB::Impl::DeformableMorph> morphs;
    morphs.resize(1u);
    morphs[0].name = Name("lift");
    morphs[0].nameText = CompactString("lift");
    morphs[0].deltas.push_back(MakeMorphDelta(1u, 0.25f));
    morphs[0].deltas.push_back(MakeMorphDelta(2u, 0.5f));

    NWB::Impl::DeformableDisplacement displacement;
    displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarUvRamp;
    displacement.amplitude = 0.125f;

    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    geometry.setSkin(Move(skin));
    geometry.setSkeletonJointCount(1u);
    geometry.setInverseBindMatrices(Move(inverseBindMatrices));
    geometry.setSourceSamples(Move(sourceSamples));
    geometry.setEditMaskPerTriangle(Move(editMasks));
    geometry.setDisplacement(displacement);
    geometry.setMorphs(Move(morphs));
    return geometry;
}

static NWB::Impl::DeformableDisplacementTexture BuildValidDisplacementTexture(){
    NWB::Impl::DeformableDisplacementTexture texture(Name("tests/textures/displacement_height"));
    texture.setSize(2u, 2u);

    Vector<Float4U> texels;
    texels.push_back(Float4U(0.0f, 0.0f, 0.0f, 0.0f));
    texels.push_back(Float4U(0.25f, 0.0f, 0.0f, 0.0f));
    texels.push_back(Float4U(0.5f, 0.0f, 0.0f, 0.0f));
    texels.push_back(Float4U(1.0f, 0.0f, 0.0f, 0.0f));
    texture.setTexels(Move(texels));
    return texture;
}

static NWB::Impl::DeformableGeometry BuildMinimalDeformableGeometry(){
    NWB::Impl::DeformableGeometry geometry(Name("tests/characters/minimal_deformable"));

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.f, 0.5f, 0.5f, 1.f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);

    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    return geometry;
}

static NWB::Impl::Geometry BuildMinimalGeometry(){
    NWB::Impl::Geometry geometry(Name("tests/meshes/minimal_geometry"));

    Vector<NWB::Impl::GeometryVertex> vertices;
    {
        NWB::Impl::GeometryVertex vertex;
        vertex.position = Float3U(-0.5f, -0.5f, 0.f);
        vertex.normal = Float3U(0.f, 0.f, 1.f);
        vertex.color0 = Float4U(1.f, 0.f, 0.f, 1.f);
        vertices.push_back(vertex);
    }
    {
        NWB::Impl::GeometryVertex vertex;
        vertex.position = Float3U(0.5f, -0.5f, 0.f);
        vertex.normal = Float3U(0.f, 0.f, 1.f);
        vertex.color0 = Float4U(0.f, 1.f, 0.f, 1.f);
        vertices.push_back(vertex);
    }
    {
        NWB::Impl::GeometryVertex vertex;
        vertex.position = Float3U(0.f, 0.5f, 0.f);
        vertex.normal = Float3U(0.f, 0.f, 1.f);
        vertex.color0 = Float4U(0.f, 0.f, 1.f, 1.f);
        vertices.push_back(vertex);
    }

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);

    geometry.setVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    return geometry;
}

static void TestGeometryCodecRoundTrip(TestContext& context){
    NWB::Impl::Geometry geometry = BuildMinimalGeometry();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

    NWB::Impl::GeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::Geometry::AssetTypeName());

    const NWB::Impl::Geometry& loadedGeometry =
        static_cast<const NWB::Impl::Geometry&>(*loadedAsset)
    ;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[1].position.x == 0.5f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[1].normal.z == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[1].color0.y == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices()[2] == 2u);
}

static void TestGeometryCodecRejectsOldBinaryVersion(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::Geometry geometry = BuildMinimalGeometry();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

    NWB::Impl::GeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU32(binary, sizeof(u32), 1u));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("unsupported version 1")));
#else
    static_cast<void>(context);
#endif
}

static void TestDeformableDisplacementTextureCodecRoundTrip(TestContext& context){
    NWB::Impl::DeformableDisplacementTexture texture = BuildValidDisplacementTexture();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, texture.validatePayload());

    NWB::Impl::DeformableDisplacementTextureAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(texture, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(texture.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        loadedAsset->assetType() == NWB::Impl::DeformableDisplacementTexture::AssetTypeName()
    );

    const NWB::Impl::DeformableDisplacementTexture& loadedTexture =
        static_cast<const NWB::Impl::DeformableDisplacementTexture&>(*loadedAsset)
    ;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedTexture.width() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedTexture.height() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedTexture.texels().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedTexture.texels()[3].x == 1.0f);
}

static void TestDeformableGeometryCodecRoundTrip(TestContext& context){
    NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::DeformableGeometry::AssetTypeName());

    const NWB::Impl::DeformableGeometry& loadedGeometry =
        static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
    ;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.virtualPath() == geometry.virtualPath());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 6u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.inverseBindMatrices().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.inverseBindMatrices()[0u].rows[3].x == -0.25f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.editMaskPerTriangle().size() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        loadedGeometry.editMaskPerTriangle()[1] == NWB::Impl::DeformableEditMaskFlag::Restricted
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        loadedGeometry.displacement().mode == NWB::Impl::DeformableDisplacementMode::ScalarUvRamp
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.displacement().amplitude == 0.125f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].name == Name("lift"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].nameText.view() == AStringView("lift"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas.size() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].vertexId == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].deltaPosition.z == 0.5f);
}

static void TestDeformableGeometryCodecRoundTripsTextureDisplacement(TestContext& context){
    auto checkRoundTrip = [&](const u32 mode, const AStringView texturePathText){
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        NWB::Impl::DeformableDisplacement displacement;
        displacement.mode = mode;
        displacement.texture.virtualPath = Name(texturePathText);
        displacement.amplitude = 0.5f;
        displacement.bias = -0.25f;
        displacement.uvScale = Float2U(2.0f, 3.0f);
        displacement.uvOffset = Float2U(0.25f, 0.5f);
        geometry.setDisplacement(displacement);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.setDisplacementTextureVirtualPathText(texturePathText));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

        NWB::Impl::DeformableGeometryAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));

        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        const NWB::Impl::DeformableDisplacement& loadedDisplacement = loadedGeometry.displacement();
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedDisplacement.mode == mode);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedDisplacement.texture.name() == Name(texturePathText));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.displacementTextureVirtualPathText().view() == texturePathText);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedDisplacement.amplitude == 0.5f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedDisplacement.bias == -0.25f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedDisplacement.uvScale.x == 2.0f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedDisplacement.uvOffset.y == 0.5f);
    };

    checkRoundTrip(NWB::Impl::DeformableDisplacementMode::ScalarTexture, "tests/textures/displacement_height");
    checkRoundTrip(NWB::Impl::DeformableDisplacementMode::VectorTangentTexture, "tests/textures/displacement_tangent");
    checkRoundTrip(NWB::Impl::DeformableDisplacementMode::VectorObjectTexture, "tests/textures/displacement_object");
}

static void TestMinimalDeformableGeometryCodecRoundTrip(TestContext& context){
    NWB::Impl::DeformableGeometry geometry = BuildMinimalDeformableGeometry();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));

    const NWB::Impl::DeformableGeometry& loadedGeometry =
        static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
    ;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().empty());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().empty());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        loadedGeometry.displacement().mode == NWB::Impl::DeformableDisplacementMode::None
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().empty());
}

static void TestDeformableGeometryCodecRejectsOldBinaryVersion(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::DeformableGeometry geometry = BuildMinimalDeformableGeometry();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU32(binary, sizeof(u32), 1u));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("unsupported version 1")));
#else
    static_cast<void>(context);
#endif
}

static void TestDeformableGeometryCodecRejectsMalformedCounts(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::DeformableGeometry geometry = BuildMinimalDeformableGeometry();
    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    const usize morphCountOffset = DeformableHeaderCountOffset(7u);
    const u64 invalidMorphCount = static_cast<u64>(Limit<u32>::s_Max) + 1ull;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(binary, morphCountOffset, invalidMorphCount));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("payload counts exceed u32 limits")));
#else
    static_cast<void>(context);
#endif
}

static void TestDeformableGeometryCodecRejectsMalformedDependentCounts(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            DeformableHeaderCountOffset(2u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), malformed, loadedAsset));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    }

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            DeformableHeaderCountOffset(5u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), malformed, loadedAsset));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    }

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            DeformableMorphDeltaCountOffset(geometry),
            static_cast<u64>(Limit<u32>::s_Max) + 1ull
        ));

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), malformed, loadedAsset));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin count must be empty or match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("source sample count must be empty or match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("morph delta count exceeds u32 limits")));
#else
    static_cast<void>(context);
#endif
}

static void TestGeometryCookerTypedStreams(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalGeometry(
        context,
        testArena,
        s_MinimalGeometryMeta,
        "minimal_geometry",
        root,
        loadedAsset
    ))
        return;

    {
        const NWB::Impl::Geometry& loadedGeometry =
            static_cast<const NWB::Impl::Geometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[0].position.x == -0.5f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[0].normal.z == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[2].color0.z == 1.f);
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestGeometryCookerDefaultColors(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalGeometry(
        context,
        testArena,
        s_DefaultColorGeometryMeta,
        "default_color_geometry",
        root,
        loadedAsset
    ))
        return;

    {
        const NWB::Impl::Geometry& loadedGeometry =
            static_cast<const NWB::Impl::Geometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[0].color0.x == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertices()[0].color0.w == 1.f);
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestGeometryCookerValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto expectCookFailure = [&](const AStringView metaText, const AStringView caseName){
        Path root;
        Path outputDirectory;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookSingleGeometryMeta(
            metaText,
            caseName,
            testArena,
            root,
            outputDirectory
        ));

        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(root, errorCode));
    };

    expectCookFailure(s_UnsupportedGeometryFieldsMeta, "unsupported_geometry_fields");
    expectCookFailure(s_MismatchedGeometryMeta, "mismatched_geometry_streams");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() >= 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("unsupported geometry fields are present")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("vertex stream counts must match")));
#else
    static_cast<void>(context);
#endif
}

static void TestDeformableGeometryCookerMinimalAsset(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalDeformable(
        context,
        testArena,
        s_MinimalDeformableMeta,
        "minimal",
        root,
        loadedAsset
    ))
        return;

    {
        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.x == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.w == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().empty());
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().empty());
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().empty());
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerGeneratesMissingFrames(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalDeformable(
        context,
        testArena,
        s_GeneratedFrameDeformableMeta,
        "generated_frames",
        root,
        loadedAsset
    ))
        return;

    {
        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
        for(const NWB::Impl::DeformableVertexRest& vertex : loadedGeometry.restVertices()){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, vertex.normal.x == 0.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, vertex.normal.y == 0.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, vertex.normal.z == 1.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, vertex.tangent.x == 1.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, vertex.tangent.y == 0.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, vertex.tangent.z == 0.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, vertex.tangent.w == 1.0f);
        }
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[2u].bary[2u] == 1.0f);
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerU32IndexType(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalDeformable(
        context,
        testArena,
        s_U32IndexTypeDeformableMeta,
        "u32_index_type",
        root,
        loadedAsset
    ))
        return;

    {
        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices()[2] == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[0].sourceTri == 0u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[0].bary[0] == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[1].bary[1] == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[2].bary[2] == 1.f);
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerExplicitEmptyOptionalLists(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto expectCookedDefaultOptionals = [&](const AStringView metaText, const AStringView caseName){
        Path root;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(!CookAndLoadMinimalDeformable(
            context,
            testArena,
            metaText,
            caseName,
            root,
            loadedAsset
        ))
            return;

        {
            const NWB::Impl::DeformableGeometry& loadedGeometry =
                static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
            ;
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.x == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.w == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().empty());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().empty());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().empty());
        }

        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(root, errorCode));
    };

    expectCookedDefaultOptionals(s_EmptyListOptionalDeformableMeta, "empty_optional_lists");
    expectCookedDefaultOptionals(s_EmptyMapOptionalDeformableMeta, "empty_optional_maps");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerNativeCharacterMock(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalDeformable(
        context,
        testArena,
        s_NativeCharacterMockDeformableMeta,
        "native_character_mock",
        root,
        loadedAsset
    ))
        return;

    {
        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[3].color0.w == 0.5f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.inverseBindMatrices().size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.inverseBindMatrices()[1u].rows[3].x == -0.25f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1].joint[1] == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1].weight[0] == 0.75f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[3].sourceTri == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[3].bary[2] == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.editMaskPerTriangle().size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            loadedGeometry.editMaskPerTriangle()[1] == NWB::Impl::DeformableEditMaskFlag::Restricted
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            loadedGeometry.displacement().mode == NWB::Impl::DeformableDisplacementMode::ScalarUvRamp
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.displacement().amplitude == 0.125f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().size() == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].name == Name("lift"));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas.size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].vertexId == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].deltaPosition.z == 0.5f);
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerNormalizesSkinWeights(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalDeformable(
        context,
        testArena,
        s_NonnormalizedSkinDeformableMeta,
        "nonnormalized_skin",
        root,
        loadedAsset
    ))
        return;

    {
        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[0u].weight[0] == 1.0f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1u].weight[0] == 0.75f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1u].weight[1] == 0.25f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[2u].weight[0] == 0.0f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[2u].weight[1] == 1.0f);
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerTextureDisplacementModes(TestContext& context){
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto checkCookedDisplacement = [&](
        const AStringView caseName,
        const AStringView space,
        const AStringView mode,
        const AStringView texturePath,
        const u32 expectedMode
    ){
        const AString meta = BuildTextureDisplacementDeformableMeta(space, mode, texturePath);

        Path root;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(!CookAndLoadMinimalDeformable(
            context,
            testArena,
            AStringView(meta.data(), meta.size()),
            caseName,
            root,
            loadedAsset
        ))
            return;

        {
            const NWB::Impl::DeformableGeometry& loadedGeometry =
                static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
            ;
            const NWB::Impl::DeformableDisplacement& displacement = loadedGeometry.displacement();
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.mode == expectedMode);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.texture.name() == Name(texturePath));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.displacementTextureVirtualPathText().view() == texturePath);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.amplitude == 0.5f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.bias == -0.25f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.uvScale.x == 2.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.uvScale.y == 3.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.uvOffset.x == 0.25f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, displacement.uvOffset.y == 0.5f);
        }

        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(root, errorCode));
    };

    checkCookedDisplacement(
        "cooked_scalar_texture_displacement",
        "tangent",
        "scalar",
        "tests/textures/cooked_scalar_height",
        NWB::Impl::DeformableDisplacementMode::ScalarTexture
    );
    checkCookedDisplacement(
        "cooked_vector_tangent_displacement",
        "tangent",
        "vector",
        "tests/textures/cooked_vector_tangent",
        NWB::Impl::DeformableDisplacementMode::VectorTangentTexture
    );
    checkCookedDisplacement(
        "cooked_vector_object_displacement",
        "object",
        "vector",
        "tests/textures/cooked_vector_object",
        NWB::Impl::DeformableDisplacementMode::VectorObjectTexture
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto expectCookFailure = [&](const AStringView metaText, const AStringView caseName){
        Path root;
        Path outputDirectory;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookSingleDeformableMeta(
            metaText,
            caseName,
            testArena,
            root,
            outputDirectory
        ));

        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(root, errorCode));
    };

    expectCookFailure(s_MismatchedDeformableMeta, "mismatched_streams");
    expectCookFailure(s_MissingIndexTypeDeformableMeta, "missing_index_type");
    expectCookFailure(s_MismatchedSkinDeformableMeta, "mismatched_skin");
    expectCookFailure(s_MismatchedSourceSamplesDeformableMeta, "mismatched_source_samples");
    expectCookFailure(s_UnreferencedVertexGeneratedSourceSampleDeformableMeta, "unreferenced_source_sample_generation");
    expectCookFailure(s_MismatchedMorphDeformableMeta, "mismatched_morph");
    expectCookFailure(s_MissingMorphTangentDeformableMeta, "missing_morph_tangent");
    expectCookFailure(s_SourceImportDeformableMeta, "source_import");
    expectCookFailure(s_MismatchedEditMaskDeformableMeta, "mismatched_edit_mask");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() >= 9u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("rest vertex stream counts must match")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'index_type' must be 'u16' or 'u32'")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin streams must match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("source samples must match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("cannot generate source sample for unreferenced vertex")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        logger.sawErrorContaining(NWB_TEXT("morph 'lift' stream counts must match and must not be empty"))
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("morph 'lift' requires 'delta_tangent' list")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("offline converter to emit native .nwb streams")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("edit mask count must match triangle count")));
#else
    static_cast<void>(context);
#endif
}

static void TestDeformableGeometryValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].normal = Float3U(0.f, 0.f, 0.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4U(1.f, 0.f, 0.f, 0.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4U(0.f, 0.f, 1.f, 1.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4U(1.f, 0.f, 0.f, 2.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].normal = Float3U(0.f, 0.f, 2.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4U(0.70710677f, 0.f, 0.70710677f, 1.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin[0].weight[0] = 0.5f;
        geometry.setSkin(Move(skin));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin.pop_back();
        geometry.setSkin(Move(skin));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        geometry.setSkeletonJointCount(0u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableJointMatrix> inverseBindMatrices = geometry.inverseBindMatrices();
        inverseBindMatrices.push_back(MakeJointMatrix(0.0f, 0.0f, 0.0f));
        geometry.setInverseBindMatrices(Move(inverseBindMatrices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableJointMatrix> inverseBindMatrices = geometry.inverseBindMatrices();
        inverseBindMatrices[0u].rows[3].w = 0.0f;
        geometry.setInverseBindMatrices(Move(inverseBindMatrices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin[0].joint[0] = 1u;
        geometry.setSkin(Move(skin));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<u32> indices = geometry.indices();
        indices.pop_back();
        geometry.setIndices(Move(indices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<u32> indices = geometry.indices();
        indices[2] = 99u;
        geometry.setIndices(Move(indices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<u32> indices = geometry.indices();
        indices[2] = indices[1];
        geometry.setIndices(Move(indices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[2].position = Float3U(0.0f, -0.5f, 0.0f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SourceSample> sourceSamples = geometry.sourceSamples();
        sourceSamples[0].sourceTri = 99u;
        geometry.setSourceSamples(Move(sourceSamples));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SourceSample> sourceSamples = geometry.sourceSamples();
        sourceSamples[0] = MakeSourceSample(0u, 0.25f, 0.25f, 0.25f);
        geometry.setSourceSamples(Move(sourceSamples));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SourceSample> sourceSamples = geometry.sourceSamples();
        sourceSamples.pop_back();
        geometry.setSourceSamples(Move(sourceSamples));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableEditMaskFlags> editMasks = geometry.editMaskPerTriangle();
        editMasks[0] = static_cast<NWB::Impl::DeformableEditMaskFlags>(
            NWB::Impl::DeformableEditMaskFlag::Editable | NWB::Impl::DeformableEditMaskFlag::Forbidden
        );
        geometry.setEditMaskPerTriangle(Move(editMasks));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        NWB::Impl::DeformableDisplacement displacement = geometry.displacement();
        displacement.mode = 99u;
        geometry.setDisplacement(displacement);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        NWB::Impl::DeformableDisplacement displacement;
        displacement.amplitude = 0.25f;
        geometry.setDisplacement(displacement);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableMorph> morphs = geometry.morphs();
        NWB::Impl::DeformableMorph duplicateMorph;
        duplicateMorph.name = morphs[0].name;
        duplicateMorph.deltas.push_back(MakeMorphDelta(3u, 0.125f));
        morphs.push_back(Move(duplicateMorph));
        geometry.setMorphs(Move(morphs));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableMorph> morphs = geometry.morphs();
        morphs[0].deltas[1].vertexId = morphs[0].deltas[0].vertexId;
        geometry.setMorphs(Move(morphs));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableMorph> morphs = geometry.morphs();
        morphs[0].deltas[0].vertexId = 99u;
        geometry.setMorphs(Move(morphs));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 25u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("degenerate normal/tangent frame")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("invalid normal/tangent frame")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("triangle 0 is degenerate")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("triangle 0 has zero area")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("contains duplicate morph")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("edit mask 0 is invalid")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("no skeleton joint count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrices must be empty or match a valid skeleton")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("exceeds skeleton joint count")));
#else
    static_cast<void>(context);
#endif
}

static void TestFormatBlockDimensions(TestContext& context){
    const NWB::Core::FormatInfo& rgba8 = NWB::Core::GetFormatInfo(NWB::Core::Format::RGBA8_UNORM);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(rgba8) == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(rgba8) == 1u);

    const NWB::Core::FormatInfo& bc1 = NWB::Core::GetFormatInfo(NWB::Core::Format::BC1_UNORM);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(bc1) == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(bc1) == 4u);

    const NWB::Core::FormatInfo& astc8x5 = NWB::Core::GetFormatInfo(NWB::Core::Format::ASTC_8x5_UNORM);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(astc8x5) == 8u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(astc8x5) == 5u);

    const NWB::Core::FormatInfo& astc12x10 = NWB::Core::GetFormatInfo(NWB::Core::Format::ASTC_12x10_FLOAT);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(astc12x10) == 12u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(astc12x10) == 10u);
}


#undef NWB_ASSETS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "assets graphics tests failed: common initialization failed\n";
        return -1;
    }

    __hidden_assets_graphics_tests::TestContext context;
    __hidden_assets_graphics_tests::TestDeformableDisplacementTextureCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestGeometryCodecRejectsOldBinaryVersion(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRoundTripsTextureDisplacement(context);
    __hidden_assets_graphics_tests::TestMinimalDeformableGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRejectsOldBinaryVersion(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRejectsMalformedCounts(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRejectsMalformedDependentCounts(context);
    __hidden_assets_graphics_tests::TestGeometryCookerTypedStreams(context);
    __hidden_assets_graphics_tests::TestGeometryCookerDefaultColors(context);
    __hidden_assets_graphics_tests::TestGeometryCookerValidationFailures(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerMinimalAsset(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerGeneratesMissingFrames(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerU32IndexType(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerExplicitEmptyOptionalLists(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerNativeCharacterMock(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerNormalizesSkinWeights(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerTextureDisplacementModes(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerValidationFailures(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryValidationFailures(context);
    __hidden_assets_graphics_tests::TestFormatBlockDimensions(context);

    if(context.failed != 0){
        NWB_CERR
            << "assets graphics tests failed: "
            << context.failed
            << " of "
            << (context.passed + context.failed)
            << '\n'
        ;
        return -1;
    }

    NWB_COUT << "assets graphics tests passed: " << context.passed << '\n';
    return 0;
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

