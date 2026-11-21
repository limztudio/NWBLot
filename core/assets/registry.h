// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#if !defined(FOUNDATION_PLATFORM_WINDOWS)
#include <pthread.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetRegistry final : NoCopy{
private:
    using CodecMap = HashMap<Name, UniquePtr<IAssetCodec>, Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;

#if defined(FOUNDATION_PLATFORM_WINDOWS)
    class RegistryRWLock final : NoCopy{
    public:
        constexpr RegistryRWLock()noexcept = default;
        void lockShared()const noexcept{ AcquireSRWLockShared(&m_lock); }
        void unlockShared()const noexcept{ ReleaseSRWLockShared(&m_lock); }
        void lockExclusive()const noexcept{ AcquireSRWLockExclusive(&m_lock); }
        void unlockExclusive()const noexcept{ ReleaseSRWLockExclusive(&m_lock); }
    private:
        mutable SRWLOCK m_lock = SRWLOCK_INIT;
    };
#else
    class RegistryRWLock final : NoCopy{
    public:
        RegistryRWLock()noexcept{ pthread_rwlock_init(&m_lock, nullptr); }
        ~RegistryRWLock()noexcept{ pthread_rwlock_destroy(&m_lock); }
        void lockShared()const noexcept{ pthread_rwlock_rdlock(&m_lock); }
        void unlockShared()const noexcept{ pthread_rwlock_unlock(&m_lock); }
        void lockExclusive()const noexcept{ pthread_rwlock_wrlock(&m_lock); }
        void unlockExclusive()const noexcept{ pthread_rwlock_unlock(&m_lock); }
    private:
        mutable pthread_rwlock_t m_lock{};
    };
#endif

    class SharedCodecGuard final : NoCopy{
    public:
        explicit SharedCodecGuard(const RegistryRWLock& lock)noexcept
            : m_lock(lock)
        {
            m_lock.lockShared();
        }
        ~SharedCodecGuard()noexcept{ m_lock.unlockShared(); }
    private:
        const RegistryRWLock& m_lock;
    };

    class ExclusiveCodecGuard final : NoCopy{
    public:
        explicit ExclusiveCodecGuard(const RegistryRWLock& lock)noexcept
            : m_lock(lock)
        {
            m_lock.lockExclusive();
        }
        ~ExclusiveCodecGuard()noexcept{ m_lock.unlockExclusive(); }
    private:
        const RegistryRWLock& m_lock;
    };


public:
    explicit AssetRegistry(AssetArena& arena)
        : m_arena(arena)
        , m_codecs(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    b8 registerCodec(UniquePtr<IAssetCodec>&& codec, b8 replaceExisting = false);
    b8 unregisterCodec(const Name& assetType)noexcept;

    [[nodiscard]] Expected<UniquePtr<IAsset>> deserializeAsset(const Name& assetType, const Name& virtualPath, Span<const u8> binary)const;


private:
    void waitForActiveDeserializations()const noexcept;


private:
    mutable RegistryRWLock m_codecLock;
    mutable Futex m_idleMutex;
    mutable ConditionVariableAny m_idleCondition;
    mutable Atomic<usize> m_activeDeserializations{ 0u };
    mutable Atomic<u32> m_waitingModifiers{ 0u };
    AssetArena& m_arena;
    CodecMap m_codecs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

