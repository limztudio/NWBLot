// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/basic_string.h>
#include <global/span.h>
#include <global/string_view.h>
#include <global/type_properties.h>

#include <ufbx.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FbxToNwb::Ufbx{

[[nodiscard]] inline AStringView Text(const ufbx_string value)noexcept{
    return value.data ? AStringView(value.data, value.length) : AStringView{};
}

struct Node{
    const ufbx_node* ptr = nullptr;
    [[nodiscard]] constexpr bool valid()const noexcept{ return ptr != nullptr; }
    [[nodiscard]] usize identity()const noexcept{ return reinterpret_cast<usize>(ptr); }
    [[nodiscard]] AStringView name()const noexcept{ return ptr ? Text(ptr->name) : AStringView{}; }
    [[nodiscard]] Node parent()const noexcept{ return {ptr ? ptr->parent : nullptr}; }
    [[nodiscard]] bool visible()const noexcept{ return ptr && ptr->visible; }
    [[nodiscard]] const ufbx_matrix& geometryToWorld()const noexcept{ return ptr->geometry_to_world; }
};

struct Cluster{
    const ufbx_skin_cluster* ptr = nullptr;
    [[nodiscard]] constexpr bool valid()const noexcept{ return ptr != nullptr; }
    [[nodiscard]] Node bone()const noexcept{ return {ptr->bone_node}; }
    [[nodiscard]] const ufbx_matrix& geometryToBone()const noexcept{ return ptr->geometry_to_bone; }
};

struct Skin{
    const ufbx_skin_deformer* ptr = nullptr;
    [[nodiscard]] constexpr bool valid()const noexcept{ return ptr != nullptr; }
    [[nodiscard]] usize clusterCount()const noexcept{ return ptr->clusters.count; }
    [[nodiscard]] usize vertexCount()const noexcept{ return ptr->vertices.count; }
    [[nodiscard]] usize weightCount()const noexcept{ return ptr->weights.count; }
    [[nodiscard]] Cluster cluster(const usize index)const noexcept{ return {ptr->clusters[index]}; }
    [[nodiscard]] ufbx_skin_vertex vertex(const usize index)const noexcept{ return ptr->vertices[index]; }
    [[nodiscard]] ufbx_skin_weight weight(const usize index)const noexcept{ return ptr->weights[index]; }
};

struct Mesh{
    const ufbx_mesh* ptr = nullptr;
    [[nodiscard]] constexpr bool valid()const noexcept{ return ptr != nullptr; }
    [[nodiscard]] AStringView name()const noexcept{ return ptr ? Text(ptr->name) : AStringView{}; }
    [[nodiscard]] usize triangleCount()const noexcept{ return ptr ? ptr->num_triangles : 0u; }
    [[nodiscard]] usize maxFaceTriangles()const noexcept{ return ptr->max_face_triangles; }
    [[nodiscard]] usize cornerCount()const noexcept{ return ptr->vertex_indices.count; }
    [[nodiscard]] usize faceCount()const noexcept{ return ptr->num_faces; }
    [[nodiscard]] usize vertexCount()const noexcept{ return ptr->num_vertices; }
    [[nodiscard]] usize skinCount()const noexcept{ return ptr->skin_deformers.count; }
    [[nodiscard]] Skin skin(const usize index)const noexcept{ return {ptr->skin_deformers[index]}; }
    [[nodiscard]] u32 vertexIndex(const usize corner)const noexcept{ return ptr->vertex_indices[corner]; }
    [[nodiscard]] ufbx_face face(const usize index)const noexcept{ return ptr->faces[index]; }
    [[nodiscard]] u32 triangulate(const Span<u32> indices, const ufbx_face face)const noexcept{
        return ufbx_triangulate_face(indices.data(), indices.size(), ptr, face);
    }
    [[nodiscard]] bool skinnedIsLocal()const noexcept{ return ptr->skinned_is_local; }
    [[nodiscard]] bool hasPosition()const noexcept{ return ptr->vertex_position.exists; }
    [[nodiscard]] ufbx_vec3 position(const u32 corner)const noexcept{ return ufbx_get_vertex_vec3(&ptr->vertex_position, corner); }
    [[nodiscard]] ufbx_vec3 skinnedPosition(const u32 corner)const noexcept{ return ufbx_get_vertex_vec3(&ptr->skinned_position, corner); }
    [[nodiscard]] bool hasNormal()const noexcept{ return ptr->vertex_normal.exists; }
    [[nodiscard]] ufbx_vec3 normal(const u32 corner)const noexcept{ return ufbx_get_vertex_vec3(&ptr->vertex_normal, corner); }
    [[nodiscard]] ufbx_vec3 skinnedNormal(const u32 corner)const noexcept{ return ufbx_get_vertex_vec3(&ptr->skinned_normal, corner); }
    [[nodiscard]] bool hasTangent()const noexcept{ return ptr->vertex_tangent.exists; }
    [[nodiscard]] ufbx_vec3 tangent(const u32 corner)const noexcept{ return ufbx_get_vertex_vec3(&ptr->vertex_tangent, corner); }
    [[nodiscard]] bool hasBitangent()const noexcept{ return ptr->vertex_bitangent.exists; }
    [[nodiscard]] ufbx_vec3 bitangent(const u32 corner)const noexcept{ return ufbx_get_vertex_vec3(&ptr->vertex_bitangent, corner); }
    [[nodiscard]] bool hasUv()const noexcept{ return ptr->vertex_uv.exists; }
    [[nodiscard]] ufbx_vec2 uv(const u32 corner)const noexcept{ return ufbx_get_vertex_vec2(&ptr->vertex_uv, corner); }
    [[nodiscard]] bool hasColor()const noexcept{ return ptr->vertex_color.exists; }
    [[nodiscard]] ufbx_vec4 color(const u32 corner)const noexcept{ return ufbx_get_vertex_vec4(&ptr->vertex_color, corner); }
};

