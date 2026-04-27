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
    using MessageChannelPtr = CustomUniquePtr<IMessageChannel>;
    using ChannelMapAllocator = Alloc::CustomAllocator<Pair<const MessageTypeId, MessageChannelPtr>>;


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
        using MessagePtr = CustomUniquePtr<T>;
        using PendingAllocator = Alloc::CustomCacheAlignedAllocator<MessagePtr>;


    public:
        explicit MessageChannel(Alloc::CustomArena& arena)
            : m_arena(arena)
            , m_pending(PendingAllocator(arena))
            , m_readBuffer(Alloc::CustomAllocator<T>(arena))
        {}

    public:
        void post(const T& message){
            m_pending.push(MakeCustomUnique<T>(m_arena, message));
        }

        void post(T&& message){
            m_pending.push(MakeCustomUnique<T>(m_arena, Move(message)));
        }

        template<typename... Args>
        void emplace(Args&&... args){
            m_pending.push(MakeCustomUnique<T>(m_arena, Forward<Args>(args)...));
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

            MessagePtr message;
            bool hasMessage = m_pending.try_pop(message);
            while(hasMessage){
                m_readBuffer.push_back(Move(*message));
                hasMessage = m_pending.try_pop(message);
            }
        }

        virtual void clear()override{
            m_readBuffer.clear();

            MessagePtr message;
            bool hasMessage = m_pending.try_pop(message);
            while(hasMessage){
                hasMessage = m_pending.try_pop(message);
            }
        }


    private:
        Alloc::CustomArena& m_arena;
        ParallelQueue<MessagePtr, PendingAllocator> m_pending;
        Vector<T, Alloc::CustomAllocator<T>> m_readBuffer;
    };

    using ChannelMap = HashMap<
        MessageTypeId,
        MessageChannelPtr,
        Hasher<MessageTypeId>,
        EqualTo<MessageTypeId>,
        ChannelMapAllocator
    >;


public:
    explicit MessageBus(Alloc::CustomArena& arena)
        : m_arena(arena)
        , m_channels(0, Hasher<MessageTypeId>(), EqualTo<MessageTypeId>(), ChannelMapAllocator(arena))
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
        forEachChannelUnlocked([](IMessageChannel* ch){ ch->swapBuffers(); });
    }

    void clear(){
        forEachChannelUnlocked([](IMessageChannel* ch){ ch->clear(); });
    }


private:
    template<typename Func>
    void forEachChannelUnlocked(Func&& func){
        Alloc::ScratchArena<> scratchArena(4096);
        Vector<IMessageChannel*, Alloc::ScratchAllocator<IMessageChannel*>> channels{
            Alloc::ScratchAllocator<IMessageChannel*>(scratchArena)
        };

        {
            ScopedLock lock(m_channelsMutex);
            channels.reserve(m_channels.size());
            for(auto& [typeId, channel] : m_channels){
                static_cast<void>(typeId);
                channels.push_back(channel.get());
            }
        }

        for(auto* channel : channels)
            func(channel);
    }

    template<typename T>
    MessageChannel<T>* getOrCreateChannel(){
        const MessageTypeId typeId = MessageType<T>();

        ScopedLock lock(m_channelsMutex);

        auto itr = m_channels.find(typeId);
        if(itr != m_channels.end())
            return static_cast<MessageChannel<T>*>(itr.value().get());

        auto channel = MakeCustomUnique<MessageChannel<T>>(m_arena, m_arena);
        auto* raw = channel.get();
        m_channels.emplace(typeId, Move(channel));
        return raw;
    }

    template<typename T>
    const MessageChannel<T>* getChannel()const{
        const MessageTypeId typeId = MessageType<T>();

        ScopedLock lock(m_channelsMutex);

        auto itr = m_channels.find(typeId);
        if(itr == m_channels.end())
            return nullptr;
        return static_cast<const MessageChannel<T>*>(itr.value().get());
    }


private:
    Alloc::CustomArena& m_arena;
    mutable Futex m_channelsMutex;
    ChannelMap m_channels;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

