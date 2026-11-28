// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>
#include <core/alloc/scratch.h>
#include <global/containers.h>
#include <global/filesystem.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace PhysicsCookIO{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 LoadExternalMeshFileRaw(
    AStringView filePathUtf8,
    const Float4& scale,
    const Float4& rotation,
    const Float4& offset,
    AStringView submeshName,
    b8 normalizeUnitsAndAxes,
    void* user,
    void(*reserveVerts)(void*, usize),
    Float4*(*allocVerts)(void*, usize),
    void(*reserveIdx)(void*, usize),
    u32*(*allocIdx)(void*, usize),
    void(*reserveTriMats)(void*, usize),
    u16*(*allocTriMats)(void*, usize)
)noexcept;

[[nodiscard]] b8 CollectGltfExternalBufferPathsRaw(
    AStringView filePathUtf8,
    void* user,
    b8(*onPath)(void*, AStringView)
)noexcept;

[[nodiscard]] b8 LoadHeightmapImageRaw(
    AStringView filePathUtf8,
    u32 channelMode,
    f32 heightMin,
    f32 heightMax,
    u32& outCols,
    u32& outRows,
    void* user,
    f32*(*allocHeights)(void*, usize)
)noexcept;

[[nodiscard]] b8 LoadHeightmapRawBinaryRaw(
    AStringView filePathUtf8,
    u32 cols,
    u32 rows,
    u32 bitsPerSample,
    b8 bigEndian,
    b8 isFloat,
    b8 isSigned,
    f32 heightMin,
    f32 heightMax,
    void* user,
    f32*(*allocHeights)(void*, usize)
)noexcept;

[[nodiscard]] b8 LoadHeightmapHoleMaskRaw(
    AStringView filePathUtf8,
    u32 expectedCols,
    u32 expectedRows,
    f32 threshold,
    u32 channelMode,
    void* user,
    u8*(*allocHoleBytes)(void*, usize)
)noexcept;

[[nodiscard]] b8 LoadPhysicsTextFileRaw(
    AStringView filePathUtf8,
    void* user,
    char*(*allocText)(void*, usize)
)noexcept;

[[nodiscard]] b8 LoadExternalTetMeshFileRaw(
    AStringView filePathUtf8,
    void* user,
    void(*reserveVerts)(void*, usize),
    Float4*(*allocVerts)(void*, usize),
    void(*reserveIdx)(void*, usize),
    u32*(*allocIdx)(void*, usize)
)noexcept;

template<typename ArenaA, typename ArenaB>
inline void ResolvePhysicsSourcePath(
    const ::Path<ArenaA>& baseFile,
    const AStringView relativeOrAbs,
    ::Path<ArenaB>& outPath
)noexcept{
    Core::Alloc::ScratchArena scratch(Name("physics_cook/resolve_path"));
    const ::Path<Core::Alloc::ScratchArena> candidate(scratch, relativeOrAbs);
    if(candidate.isAbsolute()){
        outPath = candidate.lexicallyNormal();
        return;
    }
    ::Path<Core::Alloc::ScratchArena> combined(scratch);
    combined = baseFile.parentPath();
    if(combined.empty()){
        outPath = candidate.lexicallyNormal();
        return;
    }
    combined /= candidate;
    outPath = combined.lexicallyNormal();
}

