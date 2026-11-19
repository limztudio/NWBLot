// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ControlHandleT, typename PlanT>
class PlanReservationBase : NoCopy{
public:
    PlanReservationBase()noexcept = default;

    PlanReservationBase(ControlHandleT control, const PlanT& plan, const b8 allowReserve = true)noexcept
        : m_control(Move(control))
        , m_plan(plan)
        , m_reserved(allowReserve && m_control && m_control->reserve(m_plan))
    {}
    PlanReservationBase(PlanReservationBase&& other)noexcept
        : m_control(Move(other.m_control))
        , m_plan(Move(other.m_plan))
        , m_reserved(other.m_reserved)
    {
        other.m_reserved = false;
    }
    ~PlanReservationBase()noexcept{
        discard();
    }


public:
    PlanReservationBase& operator=(PlanReservationBase&& other)noexcept{
        if(this != &other){
            discard();
            m_control = Move(other.m_control);
            m_plan = Move(other.m_plan);
            m_reserved = other.m_reserved;
            other.m_reserved = false;
        }
        return *this;
    }


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_reserved; }

    [[nodiscard]] const ControlHandleT& control()const noexcept{ return m_control; }
    [[nodiscard]] const PlanT& plan()const noexcept{ return m_plan; }

    void adopt(ControlHandleT control, const PlanT& plan, b8 reserved)noexcept{
        discard();
        m_control = Move(control);
        m_plan = plan;
        m_reserved = reserved;
    }

    void release()noexcept{
        m_reserved = false;
        m_control = nullptr;
    }

    void discard()noexcept{
        if(m_reserved && m_control)
            m_control->discard(m_plan);
        release();
    }


protected:
    ControlHandleT m_control;
    PlanT m_plan{};
    b8 m_reserved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

