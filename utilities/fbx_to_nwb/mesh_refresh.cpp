// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "mesh_refresh_parse.h"
#include "mesh_refresh_text.h"

#include <core/common/log.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_refresh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_DeduplicateParallelGrainSize = 4096u;
inline constexpr StringView s_PositionStreamLabel = "position";
inline constexpr StringView s_NormalStreamLabel = "normal";
inline constexpr StringView s_TangentStreamLabel = "tangent";
inline constexpr StringView s_Uv0StreamLabel = "uv0";
inline constexpr StringView s_ColorsStreamLabel = "color";
inline constexpr StringView s_SkinStreamLabel = "skin";

template<typename Value>
struct StreamSortEntry{
    Value value;
    usize sourceIndex = 0u;
};

[[nodiscard]] u32 FloatSortKey(const f32 value){
    return FloatHashBits(value);
}

[[nodiscard]] bool LessF32(const f32 lhs, const f32 rhs){
    return FloatSortKey(lhs) < FloatSortKey(rhs);
}

[[nodiscard]] bool SameF32(const f32 lhs, const f32 rhs){
    return FloatSortKey(lhs) == FloatSortKey(rhs);
}

[[nodiscard]] bool SameF32Bits(const f32 lhs, const f32 rhs){
    return NWB_MEMCMP(&lhs, &rhs, sizeof(lhs)) == 0;
}

[[nodiscard]] bool LessValue(const Vec2& lhs, const Vec2& rhs){
    if(!SameF32(lhs.x, rhs.x))
        return LessF32(lhs.x, rhs.x);
    return LessF32(lhs.y, rhs.y);
}

[[nodiscard]] bool LessValue(const Vec3& lhs, const Vec3& rhs){
    if(!SameF32(lhs.x, rhs.x))
        return LessF32(lhs.x, rhs.x);
    if(!SameF32(lhs.y, rhs.y))
        return LessF32(lhs.y, rhs.y);
    return LessF32(lhs.z, rhs.z);
}

[[nodiscard]] bool LessValue(const Vec4& lhs, const Vec4& rhs){
    if(!SameF32(lhs.x, rhs.x))
        return LessF32(lhs.x, rhs.x);
    if(!SameF32(lhs.y, rhs.y))
        return LessF32(lhs.y, rhs.y);
    if(!SameF32(lhs.z, rhs.z))
        return LessF32(lhs.z, rhs.z);
    return LessF32(lhs.w, rhs.w);
}

[[nodiscard]] bool SameValue(const Vec2& lhs, const Vec2& rhs){
    return SameF32Bits(lhs.x, rhs.x) && SameF32Bits(lhs.y, rhs.y);
}

[[nodiscard]] bool SameValue(const Vec3& lhs, const Vec3& rhs){
    return SameF32Bits(lhs.x, rhs.x) && SameF32Bits(lhs.y, rhs.y) && SameF32Bits(lhs.z, rhs.z);
}

[[nodiscard]] bool SameValue(const Vec4& lhs, const Vec4& rhs){
    return SameF32Bits(lhs.x, rhs.x) && SameF32Bits(lhs.y, rhs.y) && SameF32Bits(lhs.z, rhs.z) && SameF32Bits(lhs.w, rhs.w);
}

[[nodiscard]] bool LessValue(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs){
    for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
        if(lhs.joint[i] != rhs.joint[i])
            return lhs.joint[i] < rhs.joint[i];
    }
    for(usize i = 0u; i < s_MeshSkinInfluenceCount; ++i){
        if(!SameF32(lhs.weight.raw[i], rhs.weight.raw[i]))
            return LessF32(lhs.weight.raw[i], rhs.weight.raw[i]);
    }
    return false;
}

[[nodiscard]] bool SameValue(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs){
    MeshSkinInfluenceEqual equal;
    return equal(lhs, rhs);
}