[[nodiscard]] inline Mesh NodeMesh(const Node node)noexcept{ return {node.ptr ? node.ptr->mesh : nullptr}; }

class Document final{
public:
    Document()noexcept = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&& other)noexcept : m_scene(Exchange(other.m_scene, nullptr)){}
    Document& operator=(Document&& other)noexcept{
        if(this != &other){
            if(m_scene)
                ufbx_free_scene(m_scene);
            m_scene = Exchange(other.m_scene, nullptr);
        }
        return *this;
    }
    ~Document()noexcept{
        if(m_scene)
            ufbx_free_scene(m_scene);
    }

    [[nodiscard]] bool load(const AStringView path, const ufbx_load_opts& options, ufbx_error& error)noexcept{
        ufbx_scene* loaded = ufbx_load_file_len(path.data(), path.size(), &options, &error);
        if(!loaded)
            return false;
        if(m_scene)
            ufbx_free_scene(m_scene);
        m_scene = loaded;
        return true;
    }
    [[nodiscard]] usize nodeCount()const noexcept{ return m_scene->nodes.count; }
    [[nodiscard]] Node node(const usize index)const noexcept{ return {m_scene->nodes[index]}; }

private:
    ufbx_scene* m_scene = nullptr;
};

inline void FormatError(const ufbx_error& error, const Span<u8> buffer, AString<ArenaBase>& out){
    char* const text = reinterpret_cast<char*>(buffer.data());
    out.assign(text, ufbx_format_error(text, buffer.size(), &error));
}
[[nodiscard]] inline ufbx_matrix Invert(const ufbx_matrix& matrix)noexcept{ return ufbx_matrix_invert(&matrix); }
[[nodiscard]] inline ufbx_matrix Multiply(const ufbx_matrix& lhs, const ufbx_matrix& rhs)noexcept{ return ufbx_matrix_mul(&lhs, &rhs); }
[[nodiscard]] inline ufbx_matrix ForNormals(const ufbx_matrix& matrix)noexcept{ return ufbx_matrix_for_normals(&matrix); }
[[nodiscard]] inline ufbx_real Determinant(const ufbx_matrix& matrix)noexcept{ return ufbx_matrix_determinant(&matrix); }
[[nodiscard]] inline ufbx_vec3 TransformPosition(const ufbx_matrix& matrix, const ufbx_vec3 value)noexcept{ return ufbx_transform_position(&matrix, value); }
[[nodiscard]] inline ufbx_vec3 TransformDirection(const ufbx_matrix& matrix, const ufbx_vec3 value)noexcept{ return ufbx_transform_direction(&matrix, value); }


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

