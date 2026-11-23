// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_id.h"

#include <global/allocation_size.h>
#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MessageBus : NoCopy{
    friend class World;


private:
    class IMessageChannel;


private:
    static constexpr usize s_FastChannelCount = 64u;
    using MessageChannelPtr = GlobalUniquePtr<IMessageChannel>;


private:
    class IMessageChannel{
    public:
        virtual ~IMessageChannel()noexcept = default;


    public:
        virtual b8 swapBuffers() = 0;
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
                FOUNDATION_ASSERT(m_value.has_value());
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
        virtual b8 swapBuffers()override{
            m_readBuffer.clear();
            drainPending([this](T& message){ m_readBuffer.push_back(Move(message)); });
            return !m_readBuffer.empty();
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
    using OwnedChannelVector = Vector<MessageChannelPtr, Alloc::GlobalArena>;
    using ChannelRawVector = Vector<IMessageChannel*, Alloc::GlobalArena>;


public:
    explicit MessageBus(Alloc::GlobalArena& arena)noexcept
        : m_arena(arena)
        , m_ownedChannels(arena)
        , m_overflowChannels(arena)
    {}
    ~MessageBus()noexcept = default;


public:
    template<typename T>
    void post(const T& message){
        using MessageT = Decay_T<T>;
        const MessageTypeId typeId = MessageType<MessageT>();
        auto& channel = getOrCreateChannel<MessageT>(typeId);
        channel.post(message);
        markWriteDirty(typeId);
    }

    template<typename T>
    void post(T&& message){
        using MessageT = Decay_T<T>;
        const MessageTypeId typeId = MessageType<MessageT>();
        auto& channel = getOrCreateChannel<MessageT>(typeId);
        channel.post(Forward<T>(message));
        markWriteDirty(typeId);
    }

    template<typename T, typename... Args>
    void emplace(Args&&... args){
        using MessageT = Decay_T<T>;
        const MessageTypeId typeId = MessageType<MessageT>();
        auto& channel = getOrCreateChannel<MessageT>(typeId);
        channel.emplace(Forward<Args>(args)...);
        markWriteDirty(typeId);
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
        u64 pending = m_dirtyWriteFastMask.exchange(0u, MemoryOrder::acq_rel) | m_activeReadFastMask;
        u64 nextActive = 0u;
        while(pending != 0u){
            const u32 slot = static_cast<u32>(CountTrailingZeros(pending));
            pending &= pending - 1u;
            IMessageChannel* const ch = m_fastChannels[slot].load(MemoryOrder::acquire);
            if(ch && ch->swapBuffers())
                nextActive |= (1ull << slot);
        }
        m_activeReadFastMask = nextActive;

        if(m_hasOverflow.load(MemoryOrder::acquire)){
            NothrowScopedLock lock(m_channelsMutex);
            for(IMessageChannel* const channel : m_overflowChannels){
                if(channel && channel->swapBuffers())
                    continue;
            }
        }
    }

    void clear(){
        u64 dirty = m_dirtyWriteFastMask.exchange(0u, MemoryOrder::acq_rel) | m_activeReadFastMask;
        m_activeReadFastMask = 0u;
        while(dirty != 0u){
            const u32 slot = static_cast<u32>(CountTrailingZeros(dirty));
            dirty &= dirty - 1u;
            IMessageChannel* const ch = m_fastChannels[slot].load(MemoryOrder::acquire);
            if(ch)
                ch->clear();
        }

        if(m_hasOverflow.load(MemoryOrder::acquire)){
            NothrowScopedLock lock(m_channelsMutex);
            for(IMessageChannel* const channel : m_overflowChannels){
                if(channel)
                    channel->clear();
            }
        }
    }


private:
    FOUNDATION_INLINE void markWriteDirty(const MessageTypeId typeId)noexcept{
        if(typeId < s_FastChannelCount){
            const u64 bit = 1ull << typeId;
            if((m_dirtyWriteFastMask.load(MemoryOrder::relaxed) & bit) == 0u)
                m_dirtyWriteFastMask.fetch_or(bit, MemoryOrder::release);
        }
    }

    void discardForDestruction()noexcept{
        m_dirtyWriteFastMask.store(0u, MemoryOrder::relaxed);
        m_activeReadFastMask = 0u;
        NothrowScopedLock lock(m_channelsMutex);
        for(auto& channel : m_ownedChannels){
            if(channel)
                channel->discardForDestruction();
        }
    }

    template<typename T>
    MessageChannel<T>& getOrCreateChannel(const MessageTypeId typeId){
        if(typeId < s_FastChannelCount){
            if(IMessageChannel* const fast = m_fastChannels[typeId].load(MemoryOrder::acquire))
                return *checked_cast<MessageChannel<T>*>(fast);

            ScopedLock writeLock(m_channelsMutex);
            if(IMessageChannel* const fast = m_fastChannels[typeId].load(MemoryOrder::relaxed))
                return *checked_cast<MessageChannel<T>*>(fast);

            auto channel = MakeGlobalUnique<MessageChannel<T>>(m_arena, m_arena);
            if(!channel)
                throw AllocationException{};
            auto& created = *channel;
            m_ownedChannels.push_back(Move(channel));
            m_fastChannels[typeId].store(&created, MemoryOrder::release);
            return created;
        }

        const usize overflowIndex = typeId - s_FastChannelCount;
        if(m_hasOverflow.load(MemoryOrder::acquire)){
            ScopedLock readLock(m_channelsMutex);
            if(overflowIndex < m_overflowChannels.size()){
                if(IMessageChannel* const existing = m_overflowChannels[overflowIndex])
                    return *checked_cast<MessageChannel<T>*>(existing);
            }
        }

        ScopedLock writeLock(m_channelsMutex);
        if(overflowIndex >= m_overflowChannels.size())
            m_overflowChannels.resize(overflowIndex + 1u, nullptr);

        if(IMessageChannel* const existing = m_overflowChannels[overflowIndex])
            return *checked_cast<MessageChannel<T>*>(existing);

        auto channel = MakeGlobalUnique<MessageChannel<T>>(m_arena, m_arena);
        if(!channel)
            throw AllocationException{};
        auto& created = *channel;
        m_ownedChannels.push_back(Move(channel));
        m_overflowChannels[overflowIndex] = &created;
        m_hasOverflow.store(true, MemoryOrder::release);
        return created;
    }

    template<typename T>
    const MessageChannel<T>* getChannel()const noexcept{
        const MessageTypeId typeId = MessageType<T>();
        if(typeId < s_FastChannelCount)
            return checked_cast<const MessageChannel<T>*>(m_fastChannels[typeId].load(MemoryOrder::acquire));

        if(!m_hasOverflow.load(MemoryOrder::acquire))
            return nullptr;

        const usize overflowIndex = typeId - s_FastChannelCount;
        NothrowScopedLock lock(m_channelsMutex);
        if(overflowIndex >= m_overflowChannels.size())
            return nullptr;

        return checked_cast<const MessageChannel<T>*>(m_overflowChannels[overflowIndex]);
    }


private:
    Alloc::GlobalArena& m_arena;
    Array<Atomic<IMessageChannel*>, s_FastChannelCount> m_fastChannels{};
    Atomic<u64> m_dirtyWriteFastMask{ 0u };
    u64 m_activeReadFastMask = 0u;
    Atomic<b8> m_hasOverflow{ false };
    mutable Futex m_channelsMutex;
    OwnedChannelVector m_ownedChannels;
    ChannelRawVector m_overflowChannels;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

