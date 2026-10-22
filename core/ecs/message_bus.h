// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_id.h"

#include <global/allocation_size.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MessageBus : NoCopy{
    friend class World;


private:
    class IMessageChannel;


private:
    using MessageChannelPtr = GlobalUniquePtr<IMessageChannel>;


private:
    class IMessageChannel{
    public:
        virtual ~IMessageChannel()noexcept = default;


    public:
        virtual void swapBuffers() = 0;
        virtual void clear() = 0;
        virtual void discardForDestruction()noexcept = 0;
    };

    template<typename T>
    class MessageChannel final : public IMessageChannel{
    private:
        class PendingMessage{
        public:
            PendingMessage()noexcept = default;
            PendingMessage(const PendingMessage&) = delete;
            PendingMessage(PendingMessage&& rhs)noexcept(IsNothrowMoveConstructible_V<T>){
                moveFrom(Move(rhs));
            }
            template<typename... Args>
            explicit PendingMessage(InPlaceType, Args&&... args)noexcept(noexcept(Optional<T>(s_InPlace, Forward<Args>(args)...)))
                : m_value(s_InPlace, Forward<Args>(args)...)
            {}
            ~PendingMessage()noexcept(IsNothrowDestructible_V<Optional<T>>) = default;


        public:
            PendingMessage& operator=(const PendingMessage&) = delete;

            PendingMessage& operator=(PendingMessage&& rhs)noexcept(IsNothrowMoveConstructible_V<T>){
                if(this != &rhs){
                    m_value.reset();
                    moveFrom(Move(rhs));
                }
                return *this;
            }


        public:
            T& value()noexcept{
                NWB_ASSERT(m_value.has_value());
                return *m_value;
            }

            void reset()noexcept{
                m_value.reset();
            }


        private:
            void moveFrom(PendingMessage&& rhs)noexcept(IsNothrowMoveConstructible_V<T>){
                if(rhs.m_value)
                    m_value.emplace(Move(*rhs.m_value));
            }


        private:
            Optional<T> m_value;
        };


    public:
        explicit MessageChannel(Alloc::GlobalArena& arena)
            : m_pending(arena)
            , m_readBuffer(arena)
        {}


    public:
        void post(const T& message){
            m_pending.emplace(s_InPlace, message);
        }

        void post(T&& message){
            m_pending.emplace(s_InPlace, Move(message));
        }

        template<typename... Args>
        void emplace(Args&&... args){
            m_pending.emplace(s_InPlace, Forward<Args>(args)...);
        }

        template<typename Func>
        void consume(Func&& func)const noexcept(noexcept(DeclVal<Func&>()(DeclVal<const T&>()))){
            for(const auto& message : m_readBuffer)
                func(message);
        }

        usize size()const noexcept{ return m_readBuffer.size(); }


    public:
        virtual void swapBuffers()override{
            m_readBuffer.clear();
            drainPending([this](T& message){ m_readBuffer.push_back(Move(message)); });
        }

        virtual void clear()override{
            m_readBuffer.clear();
            drainPending([](T&)noexcept{});
        }

        virtual void discardForDestruction()noexcept override{
            m_readBuffer.clear();
            m_pending.clear();
        }


    private:
        template<typename Func>
        void drainPending(Func&& func){
            PendingMessage message;
            while(m_pending.try_pop(message)){
                func(message.value());
                message.reset();
            }
        }


    private:
        ParallelQueue<PendingMessage, Alloc::GlobalArena> m_pending;
        Vector<T, Alloc::GlobalArena> m_readBuffer;
    };


private:
    using ChannelVector = Vector<MessageChannelPtr, Alloc::GlobalArena>;


public:
    explicit MessageBus(Alloc::GlobalArena& arena)noexcept
        : m_arena(arena)
        , m_channels(arena)
    {}
    ~MessageBus()noexcept = default;


public:
    template<typename T>
    void post(const T& message){
        using MessageT = Decay_T<T>;
        auto& channel = getOrCreateChannel<MessageT>();
        channel.post(message);
    }

    template<typename T>
    void post(T&& message){
        using MessageT = Decay_T<T>;
        auto& channel = getOrCreateChannel<MessageT>();
        channel.post(Forward<T>(message));
    }

    template<typename T, typename... Args>
    void emplace(Args&&... args){
        using MessageT = Decay_T<T>;
        auto& channel = getOrCreateChannel<MessageT>();
        channel.emplace(Forward<Args>(args)...);
    }

    template<typename T, typename Func>
    void consume(Func&& func)const noexcept(noexcept(DeclVal<Func&>()(DeclVal<const Decay_T<T>&>()))){
        using MessageT = Decay_T<T>;
        const auto* channel = getChannel<MessageT>();
        if(!channel)
            return;
        channel->consume(Forward<Func>(func));
    }

    template<typename T>
    usize messageCount()const noexcept{
        using MessageT = Decay_T<T>;
        const auto* channel = getChannel<MessageT>();
        if(!channel)
            return 0;
        return channel->size();
    }

    void swapBuffers(){
        forEachChannel([](IMessageChannel& ch){ ch.swapBuffers(); });
    }

    void clear(){
        forEachChannel([](IMessageChannel& ch){ ch.clear(); });
    }


private:
    template<typename Func>
    void forEachChannel(Func&& func)noexcept(noexcept(DeclVal<Func&>()(DeclVal<IMessageChannel&>()))){
        if(!m_hasChannels.load(MemoryOrder::acquire))
            return;

        NothrowScopedLock lock(m_channelsMutex);

        for(auto& channel : m_channels){
            if(!channel)
                continue;
            func(*channel);
        }
    }

    // Destruction requires joined task producers and stopped external producers.
    void discardForDestruction()noexcept{
        forEachChannel([](IMessageChannel& channel)noexcept{ channel.discardForDestruction(); });
    }

    template<typename T>
    MessageChannel<T>& getOrCreateChannel(){
        const MessageTypeId typeId = MessageType<T>();

        if(m_hasChannels.load(MemoryOrder::acquire)){
            ScopedLock readLock(m_channelsMutex);
            if(typeId < m_channels.size()){
                auto& channel = m_channels[typeId];
                if(channel)
                    return *checked_cast<MessageChannel<T>*>(channel.get());
            }
        }

        ScopedLock writeLock(m_channelsMutex);
        if(typeId >= m_channels.size())
            m_channels.resize(typeId + 1u);

        auto& slot = m_channels[typeId];
        if(slot)
            return *checked_cast<MessageChannel<T>*>(slot.get());

        auto channel = MakeGlobalUnique<MessageChannel<T>>(m_arena, m_arena);
        if(!channel)
            throw AllocationException{};
        auto& created = *channel;
        slot = Move(channel);
        m_hasChannels.store(true, MemoryOrder::release);
        return created;
    }

    template<typename T>
    const MessageChannel<T>* getChannel()const noexcept{
        if(!m_hasChannels.load(MemoryOrder::acquire))
            return nullptr;

        const MessageTypeId typeId = MessageType<T>();

        NothrowScopedLock lock(m_channelsMutex);

        if(typeId >= m_channels.size())
            return nullptr;

        const auto& channel = m_channels[typeId];
        if(!channel)
            return nullptr;
        return checked_cast<const MessageChannel<T>*>(channel.get());
    }


private:
    Alloc::GlobalArena& m_arena;
    mutable Futex m_channelsMutex;
    Atomic<bool> m_hasChannels{ false };
    ChannelVector m_channels;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

