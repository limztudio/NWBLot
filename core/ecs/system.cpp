// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <tbb/task_group.h>


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

    Vector<bool> assigned(m_allSystems.size(), false);
    usize numAssigned = 0;

    while(numAssigned < m_allSystems.size()){
        Vector<ISystem*> stage;

        // Collect writes and reads for the current stage
        HashSet<ComponentTypeId> stageWrites;
        HashSet<ComponentTypeId> stageReads;

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
                assigned[i] = true;
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

    for(auto& stage : m_stages){
        if(stage.size() == 1){
            // Single system: run inline, avoid task_group overhead
            if(stage[0]->enabled())
                stage[0]->update(world, delta);
        }
        else{
            // Multiple systems: run in parallel
            tbb::task_group group;

            for(auto* sys : stage){
                if(sys->enabled()){
                    group.run([sys, &world, delta](){
                        sys->update(world, delta);
                    });
                }
            }

            group.wait();
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