template<typename PathArenaT, typename VertArenaT, typename IdxArenaT, typename MatArenaT>
[[nodiscard]] inline b8 LoadExternalMeshFile(
    const ::Path<PathArenaT>& filePath,
    const SIMDVectorSingle scale,
    const SIMDVectorSingle rotation,
    const SIMDVectorSingle offset,
    const AStringView submeshName,
    const b8 normalizeUnitsAndAxes,
    Vector<Float4, VertArenaT>& outVertices,
    Vector<u32, IdxArenaT>& outIndices,
    Vector<u16, MatArenaT>& outTriangleMaterialIndices
)noexcept{
    outVertices.clear();
    outIndices.clear();
    outTriangleMaterialIndices.clear();
    Float4 scaleF4;
    Float4 rotF4;
    Float4 offsetF4;
    StoreFloat(scale, scaleF4);
    StoreFloat(rotation, rotF4);
    StoreFloat(offset, offsetF4);
    struct MeshOutContext{
        Vector<Float4, VertArenaT>* verts;
        Vector<u32, IdxArenaT>* indices;
        Vector<u16, MatArenaT>* triMats;
    };
    MeshOutContext ctx{ &outVertices, &outIndices, &outTriangleMaterialIndices };
    Core::Alloc::ScratchArena scratch(Name("physics_cook/mesh_path"));
    AString<Core::Alloc::ScratchArena> pathText(scratch);
    const b8 ok = LoadExternalMeshFileRaw(
        GenericNativeTextView(filePath.native(), pathText),
        scaleF4,
        rotF4,
        offsetF4,
        submeshName,
        normalizeUnitsAndAxes,
        &ctx,
        [](void* user, const usize n){ static_cast<MeshOutContext*>(user)->verts->reserve(n); },
        [](void* user, const usize count) -> Float4*{
            if(count == 0u)
                return nullptr;
            auto* vec = static_cast<MeshOutContext*>(user)->verts;
            const usize oldSize = vec->size();
            vec->resize(oldSize + count, Float4(0.0f, 0.0f, 0.0f, 0.0f));
            return vec->data() + oldSize;
        },
        [](void* user, const usize n){ static_cast<MeshOutContext*>(user)->indices->reserve(n); },
        [](void* user, const usize count) -> u32*{
            if(count == 0u)
                return nullptr;
            auto* vec = static_cast<MeshOutContext*>(user)->indices;
            const usize oldSize = vec->size();
            vec->resize(oldSize + count, 0u);
            return vec->data() + oldSize;
        },
        [](void* user, const usize n){ static_cast<MeshOutContext*>(user)->triMats->reserve(n); },
        [](void* user, const usize count) -> u16*{
            if(count == 0u)
                return nullptr;
            auto* vec = static_cast<MeshOutContext*>(user)->triMats;
            const usize oldSize = vec->size();
            vec->resize(oldSize + count, 0u);
            return vec->data() + oldSize;
        }
    );
    if(!ok){
        outVertices.clear();
        outIndices.clear();
        outTriangleMaterialIndices.clear();
        return false;
    }
    return true;
}

template<typename PathArenaT, typename Fn>
[[nodiscard]] inline b8 CollectGltfExternalBufferPaths(
    const ::Path<PathArenaT>& filePath,
    Fn&& onPath
)noexcept{
    struct CallbackCtx{
        Fn* fn;
    };
    CallbackCtx ctx{ &onPath };
    Core::Alloc::ScratchArena scratch(Name("physics_cook/gltf_buf_path"));
    AString<Core::Alloc::ScratchArena> pathText(scratch);
    return CollectGltfExternalBufferPathsRaw(
        GenericNativeTextView(filePath.native(), pathText),
        &ctx,
        [](void* user, const AStringView resolvedPath) -> b8{
            return (*static_cast<CallbackCtx*>(user)->fn)(resolvedPath);
        }
    );
}

template<typename PathArenaT, typename HeightsArenaT>
[[nodiscard]] inline b8 LoadHeightmapImage(
    const ::Path<PathArenaT>& filePath,
    const u32 channelMode,
    const f32 heightMin,
    const f32 heightMax,
    u32& outCols,
    u32& outRows,
    Vector<f32, HeightsArenaT>& outHeights
)noexcept{
    outHeights.clear();
    outCols = 0u;
    outRows = 0u;
    Core::Alloc::ScratchArena scratch(Name("physics_cook/hf_img_path"));
    AString<Core::Alloc::ScratchArena> pathText(scratch);
    const b8 ok = LoadHeightmapImageRaw(
        GenericNativeTextView(filePath.native(), pathText),
        channelMode,
        heightMin,
        heightMax,
        outCols,
        outRows,
        &outHeights,
        [](void* user, const usize count) -> f32*{
            Vector<f32, HeightsArenaT>* vec = static_cast<Vector<f32, HeightsArenaT>*>(user);
            vec->resize(count, 0.0f);
            if(count == 0u)
                return nullptr;
            return vec->data();
        }
    );
    if(!ok){
        outHeights.clear();
        outCols = 0u;
        outRows = 0u;
        return false;
    }
    return true;
}

