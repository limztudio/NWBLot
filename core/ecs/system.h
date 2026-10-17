// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "component.h"
#include "type_id.h"

#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessMode{
    enum Enum : u8{
        Read,
        Write
    };
};


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
    explicit ISystem(Alloc::GlobalArena& arena)noexcept;
    ISystem(const ISystem&) = delete;
    virtual ~ISystem() = default;


public:
    ISystem& operator=(const ISystem&) = delete;


public:
    virtual void prepare(World& world){ static_cast<void>(world); }

    // The scheduler keeps this task incomplete until update and its submitted descendants finish.
    [[nodiscard]] virtual Expected<void, CpuTaskAdmissionError::Enum> update(World& world, f32 delta) = 0;

    [[nodiscard]] virtual CpuTaskOptions taskOptions()const noexcept{
        return { .cost = CpuTaskCost::Heavy };
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
    explicit SystemScheduler(Alloc::GlobalArena& arena)noexcept;
    ~SystemScheduler() = default;


public:
    void addSystem(ISystem& system);
    void removeSystem(ISystem& system)noexcept;
    void clear()noexcept;
    void rebuild();
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> execute(World& world, f32 delta);


private:
    Alloc::GlobalArena& m_arena;

    // Every conflicting predecessor keeps registration order; independent work has no stage barrier.
    Vector<DependencyList, Alloc::GlobalArena> m_dependencies;
    SystemList m_allSystems;
    // Retain capacity across execute() calls.
    Vector<CpuTaskScheduler::TaskHandle, Alloc::GlobalArena> m_executionHandles;
    Vector<CpuTaskScheduler::TaskHandle, Alloc::GlobalArena> m_executionDependencies;
    Vector<Expected<void, CpuTaskAdmissionError::Enum>, Alloc::GlobalArena> m_executionResults;
    bool m_dirty;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

