// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "component.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MessageTypeId = usize;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MessageTypeIdGenerator{
public:
    template<typename T>
    static MessageTypeId id(){
        static const MessageTypeId value = s_NextMessageTypeId++;
        return value;
    }


private:
    inline static Atomic<MessageTypeId> s_NextMessageTypeId{ 0 };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline MessageTypeId MessageType(){
    return __hidden_ecs::MessageTypeIdGenerator::id<Decay_T<T>>();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MessageBus : NoCopy{
private:
    using ChannelMapAllocator = Alloc::CustomAllocator<Pair<const MessageTypeId, UniquePtr<class IMessageChannel>>>;


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
    public:
        explicit MessageChannel(Alloc::CustomArena& arena)
            : m_readBuffer(Alloc::CustomAllocator<T>(arena))
        {}

    public:
        void post(const T& message){
            m_pending.push(MakeUnique<T>(message));
        }

        void post(T&& message){
            m_pending.push(MakeUnique<T>(Move(message)));
        }

        template<typename... Args>
        void emplace(Args&&... args){
            m_pending.push(MakeUnique<T>(Forward<Args>(args)...));
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

            UniquePtr<T> message;
            while(m_pending.try_pop(message))
                m_readBuffer.push_back(Move(*message));
        }

        virtual void clear()override{
            m_readBuffer.clear();

            UniquePtr<T> message;
            while(m_pending.try_pop(message)){
            }
        }


    private:
        ParallelQueue<UniquePtr<T>> m_pending;
        Vector<T, Alloc::CustomAllocator<T>> m_readBuffer;
    };


public:
    explicit MessageBus(Alloc::CustomArena& arena)
        : m_arena(arena)
        , m_channels(0, Hasher<MessageTypeId>(), EqualTo<MessageTypeId>(), ChannelMapAllocator(arena))
    {}
    ~MessageBus() = default;


public:
    template<typename T>
    void post(const T& message){
        auto* channel = getOrCreateChannel<T>();
        channel->post(message);
    }

    template<typename T>
    void post(T&& message){
        auto* channel = getOrCreateChannel<T>();
        channel->post(Move(message));
    }

    template<typename T, typename... Args>
    void emplace(Args&&... args){
        auto* channel = getOrCreateChannel<T>();
        channel->emplace(Forward<Args>(args)...);
    }

    template<typename T, typename Func>
    void consume(Func&& func)const{
        const auto* channel = getChannel<T>();
        if(!channel)
            return;
        channel->consume(Forward<Func>(func));
    }

    template<typename T>
    usize messageCount()const{
        const auto* channel = getChannel<T>();
        if(!channel)
            return 0;
        return channel->size();
    }

    void swapBuffers(){
        Vector<IMessageChannel*, Alloc::CustomAllocator<IMessageChannel*>> channels(
            Alloc::CustomAllocator<IMessageChannel*>(m_arena)
        );

        {
            Mutex::scoped_lock lock(m_channelsMutex);
            channels.reserve(m_channels.size());
            for(auto& [typeId, channel] : m_channels){
                (void)typeId;
                channels.push_back(channel.get());
            }
        }

        for(auto* channel : channels)
            channel->swapBuffers();
    }

    void clear(){
        Vector<IMessageChannel*, Alloc::CustomAllocator<IMessageChannel*>> channels(
            Alloc::CustomAllocator<IMessageChannel*>(m_arena)
        );

        {
            Mutex::scoped_lock lock(m_channelsMutex);
            channels.reserve(m_channels.size());
            for(auto& [typeId, channel] : m_channels){
                (void)typeId;
                channels.push_back(channel.get());
            }
        }

        for(auto* channel : channels)
            channel->clear();
    }


private:
    template<typename T>
    MessageChannel<T>* getOrCreateChannel(){
        const MessageTypeId typeId = MessageType<T>();

        Mutex::scoped_lock lock(m_channelsMutex);

        auto itr = m_channels.find(typeId);
        if(itr != m_channels.end())
            return static_cast<MessageChannel<T>*>(itr->second.get());

        auto channel = MakeUnique<MessageChannel<T>>(m_arena);
        auto* raw = channel.get();
        m_channels.emplace(typeId, Move(channel));
        return raw;
    }

    template<typename T>
    const MessageChannel<T>* getChannel()const{
        const MessageTypeId typeId = MessageType<T>();

        Mutex::scoped_lock lock(m_channelsMutex);

        auto itr = m_channels.find(typeId);
        if(itr == m_channels.end())
            return nullptr;
        return static_cast<const MessageChannel<T>*>(itr->second.get());
    }


private:
    Alloc::CustomArena& m_arena;
    mutable Mutex m_channelsMutex;
    HashMap<MessageTypeId, UniquePtr<IMessageChannel>, Hasher<MessageTypeId>, EqualTo<MessageTypeId>, ChannelMapAllocator> m_channels;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

