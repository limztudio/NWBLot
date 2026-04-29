// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "world.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ISystem::ISystem(Alloc::CustomArena& arena)
    : m_access(AccessAllocator(arena))
{}


void ISystem::registerAccess(ComponentTypeId typeId, AccessMode::Enum mode){
    for(auto& access : m_access){
        if(access.typeId != typeId)
            continue;

        // Write is stronger than read; keep the strongest access mode.
        if(mode == AccessMode::Write)
            access.mode = AccessMode::Write;
        return;
    }

    m_access.push_back(ComponentAccess{ typeId, mode });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SystemScheduler::SystemScheduler(Alloc::CustomArena& arena)
    : m_arena(arena)
    , m_stages(StageAllocator(arena))
    , m_allSystems(SystemAllocator(arena))
    , m_dirty(false)
{}


void SystemScheduler::addSystem(ISystem& system){
#if NWB_OCCUR_ASSERT
    if(
        FindIf(m_allSystems.begin(), m_allSystems.end(),
            [&system](ISystem* iterSystem){ return iterSystem == &system; }
        ) != m_allSystems.end()
    ){
        NWB_ASSERT(false);
        return;
    }
#endif

    m_allSystems.push_back(&system);
    m_dirty = true;
}


void SystemScheduler::removeSystem(ISystem& system){
    auto itr = FindIf(m_allSystems.begin(), m_allSystems.end(),
        [&system](ISystem* iterSystem){ return iterSystem == &system; }
    );
    if(itr != m_allSystems.end()){
        m_allSystems.erase(itr);
        m_dirty = true;
    }
}


void SystemScheduler::clear(){
    m_allSystems.clear();
    m_stages.clear();
    m_dirty = false;
}


void SystemScheduler::rebuild(){
    m_stages.clear();
    const usize systemCount = m_allSystems.size();
    m_stages.reserve(systemCount);

    if(systemCount == 0u){
        m_dirty = false;
        return;
    }

    if(systemCount == 1u){
        Stage stage{SystemAllocator(m_arena)};
        stage.push_back(m_allSystems[0]);
        m_stages.push_back(Move(stage));
        m_dirty = false;
        return;
    }

    // Determine parallel stages by analyzing read/write dependencies.
    // Two systems can share a stage if:
    //  - They do not both write the same component type
    //  - One does not write a component that the other reads

    Alloc::ScratchArena<> scratchArena(4096);
    using ScratchComponentAccessAllocator = Alloc::ScratchAllocator<ComponentAccess>;
    using ScratchSystemAllocator = Alloc::ScratchAllocator<ISystem*>;

    Vector<u8, Alloc::ScratchAllocator<u8>> assignedSystems(
        systemCount, 0,
        Alloc::ScratchAllocator<u8>(scratchArena)
    );

    usize componentAccessReserve = 0;
    for(ISystem* sys : m_allSystems){
        if(sys->m_access.size() > Limit<usize>::s_Max - componentAccessReserve){
            componentAccessReserve = systemCount;
            break;
        }
        componentAccessReserve += sys->m_access.size();
    }

    Vector<ComponentAccess, ScratchComponentAccessAllocator> stageAccesses{ScratchComponentAccessAllocator(scratchArena)};
    stageAccesses.reserve(componentAccessReserve);

    Vector<ISystem*, ScratchSystemAllocator> stageSystems{ScratchSystemAllocator(scratchArena)};
    stageSystems.reserve(systemCount);

    auto componentAccessCompatible = [](const auto& accesses, const ComponentAccess& access) -> bool{
        for(const ComponentAccess& stageAccess : accesses){
            if(stageAccess.typeId != access.typeId)
                continue;
            return access.mode != AccessMode::Write && stageAccess.mode != AccessMode::Write;
        }
        return true;
    };

    auto addComponentAccess = [](auto& accesses, const ComponentAccess& access){
        for(ComponentAccess& stageAccess : accesses){
            if(stageAccess.typeId != access.typeId)
                continue;
            if(access.mode == AccessMode::Write)
                stageAccess.mode = AccessMode::Write;
            return;
        }
        accesses.push_back(access);
    };

    usize numAssigned = 0;

    while(numAssigned < systemCount){
        stageAccesses.clear();
        stageSystems.clear();

        for(usize i = 0; i < systemCount; ++i){
            if(assignedSystems[i] != 0u)
                continue;

            ISystem* sys = m_allSystems[i];
            const auto& acc = sys->m_access;

            bool compatible = true;
            for(const auto& ca : acc){
                if(!componentAccessCompatible(stageAccesses, ca)){
                    compatible = false;
                    break;
                }
            }

            if(compatible){
                for(const auto& ca : acc)
                    addComponentAccess(stageAccesses, ca);
                assignedSystems[i] = 1u;
                stageSystems.push_back(sys);
                ++numAssigned;
            }
        }

        Stage stage{SystemAllocator(m_arena)};
        stage.reserve(stageSystems.size());
        for(ISystem* system : stageSystems)
            stage.push_back(system);
        m_stages.push_back(Move(stage));
    }

    m_dirty = false;
}


void SystemScheduler::execute(World& world, f32 delta){
    if(m_dirty)
        rebuild();

    Alloc::ThreadPool& pool = world.taskPool();

    for(auto& stage : m_stages){
        if(stage.size() == 1){
            stage[0]->update(world, delta);
        }
        else{
            pool.parallelFor(static_cast<usize>(0), stage.size(),
                [&stage, &world, delta](usize i){
                    stage[i]->update(world, delta);
                }
            );
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

