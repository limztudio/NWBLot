// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Destruction releases only an unaccepted reservation.
template<typename ControlHandleT, typename PlanT>
class PlanReservationBase : NoCopy{
public:
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

    void discard()noexcept{
        if(m_reserved && m_control)
            m_control->discard(m_plan);
        m_reserved = false;
        m_control = nullptr;
    }


protected:
    ControlHandleT m_control;
    PlanT m_plan;
    b8 m_reserved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

