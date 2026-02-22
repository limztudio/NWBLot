// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "world.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SystemScheduler::SystemScheduler()
    : m_dirty(false)
{}
SystemScheduler::~SystemScheduler()
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SystemScheduler::addSystem(ISystem* system){
    m_allSystems.push_back(system);
    m_dirty = true;
}

void SystemScheduler::removeSystem(ISystem* system){
    auto itr = std::find(m_allSystems.begin(), m_allSystems.end(), system);
    if(itr != m_allSystems.end()){
        m_allSystems.erase(itr);
        m_dirty = true;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        Vector<ISystem*> stage;

        // Collect writes and reads for the current stage
        HashSet<ComponentTypeId, Hasher<ComponentTypeId>, EqualTo<ComponentTypeId>,
                Alloc::ScratchAllocator<ComponentTypeId>> stageWrites(
            0, Hasher<ComponentTypeId>(), EqualTo<ComponentTypeId>(),
            Alloc::ScratchAllocator<ComponentTypeId>(scratchArena)
        );
        HashSet<ComponentTypeId, Hasher<ComponentTypeId>, EqualTo<ComponentTypeId>,
                Alloc::ScratchAllocator<ComponentTypeId>> stageReads(
            0, Hasher<ComponentTypeId>(), EqualTo<ComponentTypeId>(),
            Alloc::ScratchAllocator<ComponentTypeId>(scratchArena)
        );

        for(usize i = 0; i < m_allSystems.size(); ++i){
            if(assigned[i])
                continue;

            ISystem* sys = m_allSystems[i];
            const auto& acc = sys->access();

            // Check compatibility with current stage
            bool compatible = true;
            for(usize a = 0; a < acc.size(); ++a){
                const auto& ca = acc[a];
                if(ca.mode == AccessMode::Write){
                    // Cannot write something another system in this stage writes or reads
                    if(stageWrites.count(ca.typeId) || stageReads.count(ca.typeId)){
                        compatible = false;
                        break;
                    }
                }
                else{
                    // Cannot read something another system in this stage writes
                    if(stageWrites.count(ca.typeId)){
                        compatible = false;
                        break;
                    }
                }
            }

            if(compatible){
                // Add this system's access tokens to the stage
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

        if(!stage.empty())
            m_stages.push_back(Move(stage));
    }

    m_dirty = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SystemScheduler::execute(World& world, float delta){
    if(m_dirty)
        rebuild();

    Alloc::ThreadPool& pool = world.taskPool();

    for(auto& stage : m_stages){
        if(stage.size() == 1){
            // Single system: run inline, avoid scheduling overhead
            if(stage[0]->enabled())
                stage[0]->update(world, delta);
        }
        else{
            // Multiple systems: run in parallel via thread pool
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

