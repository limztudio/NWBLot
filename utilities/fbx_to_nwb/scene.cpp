#include "module.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_UfbxErrorBufferSize = 4096u;
static constexpr f32 s_TargetUnitMeters = 1.0f;

AString FromUfbxString(const ufbx_string value){
    if(!value.data || value.length == 0u)
        return {};
    return AString(value.data, value.length);
}

AStringView UfbxStringView(const ufbx_string value){
    if(!value.data || value.length == 0u)
        return {};
    return AStringView(value.data, value.length);
}

bool NormalizedAsciiEqual(const ufbx_string value, const AStringView normalized){
    const AStringView text = UfbxStringView(value);
    if(text.size() != normalized.size())
        return false;

    for(usize i = 0u; i < normalized.size(); ++i){
        if(ToAsciiLower(text[i]) != normalized[i])
            return false;
    }
    return true;
}

bool NormalizedAsciiContains(const ufbx_string value, const AStringView normalized){
    const AStringView text = UfbxStringView(value);
    if(normalized.empty())
        return true;
    if(text.size() < normalized.size())
        return false;

    const usize lastBegin = text.size() - normalized.size();
    for(usize begin = 0u; begin <= lastBegin; ++begin){
        bool matched = true;
        for(usize i = 0u; i < normalized.size(); ++i){
            if(ToAsciiLower(text[begin + i]) != normalized[i]){
                matched = false;
                break;
            }
        }
        if(matched)
            return true;
    }
    return false;
}

AString MeshDisplayName(const MeshInstance& instance){
    AString nodeName = FromUfbxString(instance.node->name);
    AString meshName = FromUfbxString(instance.mesh->name);
    if(nodeName.empty())
        nodeName = "<unnamed node>";
    if(meshName.empty())
        meshName = "<unnamed mesh>";

    AStringStream out;
    out << "[" << instance.index << "] node=\"" << nodeName << "\" mesh=\"" << meshName
        << "\" triangles=" << instance.mesh->num_triangles;
    if(!instance.node->visible)
        out << " hidden";
    return out.str();
}

AString FormatUfbxError(const ufbx_error& error){
    char buffer[s_UfbxErrorBufferSize] = {};
    ufbx_format_error(buffer, sizeof(buffer), &error);
    return buffer;
}

bool ParseIndexSelector(const AString& text, usize& outIndex){
    const AString trimmed = TrimCopy(text);
    if(trimmed.empty())
        return false;

    u64 parsed = 0u;
    if(!ParseU64(trimmed, parsed))
        return false;
    if(parsed > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    outIndex = static_cast<usize>(parsed);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneHandle::~SceneHandle(){
    if(scene){
        ufbx_free_scene(scene);
        scene = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LoadScene(const ImportOptions& options, SceneHandle& outScene){
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

    const AString inputPath = PathToUtf8(PathFromUtf8(options.inputPath));
    ufbx_error error = {};
    outScene.scene = ufbx_load_file_len(inputPath.data(), inputPath.size(), &loadOptions, &error);
    if(!outScene.scene){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to load FBX: {}"), StringConvert(__hidden_scene::FormatUfbxError(error)));
        return false;
    }

    return true;
}

UtilityVector<MeshInstance> CollectMeshInstances(ufbx_scene* scene, const bool includeHidden){
    UtilityVector<MeshInstance> instances;
    if(!scene)
        return instances;

    instances.reserve(scene->nodes.count);
    for(usize i = 0; i < scene->nodes.count; ++i){
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

void PrintMeshInstances(const UtilityVector<MeshInstance>& instances){
    AStringStream report;
    if(instances.empty()){
        report << "No mesh instances found.\n";
        NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));
        return;
    }

    report << "Mesh instances:\n";
    for(const MeshInstance& instance : instances)
        report << "  " << __hidden_scene::MeshDisplayName(instance) << "\n";
    NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));
}

bool SelectMeshInstances(
    const UtilityVector<MeshInstance>& instances,
    const AString& selector,
    UtilityVector<usize>& outSelection
){
    outSelection.clear();

    const AString normalized = ToAsciiLowerCopy(TrimCopy(selector));
    if(normalized.empty() || normalized == "all"){
        outSelection.reserve(instances.size());
        for(usize instanceIndex = 0u; instanceIndex < instances.size(); ++instanceIndex)
            outSelection.push_back(instanceIndex);
        return true;
    }
    if(normalized == "first"){
        if(instances.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("Invalid mesh selector '{}': no mesh instances are available"), StringConvert(selector));
            return false;
        }
        outSelection.push_back(0u);
        return true;
    }

    usize parsedIndex = 0u;
    if(__hidden_scene::ParseIndexSelector(normalized, parsedIndex)){
        if(parsedIndex >= instances.size()){
            NWB_LOGGER_WARNING(NWB_TEXT("Invalid mesh selector '{}': mesh index is out of range"), StringConvert(selector));
            return false;
        }
        outSelection.push_back(parsedIndex);
        return true;
    }

    UtilityVector<usize> partialSelection;
    outSelection.reserve(instances.size());
    for(const MeshInstance& instance : instances){
        if(
            __hidden_scene::NormalizedAsciiEqual(instance.node->name, normalized)
            || __hidden_scene::NormalizedAsciiEqual(instance.mesh->name, normalized)
        ){
            outSelection.push_back(instance.index);
        }
        else if(
            outSelection.empty()
            && (
                __hidden_scene::NormalizedAsciiContains(instance.node->name, normalized)
                || __hidden_scene::NormalizedAsciiContains(instance.mesh->name, normalized)
            )
        ){
            if(partialSelection.empty())
                partialSelection.reserve(instances.size());
            partialSelection.push_back(instance.index);
        }
    }
    if(!outSelection.empty())
        return true;
    if(!partialSelection.empty()){
        outSelection = Move(partialSelection);
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("Invalid mesh selector '{}': did not match any node or mesh"), StringConvert(selector));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

