// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_id.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MessageTypeId = usize;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline MessageTypeId MessageType(){
    return ECSDetail::TypeCounter<ECSDetail::MessageTypeTag>::id<Decay_T<T>>();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MessageBus : NoCopy{
private:
    class IMessageChannel;
    using MessageChannelPtr = GlobalUniquePtr<IMessageChannel>;
    using ChannelLock = SharedMutex::scoped_lock;


private:
    class IMessageChannel{
    public:
        virtual ~IMessageChannel() = default;


    public:
        virtual void swapBuffers() = 0;
        virtual void clear() = 0;
    };

    template<typename T>
    class MessageChannel final : public IMessageChannel{
    private:
        class PendingMessage{
        public:
            PendingMessage() = default;
            PendingMessage(const PendingMessage&) = delete;
            PendingMessage(PendingMessage&& rhs){
                moveFrom(Move(rhs));
            }
            template<typename... Args>
            explicit PendingMessage(InPlaceType, Args&&... args)
                : m_value(s_InPlace, Forward<Args>(args)...)
            {}
            ~PendingMessage() = default;

            PendingMessage& operator=(const PendingMessage&) = delete;
            PendingMessage& operator=(PendingMessage&& rhs){
                if(this != &rhs){
                    m_value.reset();
                    moveFrom(Move(rhs));
                }
                return *this;
            }

            T& value(){
                NWB_ASSERT(m_value.has_value());
                return *m_value;
            }

            void reset(){
                m_value.reset();
            }


        private:
            void moveFrom(PendingMessage&& rhs){
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
        void consume(Func&& func)const{
            for(const auto& message : m_readBuffer)
                func(message);
        }

        usize size()const{ return m_readBuffer.size(); }


    public:
        virtual void swapBuffers()override{
            m_readBuffer.clear();
            drainPending([this](T& message){ m_readBuffer.push_back(Move(message)); });
        }

        virtual void clear()override{
            m_readBuffer.clear();
            drainPending([](T& message){ static_cast<void>(message); });
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

    using ChannelVector = Vector<MessageChannelPtr, Alloc::GlobalArena>;


private:
    static ChannelLock readChannelLock(SharedMutex& mutex){
        return ChannelLock(mutex, false);
    }

    static ChannelLock writeChannelLock(SharedMutex& mutex){
        return ChannelLock(mutex, true);
    }


public:
    explicit MessageBus(Alloc::GlobalArena& arena)
        : m_arena(arena)
        , m_channels(arena)
    {}
    ~MessageBus() = default;


public:
    template<typename T>
    void post(const T& message){
        using MessageT = Decay_T<T>;
        auto* channel = getOrCreateChannel<MessageT>();
        channel->post(message);
    }

    template<typename T>
    void post(T&& message){
        using MessageT = Decay_T<T>;
        auto* channel = getOrCreateChannel<MessageT>();
        channel->post(Forward<T>(message));
    }

    template<typename T, typename... Args>
    void emplace(Args&&... args){
        using MessageT = Decay_T<T>;
        auto* channel = getOrCreateChannel<MessageT>();
        channel->emplace(Forward<Args>(args)...);
    }

    template<typename T, typename Func>
    void consume(Func&& func)const{
        using MessageT = Decay_T<T>;
        const auto* channel = getChannel<MessageT>();
        if(!channel)
            return;
        channel->consume(Forward<Func>(func));
    }

    template<typename T>
    usize messageCount()const{
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
    void forEachChannel(Func&& func){
        if(!m_hasChannels.load(MemoryOrder::acquire))
            return;

        ChannelLock lock = readChannelLock(m_channelsMutex);

        for(auto& channel : m_channels){
            if(!channel)
                continue;
            func(*channel);
        }
    }

    template<typename T>
    MessageChannel<T>* getOrCreateChannel(){
        const MessageTypeId typeId = MessageType<T>();

        if(m_hasChannels.load(MemoryOrder::acquire)){
            ChannelLock readLock = readChannelLock(m_channelsMutex);
            if(typeId < m_channels.size()){
                auto& channel = m_channels[typeId];
                if(channel)
                    return checked_cast<MessageChannel<T>*>(channel.get());
            }
        }

        ChannelLock writeLock = writeChannelLock(m_channelsMutex);
        if(typeId >= m_channels.size())
            m_channels.resize(typeId + 1u);

        auto& slot = m_channels[typeId];
        if(slot)
            return checked_cast<MessageChannel<T>*>(slot.get());

        auto channel = MakeGlobalUnique<MessageChannel<T>>(m_arena, m_arena);
        auto* raw = channel.get();
        slot = Move(channel);
        m_hasChannels.store(true, MemoryOrder::release);
        return raw;
    }

    template<typename T>
    const MessageChannel<T>* getChannel()const{
        if(!m_hasChannels.load(MemoryOrder::acquire))
            return nullptr;

        const MessageTypeId typeId = MessageType<T>();

        ChannelLock lock = readChannelLock(m_channelsMutex);

        if(typeId >= m_channels.size())
            return nullptr;

        const auto& channel = m_channels[typeId];
        if(!channel)
            return nullptr;
        return checked_cast<const MessageChannel<T>*>(channel.get());
    }


private:
    Alloc::GlobalArena& m_arena;
    mutable SharedMutex m_channelsMutex;
    Atomic<bool> m_hasChannels{ false };
    ChannelVector m_channels;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

