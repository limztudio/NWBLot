// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_import{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AString FromUfbxString(const ufbx_string value){
    if(!value.data || value.length == 0u)
        return {};
    return AString(value.data, value.length);
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
    char buffer[4096] = {};
    ufbx_format_error(buffer, sizeof(buffer), &error);
    return buffer;
}

bool ParseIndexSelector(const AString& text, usize& outIndex){
    const AString trimmed = Trim(text);
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

Vec3 ToVec3(const ufbx_vec3 value){
    return Vec3{
        static_cast<f32>(value.x),
        static_cast<f32>(value.y),
        static_cast<f32>(value.z),
    };
}

Vec2 ToVec2(const ufbx_vec2 value){
    return Vec2{
        static_cast<f32>(value.x),
        static_cast<f32>(value.y),
    };
}

Vec4 ToVec4(const ufbx_vec4 value){
    return Vec4{
        static_cast<f32>(value.x),
        static_cast<f32>(value.y),
        static_cast<f32>(value.z),
        static_cast<f32>(value.w),
    };
}

bool AppendInstanceGeometry(
    const MeshInstance& instance,
    const ImportOptions& options,
    const bool wantsDeformableGeometry,
    const Vec4& defaultColor,
    UtilityVector<u32>& inOutTriangleIndices,
    UtilityVector<GeometryVertex>& outFlatVertices,
    bool& inOutSawVertexColors,
    bool& inOutSawVertexUvs,
    AString& outError
){
    ufbx_mesh* mesh = instance.mesh;
    ufbx_node* node = instance.node;
    if(!mesh || !node){
        outError = "internal mesh instance is null";
        return false;
    }
    if(!mesh->vertex_position.exists){
        outError = "mesh is missing positions";
        return false;
    }
    if(!mesh->vertex_normal.exists){
        outError = "mesh is missing normals after ufbx import";
        return false;
    }

    if(mesh->max_face_triangles > Limit<usize>::s_Max / 3u){
        outError = "mesh face triangulation scratch size overflows";
        return false;
    }

    inOutTriangleIndices.resize(static_cast<usize>(mesh->max_face_triangles) * 3u);
    const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&node->geometry_to_world);
    const bool importUvs = wantsDeformableGeometry && mesh->vertex_uv.exists;
    const bool importColors = options.importColors && mesh->vertex_color.exists;

    for(usize faceIndex = 0; faceIndex < mesh->num_faces; ++faceIndex){
        const ufbx_face face = mesh->faces.data[faceIndex];
        if(face.num_indices < 3u)
            continue;

        const u32 triangleCount = ufbx_triangulate_face(
            inOutTriangleIndices.data(),
            inOutTriangleIndices.size(),
            mesh,
            face
        );

        for(u32 triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
            u32 cornerIndices[3] = {
                inOutTriangleIndices[triangleIndex * 3u + 0u],
                inOutTriangleIndices[triangleIndex * 3u + 1u],
                inOutTriangleIndices[triangleIndex * 3u + 2u],
            };
            if(options.flipWinding)
                Swap(cornerIndices[1], cornerIndices[2]);

            for(const u32 cornerIndex : cornerIndices){
                ufbx_vec3 position = {};
                ufbx_vec3 normal = {};

                if(options.bakeTransforms){
                    position = ufbx_get_vertex_vec3(&mesh->skinned_position, cornerIndex);
                    normal = ufbx_get_vertex_vec3(&mesh->skinned_normal, cornerIndex);
                    if(mesh->skinned_is_local){
                        position = ufbx_transform_position(&node->geometry_to_world, position);
                        normal = ufbx_transform_direction(&normalToWorld, normal);
                    }
                }
                else{
                    position = ufbx_get_vertex_vec3(&mesh->vertex_position, cornerIndex);
                    normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, cornerIndex);
                }

                GeometryVertex vertex;
                vertex.position = ToVec3(position);
                vertex.position.x = static_cast<f32>(static_cast<f64>(vertex.position.x) * options.scale);
                vertex.position.y = static_cast<f32>(static_cast<f64>(vertex.position.y) * options.scale);
                vertex.position.z = static_cast<f32>(static_cast<f64>(vertex.position.z) * options.scale);
                vertex.normal = ToVec3(normal);
                if(!Normalize(vertex.normal))
                    vertex.normal = Vec3{ 0.0f, 0.0f, 1.0f };

                vertex.uv0 = Vec2{};
                if(importUvs){
                    vertex.uv0 = ToVec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIndex));
                    inOutSawVertexUvs = true;
                }

                vertex.color = defaultColor;
                if(importColors){
                    vertex.color = ToVec4(ufbx_get_vertex_vec4(&mesh->vertex_color, cornerIndex));
                    inOutSawVertexColors = true;
                }

                if(!IsFiniteVertex(vertex)){
                    outError = "mesh contains non-finite vertex data";
                    return false;
                }

                outFlatVertices.push_back(vertex);
            }
        }
    }

    return true;
}

bool EstimateSelectedTriangleCorners(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    usize& outTriangleCorners,
    AString& outError
){
    outTriangleCorners = 0u;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            outError = "selected mesh index is out of range";
            return false;
        }

        const ufbx_mesh* const mesh = instances[instanceIndex].mesh;
        if(!mesh)
            continue;
        if(mesh->num_triangles > (Limit<usize>::s_Max - outTriangleCorners) / 3u){
            outError = "selected meshes have too many triangle corners";
            return false;
        }

        outTriangleCorners += static_cast<usize>(mesh->num_triangles) * 3u;
    }
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


