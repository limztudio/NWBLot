// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "component.h"

#include <core/alloc/cpu_task.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessMode{
    enum Enum : u8{
        Read,
        Write
    };
};


using SystemTypeId = usize;


template<typename T>
inline SystemTypeId SystemType(){
    return ECSDetail::TypeCounter<ECSDetail::SystemTypeTag>::id<Decay_T<T>>();
}


struct ComponentAccess{
    ComponentTypeId typeId;
    AccessMode::Enum mode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ISystem{
    friend class SystemScheduler;


public:
    explicit ISystem(Alloc::GlobalArena& arena);
    ISystem(const ISystem&) = delete;
    ISystem& operator=(const ISystem&) = delete;
    virtual ~ISystem() = default;


public:
    virtual void prepare(World& world){ static_cast<void>(world); }
    // The scheduler keeps this task incomplete until update and its submitted descendants finish.
    virtual void update(World& world, f32 delta) = 0;
    [[nodiscard]] virtual Alloc::CpuTaskOptions taskOptions()const{
        return { .cost = Alloc::CpuTaskCost::Heavy };
    }


protected:
    template<typename T>
    inline void readAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Read);
    }
    template<typename T>
    inline void writeAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Write);
    }
    void registerAccess(ComponentTypeId typeId, AccessMode::Enum mode);

private:
    Vector<ComponentAccess, Alloc::GlobalArena> m_access;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SystemScheduler{
private:
    using SystemList = Vector<ISystem*, Alloc::GlobalArena>;
    using DependencyList = Vector<usize, Alloc::GlobalArena>;


public:
    explicit SystemScheduler(Alloc::GlobalArena& arena);
    ~SystemScheduler() = default;


public:
    void addSystem(ISystem& system);
    void removeSystem(ISystem& system);
    void clear();
    void rebuild();
    void execute(World& world, f32 delta);


private:
    Alloc::GlobalArena& m_arena;

    // Every conflicting predecessor keeps registration order; independent work has no stage barrier.
    Vector<DependencyList, Alloc::GlobalArena> m_dependencies;
    SystemList m_allSystems;
    bool m_dirty;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