template<typename Value>
[[nodiscard]] Expected<UtilityVector<u32>> DeduplicateStream(
    UtilityVector<Value>& stream,
    Core::CpuTaskScheduler& cpuScheduler,
    const AStringView streamName
){
    UtilityVector<u32> outRemap;
    outRemap.reserve(stream.size());
    outRemap.resize(stream.size());

    if(stream.empty())
        return outRemap;

    UtilityVector<StreamSortEntry<Value>> sortedEntries;
    sortedEntries.resize(stream.size());

    auto fillEntry = [&](const usize index){
        sortedEntries[index].value = stream[index];
        sortedEntries[index].sourceIndex = index;
    };
    cpuScheduler.parallelFor(static_cast<usize>(0), stream.size(), s_DeduplicateParallelGrainSize, fillEntry);

    Sort(
        sortedEntries.begin(),
        sortedEntries.end(),
        [](const StreamSortEntry<Value>& lhs, const StreamSortEntry<Value>& rhs){
            if(LessValue(lhs.value, rhs.value))
                return true;
            if(LessValue(rhs.value, lhs.value))
                return false;
            return lhs.sourceIndex < rhs.sourceIndex;
        }
    );

    usize uniqueCount = 0u;
    for(usize sortedIndex = 0u; sortedIndex < sortedEntries.size(); ++sortedIndex){
        if(sortedIndex == 0u || !SameValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value))
            ++uniqueCount;
    }

    if(uniqueCount == stream.size()){
        for(usize i = 0u; i < outRemap.size(); ++i)
            outRemap[i] = static_cast<u32>(i);
        return outRemap;
    }

    if(uniqueCount >= static_cast<usize>(s_MissingSourceStreamIndex)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: {} stream has too many unique values"), StringConvert(streamName));
        return MakeUnexpected(Failure{});
    }

    UtilityVector<Value> compact;
    compact.reserve(uniqueCount);

    u32 compactIndex = 0u;
    for(usize sortedIndex = 0u; sortedIndex < sortedEntries.size(); ++sortedIndex){
        if(sortedIndex == 0u || !SameValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value)){
            if(compact.size() >= static_cast<usize>(s_MissingSourceStreamIndex)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: {} stream has too many unique values"), StringConvert(streamName));
                return MakeUnexpected(Failure{});
            }

            compactIndex = static_cast<u32>(compact.size());
            compact.push_back(sortedEntries[sortedIndex].value);
        }

        outRemap[sortedEntries[sortedIndex].sourceIndex] = compactIndex;
    }

    stream = Move(compact);
    return outRemap;
}