template<typename PathArenaT, typename HeightsArenaT>
[[nodiscard]] inline b8 LoadHeightmapRawBinary(
    const ::Path<PathArenaT>& filePath,
    const u32 cols,
    const u32 rows,
    const u32 bitsPerSample,
    const b8 bigEndian,
    const b8 isFloat,
    const b8 isSigned,
    const f32 heightMin,
    const f32 heightMax,
    Vector<f32, HeightsArenaT>& outHeights
)noexcept{
    outHeights.clear();
    Core::Alloc::ScratchArena scratch(Name("physics_cook/hf_raw_path"));
    AString<Core::Alloc::ScratchArena> pathText(scratch);
    const b8 ok = LoadHeightmapRawBinaryRaw(
        GenericNativeTextView(filePath.native(), pathText),
        cols,
        rows,
        bitsPerSample,
        bigEndian,
        isFloat,
        isSigned,
        heightMin,
        heightMax,
        &outHeights,
        [](void* user, const usize count) -> f32*{
            Vector<f32, HeightsArenaT>* vec = static_cast<Vector<f32, HeightsArenaT>*>(user);
            vec->resize(count, 0.0f);
            if(count == 0u)
                return nullptr;
            return vec->data();
        }
    );
    if(!ok){
        outHeights.clear();
        return false;
    }
    return true;
}

template<typename PathArenaT, typename HolesArenaT>
[[nodiscard]] inline b8 LoadHeightmapHoleMask(
    const ::Path<PathArenaT>& filePath,
    const u32 expectedCols,
    const u32 expectedRows,
    const f32 threshold,
    const u32 channelMode,
    Vector<u8, HolesArenaT>& outCellHoles
)noexcept{
    outCellHoles.clear();
    Core::Alloc::ScratchArena scratch(Name("physics_cook/hf_mask_path"));
    AString<Core::Alloc::ScratchArena> pathText(scratch);
    const b8 ok = LoadHeightmapHoleMaskRaw(
        GenericNativeTextView(filePath.native(), pathText),
        expectedCols,
        expectedRows,
        threshold,
        channelMode,
        &outCellHoles,
        [](void* user, const usize byteCount) -> u8*{
            Vector<u8, HolesArenaT>* vec = static_cast<Vector<u8, HolesArenaT>*>(user);
            vec->assign(byteCount, 0u);
            if(byteCount == 0u)
                return nullptr;
            return vec->data();
        }
    );
    if(!ok){
        outCellHoles.clear();
        return false;
    }
    return true;
}

template<typename PathArenaT, typename TextArenaT>
[[nodiscard]] inline b8 LoadPhysicsTextFile(
    const ::Path<PathArenaT>& filePath,
    AString<TextArenaT>& outText
)noexcept{
    outText.clear();
    Core::Alloc::ScratchArena scratch(Name("physics_cook/text_path"));
    AString<Core::Alloc::ScratchArena> pathText(scratch);
    const b8 ok = LoadPhysicsTextFileRaw(
        GenericNativeTextView(filePath.native(), pathText),
        &outText,
        [](void* user, const usize count) -> char*{
            auto* str = static_cast<AString<TextArenaT>*>(user);
            str->resize(count);
            if(count == 0u)
                return nullptr;
            return str->data();
        }
    );
    if(!ok){
        outText.clear();
        return false;
    }
    return true;
}

template<typename PathArenaT, typename VertArenaT, typename IdxArenaT>
[[nodiscard]] inline b8 LoadExternalTetMeshFile(
    const ::Path<PathArenaT>& filePath,
    Vector<Float4, VertArenaT>& outVertices,
    Vector<u32, IdxArenaT>& outTetIndices
)noexcept{
    outVertices.clear();
    outTetIndices.clear();
    struct TetOutContext{
        Vector<Float4, VertArenaT>* verts;
        Vector<u32, IdxArenaT>* indices;
    };
    TetOutContext ctx{ &outVertices, &outTetIndices };
    Core::Alloc::ScratchArena scratch(Name("physics_cook/tet_path"));
    AString<Core::Alloc::ScratchArena> pathText(scratch);
    const b8 ok = LoadExternalTetMeshFileRaw(
        GenericNativeTextView(filePath.native(), pathText),
        &ctx,
        [](void* user, const usize n){ static_cast<TetOutContext*>(user)->verts->reserve(n); },
        [](void* user, const usize count) -> Float4*{
            if(count == 0u)
                return nullptr;
            auto* vec = static_cast<TetOutContext*>(user)->verts;
            const usize oldSize = vec->size();
            vec->resize(oldSize + count, Float4(0.0f, 0.0f, 0.0f, 0.0f));
            return vec->data() + oldSize;
        },
        [](void* user, const usize n){ static_cast<TetOutContext*>(user)->indices->reserve(n); },
        [](void* user, const usize count) -> u32*{
            if(count == 0u)
                return nullptr;
            auto* vec = static_cast<TetOutContext*>(user)->indices;
            const usize oldSize = vec->size();
            vec->resize(oldSize + count, 0u);
            return vec->data() + oldSize;
        }
    );
    if(!ok){
        outVertices.clear();
        outTetIndices.clear();
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

