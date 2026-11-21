// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "registry.h"

#include <core/common/log.h>

#include <global/assert.h>
#include <global/not_null.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AssetRegistry::waitForActiveDeserializations()const noexcept{
    if(m_activeDeserializations.load(MemoryOrder::acquire) == 0u)
        return;

    m_waitingModifiers.fetch_add(1u, MemoryOrder::acq_rel);
    UniqueLock idleLock(m_idleMutex);
    m_idleCondition.wait(idleLock, [this]()noexcept{ return m_activeDeserializations.load(MemoryOrder::acquire) == 0u; });
    m_waitingModifiers.fetch_sub(1u, MemoryOrder::release);
}

b8 AssetRegistry::registerCodec(UniquePtr<IAssetCodec>&& codec, const b8 replaceExisting){
    if(!codec){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetRegistry: rejected null codec registration"));
        return false;
    }

    const Name typeName = codec->assetType();
    if(!typeName){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetRegistry: rejected codec registration with empty asset type"));
        return false;
    }

    UniquePtr<IAssetCodec> replacedCodec;
    {
        ExclusiveCodecGuard lock(m_codecLock);
        auto found = m_codecs.find(typeName);
        if(found != m_codecs.end()){
            if(!replaceExisting){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetRegistry: codec for type '{}' is already registered"), ConvertString(typeName.resolvedText()));
                return false;
            }

            waitForActiveDeserializations();
            found = m_codecs.find(typeName);
            if(found != m_codecs.end()){
                replacedCodec = Move(found.value());
                found.value() = Move(codec);
                return true;
            }
        }

        if(!m_codecs.emplace(typeName, Move(codec)).second){
            FOUNDATION_ASSERT(false);
            return false;
        }
    }
    return true;
}

b8 AssetRegistry::unregisterCodec(const Name& assetType)noexcept{
    if(!assetType)
        return false;

    UniquePtr<IAssetCodec> removedCodec;
    {
        ExclusiveCodecGuard lock(m_codecLock);
        auto found = m_codecs.find(assetType);
        if(found == m_codecs.end())
            return false;
        waitForActiveDeserializations();
        found = m_codecs.find(assetType);
        if(found == m_codecs.end())
            return false;
        removedCodec = Move(found.value());
        m_codecs.erase(found);
    }
    return true;
}

Expected<UniquePtr<IAsset>> AssetRegistry::deserializeAsset(const Name& assetType, const Name& virtualPath, const Span<const u8> binary)const{
    if(!assetType){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetRegistry: asset type is empty"));
        return MakeUnexpected(Failure{});
    }

    IAssetCodec* rawCodec = nullptr;
    {
        SharedCodecGuard lock(m_codecLock);
        const auto found = m_codecs.find(assetType);
        if(found == m_codecs.end()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetRegistry: no codec for type '{}'"), ConvertString(assetType.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        rawCodec = found.value().get();
        m_activeDeserializations.fetch_add(1u, MemoryOrder::acq_rel);
    }
    ScopeExit releaseActive([this]()noexcept{
        if(m_activeDeserializations.fetch_sub(1u, MemoryOrder::acq_rel) == 1u && m_waitingModifiers.load(MemoryOrder::acquire) != 0u){
            NothrowScopedLock lock(m_idleMutex);
            m_idleCondition.notify_all();
        }
    });
    const NotNull<IAssetCodec*> codec(rawCodec);

    return codec->deserialize(virtualPath, binary, m_arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

