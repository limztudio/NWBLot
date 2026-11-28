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

[[nodiscard]] u64 ReadLe64(const u8* p)noexcept{
    return static_cast<u64>(ReadLe32(p)) | (static_cast<u64>(ReadLe32(p + 4u)) << 32u);
}

[[nodiscard]] u64 ReadBe64(const u8* p)noexcept{
    return (static_cast<u64>(ReadBe32(p)) << 32u) | static_cast<u64>(ReadBe32(p + 4u));
}

[[nodiscard]] f64 ReadFloat64(const u8* p, const b8 bigEndian)noexcept{
    const u64 bits = bigEndian ? ReadBe64(p) : ReadLe64(p);
    f64 out = 0.0;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

[[nodiscard]] f32 HalfToFloat(const u16 h)noexcept{
    const u32 sign = (static_cast<u32>(h) & 0x8000u) << 16u;
    const u32 exp = (static_cast<u32>(h) >> 10u) & 0x1fu;
    const u32 mant = static_cast<u32>(h) & 0x03ffu;
    u32 bits = 0u;
    if(exp == 0u){
        if(mant == 0u){
            bits = sign;
        }
        else{
            u32 m = mant;
            u32 e = 127u - 15u + 1u;
            while((m & 0x0400u) == 0u){
                m <<= 1u;
                --e;
            }
            m &= 0x03ffu;
            bits = sign | (e << 23u) | (m << 13u);
        }
    }
    else if(exp == 31u){
        if(mant == 0u)
            bits = sign | 0x7f800000u;
        else
            bits = sign | 0x7fc00000u | (mant << 13u);
    }
    else{
        bits = sign | ((exp + (127u - 15u)) << 23u) | (mant << 13u);
    }
    f32 out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

[[nodiscard]] b8 IsAsciiSpace(const char c)noexcept{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

[[nodiscard]] AStringView TrimAsciiView(AStringView view)noexcept{
    while(!view.empty() && IsAsciiSpace(view.front()))
        view.remove_prefix(1u);
    while(!view.empty() && IsAsciiSpace(view.back()))
        view.remove_suffix(1u);
    return view;
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

[[nodiscard]] b8 ParseAsciiI64Token(const char*& cur, const char* end, i64& outVal)noexcept{
    while(cur < end && IsAsciiSpace(*cur))
        ++cur;
    if(cur >= end)
        return false;
    const char* tokStart = cur;
    while(cur < end && !IsAsciiSpace(*cur))
        ++cur;
    return ParseAsciiI64Slice(tokStart, cur, outVal);
}

[[nodiscard]] b8 ParseAsciiU64Token(const char*& cur, const char* end, u64& outVal)noexcept{
    i64 signedVal = 0;
    if(!ParseAsciiI64Token(cur, end, signedVal) || signedVal < 0)
        return false;
    outVal = static_cast<u64>(signedVal);
    return true;
}

[[nodiscard]] b8 ParseAsciiStringToken(const char*& cur, const char* end, AStringView& outTok)noexcept{
    while(cur < end && IsAsciiSpace(*cur))
        ++cur;
    if(cur >= end)
        return false;
    const char* tokStart = cur;
    while(cur < end && !IsAsciiSpace(*cur))
        ++cur;
    outTok = AStringView(tokStart, static_cast<usize>(cur - tokStart));
    return true;
}

struct EarClipPoint2D{
    f32 x = 0.0f;
    f32 y = 0.0f;
};

[[nodiscard]] f32 EarClipCross2D(const EarClipPoint2D& a, const EarClipPoint2D& b, const EarClipPoint2D& c)noexcept{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[[nodiscard]] b8 TriangulatePolygonEarClip(
    const Span<const Float4> verts,
    const Span<const u32> polyIndices,
    Core::Alloc::ScratchArena& scratch,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices
)noexcept{
    const usize n = polyIndices.size();
    if(n < 3u)
        return false;
    if(n == 3u){
        outIndices.push_back(polyIndices[0u]);
        outIndices.push_back(polyIndices[1u]);
        outIndices.push_back(polyIndices[2u]);
        return true;
    }

    f32 nx = 0.0f;
    f32 ny = 0.0f;
    f32 nz = 0.0f;
    for(usize i = 0u; i < n; ++i){
        const Float4& p0 = verts[polyIndices[i]];
        const Float4& p1 = verts[polyIndices[(i + 1u) % n]];
        nx += (p0.y - p1.y) * (p0.z + p1.z);
        ny += (p0.z - p1.z) * (p0.x + p1.x);
        nz += (p0.x - p1.x) * (p0.y + p1.y);
    }
    const f32 nLenSq = nx * nx + ny * ny + nz * nz;
    if(!(nLenSq > 1.0e-20f)){
        for(usize k = 1u; k + 1u < n; ++k){
            outIndices.push_back(polyIndices[0u]);
            outIndices.push_back(polyIndices[k]);
            outIndices.push_back(polyIndices[k + 1u]);
        }
        return true;
    }

    const SIMDVectorSingle normal = Vector3Normalize(VectorSet(nx, ny, nz, 0.0f));
    const f32 absNx = std::fabs(VectorGetX(normal));
    const SIMDVectorSingle refAxis = (absNx > 0.9f) ? VectorSet(0.0f, 1.0f, 0.0f, 0.0f) : VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVectorSingle uAxis = Vector3Normalize(Vector3Cross(refAxis, normal));
    const SIMDVectorSingle vAxis = Vector3Cross(normal, uAxis);

    Vector<EarClipPoint2D, Core::Alloc::ScratchArena> pts(scratch);
    pts.resize(n);
    Vector<usize, Core::Alloc::ScratchArena> rem(scratch);
    rem.resize(n);
    for(usize i = 0u; i < n; ++i){
        const Float4& fp = verts[polyIndices[i]];
        const SIMDVectorSingle pVec = VectorSet(fp.x, fp.y, fp.z, 0.0f);
        pts[i].x = VectorGetX(Vector3Dot(pVec, uAxis));
        pts[i].y = VectorGetX(Vector3Dot(pVec, vAxis));
        rem[i] = i;
    }

    f32 area2 = 0.0f;
    for(usize i = 0u; i < n; ++i){
        const EarClipPoint2D& a = pts[i];
        const EarClipPoint2D& b = pts[(i + 1u) % n];
        area2 += a.x * b.y - b.x * a.y;
    }
    const f32 orient = (area2 >= 0.0f) ? 1.0f : -1.0f;

    while(rem.size() > 3u){
        const usize m = rem.size();
        usize earPos = Limit<usize>::s_Max;
        for(usize i = 0u; i < m; ++i){
            const usize iPrev = rem[(i + m - 1u) % m];
            const usize iCurr = rem[i];
            const usize iNext = rem[(i + 1u) % m];
            const EarClipPoint2D& a = pts[iPrev];
            const EarClipPoint2D& b = pts[iCurr];
            const EarClipPoint2D& c = pts[iNext];
            if(EarClipCross2D(a, b, c) * orient <= 1.0e-12f)
                continue;

            b8 anyInside = false;
            for(usize j = 0u; j < m; ++j){
                const usize iTest = rem[j];
                if(iTest == iPrev || iTest == iCurr || iTest == iNext)
                    continue;
                const EarClipPoint2D& q = pts[iTest];
                const f32 s0 = EarClipCross2D(a, b, q) * orient;
                const f32 s1 = EarClipCross2D(b, c, q) * orient;
                const f32 s2 = EarClipCross2D(c, a, q) * orient;
                if(s0 >= -1.0e-9f && s1 >= -1.0e-9f && s2 >= -1.0e-9f){
                    anyInside = true;
                    break;
                }
            }
            if(!anyInside){
                earPos = i;
                break;
            }
        }

        if(earPos == Limit<usize>::s_Max){
            for(usize i = 0u; i < m; ++i){
                const usize iPrev = rem[(i + m - 1u) % m];
                const usize iCurr = rem[i];
                const usize iNext = rem[(i + 1u) % m];
                if(EarClipCross2D(pts[iPrev], pts[iCurr], pts[iNext]) * orient >= -1.0e-12f){
                    earPos = i;
                    break;
                }
            }
            if(earPos == Limit<usize>::s_Max)
                earPos = 0u;
        }

        const usize iPrev = rem[(earPos + m - 1u) % m];
        const usize iCurr = rem[earPos];
        const usize iNext = rem[(earPos + 1u) % m];
        outIndices.push_back(polyIndices[iPrev]);
        outIndices.push_back(polyIndices[iCurr]);
        outIndices.push_back(polyIndices[iNext]);

        for(usize k = earPos; k + 1u < m; ++k)
            rem[k] = rem[k + 1u];
        rem.pop_back();
    }

    outIndices.push_back(polyIndices[rem[0u]]);
    outIndices.push_back(polyIndices[rem[1u]]);
    outIndices.push_back(polyIndices[rem[2u]]);
    return true;
}

void CompactReferencedVertices(
    Vector<Float4, Core::Alloc::ScratchArena>& verts,
    Vector<u32, Core::Alloc::ScratchArena>& indices,
    Core::Alloc::ScratchArena& scratch
)noexcept{
    if(verts.empty() || indices.empty())
        return;
    Vector<u32, Core::Alloc::ScratchArena> remap(verts.size(), Limit<u32>::s_Max, scratch);
    Vector<Float4, Core::Alloc::ScratchArena> compacted(scratch);
    compacted.reserve(verts.size());
    for(usize i = 0u; i < indices.size(); ++i){
        const u32 oldIdx = indices[i];
        if(oldIdx >= verts.size())
            return;
        if(remap[oldIdx] == Limit<u32>::s_Max){
            remap[oldIdx] = static_cast<u32>(compacted.size());
            compacted.push_back(verts[oldIdx]);
        }
        indices[i] = remap[oldIdx];
    }
    verts = Move(compacted);
}

[[nodiscard]] b8 ApplyExternalMeshTransformAndWinding(
    const Float4& scaleF4,
    const Float4& rotationF4,
    const Float4& offsetF4,
    Vector<Float4, Core::Alloc::ScratchArena>& verts,
    Vector<u32, Core::Alloc::ScratchArena>& indices
)noexcept{
    if(!std::isfinite(scaleF4.x) || !std::isfinite(scaleF4.y) || !std::isfinite(scaleF4.z))
        return false;
    if(std::fabs(scaleF4.x) <= 1.0e-6f || std::fabs(scaleF4.y) <= 1.0e-6f || std::fabs(scaleF4.z) <= 1.0e-6f)
        return false;
    if(!std::isfinite(rotationF4.x) || !std::isfinite(rotationF4.y) || !std::isfinite(rotationF4.z) || !std::isfinite(rotationF4.w))
        return false;
    if(!std::isfinite(offsetF4.x) || !std::isfinite(offsetF4.y) || !std::isfinite(offsetF4.z))
        return false;

    const f32 rotLenSq = rotationF4.x * rotationF4.x + rotationF4.y * rotationF4.y + rotationF4.z * rotationF4.z + rotationF4.w * rotationF4.w;
    SIMDVectorSingle rotVec = VectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    if(rotLenSq > 1.0e-8f)
        rotVec = QuaternionNormalize(VectorSet(rotationF4.x, rotationF4.y, rotationF4.z, rotationF4.w));

    const b8 hasScale = std::fabs(scaleF4.x - 1.0f) > 1.0e-6f || std::fabs(scaleF4.y - 1.0f) > 1.0e-6f || std::fabs(scaleF4.z - 1.0f) > 1.0e-6f;
    const b8 hasRot = rotLenSq > 1.0e-8f && (std::fabs(VectorGetX(rotVec)) > 1.0e-6f || std::fabs(VectorGetY(rotVec)) > 1.0e-6f || std::fabs(VectorGetZ(rotVec)) > 1.0e-6f);
    const b8 hasOffset = std::fabs(offsetF4.x) > 1.0e-6f || std::fabs(offsetF4.y) > 1.0e-6f || std::fabs(offsetF4.z) > 1.0e-6f;
    if(!hasScale && !hasRot && !hasOffset)
        return true;

    const SIMDVectorSingle scaleVec = VectorSet(scaleF4.x, scaleF4.y, scaleF4.z, 0.0f);
    const SIMDVectorSingle offsetVec = VectorSet(offsetF4.x, offsetF4.y, offsetF4.z, 0.0f);
    for(usize i = 0u; i < verts.size(); ++i){
        const Float4& src = verts[i];
        SIMDVectorSingle p = VectorMultiply(VectorSet(src.x, src.y, src.z, 0.0f), scaleVec);
        p = Vector3Rotate(p, rotVec);
        p = VectorAdd(p, offsetVec);
        if(!VectorIsFinite(p, VectorComponentMask::s_XYZW))
            return false;
        verts[i] = Float4(VectorGetX(p), VectorGetY(p), VectorGetZ(p), 0.0f);
    }

    if((scaleF4.x * scaleF4.y * scaleF4.z) < 0.0f){
        for(usize i = 0u; i + 2u < indices.size(); i += 3u)
            std::swap(indices[i + 1u], indices[i + 2u]);
    }
    return true;
}

[[nodiscard]] b8 LoadObjMesh(
    const AStringView filePathUtf8,
    const AStringView submeshName,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices,
    Vector<u16, Core::Alloc::ScratchArena>& outTriMats
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> bytes(scratch);
    if(!ReadBytesUtf8(filePathUtf8, scratch, bytes) || bytes.empty())
        return false;

    const char* data = reinterpret_cast<const char*>(bytes.data());
    const char* end = data + bytes.size();
    outVerts.reserve(bytes.size() / 48u + 16u);
    outIndices.reserve(bytes.size() / 24u + 16u);
    Vector<u32, Core::Alloc::ScratchArena> faceVerts(scratch);
    Vector<AString<Core::Alloc::ScratchArena>, Core::Alloc::ScratchArena> matNames(scratch);
    b8 hasUsemtl = false;
    u16 curMat = 0u;
    AString<Core::Alloc::ScratchArena> curObj(scratch);
    AString<Core::Alloc::ScratchArena> curGroup(scratch);
    b8 inSubmesh = submeshName.empty();

    const auto stripLineCommentAndTrim = [](const char* startPtr, const char* stopPtr) -> AStringView{
        const char* commentPtr = startPtr;
        while(commentPtr < stopPtr && *commentPtr != '#')
            ++commentPtr;
        return TrimAsciiView(AStringView(startPtr, static_cast<usize>(commentPtr - startPtr)));
    };

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
            if(outVerts.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            outVerts.push_back(Float4(x, y, z, 0.0f));
        }
        else if(p + 2 <= lineEnd && p[0] == 'o' && IsAsciiSpace(p[1])){
            const AStringView name = stripLineCommentAndTrim(p + 2, lineEnd);
            curObj.assign(name.data(), name.size());
            if(!submeshName.empty())
                inSubmesh = (AStringView(curObj.data(), curObj.size()) == submeshName || AStringView(curGroup.data(), curGroup.size()) == submeshName);
        }
        else if(p + 2 <= lineEnd && p[0] == 'g' && IsAsciiSpace(p[1])){
            const AStringView name = stripLineCommentAndTrim(p + 2, lineEnd);
            curGroup.assign(name.data(), name.size());
            if(!submeshName.empty())
                inSubmesh = (AStringView(curObj.data(), curObj.size()) == submeshName || AStringView(curGroup.data(), curGroup.size()) == submeshName);
        }
        else if(p + 7 <= lineEnd && std::memcmp(p, "usemtl", 6) == 0 && IsAsciiSpace(p[6])){
            const AStringView matName = stripLineCommentAndTrim(p + 7, lineEnd);
            if(!matName.empty()){
                hasUsemtl = true;
                u16 foundMat = Limit<u16>::s_Max;
                for(usize mi = 0u; mi < matNames.size(); ++mi){
                    if(AStringView(matNames[mi].data(), matNames[mi].size()) == matName){
                        foundMat = static_cast<u16>(mi);
                        break;
                    }
                }
                if(foundMat == Limit<u16>::s_Max){
                    if(matNames.size() >= static_cast<usize>(Limit<u16>::s_Max))
                        return false;
                    foundMat = static_cast<u16>(matNames.size());
                    matNames.emplace_back(scratch);
                    matNames.back().assign(matName.data(), matName.size());
                }
                curMat = foundMat;
            }
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
                    resolved = static_cast<i64>(outVerts.size()) + rawIdx;
                if(resolved < 0 || resolved >= static_cast<i64>(outVerts.size()))
                    return false;
                faceVerts.push_back(static_cast<u32>(resolved));
                p = tokEnd;
            }
            if(faceVerts.size() < 3u)
                return false;
            if(inSubmesh){
                const usize beforeIdxCount = outIndices.size();
                if(!TriangulatePolygonEarClip(Span<const Float4>(outVerts.data(), outVerts.size()), Span<const u32>(faceVerts.data(), faceVerts.size()), scratch, outIndices))
                    return false;
                if(hasUsemtl){
                    const usize addedTris = (outIndices.size() - beforeIdxCount) / 3u;
                    const usize curTris = beforeIdxCount / 3u;
                    if(outTriMats.size() < curTris)
                        outTriMats.resize(curTris, 0u);
                    outTriMats.resize(curTris + addedTris, curMat);
                }
            }
        }

        cur = lineEnd;
        while(cur < end && (*cur == '\n' || *cur == '\r'))
            ++cur;
    }

    if(!hasUsemtl)
        outTriMats.clear();
    if(!submeshName.empty())
        CompactReferencedVertices(outVerts, outIndices, scratch);
    return !outVerts.empty() && outIndices.size() >= 3u;
}

[[nodiscard]] b8 LoadFbxMesh(
    const AStringView filePathUtf8,
    const AStringView submeshName,
    const b8 normalizeUnitsAndAxes,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices,
    Vector<u16, Core::Alloc::ScratchArena>& outTriMats
)noexcept{
    ufbx_load_opts opts = {};
    if(normalizeUnitsAndAxes){
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;
    }
    ufbx_error ufbxErr = {};
    ufbx_scene* scene = ufbx_load_file_len(filePathUtf8.data(), filePathUtf8.size(), &opts, &ufbxErr);
    if(!scene)
        return false;

    Vector<u32, Core::Alloc::ScratchArena> triBuf(scratch);
    b8 ok = true;
    b8 hasFaceMaterials = false;

    for(usize ni = 0u; ni < scene->nodes.count && ok; ++ni){
        const ufbx_node* node = scene->nodes.data[ni];
        if(!node || !node->mesh)
            continue;
        const ufbx_mesh* mesh = node->mesh;
        if(!mesh->vertex_position.exists || mesh->num_vertices == 0u || mesh->num_faces == 0u)
            continue;
        if(!submeshName.empty()){
            const AStringView nodeName(node->name.data ? node->name.data : "", node->name.length);
            const AStringView meshName(mesh->name.data ? mesh->name.data : "", mesh->name.length);
            if(nodeName != submeshName && meshName != submeshName)
                continue;
        }
        if(mesh->num_vertices > static_cast<usize>(Limit<u32>::s_Max) - outVerts.size()){
            ok = false;
            break;
        }
        const u32 baseVertex = static_cast<u32>(outVerts.size());
        const usize oldVertSize = outVerts.size();
        outVerts.resize(oldVertSize + mesh->num_vertices, Float4(0.0f, 0.0f, 0.0f, 0.0f));
        for(usize vi = 0u; vi < mesh->num_vertices; ++vi){
            const ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, mesh->vertices.data[vi]);
            const f32 x = static_cast<f32>(p.x);
            const f32 y = static_cast<f32>(p.y);
            const f32 z = static_cast<f32>(p.z);
            if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)){
                ok = false;
                break;
            }
            outVerts[oldVertSize + vi] = Float4(x, y, z, 0.0f);
        }
        if(!ok)
            break;

        const usize needTriBuf = mesh->max_face_triangles * 3u;
        if(triBuf.size() < needTriBuf)
            triBuf.resize(needTriBuf);

        const b8 nodeHasMats = mesh->face_material.count >= mesh->num_faces;
        if(nodeHasMats)
            hasFaceMaterials = true;

        for(usize fi = 0u; fi < mesh->num_faces; ++fi){
            if(mesh->face_hole.count > fi && mesh->face_hole.data[fi])
                continue;
            const ufbx_face face = mesh->faces.data[fi];
            if(face.num_indices < 3u)
                continue;
            const u32 triCount = ufbx_triangulate_face(triBuf.data(), triBuf.size(), mesh, face);
            const usize cornerTotal = static_cast<usize>(triCount) * 3u;
            u16 faceMatSlot = 0u;
            if(nodeHasMats){
                const u32 localMat = mesh->face_material.data[fi];
                u32 resolvedMat = localMat;
                if(localMat < node->materials.count && node->materials.data[localMat])
                    resolvedMat = node->materials.data[localMat]->typed_id;
                else if(localMat < mesh->materials.count && mesh->materials.data[localMat])
                    resolvedMat = mesh->materials.data[localMat]->typed_id;
                if(resolvedMat <= static_cast<u32>(Limit<u16>::s_Max))
                    faceMatSlot = static_cast<u16>(resolvedMat);
            }
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
                outIndices.push_back(baseVertex + logicalVert);
            }
            if(!ok)
                break;
            if(hasFaceMaterials){
                const usize curTriTotal = outIndices.size() / 3u;
                if(outTriMats.size() < curTriTotal - static_cast<usize>(triCount))
                    outTriMats.resize(curTriTotal - static_cast<usize>(triCount), 0u);
                outTriMats.resize(curTriTotal, faceMatSlot);
            }
        }
    }

    if(!hasFaceMaterials)
        outTriMats.clear();
    ufbx_free_scene(scene);
    return ok && !outVerts.empty() && outIndices.size() >= 3u;
}

