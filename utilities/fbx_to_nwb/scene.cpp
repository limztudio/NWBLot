// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <global/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_UfbxErrorBufferSize = 4096u;
static constexpr f32 s_TargetUnitMeters = 1.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AStringView UfbxStringView(const ufbx_string value)noexcept{
    if(!value.data || value.length == 0u)
        return {};
    return AStringView(value.data, value.length);
}

void AppendMeshDisplayName(ScratchStringStream& out, const MeshInstance& instance){
    AStringView nodeName = UfbxStringView(instance.node->name);
    AStringView meshName = UfbxStringView(instance.mesh->name);
    if(nodeName.empty())
        nodeName = "<unnamed node>";
    if(meshName.empty())
        meshName = "<unnamed mesh>";

    out << "[" << instance.index << "] node=\"" << nodeName << "\" mesh=\"" << meshName << "\" triangles=" << instance.mesh->num_triangles;
    if(!instance.node->visible)
        out << " hidden";
}

ScratchString FormatUfbxError(const ufbx_error& error, Core::Alloc::ScratchArena& scratchArena){
    char buffer[s_UfbxErrorBufferSize] = {};
    const usize written = ufbx_format_error(buffer, sizeof(buffer), &error);
    return ScratchString(buffer, written, scratchArena);
}

Expected<usize> ParseIndexSelector(const AStringView text)noexcept{
    const AStringView trimmed = TrimView(text);
    if(trimmed.empty())
        return MakeUnexpected(Failure{});

    const auto parsed = ParseU64(trimmed);
    if(!parsed)
        return MakeUnexpected(Failure{});
    if(*parsed > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(Failure{});

    return static_cast<usize>(*parsed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneHandle::SceneHandle(SceneHandle&& other)noexcept
    : scene(Exchange(other.scene, nullptr))
{}

SceneHandle::~SceneHandle()noexcept{
    if(scene){
        ufbx_free_scene(scene);
        scene = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SceneHandle> LoadScene(const ImportOptions& options, Core::Alloc::ScratchArena& scratchArena){
    SceneHandle outScene;
    ufbx_load_opts loadOptions = {};
    loadOptions.load_external_files = true;
    loadOptions.ignore_missing_external_files = true;
    loadOptions.generate_missing_normals = true;
    loadOptions.normalize_normals = true;
    loadOptions.clean_skin_weights = true;
    loadOptions.evaluate_skinning = true;
    loadOptions.evaluate_caches = true;
    loadOptions.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_PRESERVE;

    if(!options.preserveSpace){
        loadOptions.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
        loadOptions.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
        loadOptions.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
        loadOptions.target_unit_meters = __hidden_scene::s_TargetUnitMeters;
    }

    const ScratchPath nativeInputPath(scratchArena, options.inputPath);
    const auto inputPath = PathToGenericString(scratchArena, nativeInputPath);
    ufbx_error error = {};
    outScene.scene = ufbx_load_file_len(inputPath.data(), inputPath.size(), &loadOptions, &error);
    if(!outScene.scene){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to load FBX: {}"), StringConvert(__hidden_scene::FormatUfbxError(error, scratchArena)));
        return MakeUnexpected(Failure{});
    }

    return outScene;
}

UtilityVector<MeshInstance> CollectMeshInstances(ufbx_scene* scene, const bool includeHidden){
    UtilityVector<MeshInstance> instances;
    if(!scene)
        return instances;

    instances.reserve(scene->nodes.count);
    for(usize i = 0u; i < scene->nodes.count; ++i){
        ufbx_node* node = scene->nodes.data[i];
        if(!node || !node->mesh)
            continue;
        if(!includeHidden && !node->visible)
            continue;

        MeshInstance instance;
        instance.node = node;
        instance.mesh = node->mesh;
        instance.index = instances.size();
        instances.push_back(instance);
    }

    return instances;
}

void PrintMeshInstances(const UtilityVector<MeshInstance>& instances, Core::Alloc::ScratchArena& scratchArena){
    ScratchStringStream report(s_FileOpenWrite, scratchArena);
    if(instances.empty()){
        report << "No mesh instances found.\n";
        NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));
        return;
    }

    report << "Mesh instances:\n";
    for(const MeshInstance& instance : instances){
        report << "  ";
        __hidden_scene::AppendMeshDisplayName(report, instance);
        report << s_ReportLineEndText;
    }
    NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));
}

Expected<UtilityVector<usize>> SelectMeshInstances(const UtilityVector<MeshInstance>& instances, const AStringView selector, Core::Alloc::ScratchArena& scratchArena){
    UtilityVector<usize> outSelection;

    const ScratchString normalized = NormalizeOptionText(ScratchString(selector, scratchArena));
    if(normalized.empty() || normalized == s_DefaultMeshSelectorText){
        outSelection.reserve(instances.size());
        for(usize instanceIndex = 0u; instanceIndex < instances.size(); ++instanceIndex)
            outSelection.push_back(instanceIndex);
        return outSelection;
    }
    if(normalized == s_FirstMeshSelectorText){
        if(instances.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("Invalid mesh selector '{}': no mesh instances are available"), StringConvert(selector));
            return MakeUnexpected(Failure{});
        }
        outSelection.push_back(0u);
        return outSelection;
    }

    const auto parsedIndex = __hidden_scene::ParseIndexSelector(normalized);
    if(parsedIndex){
        if(*parsedIndex >= instances.size()){
            NWB_LOGGER_WARNING(NWB_TEXT("Invalid mesh selector '{}': mesh index is out of range"), StringConvert(selector));
            return MakeUnexpected(Failure{});
        }
        outSelection.push_back(*parsedIndex);
        return outSelection;
    }

    UtilityVector<usize> partialSelection;
    outSelection.reserve(instances.size());
    for(const MeshInstance& instance : instances){
        if(EqualsAsciiIgnoreCase(__hidden_scene::UfbxStringView(instance.node->name), AStringView(normalized)) || EqualsAsciiIgnoreCase(__hidden_scene::UfbxStringView(instance.mesh->name), AStringView(normalized))){
            outSelection.push_back(instance.index);
        }
        else if(outSelection.empty() && (ContainsAsciiIgnoreCase(__hidden_scene::UfbxStringView(instance.node->name), AStringView(normalized)) || ContainsAsciiIgnoreCase(__hidden_scene::UfbxStringView(instance.mesh->name), AStringView(normalized)))){
            if(partialSelection.empty())
                partialSelection.reserve(instances.size());
            partialSelection.push_back(instance.index);
        }
    }
    if(!outSelection.empty())
        return outSelection;
    if(!partialSelection.empty()){
        outSelection = Move(partialSelection);
        return outSelection;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("Invalid mesh selector '{}': did not match any node or mesh"), StringConvert(selector));
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

