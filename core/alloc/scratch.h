// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScratchArena : NoCopy{
private:
    class Chunk{
    public:
        inline Chunk(usize size)
            : m_buffer(mi_malloc(size))
            , m_next(nullptr)
            , m_available(nullptr)
            , m_remaining(size)
        {}
        ~Chunk(){
            mi_free(m_buffer);
        }


    public:
        inline void* buffer()const{ return m_buffer; }
        inline Chunk* next()const{ return m_next; }
        inline usize remaining()const{ return m_remaining; }

        inline void* allocate(usize size){
            void* ret = nullptr;
            if(size <= m_remaining){
                ret = m_available;
                m_available = reinterpret_cast<u8*>(m_available) + size;
                m_remaining -= size;
            }
            return ret;
        }

        inline void add(Chunk* next){
            m_next = next;
        }


    private:
        void* m_buffer;

    private:
        Chunk* m_next;

        void* m_available;
        usize m_remaining;
    };


public:
    ScratchArena(usize initSize = 1024)
        : m_head(new Chunk(initSize))
        , m_last(m_head)
    {
    }
    ~ScratchArena(){
        for(auto* cur = m_head; cur;){
            auto* next = cur->next();
            delete cur;
            cur = next;
        }
    }


public:
    template <typename T>
    inline T* allocate(usize size){
        if(size > m_last->remaining()){
            m_last->add(new Chunk(size));
            m_last = m_last->next();
        }
        return reinterpret_cast<T*>(m_last->allocate(size));
    }


private:
    Chunk* m_head;
    Chunk* m_last;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