[[nodiscard]] b8 LoadStlMesh(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> bytes(scratch);
    if(!ReadBytesUtf8(filePathUtf8, scratch, bytes) || bytes.empty())
        return false;

    const char* data = reinterpret_cast<const char*>(bytes.data());
    const char* end = data + bytes.size();
    const char* scan = data;
    while(scan < end && IsAsciiSpace(*scan))
        ++scan;
    const AStringView trimmedStart(scan, static_cast<usize>(end - scan));
    const b8 startsWithSolid = trimmedStart.size() >= 5u && std::memcmp(trimmedStart.data(), "solid", 5u) == 0;
    const usize sniffLen = (trimmedStart.size() < 512u) ? trimmedStart.size() : 512u;
    const AStringView headView(trimmedStart.data(), sniffLen);
    const b8 looksLikeAscii = startsWithSolid
        && (headView.find("facet") != AStringView::npos || headView.find("endsolid") != AStringView::npos)
        && headView.find("vertex") != AStringView::npos;

    if(!looksLikeAscii && bytes.size() >= 84u){
        const u32 triCount = ReadLe32(bytes.data() + 80u);
        const u64 expectedBytes = 84ull + static_cast<u64>(triCount) * 50ull;
        if(triCount > 0u && expectedBytes == static_cast<u64>(bytes.size())){
            const usize vCount = static_cast<usize>(triCount) * 3u;
            if(vCount > static_cast<usize>(Limit<u32>::s_Max))
                return false;
            outVerts.reserve(vCount);
            outIndices.reserve(vCount);
            for(u32 ti = 0u; ti < triCount; ++ti){
                const u8* rec = bytes.data() + 84u + static_cast<usize>(ti) * 50u;
                for(u32 ci = 0u; ci < 3u; ++ci){
                    const u8* vp = rec + 12u + static_cast<usize>(ci) * 12u;
                    const f32 x = ReadFloat32(vp + 0u, false);
                    const f32 y = ReadFloat32(vp + 4u, false);
                    const f32 z = ReadFloat32(vp + 8u, false);
                    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                        return false;
                    outIndices.push_back(static_cast<u32>(outVerts.size()));
                    outVerts.push_back(Float4(x, y, z, 0.0f));
                }
            }
            return !outVerts.empty() && outIndices.size() >= 3u;
        }
    }

    const char* cur = data;
    while(cur < end){
        AStringView kw;
        if(!ParseAsciiStringToken(cur, end, kw))
            break;
        if(kw == "vertex"){
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            if(!ParseAsciiFloatToken(cur, end, x) || !ParseAsciiFloatToken(cur, end, y) || !ParseAsciiFloatToken(cur, end, z))
                return false;
            if(outVerts.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            outIndices.push_back(static_cast<u32>(outVerts.size()));
            outVerts.push_back(Float4(x, y, z, 0.0f));
        }
    }
    return !outVerts.empty() && outIndices.size() >= 3u && (outIndices.size() % 3u) == 0u;
}

enum class PlyScalarType : u8{
    I8 = 0u,
    U8,
    I16,
    U16,
    I32,
    U32,
    F32,
    F64,
};

[[nodiscard]] b8 ParsePlyScalarType(const AStringView name, PlyScalarType& outType, usize& outBytes)noexcept{
    if(name == "char" || name == "int8"){ outType = PlyScalarType::I8; outBytes = 1u; return true; }
    if(name == "uchar" || name == "uint8"){ outType = PlyScalarType::U8; outBytes = 1u; return true; }
    if(name == "short" || name == "int16"){ outType = PlyScalarType::I16; outBytes = 2u; return true; }
    if(name == "ushort" || name == "uint16"){ outType = PlyScalarType::U16; outBytes = 2u; return true; }
    if(name == "int" || name == "int32"){ outType = PlyScalarType::I32; outBytes = 4u; return true; }
    if(name == "uint" || name == "uint32"){ outType = PlyScalarType::U32; outBytes = 4u; return true; }
    if(name == "float" || name == "float32"){ outType = PlyScalarType::F32; outBytes = 4u; return true; }
    if(name == "double" || name == "float64"){ outType = PlyScalarType::F64; outBytes = 8u; return true; }
    return false;
}

[[nodiscard]] f64 ReadPlyBinaryScalarAsF64(const u8* p, const PlyScalarType type, const b8 bigEndian)noexcept{
    if(type == PlyScalarType::I8)
        return static_cast<f64>(static_cast<i8>(p[0]));
    if(type == PlyScalarType::U8)
        return static_cast<f64>(p[0]);
    if(type == PlyScalarType::I16)
        return static_cast<f64>(static_cast<i16>(bigEndian ? ReadBe16(p) : ReadLe16(p)));
    if(type == PlyScalarType::U16)
        return static_cast<f64>(bigEndian ? ReadBe16(p) : ReadLe16(p));
    if(type == PlyScalarType::I32)
        return static_cast<f64>(static_cast<i32>(bigEndian ? ReadBe32(p) : ReadLe32(p)));
    if(type == PlyScalarType::U32)
        return static_cast<f64>(bigEndian ? ReadBe32(p) : ReadLe32(p));
    if(type == PlyScalarType::F32)
        return static_cast<f64>(ReadFloat32(p, bigEndian));
    return ReadFloat64(p, bigEndian);
}

[[nodiscard]] b8 ReadPlyBinaryScalarAsI64(const u8* p, const PlyScalarType type, const b8 bigEndian, i64& outVal)noexcept{
    if(type == PlyScalarType::I8){ outVal = static_cast<i64>(static_cast<i8>(p[0])); return true; }
    if(type == PlyScalarType::U8){ outVal = static_cast<i64>(p[0]); return true; }
    if(type == PlyScalarType::I16){ outVal = static_cast<i64>(static_cast<i16>(bigEndian ? ReadBe16(p) : ReadLe16(p))); return true; }
    if(type == PlyScalarType::U16){ outVal = static_cast<i64>(bigEndian ? ReadBe16(p) : ReadLe16(p)); return true; }
    if(type == PlyScalarType::I32){ outVal = static_cast<i64>(static_cast<i32>(bigEndian ? ReadBe32(p) : ReadLe32(p))); return true; }
    if(type == PlyScalarType::U32){ outVal = static_cast<i64>(bigEndian ? ReadBe32(p) : ReadLe32(p)); return true; }
    return false;
}

[[nodiscard]] b8 LoadPlyMesh(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> bytes(scratch);
    if(!ReadBytesUtf8(filePathUtf8, scratch, bytes) || bytes.empty())
        return false;

    struct PlyProperty{
        AString<Core::Alloc::ScratchArena> name;
        b8 isList = false;
        PlyScalarType countType = PlyScalarType::U8;
        usize countBytes = 1u;
        PlyScalarType valueType = PlyScalarType::F32;
        usize valueBytes = 4u;
        explicit PlyProperty(Core::Alloc::ScratchArena& arena)noexcept : name(arena){}
    };
    struct PlyElement{
        AString<Core::Alloc::ScratchArena> name;
        usize count = 0u;
        Vector<PlyProperty, Core::Alloc::ScratchArena> properties;
        explicit PlyElement(Core::Alloc::ScratchArena& arena)noexcept : name(arena), properties(arena){}
    };

    const char* data = reinterpret_cast<const char*>(bytes.data());
    const char* end = data + bytes.size();
    const char* cur = data;

    const auto nextLine = [&cur, end](AStringView& outLine) -> b8{
        if(cur >= end)
            return false;
        const char* lineEnd = cur;
        while(lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            ++lineEnd;
        outLine = AStringView(cur, static_cast<usize>(lineEnd - cur));
        cur = lineEnd;
        if(cur < end && *cur == '\r')
            ++cur;
        if(cur < end && *cur == '\n')
            ++cur;
        return true;
    };

    AStringView firstLine;
    if(!nextLine(firstLine) || TrimAsciiView(firstLine) != "ply")
        return false;

    enum class PlyFormat : u8{ Unknown = 0u, Ascii, BinaryLE, BinaryBE };
    PlyFormat format = PlyFormat::Unknown;
    Vector<PlyElement, Core::Alloc::ScratchArena> elements(scratch);
    b8 seenEndHeader = false;

    AStringView line;
    while(nextLine(line)){
        const char* lp = line.data();
        const char* le = lp + line.size();
        AStringView tok;
        if(!ParseAsciiStringToken(lp, le, tok))
            continue;
        if(tok == "comment" || tok == "obj_info")
            continue;
        if(tok == "end_header"){
            seenEndHeader = true;
            break;
        }
        if(tok == "format"){
            AStringView fmtName;
            AStringView fmtVer;
            if(!ParseAsciiStringToken(lp, le, fmtName) || !ParseAsciiStringToken(lp, le, fmtVer))
                return false;
            if(fmtVer != "1.0")
                return false;
            if(fmtName == "ascii")
                format = PlyFormat::Ascii;
            else if(fmtName == "binary_little_endian")
                format = PlyFormat::BinaryLE;
            else if(fmtName == "binary_big_endian")
                format = PlyFormat::BinaryBE;
            else
                return false;
        }
        else if(tok == "element"){
            AStringView elemName;
            u64 elemCount = 0u;
            if(!ParseAsciiStringToken(lp, le, elemName) || !ParseAsciiU64Token(lp, le, elemCount))
                return false;
            if(elemCount > static_cast<u64>(Limit<usize>::s_Max))
                return false;
            elements.emplace_back(scratch);
            elements.back().name.assign(elemName.data(), elemName.size());
            elements.back().count = static_cast<usize>(elemCount);
        }
        else if(tok == "property"){
            if(elements.empty())
                return false;
            AStringView typeTok;
            if(!ParseAsciiStringToken(lp, le, typeTok))
                return false;
            PlyProperty prop(scratch);
            if(typeTok == "list"){
                AStringView cntTok;
                AStringView valTok;
                AStringView nameTok;
                if(!ParseAsciiStringToken(lp, le, cntTok) || !ParseAsciiStringToken(lp, le, valTok) || !ParseAsciiStringToken(lp, le, nameTok))
                    return false;
                if(!ParsePlyScalarType(cntTok, prop.countType, prop.countBytes) || !ParsePlyScalarType(valTok, prop.valueType, prop.valueBytes))
                    return false;
                prop.isList = true;
                prop.name.assign(nameTok.data(), nameTok.size());
            }
            else{
                AStringView nameTok;
                if(!ParseAsciiStringToken(lp, le, nameTok))
                    return false;
                if(!ParsePlyScalarType(typeTok, prop.valueType, prop.valueBytes))
                    return false;
                prop.isList = false;
                prop.name.assign(nameTok.data(), nameTok.size());
            }
            elements.back().properties.push_back(Move(prop));
        }
        else{
            return false;
        }
    }

    if(!seenEndHeader || format == PlyFormat::Unknown)
        return false;

    Vector<u32, Core::Alloc::ScratchArena> faceVerts(scratch);
    if(format == PlyFormat::Ascii){
        for(usize ei = 0u; ei < elements.size(); ++ei){
            const PlyElement& elem = elements[ei];
            const AStringView eName(elem.name.data(), elem.name.size());
            const b8 isVertex = (eName == "vertex");
            const b8 isFace = (eName == "face");
            usize xIdx = Limit<usize>::s_Max;
            usize yIdx = Limit<usize>::s_Max;
            usize zIdx = Limit<usize>::s_Max;
            usize faceListIdx = Limit<usize>::s_Max;
            if(isVertex){
                for(usize pi = 0u; pi < elem.properties.size(); ++pi){
                    const AStringView pName(elem.properties[pi].name.data(), elem.properties[pi].name.size());
                    if(pName == "x") xIdx = pi;
                    else if(pName == "y") yIdx = pi;
                    else if(pName == "z") zIdx = pi;
                }
                if(xIdx == Limit<usize>::s_Max || yIdx == Limit<usize>::s_Max || zIdx == Limit<usize>::s_Max)
                    return false;
            }
            else if(isFace){
                for(usize pi = 0u; pi < elem.properties.size(); ++pi){
                    const AStringView pName(elem.properties[pi].name.data(), elem.properties[pi].name.size());
                    if(elem.properties[pi].isList && (pName == "vertex_indices" || pName == "vertex_index"))
                        faceListIdx = pi;
                }
            }

            for(usize item = 0u; item < elem.count; ++item){
                f32 vx = 0.0f;
                f32 vy = 0.0f;
                f32 vz = 0.0f;
                for(usize pi = 0u; pi < elem.properties.size(); ++pi){
                    const PlyProperty& prop = elem.properties[pi];
                    if(prop.isList){
                        u64 listLen = 0u;
                        if(!ParseAsciiU64Token(cur, end, listLen))
                            return false;
                        if(isFace && pi == faceListIdx){
                            faceVerts.clear();
                            faceVerts.reserve(static_cast<usize>(listLen));
                            for(u64 li = 0u; li < listLen; ++li){
                                i64 idxVal = 0;
                                if(!ParseAsciiI64Token(cur, end, idxVal) || idxVal < 0 || static_cast<u64>(idxVal) >= static_cast<u64>(outVerts.size()))
                                    return false;
                                faceVerts.push_back(static_cast<u32>(idxVal));
                            }
                            if(faceVerts.size() >= 3u && !TriangulatePolygonEarClip(Span<const Float4>(outVerts.data(), outVerts.size()), Span<const u32>(faceVerts.data(), faceVerts.size()), scratch, outIndices))
                                return false;
                        }
                        else{
                            AStringView dummy;
                            for(u64 li = 0u; li < listLen; ++li){
                                if(!ParseAsciiStringToken(cur, end, dummy))
                                    return false;
                            }
                        }
                    }
                    else if(isVertex && (pi == xIdx || pi == yIdx || pi == zIdx)){
                        f32 fv = 0.0f;
                        if(!ParseAsciiFloatToken(cur, end, fv))
                            return false;
                        if(pi == xIdx) vx = fv;
                        else if(pi == yIdx) vy = fv;
                        else vz = fv;
                    }
                    else{
                        AStringView dummy;
                        if(!ParseAsciiStringToken(cur, end, dummy))
                            return false;
                    }
                }
                if(isVertex){
                    if(outVerts.size() >= static_cast<usize>(Limit<u32>::s_Max))
                        return false;
                    outVerts.push_back(Float4(vx, vy, vz, 0.0f));
                }
            }
        }
        return !outVerts.empty() && outIndices.size() >= 3u;
    }

    const b8 bigEndian = (format == PlyFormat::BinaryBE);
    usize byteCursor = static_cast<usize>(cur - data);
    for(usize ei = 0u; ei < elements.size(); ++ei){
        const PlyElement& elem = elements[ei];
        const AStringView eName(elem.name.data(), elem.name.size());
        const b8 isVertex = (eName == "vertex");
        const b8 isFace = (eName == "face");
        usize xIdx = Limit<usize>::s_Max;
        usize yIdx = Limit<usize>::s_Max;
        usize zIdx = Limit<usize>::s_Max;
        usize faceListIdx = Limit<usize>::s_Max;
        if(isVertex){
            for(usize pi = 0u; pi < elem.properties.size(); ++pi){
                const AStringView pName(elem.properties[pi].name.data(), elem.properties[pi].name.size());
                if(pName == "x") xIdx = pi;
                else if(pName == "y") yIdx = pi;
                else if(pName == "z") zIdx = pi;
            }
            if(xIdx == Limit<usize>::s_Max || yIdx == Limit<usize>::s_Max || zIdx == Limit<usize>::s_Max)
                return false;
        }
        else if(isFace){
            for(usize pi = 0u; pi < elem.properties.size(); ++pi){
                const AStringView pName(elem.properties[pi].name.data(), elem.properties[pi].name.size());
                if(elem.properties[pi].isList && (pName == "vertex_indices" || pName == "vertex_index"))
                    faceListIdx = pi;
            }
        }

        for(usize item = 0u; item < elem.count; ++item){
            f32 vx = 0.0f;
            f32 vy = 0.0f;
            f32 vz = 0.0f;
            for(usize pi = 0u; pi < elem.properties.size(); ++pi){
                const PlyProperty& prop = elem.properties[pi];
                if(prop.isList){
                    if(prop.countBytes > bytes.size() - byteCursor)
                        return false;
                    i64 listLen = 0;
                    if(!ReadPlyBinaryScalarAsI64(bytes.data() + byteCursor, prop.countType, bigEndian, listLen) || listLen < 0)
                        return false;
                    byteCursor += prop.countBytes;
                    const u64 elemTotalBytes = static_cast<u64>(listLen) * static_cast<u64>(prop.valueBytes);
                    if(elemTotalBytes > static_cast<u64>(bytes.size() - byteCursor))
                        return false;
                    if(isFace && pi == faceListIdx){
                        faceVerts.clear();
                        faceVerts.reserve(static_cast<usize>(listLen));
                        for(i64 li = 0; li < listLen; ++li){
                            i64 idxVal = 0;
                            if(!ReadPlyBinaryScalarAsI64(bytes.data() + byteCursor + static_cast<usize>(li) * prop.valueBytes, prop.valueType, bigEndian, idxVal)
                                || idxVal < 0
                                || static_cast<u64>(idxVal) >= static_cast<u64>(outVerts.size()))
                                return false;
                            faceVerts.push_back(static_cast<u32>(idxVal));
                        }
                        if(faceVerts.size() >= 3u && !TriangulatePolygonEarClip(Span<const Float4>(outVerts.data(), outVerts.size()), Span<const u32>(faceVerts.data(), faceVerts.size()), scratch, outIndices))
                            return false;
                    }
                    byteCursor += static_cast<usize>(elemTotalBytes);
                }
                else{
                    if(prop.valueBytes > bytes.size() - byteCursor)
                        return false;
                    if(isVertex && (pi == xIdx || pi == yIdx || pi == zIdx)){
                        const f64 dv = ReadPlyBinaryScalarAsF64(bytes.data() + byteCursor, prop.valueType, bigEndian);
                        const f32 fv = static_cast<f32>(dv);
                        if(!std::isfinite(fv))
                            return false;
                        if(pi == xIdx) vx = fv;
                        else if(pi == yIdx) vy = fv;
                        else vz = fv;
                    }
                    byteCursor += prop.valueBytes;
                }
            }
            if(isVertex){
                if(outVerts.size() >= static_cast<usize>(Limit<u32>::s_Max))
                    return false;
                outVerts.push_back(Float4(vx, vy, vz, 0.0f));
            }
        }
    }

    return !outVerts.empty() && outIndices.size() >= 3u;
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

[[nodiscard]] b8 ReadGltfJsonDocument(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    Vector<u8, Core::Alloc::ScratchArena>& outFileBytes,
    Span<const u8>& outGlbBinSpan,
    b8& outHasGlbBin,
    nlohmann::json& outDoc
)noexcept{
    outGlbBinSpan = {};
    outHasGlbBin = false;
    if(!ReadBytesUtf8(filePathUtf8, scratch, outFileBytes) || outFileBytes.size() < 4u)
        return false;

    const u8* jsonStart = outFileBytes.data();
    const u8* jsonEnd = outFileBytes.data() + outFileBytes.size();

    if(outFileBytes.size() >= 12u && ReadLe32(outFileBytes.data()) == 0x46546C67u){
        const u32 version = ReadLe32(outFileBytes.data() + 4u);
        const u32 totalLength = ReadLe32(outFileBytes.data() + 8u);
        if(version != 2u || totalLength < 20u || totalLength > outFileBytes.size())
            return false;

        usize cursor = 12u;
        b8 foundJson = false;
        while(cursor + 8u <= totalLength){
            const u32 chunkLen = ReadLe32(outFileBytes.data() + cursor);
            const u32 chunkType = ReadLe32(outFileBytes.data() + cursor + 4u);
            cursor += 8u;
            if(chunkLen > totalLength - cursor)
                return false;
            if(chunkType == 0x4E4F534Au && !foundJson){
                jsonStart = outFileBytes.data() + cursor;
                jsonEnd = jsonStart + chunkLen;
                foundJson = true;
            }
            else if(chunkType == 0x004E4942u && !outHasGlbBin){
                outGlbBinSpan = Span<const u8>(outFileBytes.data() + cursor, chunkLen);
                outHasGlbBin = true;
            }
            cursor += chunkLen;
        }
        if(!foundJson)
            return false;
    }

    outDoc = nlohmann::json::parse(jsonStart, jsonEnd, nullptr, false);
    return !outDoc.is_discarded() && outDoc.is_object();
}

[[nodiscard]] b8 ResolveGltfBufferViewSpan(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize viewIndex,
    const usize accOffset,
    const usize minElemBytes,
    const usize count,
    const b8 allowByteStride,
    Span<const u8>& outBuf,
    usize& outByteOffset,
    usize& outStride
)noexcept{
    if(!doc.contains("bufferViews") || !doc["bufferViews"].is_array() || viewIndex >= doc["bufferViews"].size())
        return false;
    const nlohmann::json& view = doc["bufferViews"][viewIndex];
    if(!view.is_object() || !view.contains("buffer") || !view["buffer"].is_number_unsigned() || !view.contains("byteLength") || !view["byteLength"].is_number_unsigned())
        return false;
    const usize bufferIndex = view["buffer"].get<usize>();
    if(bufferIndex >= bufferSpans.size())
        return false;

    const usize viewOffset = view.value("byteOffset", usize(0u));
    const usize viewLength = view["byteLength"].get<usize>();
    const usize stride = allowByteStride ? view.value("byteStride", minElemBytes) : minElemBytes;
    if(stride < minElemBytes)
        return false;

    const Span<const u8> buf = bufferSpans[bufferIndex];
    if(viewOffset > buf.size() || viewLength > buf.size() - viewOffset)
        return false;
    if(accOffset > viewLength)
        return false;
    if(count > 0u){
        if(minElemBytes > viewLength - accOffset)
            return false;
        if((count - 1u) > (viewLength - accOffset - minElemBytes) / stride)
            return false;
    }

    outBuf = buf;
    outByteOffset = viewOffset + accOffset;
    outStride = stride;
    return true;
}

[[nodiscard]] b8 ReadGltfComponentElementSize(const u32 compType, const usize numComponents, usize& outBytes)noexcept{
    usize compBytes = 0u;
    if(compType == 5120u || compType == 5121u)
        compBytes = 1u;
    else if(compType == 5122u || compType == 5123u)
        compBytes = 2u;
    else if(compType == 5125u || compType == 5126u)
        compBytes = 4u;
    else
        return false;
    outBytes = compBytes * numComponents;
    return true;
}

[[nodiscard]] b8 ReadGltfPositionComponentAsF32(const u8* p, const u32 compType, const b8 normalized, f32& outVal)noexcept{
    if(compType == 5126u){
        outVal = ReadFloat32(p, false);
        return std::isfinite(outVal);
    }
    if(compType == 5120u){
        const i8 v = static_cast<i8>(p[0]);
        outVal = normalized ? ((static_cast<f32>(v) / 127.0f < -1.0f) ? -1.0f : static_cast<f32>(v) / 127.0f) : static_cast<f32>(v);
        return true;
    }
    if(compType == 5121u){
        const u8 v = p[0];
        outVal = normalized ? (static_cast<f32>(v) / 255.0f) : static_cast<f32>(v);
        return true;
    }
    if(compType == 5122u){
        const i16 v = static_cast<i16>(ReadLe16(p));
        const f32 nf = static_cast<f32>(v) / 32767.0f;
        outVal = normalized ? ((nf < -1.0f) ? -1.0f : nf) : static_cast<f32>(v);
        return true;
    }
    if(compType == 5123u){
        const u16 v = ReadLe16(p);
        outVal = normalized ? (static_cast<f32>(v) / 65535.0f) : static_cast<f32>(v);
        return true;
    }
    return false;
}

[[nodiscard]] b8 ReadGltfScalarUintAt(const u8* p, const u32 compType, u32& outVal)noexcept{
    if(compType == 5121u){ outVal = static_cast<u32>(p[0]); return true; }
    if(compType == 5123u){ outVal = static_cast<u32>(ReadLe16(p)); return true; }
    if(compType == 5125u){ outVal = ReadLe32(p); return true; }
    return false;
}

[[nodiscard]] b8 DecodeGltfVec3Positions(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize accIndex,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outPositions
)noexcept{
    outPositions.clear();
    if(!doc.contains("accessors") || !doc["accessors"].is_array() || accIndex >= doc["accessors"].size())
        return false;
    const nlohmann::json& acc = doc["accessors"][accIndex];
    if(!acc.is_object() || !acc.contains("componentType") || !acc["componentType"].is_number_unsigned() || !acc.contains("count") || !acc["count"].is_number_unsigned() || !acc.contains("type") || !acc["type"].is_string())
        return false;
    const std::string& typeStr = acc["type"].get_ref<const std::string&>();
    if(AStringView(typeStr.data(), typeStr.size()) != "VEC3")
        return false;

    const u32 compType = acc["componentType"].get<u32>();
    if(compType != 5126u && compType != 5120u && compType != 5121u && compType != 5122u && compType != 5123u)
        return false;
    const usize count = acc["count"].get<usize>();
    if(count == 0u || count > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    const b8 normalized = acc.value("normalized", false);

    usize elemBytes = 0u;
    if(!ReadGltfComponentElementSize(compType, 3u, elemBytes))
        return false;
    const usize compStep = elemBytes / 3u;
    const b8 hasSparse = acc.contains("sparse");
    if(!acc.contains("bufferView") && !hasSparse)
        return false;

    outPositions.resize(count, Float4(0.0f, 0.0f, 0.0f, 0.0f));
    if(acc.contains("bufferView")){
        if(!acc["bufferView"].is_number_unsigned())
            return false;
        Span<const u8> buf;
        usize byteOffset = 0u;
        usize stride = 0u;
        const usize accOffset = acc.value("byteOffset", usize(0u));
        if(!ResolveGltfBufferViewSpan(doc, bufferSpans, acc["bufferView"].get<usize>(), accOffset, elemBytes, count, true, buf, byteOffset, stride))
            return false;
        for(usize i = 0u; i < count; ++i){
            const u8* vp = buf.data() + byteOffset + i * stride;
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            if(!ReadGltfPositionComponentAsF32(vp, compType, normalized, x)
                || !ReadGltfPositionComponentAsF32(vp + compStep, compType, normalized, y)
                || !ReadGltfPositionComponentAsF32(vp + compStep * 2u, compType, normalized, z))
                return false;
            outPositions[i] = Float4(x, y, z, 0.0f);
        }
    }

    if(hasSparse){
        const nlohmann::json& sp = acc["sparse"];
        if(!sp.is_object() || !sp.contains("count") || !sp["count"].is_number_unsigned() || !sp.contains("indices") || !sp.contains("values"))
            return false;
        const usize spCount = sp["count"].get<usize>();
        if(spCount > count)
            return false;
        const nlohmann::json& spIdx = sp["indices"];
        const nlohmann::json& spVal = sp["values"];
        if(!spIdx.is_object() || !spVal.is_object() || !spIdx.contains("bufferView") || !spIdx["bufferView"].is_number_unsigned() || !spIdx.contains("componentType") || !spIdx["componentType"].is_number_unsigned() || !spVal.contains("bufferView") || !spVal["bufferView"].is_number_unsigned())
            return false;
        const u32 spIdxComp = spIdx["componentType"].get<u32>();
        usize spIdxBytes = 0u;
        if(spIdxComp != 5121u && spIdxComp != 5123u && spIdxComp != 5125u)
            return false;
        if(!ReadGltfComponentElementSize(spIdxComp, 1u, spIdxBytes))
            return false;

        Span<const u8> idxBuf;
        usize idxOffset = 0u;
        usize idxStride = 0u;
        if(!ResolveGltfBufferViewSpan(doc, bufferSpans, spIdx["bufferView"].get<usize>(), spIdx.value("byteOffset", usize(0u)), spIdxBytes, spCount, false, idxBuf, idxOffset, idxStride))
            return false;

        Span<const u8> valBuf;
        usize valOffset = 0u;
        usize valStride = 0u;
        if(!ResolveGltfBufferViewSpan(doc, bufferSpans, spVal["bufferView"].get<usize>(), spVal.value("byteOffset", usize(0u)), elemBytes, spCount, false, valBuf, valOffset, valStride))
            return false;

        for(usize si = 0u; si < spCount; ++si){
            u32 targetIdx = 0u;
            if(!ReadGltfScalarUintAt(idxBuf.data() + idxOffset + si * idxStride, spIdxComp, targetIdx) || static_cast<usize>(targetIdx) >= count)
                return false;
            const u8* vp = valBuf.data() + valOffset + si * valStride;
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            if(!ReadGltfPositionComponentAsF32(vp, compType, normalized, x)
                || !ReadGltfPositionComponentAsF32(vp + compStep, compType, normalized, y)
                || !ReadGltfPositionComponentAsF32(vp + compStep * 2u, compType, normalized, z))
                return false;
            outPositions[targetIdx] = Float4(x, y, z, 0.0f);
        }
    }

    (void)scratch;
    return true;
}

[[nodiscard]] b8 DecodeGltfScalarIndices(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize accIndex,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices
)noexcept{
    outIndices.clear();
    if(!doc.contains("accessors") || !doc["accessors"].is_array() || accIndex >= doc["accessors"].size())
        return false;
    const nlohmann::json& acc = doc["accessors"][accIndex];
    if(!acc.is_object() || !acc.contains("componentType") || !acc["componentType"].is_number_unsigned() || !acc.contains("count") || !acc["count"].is_number_unsigned() || !acc.contains("type") || !acc["type"].is_string())
        return false;
    const std::string& typeStr = acc["type"].get_ref<const std::string&>();
    if(AStringView(typeStr.data(), typeStr.size()) != "SCALAR")
        return false;

    const u32 compType = acc["componentType"].get<u32>();
    if(compType != 5121u && compType != 5123u && compType != 5125u)
        return false;
    const usize count = acc["count"].get<usize>();
    if(count == 0u)
        return true;
    usize elemBytes = 0u;
    if(!ReadGltfComponentElementSize(compType, 1u, elemBytes))
        return false;
    const b8 hasSparse = acc.contains("sparse");
    if(!acc.contains("bufferView") && !hasSparse)
        return false;

    outIndices.resize(count, 0u);
    if(acc.contains("bufferView")){
        if(!acc["bufferView"].is_number_unsigned())
            return false;
        Span<const u8> buf;
        usize byteOffset = 0u;
        usize stride = 0u;
        if(!ResolveGltfBufferViewSpan(doc, bufferSpans, acc["bufferView"].get<usize>(), acc.value("byteOffset", usize(0u)), elemBytes, count, true, buf, byteOffset, stride))
            return false;
        for(usize i = 0u; i < count; ++i){
            if(!ReadGltfScalarUintAt(buf.data() + byteOffset + i * stride, compType, outIndices[i]))
                return false;
        }
    }

    if(hasSparse){
        const nlohmann::json& sp = acc["sparse"];
        if(!sp.is_object() || !sp.contains("count") || !sp["count"].is_number_unsigned() || !sp.contains("indices") || !sp.contains("values"))
            return false;
        const usize spCount = sp["count"].get<usize>();
        if(spCount > count)
            return false;
        const nlohmann::json& spIdx = sp["indices"];
        const nlohmann::json& spVal = sp["values"];
        if(!spIdx.is_object() || !spVal.is_object() || !spIdx.contains("bufferView") || !spIdx["bufferView"].is_number_unsigned() || !spIdx.contains("componentType") || !spIdx["componentType"].is_number_unsigned() || !spVal.contains("bufferView") || !spVal["bufferView"].is_number_unsigned())
            return false;
        const u32 spIdxComp = spIdx["componentType"].get<u32>();
        usize spIdxBytes = 0u;
        if(spIdxComp != 5121u && spIdxComp != 5123u && spIdxComp != 5125u)
            return false;
        if(!ReadGltfComponentElementSize(spIdxComp, 1u, spIdxBytes))
            return false;

        Span<const u8> idxBuf;
        usize idxOffset = 0u;
        usize idxStride = 0u;
        if(!ResolveGltfBufferViewSpan(doc, bufferSpans, spIdx["bufferView"].get<usize>(), spIdx.value("byteOffset", usize(0u)), spIdxBytes, spCount, false, idxBuf, idxOffset, idxStride))
            return false;

        Span<const u8> valBuf;
        usize valOffset = 0u;
        usize valStride = 0u;
        if(!ResolveGltfBufferViewSpan(doc, bufferSpans, spVal["bufferView"].get<usize>(), spVal.value("byteOffset", usize(0u)), elemBytes, spCount, false, valBuf, valOffset, valStride))
            return false;

        for(usize si = 0u; si < spCount; ++si){
            u32 targetIdx = 0u;
            u32 targetVal = 0u;
            if(!ReadGltfScalarUintAt(idxBuf.data() + idxOffset + si * idxStride, spIdxComp, targetIdx)
                || static_cast<usize>(targetIdx) >= count
                || !ReadGltfScalarUintAt(valBuf.data() + valOffset + si * valStride, compType, targetVal))
                return false;
            outIndices[targetIdx] = targetVal;
        }
    }
    return true;
}

[[nodiscard]] b8 AppendGltfMesh(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize meshIndex,
    const SIMDMatrix& worldMatrix,
    const AStringView submeshName,
    const b8 nodeMatched,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices,
    Vector<u16, Core::Alloc::ScratchArena>& outTriMats,
    b8& inOutHasMaterials
)noexcept{
    if(!doc.contains("meshes") || !doc["meshes"].is_array() || meshIndex >= doc["meshes"].size())
        return false;
    const nlohmann::json& mesh = doc["meshes"][meshIndex];
    if(!mesh.is_object() || !mesh.contains("primitives") || !mesh["primitives"].is_array())
        return false;

    b8 meshMatched = nodeMatched || submeshName.empty();
    if(!meshMatched && mesh.contains("name") && mesh["name"].is_string()){
        const std::string& mName = mesh["name"].get_ref<const std::string&>();
        if(AStringView(mName.data(), mName.size()) == submeshName)
            meshMatched = true;
    }
    if(!meshMatched)
        return true;

    Vector<Float4, Core::Alloc::ScratchArena> localPositions(scratch);
    Vector<u32, Core::Alloc::ScratchArena> rawIndices(scratch);

    for(const nlohmann::json& prim : mesh["primitives"]){
        if(!prim.is_object())
            return false;
        i64 mode = 4;
        if(prim.contains("mode")){
            if(!prim["mode"].is_number_integer())
                return false;
            mode = prim["mode"].get<i64>();
        }
        if(mode != 4 && mode != 5 && mode != 6)
            continue;
        if(!prim.contains("attributes") || !prim["attributes"].is_object())
            return false;
        const nlohmann::json& attrs = prim["attributes"];
        if(!attrs.contains("POSITION") || !attrs["POSITION"].is_number_unsigned())
            continue;

        const usize posAccIndex = attrs["POSITION"].get<usize>();
        if(!DecodeGltfVec3Positions(doc, bufferSpans, posAccIndex, scratch, localPositions))
            return false;
        const usize posCount = localPositions.size();
        if(posCount > static_cast<usize>(Limit<u32>::s_Max) - outVerts.size())
            return false;

        u16 primMatSlot = 0u;
        if(prim.contains("material")){
            if(!prim["material"].is_number_unsigned())
                return false;
            const usize matIdx = prim["material"].get<usize>();
            if(matIdx > static_cast<usize>(Limit<u16>::s_Max))
                return false;
            inOutHasMaterials = true;
            primMatSlot = static_cast<u16>(matIdx);
        }

        const u32 baseVertex = static_cast<u32>(outVerts.size());
        const usize oldVertSize = outVerts.size();
        outVerts.resize(oldVertSize + posCount, Float4(0.0f, 0.0f, 0.0f, 0.0f));
        for(usize vi = 0u; vi < posCount; ++vi){
            const Float4& lp = localPositions[vi];
            const SIMDVectorSingle worldPt = Vector3Transform(VectorSet(lp.x, lp.y, lp.z, 1.0f), worldMatrix);
            const f32 wx = VectorGetX(worldPt);
            const f32 wy = VectorGetY(worldPt);
            const f32 wz = VectorGetZ(worldPt);
            if(!std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz))
                return false;
            outVerts[oldVertSize + vi] = Float4(wx, wy, wz, 0.0f);
        }

        if(prim.contains("indices")){
            if(!prim["indices"].is_number_unsigned())
                return false;
            if(!DecodeGltfScalarIndices(doc, bufferSpans, prim["indices"].get<usize>(), rawIndices))
                return false;
            for(usize ii = 0u; ii < rawIndices.size(); ++ii){
                if(static_cast<usize>(rawIndices[ii]) >= posCount)
                    return false;
                rawIndices[ii] = baseVertex + rawIndices[ii];
            }
        }
        else{
            rawIndices.resize(posCount);
            for(usize ii = 0u; ii < posCount; ++ii)
                rawIndices[ii] = baseVertex + static_cast<u32>(ii);
        }

        const usize beforeIdxCount = outIndices.size();
        const usize rc = rawIndices.size();
        if(mode == 4){
            if((rc % 3u) != 0u)
                return false;
            outIndices.reserve(beforeIdxCount + rc);
            for(usize ii = 0u; ii < rc; ++ii)
                outIndices.push_back(rawIndices[ii]);
        }
        else if(mode == 5){
            if(rc >= 3u){
                outIndices.reserve(beforeIdxCount + (rc - 2u) * 3u);
                for(usize si = 0u; si + 2u < rc; ++si){
                    const u32 a = ((si & 1u) == 0u) ? rawIndices[si] : rawIndices[si + 1u];
                    const u32 b = ((si & 1u) == 0u) ? rawIndices[si + 1u] : rawIndices[si];
                    const u32 c = rawIndices[si + 2u];
                    if(a == b || b == c || c == a)
                        continue;
                    outIndices.push_back(a);
                    outIndices.push_back(b);
                    outIndices.push_back(c);
                }
            }
        }
        else{
            if(rc >= 3u){
                outIndices.reserve(beforeIdxCount + (rc - 2u) * 3u);
                for(usize fi = 1u; fi + 1u < rc; ++fi){
                    const u32 a = rawIndices[0u];
                    const u32 b = rawIndices[fi];
                    const u32 c = rawIndices[fi + 1u];
                    if(a == b || b == c || c == a)
                        continue;
                    outIndices.push_back(a);
                    outIndices.push_back(b);
                    outIndices.push_back(c);
                }
            }
        }

        const usize beforeTriCount = beforeIdxCount / 3u;
        if(inOutHasMaterials){
            const usize totalTris = outIndices.size() / 3u;
            if(outTriMats.size() < beforeTriCount)
                outTriMats.resize(beforeTriCount, 0u);
            outTriMats.resize(totalTris, primMatSlot);
        }
    }
    return true;
}

[[nodiscard]] b8 TraverseGltfNode(
    const nlohmann::json& doc,
    const Vector<Span<const u8>, Core::Alloc::ScratchArena>& bufferSpans,
    const usize nodeIndex,
    const SIMDMatrix& parentWorld,
    const AStringView submeshName,
    const b8 ancestorMatched,
    Vector<u8, Core::Alloc::ScratchArena>& visited,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices,
    Vector<u16, Core::Alloc::ScratchArena>& outTriMats,
    b8& inOutHasMaterials
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

    b8 nodeMatched = ancestorMatched || submeshName.empty();
    if(!nodeMatched && nodeJson.contains("name") && nodeJson["name"].is_string()){
        const std::string& nName = nodeJson["name"].get_ref<const std::string&>();
        if(AStringView(nName.data(), nName.size()) == submeshName)
            nodeMatched = true;
    }

    if(nodeJson.contains("mesh")){
        if(!nodeJson["mesh"].is_number_unsigned())
            return false;
        if(!AppendGltfMesh(doc, bufferSpans, nodeJson["mesh"].get<usize>(), worldMat, submeshName, nodeMatched, scratch, outVerts, outIndices, outTriMats, inOutHasMaterials))
            return false;
    }

    if(nodeJson.contains("children")){
        if(!nodeJson["children"].is_array())
            return false;
        for(const nlohmann::json& childVal : nodeJson["children"]){
            if(!childVal.is_number_unsigned())
                return false;
            if(!TraverseGltfNode(doc, bufferSpans, childVal.get<usize>(), worldMat, submeshName, nodeMatched, visited, scratch, outVerts, outIndices, outTriMats, inOutHasMaterials))
                return false;
        }
    }

    visited[nodeIndex] = 2u;
    return true;
}

[[nodiscard]] b8 LoadGltfOrGlbMesh(
    const AStringView filePathUtf8,
    const AStringView submeshName,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outIndices,
    Vector<u16, Core::Alloc::ScratchArena>& outTriMats
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> fileBytes(scratch);
    Span<const u8> glbBinSpan;
    b8 hasGlbBin = false;
    nlohmann::json doc;
    if(!ReadGltfJsonDocument(filePathUtf8, scratch, fileBytes, glbBinSpan, hasGlbBin, doc))
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

    b8 hasMaterials = false;
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

        for(const u32 rootIdx : rootNodes){
            if(!TraverseGltfNode(doc, bufferSpans, rootIdx, MatrixIdentity(), submeshName, false, visited, scratch, outVerts, outIndices, outTriMats, hasMaterials))
                return false;
        }
    }
    else{
        for(usize mi = 0u; mi < doc["meshes"].size(); ++mi){
            if(!AppendGltfMesh(doc, bufferSpans, mi, MatrixIdentity(), submeshName, false, scratch, outVerts, outIndices, outTriMats, hasMaterials))
                return false;
        }
    }

    if(!hasMaterials)
        outTriMats.clear();
    if(!submeshName.empty())
        CompactReferencedVertices(outVerts, outIndices, scratch);
    return !outVerts.empty() && outIndices.size() >= 3u;
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
    u8 interlaceMethod = 0u;
    void* decompressed = nullptr;

    ~ParsedPng16(){
        if(decompressed)
            mz_free(decompressed);
    }
};

[[nodiscard]] b8 ComputePng16ExpectedRawSize(
    const u32 width,
    const u32 height,
    const u32 channels,
    const u8 interlaceMethod,
    usize& outExpectedRawSize
)noexcept{
    const u64 bpp = static_cast<u64>(channels) * 2u;
    if(interlaceMethod == 0u){
        const u64 stride = 1u + static_cast<u64>(width) * bpp;
        if(stride > static_cast<u64>(Limit<usize>::s_Max) / static_cast<u64>(height))
            return false;
        outExpectedRawSize = static_cast<usize>(stride * static_cast<u64>(height));
        return true;
    }
    if(interlaceMethod == 1u){
        static constexpr u32 s_Adam7X0[7] = { 0u, 4u, 0u, 2u, 0u, 1u, 0u };
        static constexpr u32 s_Adam7Dx[7] = { 8u, 8u, 4u, 4u, 2u, 2u, 1u };
        static constexpr u32 s_Adam7Y0[7] = { 0u, 0u, 4u, 0u, 2u, 0u, 1u };
        static constexpr u32 s_Adam7Dy[7] = { 8u, 8u, 8u, 4u, 4u, 2u, 2u };
        u64 total = 0u;
        for(usize pass = 0u; pass < 7u; ++pass){
            if(s_Adam7X0[pass] >= width || s_Adam7Y0[pass] >= height)
                continue;
            const u64 passW = static_cast<u64>((width - s_Adam7X0[pass] + s_Adam7Dx[pass] - 1u) / s_Adam7Dx[pass]);
            const u64 passH = static_cast<u64>((height - s_Adam7Y0[pass] + s_Adam7Dy[pass] - 1u) / s_Adam7Dy[pass]);
            const u64 stride = 1u + passW * bpp;
            if(stride > static_cast<u64>(Limit<usize>::s_Max) / passH)
                return false;
            const u64 passBytes = stride * passH;
            if(passBytes > static_cast<u64>(Limit<usize>::s_Max) - total)
                return false;
            total += passBytes;
        }
        outExpectedRawSize = static_cast<usize>(total);
        return total > 0u;
    }
    return false;
}

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
    if(compMethod != 0u || filterMethod != 0u || (interlaceMethod != 0u && interlaceMethod != 1u) || idatBytes.empty())
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

    usize expectedRawSize = 0u;
    if(!ComputePng16ExpectedRawSize(width, height, channels, interlaceMethod, expectedRawSize))
        return false;

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
    outPng.interlaceMethod = interlaceMethod;
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

[[nodiscard]] f32 SelectHoleMaskChannelValue(const f32 r, const f32 g, const f32 b, const f32 a, const u32 channelMode)noexcept{
    if(channelMode == 0u) return r;
    if(channelMode == 1u) return g;
    if(channelMode == 2u) return b;
    if(channelMode == 3u) return a;
    const f32 lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    if(channelMode == 4u) return lum;
    return (a < lum) ? a : lum;
}

template<u32 Channels>
void DecodePng16RowToMaskBits(
    const u8* currRow,
    const u32 cz,
    const u32 cellCols,
    const u32 channelMode,
    const f32 threshold,
    u8* dst
)noexcept{
    constexpr f32 s_InvMax = 1.0f / 65535.0f;
    constexpr usize s_Step = static_cast<usize>(Channels) * 2u;
    const usize rowBase = static_cast<usize>(cz) * static_cast<usize>(cellCols);
    for(u32 cx = 0u; cx < cellCols; ++cx){
        const u8* px = currRow + static_cast<usize>(cx) * s_Step;
        f32 r = 0.0f;
        f32 g = 0.0f;
        f32 b = 0.0f;
        f32 a = 1.0f;
        if constexpr(Channels == 1u){
            const f32 gray = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            r = gray;
            g = gray;
            b = gray;
        }
        else if constexpr(Channels == 2u){
            const f32 gray = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            r = gray;
            g = gray;
            b = gray;
            a = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
        }
        else if constexpr(Channels == 3u){
            r = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            g = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
            b = static_cast<f32>(ReadBe16(px + 4u)) * s_InvMax;
        }
        else{
            r = static_cast<f32>(ReadBe16(px + 0u)) * s_InvMax;
            g = static_cast<f32>(ReadBe16(px + 2u)) * s_InvMax;
            b = static_cast<f32>(ReadBe16(px + 4u)) * s_InvMax;
            a = static_cast<f32>(ReadBe16(px + 6u)) * s_InvMax;
        }
        const f32 maskVal = SelectHoleMaskChannelValue(r, g, b, a, channelMode);
        if(maskVal < threshold){
            const usize cellIdx = rowBase + static_cast<usize>(cx);
            dst[cellIdx >> 3u] = static_cast<u8>(dst[cellIdx >> 3u] | static_cast<u8>(1u << (cellIdx & 7u)));
        }
    }
}

template<typename RowFn>
[[nodiscard]] b8 ProcessPng16Rows(
    const ParsedPng16& png,
    const u32 rowCount,
    Core::Alloc::ScratchArena& scratch,
    RowFn&& onRow
)noexcept{
    const usize step = static_cast<usize>(png.channels) * 2u;
    const usize rowBytes = static_cast<usize>(png.width) * step;
    Vector<u8, Core::Alloc::ScratchArena> rowA(rowBytes, 0u, scratch);
    Vector<u8, Core::Alloc::ScratchArena> rowB(rowBytes, 0u, scratch);
    u8* prevPtr = rowA.data();
    u8* currPtr = rowB.data();
    const u8* src = static_cast<const u8*>(png.decompressed);

    if(png.interlaceMethod == 0u){
        for(u32 y = 0u; y < rowCount; ++y){
            const u8* rowSrc = src + static_cast<usize>(y) * (1u + rowBytes);
            if(!UnfilterPng16Scanline(rowSrc[0], rowSrc + 1u, prevPtr, currPtr, rowBytes, step))
                return false;
            onRow(y, currPtr);
            std::swap(prevPtr, currPtr);
        }
        return true;
    }

    static constexpr u32 s_Adam7X0[7] = { 0u, 4u, 0u, 2u, 0u, 1u, 0u };
    static constexpr u32 s_Adam7Dx[7] = { 8u, 8u, 4u, 4u, 2u, 2u, 1u };
    static constexpr u32 s_Adam7Y0[7] = { 0u, 0u, 4u, 0u, 2u, 0u, 1u };
    static constexpr u32 s_Adam7Dy[7] = { 8u, 8u, 8u, 4u, 4u, 2u, 2u };

    const usize totalPixels = static_cast<usize>(png.width) * static_cast<usize>(png.height);
    if(step > Limit<usize>::s_Max / totalPixels)
        return false;
    Vector<u8, Core::Alloc::ScratchArena> fullImage(totalPixels * step, 0u, scratch);
    usize srcOffset = 0u;
    for(usize pass = 0u; pass < 7u; ++pass){
        if(s_Adam7X0[pass] >= png.width || s_Adam7Y0[pass] >= png.height)
            continue;
        const u32 passW = (png.width - s_Adam7X0[pass] + s_Adam7Dx[pass] - 1u) / s_Adam7Dx[pass];
        const u32 passH = (png.height - s_Adam7Y0[pass] + s_Adam7Dy[pass] - 1u) / s_Adam7Dy[pass];
        const usize passRowBytes = static_cast<usize>(passW) * step;
        std::memset(rowA.data(), 0, passRowBytes);
        std::memset(rowB.data(), 0, passRowBytes);
        prevPtr = rowA.data();
        currPtr = rowB.data();

        for(u32 py = 0u; py < passH; ++py){
            const u8* rowSrc = src + srcOffset + static_cast<usize>(py) * (1u + passRowBytes);
            if(!UnfilterPng16Scanline(rowSrc[0], rowSrc + 1u, prevPtr, currPtr, passRowBytes, step))
                return false;
            const u32 dstY = s_Adam7Y0[pass] + py * s_Adam7Dy[pass];
            u8* dstRowPtr = fullImage.data() + static_cast<usize>(dstY) * rowBytes;
            for(u32 px = 0u; px < passW; ++px){
                const u32 dstX = s_Adam7X0[pass] + px * s_Adam7Dx[pass];
                std::memcpy(dstRowPtr + static_cast<usize>(dstX) * step, currPtr + static_cast<usize>(px) * step, step);
            }
            std::swap(prevPtr, currPtr);
        }
        srcOffset += static_cast<usize>(passH) * (1u + passRowBytes);
    }

    for(u32 y = 0u; y < rowCount; ++y)
        onRow(y, fullImage.data() + static_cast<usize>(y) * rowBytes);
    return true;
}

[[nodiscard]] b8 LoadTetSimpleOrHeadered(
    const char* data,
    const char* end,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outTets
)noexcept{
    const char* cur = data;
    b8 headeredParsed = false;
    Vector<AStringView, Core::Alloc::ScratchArena> lines(scratch);
    while(cur < end){
        const char* lineEnd = cur;
        while(lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            ++lineEnd;
        const char* hashPos = cur;
        while(hashPos < lineEnd && *hashPos != '#')
            ++hashPos;
        const AStringView trimmed = TrimAsciiView(AStringView(cur, static_cast<usize>(hashPos - cur)));
        if(!trimmed.empty())
            lines.push_back(trimmed);
        cur = lineEnd;
        while(cur < end && (*cur == '\n' || *cur == '\r'))
            ++cur;
    }
    if(lines.empty())
        return false;

    const char* lp0 = lines[0u].data();
    const char* le0 = lp0 + lines[0u].size();
    AStringView firstTok;
    if(!ParseAsciiStringToken(lp0, le0, firstTok))
        return false;
    if(firstTok != "v" && firstTok != "t"){
        if(lines.size() < 2u)
            return false;
        u64 numVerts = 0u;
        u64 numTets = 0u;
        const char* p0 = lines[0u].data();
        const char* e0 = p0 + lines[0u].size();
        const char* p1 = lines[1u].data();
        const char* e1 = p1 + lines[1u].size();
        if(!ParseAsciiU64Token(p0, e0, numVerts) || !ParseAsciiU64Token(p1, e1, numTets))
            return false;
        if(numVerts < 4u || numTets == 0u || lines.size() < 2u + static_cast<usize>(numVerts) + static_cast<usize>(numTets))
            return false;
        outVerts.reserve(static_cast<usize>(numVerts));
        outTets.reserve(static_cast<usize>(numTets) * 4u);
        for(usize vi = 0u; vi < static_cast<usize>(numVerts); ++vi){
            const char* vp = lines[2u + vi].data();
            const char* ve = vp + lines[2u + vi].size();
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            if(!ParseAsciiFloatToken(vp, ve, x) || !ParseAsciiFloatToken(vp, ve, y) || !ParseAsciiFloatToken(vp, ve, z))
                return false;
            outVerts.push_back(Float4(x, y, z, 0.0f));
        }
        const usize tetBase = 2u + static_cast<usize>(numVerts);
        for(usize ti = 0u; ti < static_cast<usize>(numTets); ++ti){
            const char* tp = lines[tetBase + ti].data();
            const char* te = tp + lines[tetBase + ti].size();
            i64 countOrFirst = 0;
            if(!ParseAsciiI64Token(tp, te, countOrFirst) || countOrFirst != 4)
                return false;
            for(u32 k = 0u; k < 4u; ++k){
                i64 idx = 0;
                if(!ParseAsciiI64Token(tp, te, idx) || idx < 0 || static_cast<u64>(idx) >= numVerts)
                    return false;
                outTets.push_back(static_cast<u32>(idx));
            }
        }
        headeredParsed = true;
    }

    if(headeredParsed)
        return !outVerts.empty() && outTets.size() >= 4u;

    for(usize li = 0u; li < lines.size(); ++li){
        const char* lp = lines[li].data();
        const char* le = lp + lines[li].size();
        AStringView kw;
        if(!ParseAsciiStringToken(lp, le, kw))
            continue;
        if(kw == "v"){
            f32 x = 0.0f;
            f32 y = 0.0f;
            f32 z = 0.0f;
            if(!ParseAsciiFloatToken(lp, le, x) || !ParseAsciiFloatToken(lp, le, y) || !ParseAsciiFloatToken(lp, le, z))
                return false;
            outVerts.push_back(Float4(x, y, z, 0.0f));
        }
        else if(kw == "t"){
            for(u32 k = 0u; k < 4u; ++k){
                i64 idx = 0;
                if(!ParseAsciiI64Token(lp, le, idx) || idx < 0 || static_cast<usize>(idx) >= outVerts.size())
                    return false;
                outTets.push_back(static_cast<u32>(idx));
            }
        }
        else{
            return false;
        }
    }
    return outVerts.size() >= 4u && outTets.size() >= 4u && (outTets.size() % 4u) == 0u;
}

[[nodiscard]] b8 LoadGmshAsciiMsh(
    const char* data,
    const char* end,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outTets
)noexcept{
    Vector<AStringView, Core::Alloc::ScratchArena> lines(scratch);
    const char* cur = data;
    while(cur < end){
        const char* lineEnd = cur;
        while(lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            ++lineEnd;
        const AStringView trimmed = TrimAsciiView(AStringView(cur, static_cast<usize>(lineEnd - cur)));
        if(!trimmed.empty())
            lines.push_back(trimmed);
        cur = lineEnd;
        while(cur < end && (*cur == '\n' || *cur == '\r'))
            ++cur;
    }

    f32 mshVer = 0.0f;
    Vector<u64, Core::Alloc::ScratchArena> nodeTags(scratch);
    usize li = 0u;
    while(li < lines.size()){
        const AStringView tag = lines[li];
        ++li;
        if(tag == "$MeshFormat"){
            if(li >= lines.size())
                return false;
            const char* fp = lines[li].data();
            const char* fe = fp + lines[li].size();
            i64 fileType = 0;
            if(!ParseAsciiFloatToken(fp, fe, mshVer) || !ParseAsciiI64Token(fp, fe, fileType) || fileType != 0)
                return false;
            while(li < lines.size() && lines[li] != "$EndMeshFormat")
                ++li;
            if(li >= lines.size())
                return false;
            ++li;
        }
        else if(tag == "$Nodes"){
            if(mshVer < 4.0f){
                if(li >= lines.size())
                    return false;
                u64 numNodes = 0u;
                const char* np = lines[li].data();
                const char* ne = np + lines[li].size();
                if(!ParseAsciiU64Token(np, ne, numNodes))
                    return false;
                ++li;
                if(li + static_cast<usize>(numNodes) > lines.size())
                    return false;
                for(u64 ni = 0u; ni < numNodes; ++ni, ++li){
                    const char* vp = lines[li].data();
                    const char* ve = vp + lines[li].size();
                    u64 nodeId = 0u;
                    f32 x = 0.0f;
                    f32 y = 0.0f;
                    f32 z = 0.0f;
                    if(!ParseAsciiU64Token(vp, ve, nodeId) || !ParseAsciiFloatToken(vp, ve, x) || !ParseAsciiFloatToken(vp, ve, y) || !ParseAsciiFloatToken(vp, ve, z))
                        return false;
                    nodeTags.push_back(nodeId);
                    outVerts.push_back(Float4(x, y, z, 0.0f));
                }
            }
            else{
                if(li >= lines.size())
                    return false;
                const char* hp = lines[li].data();
                const char* he = hp + lines[li].size();
                u64 numBlocks = 0u;
                u64 numNodes = 0u;
                if(!ParseAsciiU64Token(hp, he, numBlocks) || !ParseAsciiU64Token(hp, he, numNodes))
                    return false;
                ++li;
                for(u64 bi = 0u; bi < numBlocks; ++bi){
                    if(li >= lines.size())
                        return false;
                    const char* bp = lines[li].data();
                    const char* be = bp + lines[li].size();
                    u64 eDim = 0u;
                    u64 eTag = 0u;
                    u64 parametric = 0u;
                    u64 countInBlock = 0u;
                    if(!ParseAsciiU64Token(bp, be, eDim) || !ParseAsciiU64Token(bp, be, eTag) || !ParseAsciiU64Token(bp, be, parametric) || !ParseAsciiU64Token(bp, be, countInBlock))
                        return false;
                    (void)eDim;
                    (void)eTag;
                    (void)parametric;
                    ++li;
                    if(li + static_cast<usize>(countInBlock) * 2u > lines.size())
                        return false;
                    for(u64 k = 0u; k < countInBlock; ++k, ++li){
                        const char* tp = lines[li].data();
                        const char* te = tp + lines[li].size();
                        u64 nodeId = 0u;
                        if(!ParseAsciiU64Token(tp, te, nodeId))
                            return false;
                        nodeTags.push_back(nodeId);
                    }
                    for(u64 k = 0u; k < countInBlock; ++k, ++li){
                        const char* vp = lines[li].data();
                        const char* ve = vp + lines[li].size();
                        f32 x = 0.0f;
                        f32 y = 0.0f;
                        f32 z = 0.0f;
                        if(!ParseAsciiFloatToken(vp, ve, x) || !ParseAsciiFloatToken(vp, ve, y) || !ParseAsciiFloatToken(vp, ve, z))
                            return false;
                        outVerts.push_back(Float4(x, y, z, 0.0f));
                    }
                }
            }
            if(li >= lines.size() || lines[li] != "$EndNodes")
                return false;
            ++li;
        }
        else if(tag == "$Elements"){
            if(outVerts.size() != nodeTags.size() || nodeTags.empty())
                return false;
            u64 maxTag = 0u;
            for(const u64 nt : nodeTags){
                if(nt > maxTag)
                    maxTag = nt;
            }
            if(maxTag > 4194304ull)
                return false;
            Vector<u32, Core::Alloc::ScratchArena> tagToVert(static_cast<usize>(maxTag) + 1u, Limit<u32>::s_Max, scratch);
            for(usize ni = 0u; ni < nodeTags.size(); ++ni)
                tagToVert[static_cast<usize>(nodeTags[ni])] = static_cast<u32>(ni);

            if(mshVer < 4.0f){
                if(li >= lines.size())
                    return false;
                u64 numElems = 0u;
                const char* ep = lines[li].data();
                const char* ee = ep + lines[li].size();
                if(!ParseAsciiU64Token(ep, ee, numElems))
                    return false;
                ++li;
                if(li + static_cast<usize>(numElems) > lines.size())
                    return false;
                for(u64 ei = 0u; ei < numElems; ++ei, ++li){
                    const char* lp = lines[li].data();
                    const char* le = lp + lines[li].size();
                    u64 elemId = 0u;
                    u64 elemType = 0u;
                    u64 numTags = 0u;
                    if(!ParseAsciiU64Token(lp, le, elemId) || !ParseAsciiU64Token(lp, le, elemType) || !ParseAsciiU64Token(lp, le, numTags))
                        return false;
                    (void)elemId;
                    for(u64 ti = 0u; ti < numTags; ++ti){
                        u64 dummyTag = 0u;
                        if(!ParseAsciiU64Token(lp, le, dummyTag))
                            return false;
                    }
                    if(elemType == 4u){
                        for(u32 k = 0u; k < 4u; ++k){
                            u64 nTag = 0u;
                            if(!ParseAsciiU64Token(lp, le, nTag) || nTag > maxTag || tagToVert[static_cast<usize>(nTag)] == Limit<u32>::s_Max)
                                return false;
                            outTets.push_back(tagToVert[static_cast<usize>(nTag)]);
                        }
                    }
                }
            }
            else{
                if(li >= lines.size())
                    return false;
                const char* hp = lines[li].data();
                const char* he = hp + lines[li].size();
                u64 numBlocks = 0u;
                u64 numElems = 0u;
                if(!ParseAsciiU64Token(hp, he, numBlocks) || !ParseAsciiU64Token(hp, he, numElems))
                    return false;
                (void)numElems;
                ++li;
                for(u64 bi = 0u; bi < numBlocks; ++bi){
                    if(li >= lines.size())
                        return false;
                    const char* bp = lines[li].data();
                    const char* be = bp + lines[li].size();
                    u64 eDim = 0u;
                    u64 eTag = 0u;
                    u64 elemType = 0u;
                    u64 countInBlock = 0u;
                    if(!ParseAsciiU64Token(bp, be, eDim) || !ParseAsciiU64Token(bp, be, eTag) || !ParseAsciiU64Token(bp, be, elemType) || !ParseAsciiU64Token(bp, be, countInBlock))
                        return false;
                    (void)eDim;
                    (void)eTag;
                    ++li;
                    if(li + static_cast<usize>(countInBlock) > lines.size())
                        return false;
                    for(u64 k = 0u; k < countInBlock; ++k, ++li){
                        if(elemType != 4u)
                            continue;
                        const char* lp = lines[li].data();
                        const char* le = lp + lines[li].size();
                        u64 elemId = 0u;
                        if(!ParseAsciiU64Token(lp, le, elemId))
                            return false;
                        (void)elemId;
                        for(u32 vk = 0u; vk < 4u; ++vk){
                            u64 nTag = 0u;
                            if(!ParseAsciiU64Token(lp, le, nTag) || nTag > maxTag || tagToVert[static_cast<usize>(nTag)] == Limit<u32>::s_Max)
                                return false;
                            outTets.push_back(tagToVert[static_cast<usize>(nTag)]);
                        }
                    }
                }
            }
            if(li >= lines.size() || lines[li] != "$EndElements")
                return false;
            ++li;
        }
    }
    return outVerts.size() >= 4u && outTets.size() >= 4u && (outTets.size() % 4u) == 0u;
}

[[nodiscard]] b8 LoadTetGenNodeAndEle(
    const AStringView filePathUtf8,
    Core::Alloc::ScratchArena& scratch,
    Vector<Float4, Core::Alloc::ScratchArena>& outVerts,
    Vector<u32, Core::Alloc::ScratchArena>& outTets
)noexcept{
    Vector<u8, Core::Alloc::ScratchArena> nodeBytes(scratch);
    if(!ReadBytesUtf8(filePathUtf8, scratch, nodeBytes) || nodeBytes.empty())
        return false;
    ::Path<Core::Alloc::ScratchArena> elePath(scratch, filePathUtf8);
    elePath.replaceExtension(".ele");
    Vector<u8, Core::Alloc::ScratchArena> eleBytes(scratch);
    if(!ReadBinaryFile(elePath, eleBytes).has_value() || eleBytes.empty())
        return false;

    const auto splitNonEmptyLines = [](const Vector<u8, Core::Alloc::ScratchArena>& buf, Vector<AStringView, Core::Alloc::ScratchArena>& outLines){
        const char* data = reinterpret_cast<const char*>(buf.data());
        const char* end = data + buf.size();
        const char* cur = data;
        while(cur < end){
            const char* lineEnd = cur;
            while(lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
                ++lineEnd;
            const char* hashPos = cur;
            while(hashPos < lineEnd && *hashPos != '#')
                ++hashPos;
            const AStringView trimmed = TrimAsciiView(AStringView(cur, static_cast<usize>(hashPos - cur)));
            if(!trimmed.empty())
                outLines.push_back(trimmed);
            cur = lineEnd;
            while(cur < end && (*cur == '\n' || *cur == '\r'))
                ++cur;
        }
    };

    Vector<AStringView, Core::Alloc::ScratchArena> nodeLines(scratch);
    Vector<AStringView, Core::Alloc::ScratchArena> eleLines(scratch);
    splitNonEmptyLines(nodeBytes, nodeLines);
    splitNonEmptyLines(eleBytes, eleLines);
    if(nodeLines.empty() || eleLines.empty())
        return false;

    const char* np0 = nodeLines[0u].data();
    const char* ne0 = np0 + nodeLines[0u].size();
    u64 numPts = 0u;
    u64 dim = 0u;
    if(!ParseAsciiU64Token(np0, ne0, numPts) || !ParseAsciiU64Token(np0, ne0, dim) || dim != 3u || numPts < 4u)
        return false;
    if(nodeLines.size() < 1u + static_cast<usize>(numPts))
        return false;

    Vector<u64, Core::Alloc::ScratchArena> nodeIds(scratch);
    nodeIds.reserve(static_cast<usize>(numPts));
    outVerts.reserve(static_cast<usize>(numPts));
    u64 maxId = 0u;
    for(usize vi = 0u; vi < static_cast<usize>(numPts); ++vi){
        const char* lp = nodeLines[1u + vi].data();
        const char* le = lp + nodeLines[1u + vi].size();
        u64 nid = 0u;
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
        if(!ParseAsciiU64Token(lp, le, nid) || !ParseAsciiFloatToken(lp, le, x) || !ParseAsciiFloatToken(lp, le, y) || !ParseAsciiFloatToken(lp, le, z))
            return false;
        nodeIds.push_back(nid);
        if(nid > maxId)
            maxId = nid;
        outVerts.push_back(Float4(x, y, z, 0.0f));
    }
    if(maxId > 4194304ull)
        return false;
    Vector<u32, Core::Alloc::ScratchArena> idToVert(static_cast<usize>(maxId) + 1u, Limit<u32>::s_Max, scratch);
    for(usize vi = 0u; vi < nodeIds.size(); ++vi)
        idToVert[static_cast<usize>(nodeIds[vi])] = static_cast<u32>(vi);

    const char* ep0 = eleLines[0u].data();
    const char* ee0 = ep0 + eleLines[0u].size();
    u64 numTets = 0u;
    u64 nodesPerTet = 0u;
    if(!ParseAsciiU64Token(ep0, ee0, numTets) || !ParseAsciiU64Token(ep0, ee0, nodesPerTet) || nodesPerTet < 4u || numTets == 0u)
        return false;
    if(eleLines.size() < 1u + static_cast<usize>(numTets))
        return false;
    outTets.reserve(static_cast<usize>(numTets) * 4u);
    for(usize ti = 0u; ti < static_cast<usize>(numTets); ++ti){
        const char* lp = eleLines[1u + ti].data();
        const char* le = lp + eleLines[1u + ti].size();
        u64 eid = 0u;
        if(!ParseAsciiU64Token(lp, le, eid))
            return false;
        (void)eid;
        for(u32 k = 0u; k < 4u; ++k){
            u64 nid = 0u;
            if(!ParseAsciiU64Token(lp, le, nid) || nid > maxId || idToVert[static_cast<usize>(nid)] == Limit<u32>::s_Max)
                return false;
            outTets.push_back(idToVert[static_cast<usize>(nid)]);
        }
    }
    return outVerts.size() >= 4u && outTets.size() >= 4u && (outTets.size() % 4u) == 0u;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LoadExternalMeshFileRaw(
    const AStringView filePathUtf8,
    const Float4& scale,
    const Float4& rotation,
    const Float4& offset,
    const AStringView submeshName,
    const b8 normalizeUnitsAndAxes,
    void* const user,
    void(* const reserveVerts)(void*, usize),
    Float4*(* const allocVerts)(void*, usize),
    void(* const reserveIdx)(void*, usize),
    u32*(* const allocIdx)(void*, usize),
    void(* const reserveTriMats)(void*, usize),
    u16*(* const allocTriMats)(void*, usize)
)noexcept{
    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_mesh"));
    const AString<Core::Alloc::ScratchArena> ext = __hidden_physics_cook_io::LowercaseExtension(filePathUtf8, scratch);
    const AStringView extView(ext.data(), ext.size());

    Vector<Float4, Core::Alloc::ScratchArena> loadedVerts(scratch);
    Vector<u32, Core::Alloc::ScratchArena> loadedIndices(scratch);
    Vector<u16, Core::Alloc::ScratchArena> loadedTriMats(scratch);

    b8 loaded = false;
    if(extView == ".obj")
        loaded = __hidden_physics_cook_io::LoadObjMesh(filePathUtf8, submeshName, scratch, loadedVerts, loadedIndices, loadedTriMats);
    else if(extView == ".fbx")
        loaded = __hidden_physics_cook_io::LoadFbxMesh(filePathUtf8, submeshName, normalizeUnitsAndAxes, scratch, loadedVerts, loadedIndices, loadedTriMats);
    else if(extView == ".gltf" || extView == ".glb")
        loaded = __hidden_physics_cook_io::LoadGltfOrGlbMesh(filePathUtf8, submeshName, scratch, loadedVerts, loadedIndices, loadedTriMats);
    else if(extView == ".stl")
        loaded = __hidden_physics_cook_io::LoadStlMesh(filePathUtf8, scratch, loadedVerts, loadedIndices);
    else if(extView == ".ply")
        loaded = __hidden_physics_cook_io::LoadPlyMesh(filePathUtf8, scratch, loadedVerts, loadedIndices);

    if(!loaded || loadedVerts.empty() || loadedIndices.size() < 3u || (loadedIndices.size() % 3u) != 0u)
        return false;
    if(!loadedTriMats.empty() && loadedTriMats.size() != (loadedIndices.size() / 3u))
        return false;
    if(!__hidden_physics_cook_io::ApplyExternalMeshTransformAndWinding(scale, rotation, offset, loadedVerts, loadedIndices))
        return false;

    reserveVerts(user, loadedVerts.size());
    Float4* dstVerts = allocVerts(user, loadedVerts.size());
    if(!dstVerts)
        return false;
    std::memcpy(dstVerts, loadedVerts.data(), loadedVerts.size() * sizeof(Float4));

    reserveIdx(user, loadedIndices.size());
    u32* dstIndices = allocIdx(user, loadedIndices.size());
    if(!dstIndices)
        return false;
    std::memcpy(dstIndices, loadedIndices.data(), loadedIndices.size() * sizeof(u32));

    if(!loadedTriMats.empty()){
        reserveTriMats(user, loadedTriMats.size());
        u16* dstMats = allocTriMats(user, loadedTriMats.size());
        if(!dstMats)
            return false;
        std::memcpy(dstMats, loadedTriMats.data(), loadedTriMats.size() * sizeof(u16));
    }
    return true;
}

b8 CollectGltfExternalBufferPathsRaw(
    const AStringView filePathUtf8,
    void* const user,
    b8(* const onPath)(void*, AStringView)
)noexcept{
    Core::Alloc::ScratchArena scratch(Name("physics_cook/gltf_buf_collect"));
    const AString<Core::Alloc::ScratchArena> ext = __hidden_physics_cook_io::LowercaseExtension(filePathUtf8, scratch);
    const AStringView extView(ext.data(), ext.size());
    if(extView != ".gltf" && extView != ".glb")
        return true;

    Vector<u8, Core::Alloc::ScratchArena> fileBytes(scratch);
    Span<const u8> glbBinSpan;
    b8 hasGlbBin = false;
    nlohmann::json doc;
    if(!__hidden_physics_cook_io::ReadGltfJsonDocument(filePathUtf8, scratch, fileBytes, glbBinSpan, hasGlbBin, doc))
        return false;
    (void)glbBinSpan;
    (void)hasGlbBin;

    if(!doc.contains("buffers") || !doc["buffers"].is_array())
        return true;
    const ::Path<Core::Alloc::ScratchArena> gltfPath(scratch, filePathUtf8);
    for(const nlohmann::json& bJson : doc["buffers"]){
        if(!bJson.is_object() || !bJson.contains("uri") || !bJson["uri"].is_string())
            continue;
        const std::string& uri = bJson["uri"].get_ref<const std::string&>();
        const AStringView uriView(uri.data(), uri.size());
        if(uriView.size() >= 5u && uriView.substr(0u, 5u) == "data:")
            continue;
        ::Path<Core::Alloc::ScratchArena> resolvedBuf(scratch);
        ResolvePhysicsSourcePath(gltfPath, uriView, resolvedBuf);
        AString<Core::Alloc::ScratchArena> pathText(scratch);
        const AStringView outView = GenericNativeTextView(resolvedBuf.native(), pathText);
        if(!onPath(user, outView))
            return false;
    }
    return true;
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

        const auto processOk = __hidden_physics_cook_io::ProcessPng16Rows(png, png.height, scratch, [&](const u32 y, const u8* currPtr){
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
        });
        if(!processOk)
            return false;
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
    const b8 isSigned,
    const f32 heightMin,
    const f32 heightMax,
    void* const user,
    f32*(* const allocHeights)(void*, usize)
)noexcept{
    if(cols < 2u || rows < 2u || !std::isfinite(heightMin) || !std::isfinite(heightMax))
        return false;
    if((bitsPerSample != 8u && bitsPerSample != 16u && bitsPerSample != 32u) || (isFloat && bitsPerSample == 8u))
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
        if(isSigned){
            for(usize i = 0u; i < count; ++i){
                const f32 norm = (static_cast<f32>(static_cast<i8>(rawBytes[i])) + 128.0f) * s_InvMax8;
                dst[i] = heightMin + norm * range;
            }
            return true;
        }
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
    if(bitsPerSample == 16u && isFloat){
        for(usize i = 0u; i < count; ++i){
            const u16 hBits = bigEndian ? __hidden_physics_cook_io::ReadBe16(rawBytes + i * 2u) : __hidden_physics_cook_io::ReadLe16(rawBytes + i * 2u);
            const f32 v = __hidden_physics_cook_io::HalfToFloat(hBits);
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
    if(bitsPerSample == 16u){
        constexpr f32 s_InvMax16 = 1.0f / 65535.0f;
        const f32 scale16 = s_InvMax16 * range;
        const SIMDVectorSingle vScale16 = VectorReplicate(scale16);
        if(isSigned){
            for(usize i = 0u; i < count; ++i){
                const u16 rawU = bigEndian ? __hidden_physics_cook_io::ReadBe16(rawBytes + i * 2u) : __hidden_physics_cook_io::ReadLe16(rawBytes + i * 2u);
                const f32 norm = (static_cast<f32>(static_cast<i16>(rawU)) + 32768.0f) * s_InvMax16;
                dst[i] = heightMin + norm * range;
            }
            return true;
        }
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
        if(isSigned){
            for(usize i = 0u; i < count; ++i){
                const u32 rawU = bigEndian ? __hidden_physics_cook_io::ReadBe32(rawBytes + i * 4u) : __hidden_physics_cook_io::ReadLe32(rawBytes + i * 4u);
                const f32 norm = static_cast<f32>((static_cast<f64>(static_cast<i32>(rawU)) + 2147483648.0) * s_InvMax32);
                dst[i] = heightMin + norm * range;
            }
            return true;
        }
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
    const u32 channelMode,
    void* const user,
    u8*(* const allocHoleBytes)(void*, usize)
)noexcept{
    if(expectedCols < 2u || expectedRows < 2u || channelMode > 5u || !std::isfinite(threshold))
        return false;

    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_hf_mask"));
    const u32 cellCols = expectedCols - 1u;
    const u32 cellRows = expectedRows - 1u;
    const usize cellCount = static_cast<usize>(cellCols) * static_cast<usize>(cellRows);
    const usize byteCount = (cellCount + 7u) / 8u;

    const AString<Core::Alloc::ScratchArena> ext = __hidden_physics_cook_io::LowercaseExtension(filePathUtf8, scratch);
    const AStringView extView(ext.data(), ext.size());
    if(extView == ".raw" || extView == ".r8" || extView == ".bin"){
        Vector<u8, Core::Alloc::ScratchArena> rawBytes(scratch);
        if(!__hidden_physics_cook_io::ReadBytesUtf8(filePathUtf8, scratch, rawBytes))
            return false;
        if(rawBytes.size() == cellCount){
            u8* dst = allocHoleBytes(user, byteCount);
            if(!dst)
                return false;
            for(usize ci = 0u; ci < cellCount; ++ci){
                const f32 val = static_cast<f32>(rawBytes[ci]) * (1.0f / 255.0f);
                if(val < threshold)
                    dst[ci >> 3u] = static_cast<u8>(dst[ci >> 3u] | static_cast<u8>(1u << (ci & 7u)));
            }
            return true;
        }
        if(rawBytes.size() == byteCount){
            u8* dst = allocHoleBytes(user, byteCount);
            if(!dst)
                return false;
            std::memcpy(dst, rawBytes.data(), byteCount);
            const u32 remBits = static_cast<u32>(cellCount & 7u);
            if(remBits != 0u && byteCount > 0u)
                dst[byteCount - 1u] = static_cast<u8>(dst[byteCount - 1u] & static_cast<u8>((1u << remBits) - 1u));
            return true;
        }
        return false;
    }

    const auto sniff = __hidden_physics_cook_io::SniffPng16Header(filePathUtf8, scratch);
    if(sniff == __hidden_physics_cook_io::Png16SniffResult::OpenFailed)
        return false;

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

        const auto processOk = __hidden_physics_cook_io::ProcessPng16Rows(png, cellRows, scratch, [&](const u32 cz, const u8* currPtr){
            if(png.channels == 1u)
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<1u>(currPtr, cz, cellCols, channelMode, threshold, dst);
            else if(png.channels == 2u)
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<2u>(currPtr, cz, cellCols, channelMode, threshold, dst);
            else if(png.channels == 3u)
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<3u>(currPtr, cz, cellCols, channelMode, threshold, dst);
            else
                __hidden_physics_cook_io::DecodePng16RowToMaskBits<4u>(currPtr, cz, cellCols, channelMode, threshold, dst);
        });
        if(!processOk)
            return false;
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
            const f32 maskVal = __hidden_physics_cook_io::SelectHoleMaskChannelValue(p[0], p[1], p[2], p[3], channelMode);
            if(maskVal < threshold){
                const usize cellIdx = rowBase + static_cast<usize>(cx);
                dst[cellIdx >> 3u] = static_cast<u8>(dst[cellIdx >> 3u] | static_cast<u8>(1u << (cellIdx & 7u)));
            }
        }
    }
    return true;
}

b8 LoadPhysicsTextFileRaw(
    const AStringView filePathUtf8,
    void* const user,
    char*(* const allocText)(void*, usize)
)noexcept{
    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_text"));
    Vector<u8, Core::Alloc::ScratchArena> bytes(scratch);
    if(!__hidden_physics_cook_io::ReadBytesUtf8(filePathUtf8, scratch, bytes))
        return false;
    usize offset = 0u;
    if(bytes.size() >= 3u && bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu)
        offset = 3u;
    const usize textLen = bytes.size() - offset;
    char* dst = allocText(user, textLen);
    if(textLen > 0u){
        if(!dst)
            return false;
        std::memcpy(dst, bytes.data() + offset, textLen);
    }
    return true;
}

b8 LoadExternalTetMeshFileRaw(
    const AStringView filePathUtf8,
    void* const user,
    void(* const reserveVerts)(void*, usize),
    Float4*(* const allocVerts)(void*, usize),
    void(* const reserveIdx)(void*, usize),
    u32*(* const allocIdx)(void*, usize)
)noexcept{
    Core::Alloc::ScratchArena scratch(Name("physics_cook/load_tet"));
    const AString<Core::Alloc::ScratchArena> ext = __hidden_physics_cook_io::LowercaseExtension(filePathUtf8, scratch);
    const AStringView extView(ext.data(), ext.size());

    Vector<Float4, Core::Alloc::ScratchArena> verts(scratch);
    Vector<u32, Core::Alloc::ScratchArena> tets(scratch);
    b8 ok = false;

    if(extView == ".node"){
        ok = __hidden_physics_cook_io::LoadTetGenNodeAndEle(filePathUtf8, scratch, verts, tets);
    }
    else if(extView == ".msh" || extView == ".tet"){
        Vector<u8, Core::Alloc::ScratchArena> bytes(scratch);
        if(!__hidden_physics_cook_io::ReadBytesUtf8(filePathUtf8, scratch, bytes) || bytes.empty())
            return false;
        const char* data = reinterpret_cast<const char*>(bytes.data());
        const char* end = data + bytes.size();
        if(extView == ".msh")
            ok = __hidden_physics_cook_io::LoadGmshAsciiMsh(data, end, scratch, verts, tets);
        else
            ok = __hidden_physics_cook_io::LoadTetSimpleOrHeadered(data, end, scratch, verts, tets);
    }

    if(!ok || verts.size() < 4u || tets.size() < 4u || (tets.size() % 4u) != 0u)
        return false;

    reserveVerts(user, verts.size());
    Float4* dstVerts = allocVerts(user, verts.size());
    if(!dstVerts)
        return false;
    std::memcpy(dstVerts, verts.data(), verts.size() * sizeof(Float4));

    reserveIdx(user, tets.size());
    u32* dstIdx = allocIdx(user, tets.size());
    if(!dstIdx)
        return false;
    std::memcpy(dstIdx, tets.data(), tets.size() * sizeof(u32));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

