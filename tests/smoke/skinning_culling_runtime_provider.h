// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/system.h>
#include <impl/ecs_mesh/skinning/system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_culling_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BenchmarkMode{
    enum Enum : u8{
        NoCulling,
        FrustumOnly,
        FrustumCone,
    };
};

class BenchmarkRuntimeMeshProvider final : public NWB::Impl::IRuntimeMeshProvider{
public:
    BenchmarkRuntimeMeshProvider()noexcept = default;
    virtual ~BenchmarkRuntimeMeshProvider()noexcept override{
        uninstall();
    }


public:
    void install(NWB::Impl::MeshSystem& meshSystem, NWB::Impl::MeshSkinningSystem& meshSkinningSystem){
        if(m_registered)
            return;

        m_meshSystem = &meshSystem;
        m_meshSkinningSystem = &meshSkinningSystem;
        m_meshSystem->unregisterRuntimeMeshProvider(*m_meshSkinningSystem);
        m_meshSystem->registerRuntimeMeshProvider(*this);
        m_registered = true;
    }

    void uninstall(){
        if(!m_registered || !m_meshSystem || !m_meshSkinningSystem)
            return;

        m_meshSystem->unregisterRuntimeMeshProvider(*this);
        m_meshSystem->registerRuntimeMeshProvider(*m_meshSkinningSystem);
        m_meshSystem = nullptr;
        m_meshSkinningSystem = nullptr;
        m_registered = false;
    }

    void setMode(const BenchmarkMode::Enum mode)noexcept{
        m_mode = mode;
    }

    virtual Expected<NWB::Impl::RuntimeMeshDesc> resolveRuntimeMesh(const NWB::Core::ECS::EntityID entity)override{
        if(!m_meshSkinningSystem)
            return MakeUnexpected(Failure{});
        auto mesh = m_meshSkinningSystem->resolveRuntimeMesh(entity);
        if(!mesh)
            return MakeUnexpected(Failure{});

        switch(m_mode){
        case BenchmarkMode::NoCulling:
            mesh->dynamicMeshletBoundsFresh = false;
            mesh->dynamicMeshletConesFresh = false;
            return Move(*mesh);
        case BenchmarkMode::FrustumOnly:
            mesh->dynamicMeshletBoundsFresh = true;
            mesh->dynamicMeshletConesFresh = false;
            return Move(*mesh);
        case BenchmarkMode::FrustumCone:
        default:
            mesh->dynamicMeshletBoundsFresh = true;
            mesh->dynamicMeshletConesFresh = true;
            return Move(*mesh);
        }
    }

    virtual void markLiveRuntimeMeshes(NWB::Impl::RuntimeMeshRequestSet& requests)override{
        if(m_meshSkinningSystem)
            m_meshSkinningSystem->markLiveRuntimeMeshes(requests);
    }

    [[nodiscard]] virtual b8 hasRuntimeMeshBinding(const NWB::Core::ECS::EntityID entity)const noexcept override{
        return m_meshSkinningSystem && m_meshSkinningSystem->hasRuntimeMeshBinding(entity);
    }


private:
    NWB::Impl::MeshSystem* m_meshSystem = nullptr;
    NWB::Impl::MeshSkinningSystem* m_meshSkinningSystem = nullptr;
    BenchmarkMode::Enum m_mode = BenchmarkMode::FrustumCone;
    b8 m_registered = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
