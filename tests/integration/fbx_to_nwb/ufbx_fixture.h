// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>


#include <utilities/fbx_to_nwb/ufbx_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::FbxSkinCases{

static_assert(sizeof(ufbx_real) == sizeof(f64));

class NodeStorage final : NoCopy{
public:
    [[nodiscard]] FbxToNwb::Ufbx::Node view()const noexcept{ return {&m_node}; }
    void setName(const AStringView name)noexcept{ m_node.name = {name.data(), name.size()}; }
    [[nodiscard]] ufbx_node& node()noexcept{ return m_node; }

private:
    ufbx_node m_node{};
};

class SkinStorage final : NoCopy{
public:
    SkinStorage(NodeStorage& bone, const ufbx_matrix& transform)noexcept{
        m_cluster.bone_node = &bone.node();
        m_cluster.geometry_to_bone = transform;
        m_skin.clusters = {&m_clusterPointer, 1u};
    }
    [[nodiscard]] FbxToNwb::Ufbx::Skin view()const noexcept{ return {&m_skin}; }

private:
    ufbx_skin_cluster m_cluster{};
    ufbx_skin_cluster* m_clusterPointer = &m_cluster;
    ufbx_skin_deformer m_skin{};
};

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

