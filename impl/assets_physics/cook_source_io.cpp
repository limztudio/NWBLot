// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_source_io.h"

#include <global/filesystem/operations_file_io.h>
#include <core/common/log.h>

#pragma push_macro("interface")
#undef interface
#include <ufbx.h>
#include <basisu_comp.h>
#include <nlohmann/json.hpp>
#include <miniz.h>
#pragma pop_macro("interface")

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace PhysicsCookIO{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_physics_cook_io{


[[nodiscard]] AString<Core::Alloc::ScratchArena> LowercaseExtension(const AStringView filePathUtf8, Core::Alloc::ScratchArena& scratch)noexcept{
    const ::Path<Core::Alloc::ScratchArena> path(scratch, filePathUtf8);
    AString<Core::Alloc::ScratchArena> text(scratch);
    const AStringView extView = GenericNativeTextView(path.extensionView(), text);
    AString<Core::Alloc::ScratchArena> lower(scratch);
    lower.reserve(extView.size());
    for(const char ch : extView){
        if(ch >= 'A' && ch <= 'Z')
            lower.push_back(static_cast<char>(ch - 'A' + 'a'));
        else
            lower.push_back(ch);
    }
    return lower;
}

[[nodiscard]] b8 ReadBytesUtf8(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    Vector<u8, Core::Alloc::ScratchArena>& outBytes
)noexcept{
    outBytes.clear();
    const ::Path<Core::Alloc::ScratchArena> path(scratch, filePathUtf8);
    return ReadBinaryFile(path, outBytes).has_value();
}

[[nodiscard]] u16 ReadLe16(const u8* p)noexcept{
    return static_cast<u16>(p[0]) | (static_cast<u16>(p[1]) << 8u);
}

[[nodiscard]] u16 ReadBe16(const u8* p)noexcept{
    return (static_cast<u16>(p[0]) << 8u) | static_cast<u16>(p[1]);
}

[[nodiscard]] u32 ReadLe32(const u8* p)noexcept{
    return static_cast<u32>(p[0])
        | (static_cast<u32>(p[1]) << 8u)
        | (static_cast<u32>(p[2]) << 16u)
        | (static_cast<u32>(p[3]) << 24u);
}

[[nodiscard]] u32 ReadBe32(const u8* p)noexcept{
    return (static_cast<u32>(p[0]) << 24u)
        | (static_cast<u32>(p[1]) << 16u)
        | (static_cast<u32>(p[2]) << 8u)
        | static_cast<u32>(p[3]);
}

[[nodiscard]] f32 ReadFloat32(const u8* p, const b8 bigEndian)noexcept{
    u32 bits = 0u;
    if(bigEndian)
        bits = ReadBe32(p);
    else
        bits = ReadLe32(p);
    f32 out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

[[nodiscard]] b8 IsAsciiSpace(const char c)noexcept{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

[[nodiscard]] b8 ParseAsciiFloatToken(const char*& cur, const char* end, f32& outVal)noexcept{
    while(cur < end && IsAsciiSpace(*cur))
        ++cur;
    if(cur >= end)
        return false;
    const char* tokStart = cur;
    while(cur < end && !IsAsciiSpace(*cur))
        ++cur;
    const char* tokEnd = cur;
    const usize len = static_cast<usize>(tokEnd - tokStart);
    if(len == 0u || len >= 128u)
        return false;
    const char* parseStart = tokStart;
    if(*parseStart == '+'){
        if(len == 1u || parseStart[1] == '-' || parseStart[1] == '+')
            return false;
        ++parseStart;
    }
    f32 v = 0.0f;
    const std::from_chars_result res = std::from_chars(parseStart, tokEnd, v);
    if(res.ec == std::errc::result_out_of_range && res.ptr == tokEnd){
        char buf[128];
        std::memcpy(buf, tokStart, len);
        buf[len] = '\0';
        char* parseEnd = nullptr;
        v = std::strtof(buf, &parseEnd);
        if(parseEnd != buf + len || !std::isfinite(v))
            return false;
        outVal = v;
        return true;
    }
    if(res.ec != std::errc{} || res.ptr != tokEnd || !std::isfinite(v))
        return false;
    outVal = v;
    return true;
}

[[nodiscard]] b8 ParseAsciiI64Slice(const char* tokStart, const char* tokEnd, i64& outVal)noexcept{
    if(tokStart >= tokEnd)
        return false;
    const usize len = static_cast<usize>(tokEnd - tokStart);
    if(len >= 64u)
        return false;
    if(*tokStart == '+'){
        if(len == 1u || tokStart[1] == '-' || tokStart[1] == '+')
            return false;
        ++tokStart;
    }
    i64 parsed = 0;
    const std::from_chars_result res = std::from_chars(tokStart, tokEnd, parsed, 10);
    if(res.ec != std::errc{} || res.ptr != tokEnd)
        return false;
    outVal = parsed;
    return true;
}

[[nodiscard]] b8 LoadObjMesh(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    void* user,
    void(*reserveVerts)(void*, usize),
    Float4*(*allocVerts)(void*, usize),
    void(*reserveIdx)(void*, usize),
    u32*(*allocIdx)(void*, usize)
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> bytes(scratch);
    if(!ReadBytesUtf8(filePathUtf8, scratch, bytes) || bytes.empty())
        return false;

    const char* data = reinterpret_cast<const char*>(bytes.data());
    const char* end = data + bytes.size();
    Vector<Float4, Core::Alloc::ScratchArena> objVerts(scratch);
    Vector<u32, Core::Alloc::ScratchArena> objIndices(scratch);
    objVerts.reserve(bytes.size() / 48u + 16u);
    objIndices.reserve(bytes.size() / 24u + 16u);
    Vector<u32, Core::Alloc::ScratchArena> faceVerts(scratch);

    const char* cur = data;
    while(cur < end){
        const char* lineEnd = cur;
        while(lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            ++lineEnd;

        const char* p = cur;
        while(p < lineEnd && IsAsciiSpace(*p))
            ++p;

        if(p + 2 <= lineEnd && p[0] == 'v' && IsAsciiSpace(p[1])){
            p += 2;
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            if(!ParseAsciiFloatToken(p, lineEnd, x) || !ParseAsciiFloatToken(p, lineEnd, y) || !ParseAsciiFloatToken(p, lineEnd, z))
                return false;
            if(objVerts.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            objVerts.push_back(Float4(x, y, z, 0.0f));
        }
        else if(p + 2 <= lineEnd && p[0] == 'f' && IsAsciiSpace(p[1])){
            p += 2;
            faceVerts.clear();
            while(p < lineEnd){
                while(p < lineEnd && IsAsciiSpace(*p))
                    ++p;
                if(p >= lineEnd || *p == '#')
                    break;
                const char* tokEnd = p;
                while(tokEnd < lineEnd && !IsAsciiSpace(*tokEnd))
                    ++tokEnd;
                const char* slash = p;
                while(slash < tokEnd && *slash != '/')
                    ++slash;
                i64 rawIdx = 0;
                if(!ParseAsciiI64Slice(p, slash, rawIdx) || rawIdx == 0)
                    return false;
                i64 resolved = 0;
                if(rawIdx > 0)
                    resolved = rawIdx - 1;
                else
                    resolved = static_cast<i64>(objVerts.size()) + rawIdx;
                if(resolved < 0 || resolved >= static_cast<i64>(objVerts.size()))
                    return false;
                faceVerts.push_back(static_cast<u32>(resolved));
                p = tokEnd;
            }
            if(faceVerts.size() < 3u)
                return false;
            for(usize k = 1u; k + 1u < faceVerts.size(); ++k){
                objIndices.push_back(faceVerts[0u]);
                objIndices.push_back(faceVerts[k]);
                objIndices.push_back(faceVerts[k + 1u]);
            }
        }

        cur = lineEnd;
        while(cur < end && (*cur == '\n' || *cur == '\r'))
            ++cur;
    }

    if(objVerts.empty() || objIndices.size() < 3u)
        return false;

    reserveVerts(user, objVerts.size());
    Float4* dstVerts = allocVerts(user, objVerts.size());
    if(!dstVerts)
        return false;
    std::memcpy(dstVerts, objVerts.data(), objVerts.size() * sizeof(Float4));

    reserveIdx(user, objIndices.size());
    u32* dstIndices = allocIdx(user, objIndices.size());
    if(!dstIndices)
        return false;
    std::memcpy(dstIndices, objIndices.data(), objIndices.size() * sizeof(u32));
    return true;
}

[[nodiscard]] b8 LoadFbxMesh(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    void* user,
    void(*reserveVerts)(void*, usize),
    Float4*(*allocVerts)(void*, usize),
    void(*reserveIdx)(void*, usize),
    u32*(*allocIdx)(void*, usize)
)noexcept{
    ufbx_load_opts opts = {};
    ufbx_error ufbxErr = {};
    ufbx_scene* scene = ufbx_load_file_len(filePathUtf8.data(), filePathUtf8.size(), &opts, &ufbxErr);
    if(!scene)
        return false;

    usize totalVerts = 0u;
    usize totalIndices = 0u;
    Vector<u32, Core::Alloc::ScratchArena> triBuf(scratch);
    Vector<u32, Core::Alloc::ScratchArena> meshIndices(scratch);
    b8 ok = true;

    usize estVerts = 0u;
    usize estTris = 0u;
    for(usize ni = 0u; ni < scene->nodes.count; ++ni){
        const ufbx_node* node = scene->nodes.data[ni];
        if(node && node->mesh && node->mesh->vertex_position.exists){
            estVerts += node->mesh->num_vertices;
            estTris += node->mesh->num_triangles;
        }
    }
    reserveVerts(user, estVerts);
    reserveIdx(user, estTris * 3u);

    for(usize ni = 0u; ni < scene->nodes.count && ok; ++ni){
        const ufbx_node* node = scene->nodes.data[ni];
        if(!node || !node->mesh)
            continue;
        const ufbx_mesh* mesh = node->mesh;
        if(!mesh->vertex_position.exists || mesh->num_vertices == 0u || mesh->num_faces == 0u)
            continue;
        if(mesh->num_vertices > static_cast<usize>(Limit<u32>::s_Max) - totalVerts){
            ok = false;
            break;
        }
        const u32 baseVertex = static_cast<u32>(totalVerts);
        Float4* dstVerts = allocVerts(user, mesh->num_vertices);
        if(!dstVerts){
            ok = false;
            break;
        }
        for(usize vi = 0u; vi < mesh->num_vertices; ++vi){
            const ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, mesh->vertices.data[vi]);
            const f32 x = static_cast<f32>(p.x);
            const f32 y = static_cast<f32>(p.y);
            const f32 z = static_cast<f32>(p.z);
            if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)){
                ok = false;
                break;
            }
            dstVerts[vi] = Float4(x, y, z, 0.0f);
        }
        totalVerts += mesh->num_vertices;
        if(!ok)
            break;

        const usize needTriBuf = mesh->max_face_triangles * 3u;
        if(triBuf.size() < needTriBuf)
            triBuf.resize(needTriBuf);
        meshIndices.clear();
        meshIndices.reserve(mesh->num_triangles * 3u);

        for(usize fi = 0u; fi < mesh->num_faces; ++fi){
            if(mesh->face_hole.count > fi && mesh->face_hole.data[fi])
                continue;
            const ufbx_face face = mesh->faces.data[fi];
            if(face.num_indices < 3u)
                continue;
            const u32 triCount = ufbx_triangulate_face(triBuf.data(), triBuf.size(), mesh, face);
            const usize cornerTotal = static_cast<usize>(triCount) * 3u;
            for(usize ci = 0u; ci < cornerTotal; ++ci){
                const u32 cornerIdx = triBuf[ci];
                if(cornerIdx >= mesh->vertex_indices.count){
                    ok = false;
                    break;
                }
                const u32 logicalVert = mesh->vertex_indices.data[cornerIdx];
                if(logicalVert >= mesh->num_vertices){
                    ok = false;
                    break;
                }
                meshIndices.push_back(baseVertex + logicalVert);
            }
            if(!ok)
                break;
        }
        if(ok && !meshIndices.empty()){
            u32* dstIdx = allocIdx(user, meshIndices.size());
            if(!dstIdx){
                ok = false;
                break;
            }
            std::memcpy(dstIdx, meshIndices.data(), meshIndices.size() * sizeof(u32));
            totalIndices += meshIndices.size();
        }
    }

    ufbx_free_scene(scene);
    return ok && totalVerts > 0u && totalIndices >= 3u;
}

[[nodiscard]] b8 DecodeBase64(const AStringView encoded, Vector<u8, Core::Alloc::ScratchArena>& outBytes)noexcept{
    outBytes.clear();
    outBytes.reserve((encoded.size() / 4u) * 3u + 3u);
    u32 accum = 0u;
    i32 bits = -8;
    for(const char ch : encoded){
        if(IsAsciiSpace(ch))
            continue;
        if(ch == '=')
            break;
        i32 val = -1;
        if(ch >= 'A' && ch <= 'Z')
            val = static_cast<i32>(ch - 'A');
        else if(ch >= 'a' && ch <= 'z')
            val = static_cast<i32>(ch - 'a') + 26;
        else if(ch >= '0' && ch <= '9')
            val = static_cast<i32>(ch - '0') + 52;
        else if(ch == '+')
            val = 62;
        else if(ch == '/')
            val = 63;
        else
            return false;
        accum = (accum << 6u) | static_cast<u32>(val);
        bits += 6;
        if(bits >= 0){
            outBytes.push_back(static_cast<u8>((accum >> bits) & 0xffu));
            bits -= 8;
        }
    }
    return true;
}

[[nodiscard]] b8 ReadJsonF32(const nlohmann::json& val, f32& outVal)noexcept{
    if(!val.is_number())
        return false;
    const f64 num = val.get<f64>();
    if(!std::isfinite(num))
        return false;
    outVal = static_cast<f32>(num);
    return true;
}

[[nodiscard]] b8 ReadGltfNodeLocalMatrix(const nlohmann::json& nodeJson, SIMDMatrix& outMatrix)noexcept{
    if(nodeJson.contains("matrix")){
        const nlohmann::json& matJson = nodeJson["matrix"];
        if(!matJson.is_array() || matJson.size() != 16u)
            return false;
        f32 m[16] = {};
        for(usize i = 0u; i < 16u; ++i){
            if(!ReadJsonF32(matJson[i], m[i]))
                return false;
        }
        outMatrix.v[0] = VectorSet(m[0], m[4], m[8], m[12]);
        outMatrix.v[1] = VectorSet(m[1], m[5], m[9], m[13]);
        outMatrix.v[2] = VectorSet(m[2], m[6], m[10], m[14]);
        outMatrix.v[3] = VectorSet(m[3], m[7], m[11], m[15]);
        return true;
    }

    f32 tx = 0.0f;
    f32 ty = 0.0f;
    f32 tz = 0.0f;
    if(nodeJson.contains("translation")){
        const nlohmann::json& t = nodeJson["translation"];
        if(!t.is_array() || t.size() != 3u || !ReadJsonF32(t[0], tx) || !ReadJsonF32(t[1], ty) || !ReadJsonF32(t[2], tz))
            return false;
    }

    f32 qx = 0.0f;
    f32 qy = 0.0f;
    f32 qz = 0.0f;
    f32 qw = 1.0f;
    if(nodeJson.contains("rotation")){
        const nlohmann::json& r = nodeJson["rotation"];
        if(!r.is_array() || r.size() != 4u || !ReadJsonF32(r[0], qx) || !ReadJsonF32(r[1], qy) || !ReadJsonF32(r[2], qz) || !ReadJsonF32(r[3], qw))
            return false;
    }

    f32 sx = 1.0f;
    f32 sy = 1.0f;
    f32 sz = 1.0f;
    if(nodeJson.contains("scale")){
        const nlohmann::json& s = nodeJson["scale"];
        if(!s.is_array() || s.size() != 3u || !ReadJsonF32(s[0], sx) || !ReadJsonF32(s[1], sy) || !ReadJsonF32(s[2], sz))
            return false;
    }

    const SIMDMatrix tMat = MatrixTranslation(tx, ty, tz);
    const SIMDMatrix rMat = MatrixRotationQuaternion(VectorSet(qx, qy, qz, qw));
    const SIMDMatrix sMat = MatrixScaling(sx, sy, sz);
    outMatrix = MatrixMultiply(MatrixMultiply(tMat, rMat), sMat);
    return true;
}

[[nodiscard]] b8 ResolveGltfAccessorSpan(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize accessorIndex,
    const usize defaultStride,
    Span<const u8>& outBuf,
    usize& outByteOffset,
    usize& outStride,
    usize& outCount,
    u32& outComponentType,
    AStringView expectedType
)noexcept{
    if(!doc.contains("accessors") || !doc["accessors"].is_array() || accessorIndex >= doc["accessors"].size())
        return false;
    if(!doc.contains("bufferViews") || !doc["bufferViews"].is_array())
        return false;
    const nlohmann::json& acc = doc["accessors"][accessorIndex];
    if(!acc.is_object() || acc.contains("sparse"))
        return false;
    if(!acc.contains("bufferView") || !acc["bufferView"].is_number_unsigned())
        return false;
    if(!acc.contains("componentType") || !acc["componentType"].is_number_unsigned())
        return false;
    if(!acc.contains("count") || !acc["count"].is_number_unsigned())
        return false;
    if(!acc.contains("type") || !acc["type"].is_string())
        return false;
    const std::string& typeStr = acc["type"].get_ref<const std::string&>();
    if(AStringView(typeStr.data(), typeStr.size()) != expectedType)
        return false;

    const usize viewIndex = acc["bufferView"].get<usize>();
    if(viewIndex >= doc["bufferViews"].size())
        return false;
    const nlohmann::json& view = doc["bufferViews"][viewIndex];
    if(!view.is_object() || !view.contains("buffer") || !view["buffer"].is_number_unsigned() || !view.contains("byteLength") || !view["byteLength"].is_number_unsigned())
        return false;
    const usize bufferIndex = view["buffer"].get<usize>();
    if(bufferIndex >= bufferSpans.size())
        return false;

    const usize viewOffset = view.value("byteOffset", usize(0u));
    const usize viewLength = view["byteLength"].get<usize>();
    const usize accOffset = acc.value("byteOffset", usize(0u));
    const usize stride = view.value("byteStride", defaultStride);
    const usize count = acc["count"].get<usize>();
    if(stride < defaultStride)
        return false;

    const Span<const u8> buf = bufferSpans[bufferIndex];
    if(viewOffset > buf.size() || viewLength > buf.size() - viewOffset)
        return false;
    if(accOffset > viewLength)
        return false;
    if(count > 0u){
        if(defaultStride > viewLength - accOffset)
            return false;
        if((count - 1u) > (viewLength - accOffset - defaultStride) / stride)
            return false;
    }

    outBuf = buf;
    outByteOffset = viewOffset + accOffset;
    outStride = stride;
    outCount = count;
    outComponentType = acc["componentType"].get<u32>();
    return true;
}

void CountGltfMeshGeometry(
    const nlohmann::json& doc,
    const usize meshIndex,
    usize& inOutVerts,
    usize& inOutIndices
)noexcept{
    if(!doc.contains("meshes") || !doc["meshes"].is_array() || meshIndex >= doc["meshes"].size())
        return;
    if(!doc.contains("accessors") || !doc["accessors"].is_array())
        return;
    const nlohmann::json& accessors = doc["accessors"];
    const nlohmann::json& mesh = doc["meshes"][meshIndex];
    if(!mesh.is_object() || !mesh.contains("primitives") || !mesh["primitives"].is_array())
        return;
    for(const nlohmann::json& prim : mesh["primitives"]){
        if(!prim.is_object())
            continue;
        if(prim.contains("mode") && (!prim["mode"].is_number_integer() || prim["mode"].get<i64>() != 4))
            continue;
        if(!prim.contains("attributes") || !prim["attributes"].is_object())
            continue;
        const nlohmann::json& attrs = prim["attributes"];
        if(!attrs.contains("POSITION") || !attrs["POSITION"].is_number_unsigned())
            continue;
        const usize posAccIndex = attrs["POSITION"].get<usize>();
        if(posAccIndex >= accessors.size() || !accessors[posAccIndex].is_object() || !accessors[posAccIndex].contains("count") || !accessors[posAccIndex]["count"].is_number_unsigned())
            continue;
        const usize posCount = accessors[posAccIndex]["count"].get<usize>();
        inOutVerts += posCount;
        if(prim.contains("indices") && prim["indices"].is_number_unsigned()){
            const usize idxAccIndex = prim["indices"].get<usize>();
            if(idxAccIndex < accessors.size() && accessors[idxAccIndex].is_object() && accessors[idxAccIndex].contains("count") && accessors[idxAccIndex]["count"].is_number_unsigned())
                inOutIndices += accessors[idxAccIndex]["count"].get<usize>();
        }
        else{
            inOutIndices += posCount;
        }
    }
}

void CountGltfNodeGeometry(
    const nlohmann::json& doc,
    const usize nodeIndex,
    Vector<u8, Core::Alloc::ScratchArena>& visited,
    usize& inOutVerts,
    usize& inOutIndices
)noexcept{
    if(nodeIndex >= visited.size() || visited[nodeIndex] != 0u)
        return;
    visited[nodeIndex] = 1u;
    const nlohmann::json& nodeJson = doc["nodes"][nodeIndex];
    if(!nodeJson.is_object())
        return;
    if(nodeJson.contains("mesh") && nodeJson["mesh"].is_number_unsigned())
        CountGltfMeshGeometry(doc, nodeJson["mesh"].get<usize>(), inOutVerts, inOutIndices);
    if(nodeJson.contains("children") && nodeJson["children"].is_array()){
        for(const nlohmann::json& childVal : nodeJson["children"]){
            if(childVal.is_number_unsigned())
                CountGltfNodeGeometry(doc, childVal.get<usize>(), visited, inOutVerts, inOutIndices);
        }
    }
}

[[nodiscard]] b8 AppendGltfMesh(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize meshIndex,
    const SIMDMatrix& worldMatrix,
    void* user,
    Float4*(*allocVerts)(void*, usize),
    u32*(*allocIdx)(void*, usize),
    usize& inOutVertCount,
    usize& inOutIdxCount
)noexcept{
    if(!doc.contains("meshes") || !doc["meshes"].is_array() || meshIndex >= doc["meshes"].size())
        return false;
    const nlohmann::json& mesh = doc["meshes"][meshIndex];
    if(!mesh.is_object() || !mesh.contains("primitives") || !mesh["primitives"].is_array())
        return false;

    for(const nlohmann::json& prim : mesh["primitives"]){
        if(!prim.is_object())
            return false;
        if(prim.contains("mode")){
            if(!prim["mode"].is_number_integer())
                return false;
            if(prim["mode"].get<i64>() != 4)
                continue;
        }
        if(!prim.contains("attributes") || !prim["attributes"].is_object())
            return false;
        const nlohmann::json& attrs = prim["attributes"];
        if(!attrs.contains("POSITION") || !attrs["POSITION"].is_number_unsigned())
            continue;

        const usize posAccIndex = attrs["POSITION"].get<usize>();
        Span<const u8> posBuf;
        usize posOffset = 0u;
        usize posStride = 0u;
        usize posCount = 0u;
        u32 posComp = 0u;
        if(!ResolveGltfAccessorSpan(doc, bufferSpans, posAccIndex, 12u, posBuf, posOffset, posStride, posCount, posComp, "VEC3"))
            return false;
        if(posComp != 5126u || posCount == 0u)
            return false;
        if(posCount > static_cast<usize>(Limit<u32>::s_Max) - inOutVertCount)
            return false;

        const u32 baseVertex = static_cast<u32>(inOutVertCount);
        Float4* dstVerts = allocVerts(user, posCount);
        if(!dstVerts)
            return false;
        for(usize vi = 0u; vi < posCount; ++vi){
            const u8* vp = posBuf.data() + posOffset + vi * posStride;
            const f32 lx = ReadFloat32(vp + 0u, false);
            const f32 ly = ReadFloat32(vp + 4u, false);
            const f32 lz = ReadFloat32(vp + 8u, false);
            if(!std::isfinite(lx) || !std::isfinite(ly) || !std::isfinite(lz))
                return false;
            const SIMDVectorSingle worldPt = Vector3Transform(VectorSet(lx, ly, lz, 1.0f), worldMatrix);
            const f32 wx = VectorGetX(worldPt);
            const f32 wy = VectorGetY(worldPt);
            const f32 wz = VectorGetZ(worldPt);
            if(!std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz))
                return false;
            dstVerts[vi] = Float4(wx, wy, wz, 0.0f);
        }
        inOutVertCount += posCount;

        if(prim.contains("indices")){
            if(!prim["indices"].is_number_unsigned())
                return false;
            const usize idxAccIndex = prim["indices"].get<usize>();
            if(!doc.contains("accessors") || !doc["accessors"].is_array() || idxAccIndex >= doc["accessors"].size())
                return false;
            const nlohmann::json& idxAcc = doc["accessors"][idxAccIndex];
            if(!idxAcc.contains("componentType") || !idxAcc["componentType"].is_number_unsigned())
                return false;
            const u32 compType = idxAcc["componentType"].get<u32>();
            usize elemSize = 0u;
            if(compType == 5121u)
                elemSize = 1u;
            else if(compType == 5123u)
                elemSize = 2u;
            else if(compType == 5125u)
                elemSize = 4u;
            else
                return false;

            Span<const u8> idxBuf;
            usize idxOffset = 0u;
            usize idxStride = 0u;
            usize idxCount = 0u;
            u32 verifiedComp = 0u;
            if(!ResolveGltfAccessorSpan(doc, bufferSpans, idxAccIndex, elemSize, idxBuf, idxOffset, idxStride, idxCount, verifiedComp, "SCALAR"))
                return false;
            if((idxCount % 3u) != 0u)
                return false;
            if(idxCount > 0u){
                u32* dstIdx = allocIdx(user, idxCount);
                if(!dstIdx)
                    return false;
                const u8* ip = idxBuf.data() + idxOffset;
                if(compType == 5121u){
                    for(usize ii = 0u; ii < idxCount; ++ii, ip += idxStride){
                        const u32 vIdx = static_cast<u32>(ip[0]);
                        if(static_cast<usize>(vIdx) >= posCount)
                            return false;
                        dstIdx[ii] = baseVertex + vIdx;
                    }
                }
                else if(compType == 5123u){
                    for(usize ii = 0u; ii < idxCount; ++ii, ip += idxStride){
                        const u32 vIdx = static_cast<u32>(ReadLe16(ip));
                        if(static_cast<usize>(vIdx) >= posCount)
                            return false;
                        dstIdx[ii] = baseVertex + vIdx;
                    }
                }
                else{
                    for(usize ii = 0u; ii < idxCount; ++ii, ip += idxStride){
                        const u32 vIdx = ReadLe32(ip);
                        if(static_cast<usize>(vIdx) >= posCount)
                            return false;
                        dstIdx[ii] = baseVertex + vIdx;
                    }
                }
                inOutIdxCount += idxCount;
            }
        }
        else{
            if((posCount % 3u) != 0u)
                return false;
            u32* dstIdx = allocIdx(user, posCount);
            if(!dstIdx)
                return false;
            for(usize ii = 0u; ii < posCount; ++ii)
                dstIdx[ii] = baseVertex + static_cast<u32>(ii);
            inOutIdxCount += posCount;
        }
    }
    return true;
}

[[nodiscard]] b8 TraverseGltfNode(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize nodeIndex,
    const SIMDMatrix& parentWorld,
    Vector<u8, Core::Alloc::ScratchArena>& visited,
    void* user,
    Float4*(*allocVerts)(void*, usize),
    u32*(*allocIdx)(void*, usize),
    usize& inOutVertCount,
    usize& inOutIdxCount
)noexcept{
    if(nodeIndex >= visited.size() || visited[nodeIndex] != 0u)
        return false;
    visited[nodeIndex] = 1u;

    const nlohmann::json& nodeJson = doc["nodes"][nodeIndex];
    if(!nodeJson.is_object())
        return false;
    SIMDMatrix localMat = MatrixIdentity();
    if(!ReadGltfNodeLocalMatrix(nodeJson, localMat))
        return false;
    const SIMDMatrix worldMat = MatrixMultiply(parentWorld, localMat);

    if(nodeJson.contains("mesh")){
        if(!nodeJson["mesh"].is_number_unsigned())
            return false;
        if(!AppendGltfMesh(doc, bufferSpans, nodeJson["mesh"].get<usize>(), worldMat, user, allocVerts, allocIdx, inOutVertCount, inOutIdxCount))
            return false;
    }

    if(nodeJson.contains("children")){
        if(!nodeJson["children"].is_array())
            return false;
        for(const nlohmann::json& childVal : nodeJson["children"]){
            if(!childVal.is_number_unsigned())
                return false;
            if(!TraverseGltfNode(doc, bufferSpans, childVal.get<usize>(), worldMat, visited, user, allocVerts, allocIdx, inOutVertCount, inOutIdxCount))
                return false;
        }
    }

    visited[nodeIndex] = 2u;
    return true;
}

[[nodiscard]] b8 LoadGltfOrGlbMesh(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    void* user,
    void(*reserveVerts)(void*, usize),
    Float4*(*allocVerts)(void*, usize),
    void(*reserveIdx)(void*, usize),
    u32*(*allocIdx)(void*, usize)
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> fileBytes(scratch);
    if(!ReadBytesUtf8(filePathUtf8, scratch, fileBytes) || fileBytes.size() < 4u)
        return false;

    const u8* jsonStart = fileBytes.data();
    const u8* jsonEnd = fileBytes.data() + fileBytes.size();
    Span<const u8> glbBinSpan;
    b8 hasGlbBin = false;

    if(fileBytes.size() >= 12u && ReadLe32(fileBytes.data()) == 0x46546C67u){
        const u32 version = ReadLe32(fileBytes.data() + 4u);
        const u32 totalLength = ReadLe32(fileBytes.data() + 8u);
        if(version != 2u || totalLength < 20u || totalLength > fileBytes.size())
            return false;

        usize cursor = 12u;
        b8 foundJson = false;
        while(cursor + 8u <= totalLength){
            const u32 chunkLen = ReadLe32(fileBytes.data() + cursor);
            const u32 chunkType = ReadLe32(fileBytes.data() + cursor + 4u);
            cursor += 8u;
            if(chunkLen > totalLength - cursor)
                return false;
            if(chunkType == 0x4E4F534Au && !foundJson){
                jsonStart = fileBytes.data() + cursor;
                jsonEnd = jsonStart + chunkLen;
                foundJson = true;
            }
            else if(chunkType == 0x004E4942u && !hasGlbBin){
                glbBinSpan = Span<const u8>(fileBytes.data() + cursor, chunkLen);
                hasGlbBin = true;
            }
            cursor += chunkLen;
        }
        if(!foundJson)
            return false;
    }

    const nlohmann::json doc = nlohmann::json::parse(jsonStart, jsonEnd, nullptr, false);
    if(doc.is_discarded() || !doc.is_object())
        return false;
    if(!doc.contains("meshes") || !doc["meshes"].is_array() || doc["meshes"].empty())
        return false;

    Vector<Vector<u8, Core::Alloc::ScratchArena>, Core::Alloc::ScratchArena> ownedBuffers(scratch);
    Vector<Span<const u8>, Core::Alloc::ScratchArena> bufferSpans(scratch);
    const ::Path<Core::Alloc::ScratchArena> gltfPath(scratch, filePathUtf8);

    if(doc.contains("buffers")){
        if(!doc["buffers"].is_array())
            return false;
        const nlohmann::json& buffers = doc["buffers"];
        ownedBuffers.reserve(buffers.size());
        bufferSpans.reserve(buffers.size());
        for(usize bi = 0u; bi < buffers.size(); ++bi){
            const nlohmann::json& bJson = buffers[bi];
            if(!bJson.is_object())
                return false;
            if(bi == 0u && hasGlbBin && !bJson.contains("uri")){
                bufferSpans.push_back(glbBinSpan);
                continue;
            }
            if(!bJson.contains("uri") || !bJson["uri"].is_string())
                return false;
            const std::string& uri = bJson["uri"].get_ref<const std::string&>();
            const AStringView uriView(uri.data(), uri.size());
            ownedBuffers.emplace_back(scratch);
            Vector<u8, Core::Alloc::ScratchArena>& storage = ownedBuffers.back();
            if(uriView.size() >= 5u && uriView.substr(0u, 5u) == "data:"){
                const usize b64Tag = uriView.find(";base64,");
                if(b64Tag == AStringView::npos)
                    return false;
                if(!DecodeBase64(uriView.substr(b64Tag + 8u), storage))
                    return false;
            }
            else{
                ::Path<Core::Alloc::ScratchArena> resolvedBuf(scratch);
                ResolvePhysicsSourcePath(gltfPath, uriView, resolvedBuf);
                if(!ReadBinaryFile(resolvedBuf, storage).has_value())
                    return false;
            }
            bufferSpans.push_back(Span<const u8>(storage.data(), storage.size()));
        }
    }

    usize vertCount = 0u;
    usize idxCount = 0u;

    if(doc.contains("nodes") && doc["nodes"].is_array() && !doc["nodes"].empty()){
        const nlohmann::json& nodes = doc["nodes"];
        Vector<u8, Core::Alloc::ScratchArena> visited(nodes.size(), 0u, scratch);
        Vector<u32, Core::Alloc::ScratchArena> rootNodes(scratch);
        b8 traversedFromScene = false;
        if(doc.contains("scenes") && doc["scenes"].is_array() && !doc["scenes"].empty()){
            const usize sceneIdx = doc.value("scene", usize(0u));
            if(sceneIdx >= doc["scenes"].size())
                return false;
            const nlohmann::json& sc = doc["scenes"][sceneIdx];
            if(sc.is_object() && sc.contains("nodes") && sc["nodes"].is_array()){
                traversedFromScene = true;
                for(const nlohmann::json& rootVal : sc["nodes"]){
                    if(!rootVal.is_number_unsigned())
                        return false;
                    rootNodes.push_back(rootVal.get<u32>());
                }
            }
        }
        if(!traversedFromScene){
            Vector<u8, Core::Alloc::ScratchArena> hasParent(nodes.size(), 0u, scratch);
            for(const nlohmann::json& n : nodes){
                if(n.is_object() && n.contains("children") && n["children"].is_array()){
                    for(const nlohmann::json& c : n["children"]){
                        if(c.is_number_unsigned() && c.get<usize>() < hasParent.size())
                            hasParent[c.get<usize>()] = 1u;
                    }
                }
            }
            for(usize ni = 0u; ni < nodes.size(); ++ni){
                if(hasParent[ni] == 0u)
                    rootNodes.push_back(static_cast<u32>(ni));
            }
        }

        usize estVerts = 0u;
        usize estIndices = 0u;
        Vector<u8, Core::Alloc::ScratchArena> countVisited(nodes.size(), 0u, scratch);
        for(const u32 rootIdx : rootNodes)
            CountGltfNodeGeometry(doc, rootIdx, countVisited, estVerts, estIndices);
        reserveVerts(user, estVerts);
        reserveIdx(user, estIndices);

        for(const u32 rootIdx : rootNodes){
            if(!TraverseGltfNode(doc, bufferSpans, rootIdx, MatrixIdentity(), visited, user, allocVerts, allocIdx, vertCount, idxCount))
                return false;
        }
    }
    else{
        usize estVerts = 0u;
        usize estIndices = 0u;
        for(usize mi = 0u; mi < doc["meshes"].size(); ++mi)
            CountGltfMeshGeometry(doc, mi, estVerts, estIndices);
        reserveVerts(user, estVerts);
        reserveIdx(user, estIndices);

        for(usize mi = 0u; mi < doc["meshes"].size(); ++mi){
            if(!AppendGltfMesh(doc, bufferSpans, mi, MatrixIdentity(), user, allocVerts, allocIdx, vertCount, idxCount))
                return false;
        }
    }

    return vertCount > 0u && idxCount >= 3u;
}

[[nodiscard]] u8 PaethPredictor(const u8 aByte, const u8 bByte, const u8 cByte)noexcept{
    const i32 a = static_cast<i32>(aByte);
    const i32 b = static_cast<i32>(bByte);
    const i32 c = static_cast<i32>(cByte);
    const i32 p = a + b - c;
    i32 pa = p - a;
    if(pa < 0)
        pa = -pa;
    i32 pb = p - b;
    if(pb < 0)
        pb = -pb;
    i32 pc = p - c;
    if(pc < 0)
        pc = -pc;
    if(pa <= pb && pa <= pc)
        return aByte;
    if(pb <= pc)
        return bByte;
    return cByte;
}

enum class Png16SniffResult : u8{
    OpenFailed,
    NotPng16,
    IsPng16,
};

[[nodiscard]] Png16SniffResult SniffPng16Header(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch
)noexcept{
    const ::Path<Core::Alloc::ScratchArena> path(scratch, filePathUtf8);
    InputFileStream stream(path, InputFileStream::binary);
    if(!stream.is_open())
        return Png16SniffResult::OpenFailed;
    u8 hdr[33] = {};
    stream.read(reinterpret_cast<char*>(hdr), 33);
    if(stream.gcount() != 33)
        return Png16SniffResult::NotPng16;
    static constexpr u8 s_PngSig[8] = { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    if(std::memcmp(hdr, s_PngSig, 8u) != 0)
        return Png16SniffResult::NotPng16;
    if(ReadBe32(hdr + 8u) != 13u || std::memcmp(hdr + 12u, "IHDR", 4u) != 0)
        return Png16SniffResult::NotPng16;
    if(hdr[24u] != 16u)
        return Png16SniffResult::NotPng16;
    return Png16SniffResult::IsPng16;
}

[[nodiscard]] b8 UnfilterPng16Scanline(
    const u8 filterType,
    const u8* inRow,
    const u8* prevRow,
    u8* currRow,
    const usize rowBytes,
    const usize step
)noexcept{
    if(filterType == 0u){
        std::memcpy(currRow, inRow, rowBytes);
        return true;
    }
    if(filterType == 1u){
        for(usize xByte = 0u; xByte < rowBytes; ++xByte){
            u8 left = 0u;
            if(xByte >= step)
                left = currRow[xByte - step];
            currRow[xByte] = static_cast<u8>(inRow[xByte] + left);
        }
        return true;
    }
    if(filterType == 2u){
        for(usize xByte = 0u; xByte < rowBytes; ++xByte)
            currRow[xByte] = static_cast<u8>(inRow[xByte] + prevRow[xByte]);
        return true;
    }
    if(filterType == 3u){
        for(usize xByte = 0u; xByte < rowBytes; ++xByte){
            u8 left = 0u;
            if(xByte >= step)
                left = currRow[xByte - step];
            const u8 up = prevRow[xByte];
            const u32 avg = (static_cast<u32>(left) + static_cast<u32>(up)) >> 1u;
            currRow[xByte] = static_cast<u8>(inRow[xByte] + avg);
        }
        return true;
    }
    if(filterType == 4u){
        for(usize xByte = 0u; xByte < rowBytes; ++xByte){
            u8 left = 0u;
            u8 upLeft = 0u;
            if(xByte >= step){
                left = currRow[xByte - step];
                upLeft = prevRow[xByte - step];
            }
            const u8 up = prevRow[xByte];
            currRow[xByte] = static_cast<u8>(inRow[xByte] + PaethPredictor(left, up, upLeft));
        }
        return true;
    }
    return false;
}

struct ParsedPng16{
    u32 width = 0u;
    u32 height = 0u;
    u32 channels = 0u;
    void* decompressed = nullptr;

    ~ParsedPng16(){
        if(decompressed)
            mz_free(decompressed);
    }
};

[[nodiscard]] b8 ParseAndDecompressPng16(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    ParsedPng16& outPng
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> bytes(scratch);
    if(!ReadBytesUtf8(filePathUtf8, scratch, bytes))
        return false;
    static constexpr u8 s_PngSig[8] = { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    if(bytes.size() < 33u || std::memcmp(bytes.data(), s_PngSig, 8u) != 0)
        return false;

    usize cursor = 8u;
    b8 seenIhdr = false;
    b8 seenIend = false;
    u32 width = 0u;
    u32 height = 0u;
    u8 bitDepth = 0u;
    u8 colorType = 0u;
    u8 compMethod = 0u;
    u8 filterMethod = 0u;
    u8 interlaceMethod = 0u;
    Vector<u8, Core::Alloc::ScratchArena> idatBytes(scratch);
    idatBytes.reserve(bytes.size());

    while(cursor + 12u <= bytes.size()){
        const u32 chunkLen = ReadBe32(bytes.data() + cursor);
        const u8* typePtr = bytes.data() + cursor + 4u;
        cursor += 8u;
        if(static_cast<usize>(chunkLen) + 4u > bytes.size() - cursor)
            return false;
        const u8* payload = bytes.data() + cursor;
        cursor += chunkLen + 4u;

        if(std::memcmp(typePtr, "IHDR", 4) == 0){
            if(seenIhdr || chunkLen != 13u)
                return false;
            width = ReadBe32(payload);
            height = ReadBe32(payload + 4u);
            bitDepth = payload[8];
            colorType = payload[9];
            compMethod = payload[10];
            filterMethod = payload[11];
            interlaceMethod = payload[12];
            seenIhdr = true;
            if(bitDepth != 16u)
                return false;
        }
        else if(std::memcmp(typePtr, "IDAT", 4) == 0){
            if(!seenIhdr)
                return false;
            const usize oldSize = idatBytes.size();
            idatBytes.resize(oldSize + chunkLen);
            if(chunkLen > 0u)
                std::memcpy(idatBytes.data() + oldSize, payload, chunkLen);
        }
        else if(std::memcmp(typePtr, "IEND", 4) == 0){
            seenIend = true;
            break;
        }
    }

    if(!seenIhdr || !seenIend || width == 0u || height == 0u)
        return false;
    if(compMethod != 0u || filterMethod != 0u || interlaceMethod != 0u || idatBytes.empty())
        return false;

    u32 channels = 0u;
    if(colorType == 0u)
        channels = 1u;
    else if(colorType == 2u)
        channels = 3u;
    else if(colorType == 4u)
        channels = 2u;
    else if(colorType == 6u)
        channels = 4u;
    else
        return false;

    const u64 bpp = static_cast<u64>(channels) * 2u;
    const u64 stride = 1u + static_cast<u64>(width) * bpp;
    if(stride > static_cast<u64>(Limit<usize>::s_Max) / static_cast<u64>(height))
        return false;
    const usize expectedRawSize = static_cast<usize>(stride * static_cast<u64>(height));

    size_t decompressedLen = 0u;
    void* decompressed = tinfl_decompress_mem_to_heap(idatBytes.data(), idatBytes.size(), &decompressedLen, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if(!decompressed)
        return false;
    if(decompressedLen != expectedRawSize){
        mz_free(decompressed);
        return false;
    }

    outPng.width = width;
    outPng.height = height;
    outPng.channels = channels;
    outPng.decompressed = decompressed;
    return true;
}

template<u32 Channels, u32 Mode>
void DecodePng16RowToHeights(
    const u8* currRow,
    const u32 width,
    const f32 heightMin,
    const f32 scale
    , f32* dstRow
)noexcept{
    constexpr f32 s_InvMax = 1.0f / 65535.0f;
    constexpr usize s_Step = static_cast<usize>(Channels) * 2u;
    for(u32 x = 0u; x < width; ++x){
        const u8* px = currRow + static_cast<usize>(x) * s_Step;
        f32 sample = 0.0f;
        if constexpr(Mode == 0u){
            sample = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
        }
        else if constexpr(Mode == 1u){
            const usize chOff = (Channels >= 3u) ? 2u : 0u;
            sample = static_cast<f32>(ReadBe16(px + chOff)) * s_InvMax;
        }
        else if constexpr(Mode == 2u){
            const usize chOff = (Channels >= 3u) ? 4u : 0u;
            sample = static_cast<f32>(ReadBe16(px + chOff)) * s_InvMax;
        }
        else if constexpr(Mode == 3u){
            if constexpr(Channels == 2u)
                sample = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
            else if constexpr(Channels == 4u)
                sample = static_cast<f32>(ReadBe16(px + 6u)) * s_InvMax;
            else
                sample = 1.0f;
        }
        else{
            if constexpr(Channels < 3u){
                sample = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            }
            else{
                const f32 r = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
                const f32 g = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
                const f32 b = static_cast<f32>(ReadBe16(px + 4u)) * s_InvMax;
                sample = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            }
        }
        dstRow[x] = heightMin + sample * scale;
    }
}

template<u32 Mode>
void DispatchPng16HeightRowByChannels(
    const u32 channels,
    const u8* currRow,
    const u32 width,
    const f32 heightMin,
    const f32 range,
    f32* dstRow
)noexcept{
    if(channels == 1u)
        DecodePng16RowToHeights<1u, Mode>(currRow, width, heightMin, range, dstRow);
    else if(channels == 2u)
        DecodePng16RowToHeights<2u, Mode>(currRow, width, heightMin, range, dstRow);
    else if(channels == 3u)
        DecodePng16RowToHeights<3u, Mode>(currRow, width, heightMin, range, dstRow);
    else
        DecodePng16RowToHeights<4u, Mode>(currRow, width, heightMin, range, dstRow);
}

[[nodiscard]] b8 WriteScaledSamplesFromHdrRow(
    const basisu::vec4F* rowSrc,
    const u32 width,
    const u32 channelMode,
    const f32 heightMin,
    const f32 range,
    f32* dstRow
)noexcept{
    const SIMDVectorSingle vMin = VectorReplicate(heightMin);
    const SIMDVectorSingle vRange = VectorReplicate(range);
    u32 x = 0u;
    if(channelMode < 4u){
        for(; x + 4u <= width; x += 4u){
            const SIMDVectorSingle sVec = VectorSet(rowSrc[x + 0u][channelMode], rowSrc[x + 1u][channelMode], rowSrc[x + 2u][channelMode], rowSrc[x + 3u][channelMode]);
            if(!VectorIsFinite(sVec, VectorComponentMask::s_XYZW))
                return false;
            Float4U outVals;
            StoreFloat(VectorMultiplyAdd(sVec, vRange, vMin), outVals);
            std::memcpy(dstRow + x, &outVals, sizeof(outVals));
        }
        for(; x < width; ++x){
            const f32 s = rowSrc[x][channelMode];
            if(!std::isfinite(s))
                return false;
            dstRow[x] = heightMin + s * range;
        }
        return true;
    }
    for(; x + 4u <= width; x += 4u){
        const SIMDVectorSingle rVec = VectorSet(rowSrc[x + 0u][0], rowSrc[x + 1u][0], rowSrc[x + 2u][0], rowSrc[x + 3u][0]);
        const SIMDVectorSingle gVec = VectorSet(rowSrc[x + 0u][1], rowSrc[x + 1u][1], rowSrc[x + 2u][1], rowSrc[x + 3u][1]);
        const SIMDVectorSingle bVec = VectorSet(rowSrc[x + 0u][2], rowSrc[x + 1u][2], rowSrc[x + 2u][2], rowSrc[x + 3u][2]);
        const SIMDVectorSingle lum = VectorMultiplyAdd(
            rVec, VectorReplicate(0.2126f),
            VectorMultiplyAdd(gVec, VectorReplicate(0.7152f), VectorMultiply(bVec, VectorReplicate(0.0722f)))
        );
        if(!VectorIsFinite(lum, VectorComponentMask::s_XYZW))
            return false;
        Float4U outVals;
        StoreFloat(VectorMultiplyAdd(lum, vRange, vMin), outVals);
        std::memcpy(dstRow + x, &outVals, sizeof(outVals));
    }
    for(; x < width; ++x){
        const basisu::vec4F& p = rowSrc[x];
        const f32 lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        if(!std::isfinite(lum))
            return false;
        dstRow[x] = heightMin + lum * range;
    }
    return true;
}

template<u32 Channels>
void DecodePng16RowToMaskBits(
    const u8* currRow,
    const u32 cz,
    const u32 cellCols,
    const f32 threshold,
    u8* dst
)noexcept{
    constexpr f32 s_InvMax = 1.0f / 65535.0f;
    constexpr usize s_Step = static_cast<usize>(Channels) * 2u;
    const usize rowBase = static_cast<usize>(cz) * static_cast<usize>(cellCols);
    for(u32 cx = 0u; cx < cellCols; ++cx){
        const u8* px = currRow + static_cast<usize>(cx) * s_Step;
        f32 maskVal = 0.0f;
        if constexpr(Channels == 1u){
            maskVal = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
        }
        else if constexpr(Channels == 2u){
            const f32 lum = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            const f32 alpha = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
            maskVal = (alpha < lum) ? alpha : lum;
        }
        else if constexpr(Channels == 3u){
            const f32 r = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            const f32 g = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
            const f32 b = static_cast<f32>(ReadBe16(px + 4u)) * s_InvMax;
            maskVal = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        }
        else{
            const f32 r = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            const f32 g = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
            const f32 b = static_cast<f32>(ReadBe16(px + 4u)) * s_InvMax;
            const f32 lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            const f32 alpha = static_cast<f32>(ReadBe16(px + 6u)) * s_InvMax;
            maskVal = (alpha < lum) ? alpha : lum;
        }
        if(maskVal < threshold){
            const usize cellIdx = rowBase + static_cast<usize>(cx);
            dst[cellIdx >> 3u] = static_cast<u8>(dst[cellIdx >> 3u] | static_cast<u8>(1u << (cellIdx & 7u)));
        }
    }
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LoadExternalMeshFileRaw(
    const AStringView filePathUtf8,
    void* const user,
    void(* const reserveVerts)(void*, usize),
    Float4*(* const allocVerts)(void*, usize),
    void(* const reserveIdx)(void*, usize),
    u32*(* const allocIdx)(void*, usize)
)noexcept{
    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_mesh"));
    const AString<Core::Alloc::ScratchArena> ext = __hidden_physics_cook_io::LowercaseExtension(filePathUtf8, scratch);
    const AStringView extView(ext.data(), ext.size());
    if(extView == ".obj")
        return __hidden_physics_cook_io::LoadObjMesh(filePathUtf8, scratch, user, reserveVerts, allocVerts, reserveIdx, allocIdx);
    if(extView == ".fbx")
        return __hidden_physics_cook_io::LoadFbxMesh(filePathUtf8, scratch, user, reserveVerts, allocVerts, reserveIdx, allocIdx);
    if(extView == ".gltf" || extView == ".glb")
        return __hidden_physics_cook_io::LoadGltfOrGlbMesh(filePathUtf8, scratch, user, reserveVerts, allocVerts, reserveIdx, allocIdx);
    return false;
}

b8 LoadHeightmapImageRaw(
    const AStringView filePathUtf8,
    const u32 channelMode,
    const f32 heightMin,
    const f32 heightMax,
    u32& outCols,
    u32& outRows,
    void* const user,
    f32*(* const allocHeights)(void*, usize)
)noexcept{
    if(channelMode > 4u || !std::isfinite(heightMin) || !std::isfinite(heightMax))
        return false;
    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_hf_img"));
    const auto sniff = __hidden_physics_cook_io::SniffPng16Header(filePathUtf8, scratch);
    if(sniff == __hidden_physics_cook_io::Png16SniffResult::OpenFailed)
        return false;

    const f32 range = heightMax - heightMin;
    if(sniff == __hidden_physics_cook_io::Png16SniffResult::IsPng16){
        __hidden_physics_cook_io::ParsedPng16 png;
        if(!__hidden_physics_cook_io::ParseAndDecompressPng16(filePathUtf8, scratch, png))
            return false;
        if(png.width < 2u || png.height < 2u)
            return false;
        const usize count = static_cast<usize>(png.width) * static_cast<usize>(png.height);
        f32* dst = allocHeights(user, count);
        if(!dst)
            return false;

        const usize step = static_cast<usize>(png.channels) * 2u;
        const usize rowBytes = static_cast<usize>(png.width) * step;
        Vector<u8, Core::Alloc::ScratchArena> rowA(rowBytes, 0u, scratch);
        Vector<u8, Core::Alloc::ScratchArena> rowB(rowBytes, 0u, scratch);
        u8* prevPtr = rowA.data();
        u8* currPtr = rowB.data();
        const u8* src = static_cast<const u8*>(png.decompressed);

        for(u32 y = 0u; y < png.height; ++y){
            const u8* rowSrc = src + static_cast<usize>(y) * (1u + rowBytes);
            if(!__hidden_physics_cook_io::UnfilterPng16Scanline(rowSrc[0], rowSrc + 1u, prevPtr, currPtr, rowBytes, step))
                return false;
            f32* dstRow = dst + static_cast<usize>(y) * static_cast<usize>(png.width);
            if(channelMode == 0u)
                __hidden_physics_cook_io::DispatchPng16HeightRowByChannels<0u>(png.channels, currPtr, png.width, heightMin, range, dstRow);
            else if(channelMode == 1u)
                __hidden_physics_cook_io::DispatchPng16HeightRowByChannels<1u>(png.channels, currPtr, png.width, heightMin, range, dstRow);
            else if(channelMode == 2u)
                __hidden_physics_cook_io::DispatchPng16HeightRowByChannels<2u>(png.channels, currPtr, png.width, heightMin, range, dstRow);
            else if(channelMode == 3u)
                __hidden_physics_cook_io::DispatchPng16HeightRowByChannels<3u>(png.channels, currPtr, png.width, heightMin, range, dstRow);
            else
                __hidden_physics_cook_io::DispatchPng16HeightRowByChannels<4u>(png.channels, currPtr, png.width, heightMin, range, dstRow);
            std::swap(prevPtr, currPtr);
        }
        outCols = png.width;
        outRows = png.height;
        return true;
    }

    AString<Core::Alloc::ScratchArena> nullTermPath(scratch);
    nullTermPath.assign(filePathUtf8.data(), filePathUtf8.size());
    basisu::imagef hdrImg;
    if(!basisu::load_image_hdr(nullTermPath.c_str(), hdrImg, false))
        return false;
    const u32 width = hdrImg.get_width();
    const u32 height = hdrImg.get_height();
    if(width < 2u || height < 2u)
        return false;
    const usize count = static_cast<usize>(width) * static_cast<usize>(height);
    f32* dst = allocHeights(user, count);
    if(!dst)
        return false;

    for(u32 y = 0u; y < height; ++y){
        const basisu::vec4F* rowSrc = &hdrImg(0u, y);
        f32* dstRow = dst + static_cast<usize>(y) * static_cast<usize>(width);
        if(!__hidden_physics_cook_io::WriteScaledSamplesFromHdrRow(rowSrc, width, channelMode, heightMin, range, dstRow))
            return false;
    }
    outCols = width;
    outRows = height;
    return true;
}

b8 LoadHeightmapRawBinaryRaw(
    const AStringView filePathUtf8,
    const u32 cols,
    const u32 rows,
    const u32 bitsPerSample,
    const b8 bigEndian,
    const b8 isFloat,
    const f32 heightMin,
    const f32 heightMax,
    void* const user,
    f32*(* const allocHeights)(void*, usize)
)noexcept{
    if(cols < 2u || rows < 2u || !std::isfinite(heightMin) || !std::isfinite(heightMax))
        return false;
    if((bitsPerSample != 8u && bitsPerSample != 16u && bitsPerSample != 32u) || (isFloat && bitsPerSample != 32u))
        return false;

    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_hf_raw"));
    Vector<u8, Core::Alloc::ScratchArena> fileBytes(scratch);
    if(!__hidden_physics_cook_io::ReadBytesUtf8(filePathUtf8, scratch, fileBytes))
        return false;

    const u64 sampleCount = static_cast<u64>(cols) * static_cast<u64>(rows);
    const u64 byteStride = static_cast<u64>(bitsPerSample / 8u);
    if(sampleCount > static_cast<u64>(Limit<usize>::s_Max) / byteStride)
        return false;
    if(fileBytes.size() != static_cast<usize>(sampleCount * byteStride))
        return false;

    const usize count = static_cast<usize>(sampleCount);
    f32* dst = allocHeights(user, count);
    if(!dst)
        return false;

    const f32 range = heightMax - heightMin;
    const SIMDVectorSingle vMin = VectorReplicate(heightMin);
    const SIMDVectorSingle vRange = VectorReplicate(range);
    const u8* rawBytes = fileBytes.data();

    if(bitsPerSample == 8u){
        constexpr f32 s_InvMax8 = 1.0f / 255.0f;
        const f32 scale8 = s_InvMax8 * range;
        const SIMDVectorSingle vScale8 = VectorReplicate(scale8);
        usize i = 0u;
        for(; i + 4u <= count; i += 4u){
            const SIMDVectorSingle rawVec = VectorSet(
                static_cast<f32>(rawBytes[i + 0u]),
                static_cast<f32>(rawBytes[i + 1u]),
                static_cast<f32>(rawBytes[i + 2u]),
                static_cast<f32>(rawBytes[i + 3u])
            );
            Float4U outVals;
            StoreFloat(VectorMultiplyAdd(rawVec, vScale8, vMin), outVals);
            std::memcpy(dst + i, &outVals, sizeof(outVals));
        }
        for(; i < count; ++i)
            dst[i] = heightMin + static_cast<f32>(rawBytes[i]) * scale8;
        return true;
    }
    if(bitsPerSample == 16u){
        constexpr f32 s_InvMax16 = 1.0f / 65535.0f;
        const f32 scale16 = s_InvMax16 * range;
        const SIMDVectorSingle vScale16 = VectorReplicate(scale16);
        usize i = 0u;
        if(bigEndian){
            for(; i + 4u <= count; i += 4u){
                const u8* p = rawBytes + i * 2u;
                const SIMDVectorSingle rawVec = VectorSet(
                    static_cast<f32>(__hidden_physics_cook_io::ReadBe16(p + 0u)),
                    static_cast<f32>(__hidden_physics_cook_io::ReadBe16(p + 2u)),
                    static_cast<f32>(__hidden_physics_cook_io::ReadBe16(p + 4u)),
                    static_cast<f32>(__hidden_physics_cook_io::ReadBe16(p + 6u))
                );
                Float4U outVals;
                StoreFloat(VectorMultiplyAdd(rawVec, vScale16, vMin), outVals);
                std::memcpy(dst + i, &outVals, sizeof(outVals));
            }
            for(; i < count; ++i)
                dst[i] = heightMin + static_cast<f32>(__hidden_physics_cook_io::ReadBe16(rawBytes + i * 2u)) * scale16;
        }
        else{
            for(; i + 4u <= count; i += 4u){
                const u8* p = rawBytes + i * 2u;
                const SIMDVectorSingle rawVec = VectorSet(
                    static_cast<f32>(__hidden_physics_cook_io::ReadLe16(p + 0u)),
                    static_cast<f32>(__hidden_physics_cook_io::ReadLe16(p + 2u)),
                    static_cast<f32>(__hidden_physics_cook_io::ReadLe16(p + 4u)),
                    static_cast<f32>(__hidden_physics_cook_io::ReadLe16(p + 6u))
                );
                Float4U outVals;
                StoreFloat(VectorMultiplyAdd(rawVec, vScale16, vMin), outVals);
                std::memcpy(dst + i, &outVals, sizeof(outVals));
            }
            for(; i < count; ++i)
                dst[i] = heightMin + static_cast<f32>(__hidden_physics_cook_io::ReadLe16(rawBytes + i * 2u)) * scale16;
        }
        return true;
    }
    if(!isFloat){
        constexpr f64 s_InvMax32 = 1.0 / 4294967295.0;
        if(bigEndian){
            for(usize i = 0u; i < count; ++i){
                const u32 raw = __hidden_physics_cook_io::ReadBe32(rawBytes + i * 4u);
                const f32 norm = static_cast<f32>(static_cast<f64>(raw) * s_InvMax32);
                dst[i] = heightMin + norm * range;
            }
        }
        else{
            for(usize i = 0u; i < count; ++i){
                const u32 raw = __hidden_physics_cook_io::ReadLe32(rawBytes + i * 4u);
                const f32 norm = static_cast<f32>(static_cast<f64>(raw) * s_InvMax32);
                dst[i] = heightMin + norm * range;
            }
        }
        return true;
    }

    usize i = 0u;
    if(bigEndian){
        for(; i < count; ++i){
            const f32 v = __hidden_physics_cook_io::ReadFloat32(rawBytes + i * 4u, true);
            if(!std::isfinite(v)){
                if(v == v)
                    return false;
                dst[i] = v;
            }
            else{
                dst[i] = heightMin + v * range;
            }
        }
        return true;
    }

    for(; i + 4u <= count; i += 4u){
        Float4U inVals;
        std::memcpy(&inVals, rawBytes + i * 4u, sizeof(inVals));
        const SIMDVectorSingle v = LoadFloat(inVals);
        if(VectorIsFinite(v, VectorComponentMask::s_XYZW)){
            Float4U outVals;
            StoreFloat(VectorMultiplyAdd(v, vRange, vMin), outVals);
            std::memcpy(dst + i, &outVals, sizeof(outVals));
        }
        else{
            for(usize lane = 0u; lane < 4u; ++lane){
                const f32 fv = inVals.raw[lane];
                if(!std::isfinite(fv)){
                    if(fv == fv)
                        return false;
                    dst[i + lane] = fv;
                }
                else{
                    dst[i + lane] = heightMin + fv * range;
                }
            }
        }
    }
    for(; i < count; ++i){
        const f32 v = __hidden_physics_cook_io::ReadFloat32(rawBytes + i * 4u, false);
        if(!std::isfinite(v)){
            if(v == v)
                return false;
            dst[i] = v;
        }
        else{
            dst[i] = heightMin + v * range;
        }
    }
    return true;
}

b8 LoadHeightmapHoleMaskRaw(
    const AStringView filePathUtf8,
    const u32 expectedCols,
    const u32 expectedRows,
    const f32 threshold,
    void* const user,
    u8*(* const allocHoleBytes)(void*, usize)
)noexcept{
    if(expectedCols < 2u || expectedRows < 2u || !std::isfinite(threshold))
        return false;

    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_hf_mask"));
    const auto sniff = __hidden_physics_cook_io::SniffPng16Header(filePathUtf8, scratch);
    if(sniff == __hidden_physics_cook_io::Png16SniffResult::OpenFailed)
        return false;

    const u32 cellCols = expectedCols - 1u;
    const u32 cellRows = expectedRows - 1u;
    const usize cellCount = static_cast<usize>(cellCols) * static_cast<usize>(cellRows);
    const usize byteCount = (cellCount + 7u) / 8u;

    if(sniff == __hidden_physics_cook_io::Png16SniffResult::IsPng16){
        __hidden_physics_cook_io::ParsedPng16 png;
        if(!__hidden_physics_cook_io::ParseAndDecompressPng16(filePathUtf8, scratch, png))
            return false;
        const b8 matchesCells = (png.width == cellCols && png.height == cellRows);
        const b8 matchesVerts = (png.width == expectedCols && png.height == expectedRows);
        if(!matchesCells && !matchesVerts)
            return false;
        u8* dst = allocHoleBytes(user, byteCount);
        if(!dst)
            return false;

        const usize step = static_cast<usize>(png.channels) * 2u;
        const usize rowBytes = static_cast<usize>(png.width) * step;
        Vector<u8, Core::Alloc::ScratchArena> rowA(rowBytes, 0u, scratch);
        Vector<u8, Core::Alloc::ScratchArena> rowB(rowBytes, 0u, scratch);
        u8* prevPtr = rowA.data();
        u8* currPtr = rowB.data();
        const u8* src = static_cast<const u8*>(png.decompressed);

        for(u32 cz = 0u; cz < cellRows; ++cz){
            const u8* rowSrc = src + static_cast<usize>(cz) * (1u + rowBytes);
            if(!__hidden_physics_cook_io::UnfilterPng16Scanline(rowSrc[0], rowSrc + 1u, prevPtr, currPtr, rowBytes, step))
                return false;
            if(png.channels == 1u)
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<1u>(currPtr, cz, cellCols, threshold, dst);
            else if(png.channels == 2u)
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<2u>(currPtr, cz, cellCols, threshold, dst);
            else if(png.channels == 3u)
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<3u>(currPtr, cz, cellCols, threshold, dst);
            else
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<4u>(currPtr, cz, cellCols, threshold, dst);
            std::swap(prevPtr, currPtr);
        }
        return true;
    }

    AString<Core::Alloc::ScratchArena> nullTermPath(scratch);
    nullTermPath.assign(filePathUtf8.data(), filePathUtf8.size());
    basisu::imagef hdrImg;
    if(!basisu::load_image_hdr(nullTermPath.c_str(), hdrImg, false))
        return false;
    const u32 width = hdrImg.get_width();
    const u32 height = hdrImg.get_height();
    const b8 matchesCells = (width == cellCols && height == cellRows);
    const b8 matchesVerts = (width == expectedCols && height == expectedRows);
    if(!matchesCells && !matchesVerts)
        return false;

    u8* dst = allocHoleBytes(user, byteCount);
    if(!dst)
        return false;

    for(u32 cz = 0u; cz < cellRows; ++cz){
        const basisu::vec4F* rowSrc = &hdrImg(0u, cz);
        const usize rowBase = static_cast<usize>(cz) * static_cast<usize>(cellCols);
        for(u32 cx = 0u; cx < cellCols; ++cx){
            const basisu::vec4F& p = rowSrc[cx];
            const f32 lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            const f32 alpha = p[3];
            const f32 maskVal = (alpha < lum) ? alpha : lum;
            if(maskVal < threshold){
                const usize cellIdx = rowBase + static_cast<usize>(cx);
                dst[cellIdx >> 3u] = static_cast<u8>(dst[cellIdx >> 3u] | static_cast<u8>(1u << (cellIdx & 7u)));
            }
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