bool LoadScene(const ImportOptions& options, SceneHandle& outScene, AString& outError){
    ufbx_load_opts loadOptions = {};
    loadOptions.load_external_files = true;
    loadOptions.ignore_missing_external_files = true;
    loadOptions.generate_missing_normals = true;
    loadOptions.normalize_normals = true;
    loadOptions.evaluate_skinning = true;
    loadOptions.evaluate_caches = true;
    loadOptions.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_PRESERVE;

    if(!options.preserveSpace){
        loadOptions.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
        loadOptions.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
        loadOptions.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
        loadOptions.target_unit_meters = 1.0f;
    }

    const AString inputPath = PathToUtf8(PathFromUtf8(options.inputPath));
    ufbx_error error = {};
    outScene.scene = ufbx_load_file_len(inputPath.data(), inputPath.size(), &loadOptions, &error);
    if(!outScene.scene){
        outError = "failed to load FBX: " + __hidden_fbx_import::FormatUfbxError(error);
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
    if(instances.empty()){
        NWB_COUT << "No mesh instances found.\n";
        return;
    }

    NWB_COUT << "Mesh instances:\n";
    for(const MeshInstance& instance : instances)
        NWB_COUT << "  " << __hidden_fbx_import::MeshDisplayName(instance) << "\n";
}

bool SelectMeshInstances(
    const UtilityVector<MeshInstance>& instances,
    const AString& selector,
    UtilityVector<usize>& outSelection,
    AString& outError
){
    outSelection.clear();
    outError.clear();

    const AString normalized = ToLower(Trim(selector));
    if(normalized.empty() || normalized == "all"){
        outSelection.resize(instances.size());
        Iota(outSelection.begin(), outSelection.end(), usize{ 0 });
        return true;
    }
    if(normalized == "first"){
        if(instances.empty()){
            outError = "no mesh instances are available";
            return false;
        }
        outSelection.push_back(0u);
        return true;
    }

    usize parsedIndex = 0u;
    if(__hidden_fbx_import::ParseIndexSelector(normalized, parsedIndex)){
        if(parsedIndex >= instances.size()){
            outError = "mesh index is out of range";
            return false;
        }
        outSelection.push_back(parsedIndex);
        return true;
    }

    UtilityVector<usize> partialSelection;
    outSelection.reserve(instances.size());
    for(const MeshInstance& instance : instances){
        const AString nodeName = ToLower(__hidden_fbx_import::FromUfbxString(instance.node->name));
        const AString meshName = ToLower(__hidden_fbx_import::FromUfbxString(instance.mesh->name));
        if(nodeName == normalized || meshName == normalized)
            outSelection.push_back(instance.index);
        else if(outSelection.empty() && (nodeName.find(normalized) != AString::npos || meshName.find(normalized) != AString::npos)){
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

    outError = "mesh selector did not match any node or mesh";
    return false;
}

bool BuildGeometry(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const Vec4& defaultColor,
    UtilityVector<GeometryVertex>& outVertices,
    UtilityVector<u32>& outIndices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    AString& outError
){
    outVertices.clear();
    outIndices.clear();
    outSawVertexColors = false;
    outSawVertexUvs = false;
    outError.clear();

    usize estimatedTriangleCorners = 0u;
    if(
        !__hidden_fbx_import::EstimateSelectedTriangleCorners(
            instances,
            selection,
            estimatedTriangleCorners,
            outError
        )
    )
        return false;

    UtilityVector<GeometryVertex> flatVertices;
    flatVertices.reserve(estimatedTriangleCorners);
    outIndices.reserve(estimatedTriangleCorners);
    UtilityVector<u32> triangleIndices;
    const bool wantsDeformableGeometry = IsNormalizedDeformableGeometryKind(NormalizeAssetKind(options.assetKind));
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            outError = "selected mesh index is out of range";
            return false;
        }
        if(
            !__hidden_fbx_import::AppendInstanceGeometry(
                instances[instanceIndex],
                options,
                wantsDeformableGeometry,
                defaultColor,
                triangleIndices,
                flatVertices,
                outSawVertexColors,
                outSawVertexUvs,
                outError
            )
        ){
            return false;
        }
    }

    if(flatVertices.empty()){
        outError = "selected meshes produced no triangles";
        return false;
    }
    if(flatVertices.size() > static_cast<usize>(Limit<u32>::s_Max)){
        outError = "geometry has more than u32-addressable vertices";
        return false;
    }

    outIndices.resize(flatVertices.size());
    if(options.deduplicate){
        ufbx_vertex_stream stream = {};
        stream.data = flatVertices.data();
        stream.vertex_count = flatVertices.size();
        stream.vertex_size = sizeof(GeometryVertex);

        ufbx_error error = {};
        const usize uniqueVertexCount = static_cast<usize>(ufbx_generate_indices(
            &stream,
            1u,
            outIndices.data(),
            outIndices.size(),
            nullptr,
            &error
        ));
        if(error.type != UFBX_ERROR_NONE){
            outError = "ufbx failed to generate an index buffer: " + __hidden_fbx_import::FormatUfbxError(error);
            return false;
        }
        if(uniqueVertexCount > static_cast<usize>(Limit<u32>::s_Max)){
            outError = "deduplicated geometry has more than u32-addressable vertices";
            return false;
        }

        flatVertices.resize(uniqueVertexCount);
        outVertices = Move(flatVertices);
    }
    else{
        Iota(outIndices.begin(), outIndices.end(), u32{ 0 });
        outVertices = Move(flatVertices);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

