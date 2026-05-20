// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "component.h"


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
    virtual void update(World& world, f32 delta) = 0;


protected:
    template<typename T>
    inline void readAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Read);
    }
    template<typename T>
    inline void writeAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Write);
    }


private:
    void registerAccess(ComponentTypeId typeId, AccessMode::Enum mode);

    Vector<ComponentAccess, Alloc::GlobalArena> m_access;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SystemScheduler{
private:
    using Stage = Vector<ISystem*, Alloc::GlobalArena>;


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

    // Each stage is a group of systems that can safely run in parallel
    Vector<Stage, Alloc::GlobalArena> m_stages;
    Stage m_allSystems;
    bool m_dirty;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

