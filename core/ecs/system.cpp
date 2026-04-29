// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "world.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    if(
        FindIf(m_allSystems.begin(), m_allSystems.end(),
            [&system](ISystem* iterSystem){ return iterSystem == &system; }
        ) != m_allSystems.end()
    )
        return;

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

    // Determine parallel stages by analyzing read/write dependencies.
    // Two systems can share a stage if:
    //  - They do not both write the same component type
    //  - One does not write a component that the other reads

    Alloc::ScratchArena<> scratchArena(4096);
    using ScratchComponentAccessPair = Pair<const ComponentTypeId, AccessMode::Enum>;
    using ScratchComponentAccessAllocator = Alloc::ScratchAllocator<ScratchComponentAccessPair>;
    using ScratchSystemAllocator = Alloc::ScratchAllocator<ISystem*>;
    using ComponentAccessMap = HashMap<
        ComponentTypeId,
        AccessMode::Enum,
        Hasher<ComponentTypeId>,
        EqualTo<ComponentTypeId>,
        ScratchComponentAccessAllocator
    >;

    Vector<u8, Alloc::ScratchAllocator<u8>> assignedSystems(
        systemCount, 0,
        Alloc::ScratchAllocator<u8>(scratchArena)
    );
    Vector<ISystem*, ScratchSystemAllocator> stageSystems{ScratchSystemAllocator(scratchArena)};
    stageSystems.reserve(systemCount);

    usize componentAccessReserve = 0;
    for(ISystem* sys : m_allSystems){
        if(sys->m_access.size() > Limit<usize>::s_Max - componentAccessReserve){
            componentAccessReserve = systemCount;
            break;
        }
        componentAccessReserve += sys->m_access.size();
    }

    ComponentAccessMap stageAccess(
        0,
        Hasher<ComponentTypeId>(),
        EqualTo<ComponentTypeId>(),
        ScratchComponentAccessAllocator(scratchArena)
    );
    stageAccess.reserve(componentAccessReserve);

    usize numAssigned = 0;

    while(numAssigned < systemCount){
        stageSystems.clear();
        stageAccess.clear();

        for(usize i = 0; i < systemCount; ++i){
            if(assignedSystems[i] != 0u)
                continue;

            ISystem* sys = m_allSystems[i];
            const auto& acc = sys->m_access;

            bool compatible = true;
            for(const auto& ca : acc){
                const auto foundAccess = stageAccess.find(ca.typeId);
                if(foundAccess == stageAccess.end())
                    continue;

                if(ca.mode == AccessMode::Write || foundAccess.value() == AccessMode::Write){
                    compatible = false;
                    break;
                }
            }

            if(compatible){
                for(const auto& ca : acc)
                    stageAccess.try_emplace(ca.typeId, ca.mode);
                assignedSystems[i] = 1u;
                stageSystems.push_back(sys);
                ++numAssigned;
            }
        }

        if(stageSystems.empty()){
            for(usize i = 0; i < systemCount; ++i){
                if(assignedSystems[i] != 0u)
                    continue;

                assignedSystems[i] = 1u;
                stageSystems.push_back(m_allSystems[i]);
                ++numAssigned;
                break;
            }
        }

        Stage stage{SystemAllocator(m_arena)};
        stage.reserve(stageSystems.size());
        stage.insert(stage.end(), stageSystems.begin(), stageSystems.end());
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

