// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "world.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ISystem::registerAccess(ComponentTypeId typeId, AccessMode::Enum mode){
    for(usize i = 0; i < m_access.size(); ++i){
        auto& access = m_access[i];
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
SystemScheduler::~SystemScheduler()
{}


void SystemScheduler::addSystem(ISystem& system){
    if(FindIf(m_allSystems.begin(), m_allSystems.end(),
        [&system](ISystem* iterSystem){ return iterSystem == &system; }
    ) != m_allSystems.end())
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


void SystemScheduler::rebuild(){
    m_stages.clear();

    // Determine parallel stages by analyzing read/write dependencies.
    // Two systems can share a stage if:
    //  - They do not both write the same component type
    //  - One does not write a component that the other reads

    Alloc::ScratchArena<> scratchArena(4096);

    Vector<u8, Alloc::ScratchAllocator<u8>> assigned(
        m_allSystems.size(), 0,
        Alloc::ScratchAllocator<u8>(scratchArena)
    );
    usize numAssigned = 0;

    while(numAssigned < m_allSystems.size()){
        Stage stage{SystemAllocator(m_arena)};

        HashSet<ComponentTypeId, Hasher<ComponentTypeId>, EqualTo<ComponentTypeId>, Alloc::ScratchAllocator<ComponentTypeId>> stageWrites(0, Hasher<ComponentTypeId>(), EqualTo<ComponentTypeId>(), Alloc::ScratchAllocator<ComponentTypeId>(scratchArena));
        HashSet<ComponentTypeId, Hasher<ComponentTypeId>, EqualTo<ComponentTypeId>, Alloc::ScratchAllocator<ComponentTypeId>> stageReads(0, Hasher<ComponentTypeId>(), EqualTo<ComponentTypeId>(), Alloc::ScratchAllocator<ComponentTypeId>(scratchArena));

        for(usize i = 0; i < m_allSystems.size(); ++i){
            if(assigned[i])
                continue;

            ISystem* sys = m_allSystems[i];
            const auto& acc = sys->access();

            bool compatible = true;
            for(usize a = 0; a < acc.size(); ++a){
                const auto& ca = acc[a];
                if(ca.mode == AccessMode::Write){
                    if(stageWrites.count(ca.typeId) || stageReads.count(ca.typeId)){
                        compatible = false;
                        break;
                    }
                }
                else{
                    if(stageWrites.count(ca.typeId)){
                        compatible = false;
                        break;
                    }
                }
            }

            if(compatible){
                for(usize a = 0; a < acc.size(); ++a){
                    const auto& ca = acc[a];
                    if(ca.mode == AccessMode::Write)
                        stageWrites.insert(ca.typeId);
                    else
                        stageReads.insert(ca.typeId);
                }
                stage.push_back(sys);
                assigned[i] = 1;
                ++numAssigned;
            }
        }

        if(stage.empty()){
            for(usize i = 0; i < m_allSystems.size(); ++i){
                if(assigned[i])
                    continue;

                stage.push_back(m_allSystems[i]);
                assigned[i] = 1;
                ++numAssigned;
                break;
            }
        }

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
            if(stage[0]->enabled())
                stage[0]->update(world, delta);
        }
        else{
            pool.parallelFor(static_cast<usize>(0), stage.size(),
                [&stage, &world, delta](usize i){
                    if(stage[i]->enabled())
                        stage[i]->update(world, delta);
                }
            );
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