[[nodiscard]] bool RemapRequiredIndex(u32& inOutIndex, const UtilityVector<u32>& remap, const AStringView streamName){
    if(inOutIndex < remap.size()){
        inOutIndex = remap[inOutIndex];
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: vertex_ref {} index is out of range"), StringConvert(streamName));
    return false;
}

[[nodiscard]] bool RemapOptionalIndex(u32& inOutIndex, const UtilityVector<u32>& remap, const AStringView streamName){
    if(inOutIndex == s_MissingSourceStreamIndex)
        return true;
    return RemapRequiredIndex(inOutIndex, remap, streamName);
}

[[nodiscard]] bool RemapComponentRefs(
    SourceMeshStreams& mesh,
    const UtilityVector<u32>& positions,
    const UtilityVector<u32>& normals,
    const UtilityVector<u32>& tangents,
    const UtilityVector<u32>& uv0,
    const UtilityVector<u32>& colors,
    const UtilityVector<u32>& skin
){
    for(SourceVertexRef& ref : mesh.vertexRefs){
        if(!RemapRequiredIndex(ref.position, positions, s_PositionStreamLabel))
            return false;
        if(!RemapRequiredIndex(ref.normal, normals, s_NormalStreamLabel))
            return false;
        if(!RemapRequiredIndex(ref.tangent, tangents, s_TangentStreamLabel))
            return false;
        if(!RemapRequiredIndex(ref.uv0, uv0, s_Uv0StreamLabel))
            return false;
        if(!RemapRequiredIndex(ref.color, colors, s_ColorsStreamLabel))
            return false;
        if(!RemapOptionalIndex(ref.skin, skin, s_SkinStreamLabel))
            return false;
    }
    return true;
}

[[nodiscard]] Expected<SourceMeshCanonicalizeReport> CanonicalizeSkinnedMeshStreams(
    SourceMeshStreams& mesh,
    UtilityVector<MeshSkinInfluence>& skinInfluences,
    Core::CpuTaskScheduler& cpuScheduler
){
    if(mesh.positions.size() != skinInfluences.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize skinned mesh: position and skin influence counts must match"));
        return MakeUnexpected(Failure{});
    }

    SourceMeshCanonicalizeReport report;
    report.before = CountSourceMeshStreams(mesh);
    report.before.skin = skinInfluences.size();

    UtilityVector<u32> positionRemap(mesh.positions.size());
    UtilityVector<u32> skinRemap(skinInfluences.size());

    for(usize i = 0u; i < positionRemap.size(); ++i)
        positionRemap[i] = static_cast<u32>(i);
    for(usize i = 0u; i < skinRemap.size(); ++i)
        skinRemap[i] = static_cast<u32>(i);

    const auto normalRemap = DeduplicateStream(mesh.normals, cpuScheduler, s_NormalStreamLabel);
    if(!normalRemap)
        return MakeUnexpected(Failure{});
    const auto tangentRemap = DeduplicateStream(mesh.tangents, cpuScheduler, s_TangentStreamLabel);
    if(!tangentRemap)
        return MakeUnexpected(Failure{});
    const auto uv0Remap = DeduplicateStream(mesh.uv0, cpuScheduler, s_Uv0StreamLabel);
    if(!uv0Remap)
        return MakeUnexpected(Failure{});
    const auto colorRemap = DeduplicateStream(mesh.colors, cpuScheduler, s_ColorsStreamLabel);
    if(!colorRemap)
        return MakeUnexpected(Failure{});

    if(!RemapComponentRefs(mesh, positionRemap, *normalRemap, *tangentRemap, *uv0Remap, *colorRemap, skinRemap))
        return MakeUnexpected(Failure{});

    report.after = CountSourceMeshStreams(mesh);
    report.after.skin = skinInfluences.size();
    return report;
}


[[nodiscard]] Expected<AString> ReadMetascriptSource(const Path& nwbFilePath){
    AString outText;
    if(!ReadTextFile(nwbFilePath, outText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: failed to read '{}'"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    StripUtf8Bom(outText);
    return outText;
}

[[nodiscard]] Expected<Core::Metascript::Document> ParseMetascriptDocument(
    const Path& nwbFilePath,
    const AStringView text,
    Core::Metascript::MetaArena& arena
){
    Core::Metascript::Document outDoc(arena);
    if(outDoc.parse(text))
        return outDoc;

    for(const Core::Metascript::ParseError& error : outDoc.errors()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: '{}' parse error at {}:{}: {}")
            , PathToString<tchar>(nwbFilePath)
            , error.line
            , error.column
            , StringConvert(AStringView(error.message.data(), error.message.size()))
        );
    }
    return MakeUnexpected(Failure{});
}


void AccumulateReport(SourceMeshCanonicalizeReport& total, const SourceMeshCanonicalizeReport& item){
    total.before.positions += item.before.positions;
    total.before.normals += item.before.normals;
    total.before.tangents += item.before.tangents;
    total.before.uv0 += item.before.uv0;
    total.before.colors += item.before.colors;
    total.before.skin += item.before.skin;
    total.before.vertexRefs += item.before.vertexRefs;
    total.before.indices += item.before.indices;

    total.after.positions += item.after.positions;
    total.after.normals += item.after.normals;
    total.after.tangents += item.after.tangents;
    total.after.uv0 += item.after.uv0;
    total.after.colors += item.after.colors;
    total.after.skin += item.after.skin;
    total.after.vertexRefs += item.after.vertexRefs;
    total.after.indices += item.after.indices;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SourceMeshStreamCounts CountSourceMeshStreams(const SourceMeshStreams& mesh){
    SourceMeshStreamCounts counts;
    counts.positions = mesh.positions.size();
    counts.normals = mesh.normals.size();
    counts.tangents = mesh.tangents.size();
    counts.uv0 = mesh.uv0.size();
    counts.colors = mesh.colors.size();
    counts.skin = mesh.skin.size();
    counts.vertexRefs = mesh.vertexRefs.size();
    counts.indices = mesh.indices.size();
    return counts;
}

Expected<SourceMeshCanonicalizeReport> CanonicalizeSourceMeshStreams(SourceMeshStreams& mesh, Core::CpuTaskScheduler& cpuScheduler){
    SourceMeshCanonicalizeReport report;
    report.before = CountSourceMeshStreams(mesh);


    const auto positionRemap = __hidden_mesh_refresh::DeduplicateStream(mesh.positions, cpuScheduler, __hidden_mesh_refresh::s_PositionStreamLabel);
    if(!positionRemap)
        return MakeUnexpected(Failure{});
    const auto normalRemap = __hidden_mesh_refresh::DeduplicateStream(mesh.normals, cpuScheduler, __hidden_mesh_refresh::s_NormalStreamLabel);
    if(!normalRemap)
        return MakeUnexpected(Failure{});
    const auto tangentRemap = __hidden_mesh_refresh::DeduplicateStream(mesh.tangents, cpuScheduler, __hidden_mesh_refresh::s_TangentStreamLabel);
    if(!tangentRemap)
        return MakeUnexpected(Failure{});
    const auto uv0Remap = __hidden_mesh_refresh::DeduplicateStream(mesh.uv0, cpuScheduler, __hidden_mesh_refresh::s_Uv0StreamLabel);
    if(!uv0Remap)
        return MakeUnexpected(Failure{});
    const auto colorRemap = __hidden_mesh_refresh::DeduplicateStream(mesh.colors, cpuScheduler, __hidden_mesh_refresh::s_ColorsStreamLabel);
    if(!colorRemap)
        return MakeUnexpected(Failure{});
    const auto skinRemap = __hidden_mesh_refresh::DeduplicateStream(mesh.skin, cpuScheduler, __hidden_mesh_refresh::s_SkinStreamLabel);
    if(!skinRemap)
        return MakeUnexpected(Failure{});
    if(!__hidden_mesh_refresh::RemapComponentRefs(mesh, *positionRemap, *normalRemap, *tangentRemap, *uv0Remap, *colorRemap, *skinRemap))
        return MakeUnexpected(Failure{});

    report.after = CountSourceMeshStreams(mesh);
    return report;
}

Expected<SourceMeshCanonicalizeReport> RefreshNwbMeshAsset(const Path& inputPath, const Path& outputPath, Core::CpuTaskScheduler& cpuScheduler){
    SourceMeshCanonicalizeReport outReport;

    auto sourceResult = __hidden_mesh_refresh::ReadMetascriptSource(inputPath);
    if(!sourceResult)
        return MakeUnexpected(Failure{});
    AString& source = *sourceResult;
    const bool useCrlf = HasCrlfLineEndings(AStringView(source.data(), source.size()));

    Core::Metascript::MetaArena metaArena(UtilityDetail::s_UtilityArena);
    auto docResult = __hidden_mesh_refresh::ParseMetascriptDocument(inputPath, source, metaArena);
    if(!docResult)
        return MakeUnexpected(Failure{});
    const Core::Metascript::Document& doc = *docResult;

    UtilityVector<MeshRefreshTextDetail::TextReplacement> replacements;
    bool sawMesh = false;

    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const Core::Metascript::MStringView typeName(declaration.type.data(), declaration.type.size());
        if(!MeshRefreshTextDetail::IsSameText(typeName, s_MeshAssetTypeText))
            continue;

        sawMesh = true;

        const Core::Metascript::MStringView meshVariableView(declaration.variable.data(), declaration.variable.size());
        const AStringView meshVariableName = meshVariableView;
        const Core::Metascript::Value* meshValue = doc.findVariable(meshVariableView);
        if(!meshValue){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: missing mesh variable '{}'"), StringConvert(meshVariableName));
            return MakeUnexpected(Failure{});
        }

        auto meshResult = MeshRefreshParseDetail::ParseMeshValue(inputPath, meshVariableName, *meshValue);
        if(!meshResult)
            return MakeUnexpected(Failure{});
        SourceMeshStreams& mesh = *meshResult;

        SourceMeshStreams before = mesh;
        SourceMeshCanonicalizeReport itemReport;

        const auto skinReference = MeshRefreshTextDetail::FindSkinForMesh(doc, meshVariableName);
        if(!skinReference)
            return MakeUnexpected(Failure{});
        const AStringView skinVariableName = skinReference->variableName;
        const Core::Metascript::Value* skinValue = skinReference->value;
        if(skinValue){
            auto influences = MeshRefreshParseDetail::ParseSkinInfluences(inputPath, *skinValue, skinVariableName);
            if(!influences)
                return MakeUnexpected(Failure{});
            UtilityVector<MeshSkinInfluence>& skinInfluences = *influences;
            const auto canonicalized = __hidden_mesh_refresh::CanonicalizeSkinnedMeshStreams(mesh, skinInfluences, cpuScheduler);
            if(!canonicalized)
                return MakeUnexpected(Failure{});
            itemReport = *canonicalized;
            if(!MeshRefreshTextDetail::AppendMeshReplacements(replacements, source, meshVariableName, before, mesh))
                return MakeUnexpected(Failure{});
            if(itemReport.before.skin != itemReport.after.skin && !MeshRefreshTextDetail::AddReplacement(
                replacements,
                source,
                skinVariableName,
                "influences",
                MeshRefreshTextDetail::WriteSkinInfluenceList(skinInfluences)
            )){
                return MakeUnexpected(Failure{});
            }
        }
        else{
            const auto canonicalized = CanonicalizeSourceMeshStreams(mesh, cpuScheduler);
            if(!canonicalized)
                return MakeUnexpected(Failure{});
            itemReport = *canonicalized;
            if(!MeshRefreshTextDetail::AppendMeshReplacements(replacements, source, meshVariableName, before, mesh))
                return MakeUnexpected(Failure{});
        }

        __hidden_mesh_refresh::AccumulateReport(outReport, itemReport);
    }

    if(!sawMesh){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: '{}' contains no mesh declarations"), PathToString<tchar>(inputPath));
        return MakeUnexpected(Failure{});
    }

    if(!MeshRefreshTextDetail::ApplyTextReplacements(source, replacements)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: failed to apply text replacements for '{}'"), PathToString<tchar>(inputPath));
        return MakeUnexpected(Failure{});
    }
    NormalizeLineEndingsInPlace(source, useCrlf);

    if(!WriteTextFile(outputPath, AStringView(source.data(), source.size())))
        return MakeUnexpected(Failure{});
    return outReport;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

