// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_graphics/components.adhd.h>

#include <core/assets/input_list.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/volume/built_asset.h>

#include <global/filesystem/executable_paths.h>
#include <global/sha1.h>

#include <gtest/gtest.h>

#include <global/atomic.h>
#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_asset_registry_concurrency_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, AssetRegistrySupportsReentrantDeserializeAndDrainsBeforeUnregister){
    class ProbeAsset final : public NWB::Core::Assets::IAsset{
    public:
        ProbeAsset(const Name& assetType, const Name& virtualPath)noexcept
            : IAsset(assetType, virtualPath)
        {}
    };

    class LeafProbeCodec final : public NWB::Core::Assets::IAssetCodec{
    public:
        explicit LeafProbeCodec(const Name& assetType)noexcept
            : IAssetCodec(assetType)
        {}


    public:
        virtual Expected<UniquePtr<NWB::Core::Assets::IAsset>> deserialize(const Name& virtualPath, const Span<const u8>, NWB::Core::Assets::AssetArena&)const override{
            return UniquePtr<NWB::Core::Assets::IAsset>(MakeUnique<ProbeAsset>(assetType(), virtualPath));
        }

#if defined(NWB_COOK)
        virtual b8 serialize(const NWB::Core::Assets::IAsset&, NWB::Core::Assets::AssetBytes&)const override{
            return true;
        }
#endif
    };

    class ReentrantBlockingCodec final : public NWB::Core::Assets::IAssetCodec{
    public:
        ReentrantBlockingCodec(const Name& assetType, const NWB::Core::Assets::AssetRegistry& registry, const Name& leafType, Atomic<b8>& entered, Atomic<b8>& release, Atomic<b8>& destroyedWhileActive)noexcept
            : IAssetCodec(assetType)
            , m_registry(registry)
            , m_leafType(leafType)
            , m_entered(entered)
            , m_release(release)
            , m_destroyedWhileActive(destroyedWhileActive)
        {}

        virtual ~ReentrantBlockingCodec()noexcept override{
            if(m_inFlight.load())
                m_destroyedWhileActive.store(true);
        }


    public:
        virtual Expected<UniquePtr<NWB::Core::Assets::IAsset>> deserialize(const Name& virtualPath, const Span<const u8> binary, NWB::Core::Assets::AssetArena&)const override{
            m_inFlight.store(true);
            auto nested = m_registry.deserializeAsset(m_leafType, Name("project/probe/leaf"), binary);
            if(!nested){
                m_inFlight.store(false);
                return MakeUnexpected(Failure{});
            }
            m_entered.store(true);
            while(!m_release.load())
                YieldThread();
            m_inFlight.store(false);
            return UniquePtr<NWB::Core::Assets::IAsset>(MakeUnique<ProbeAsset>(assetType(), virtualPath));
        }

#if defined(NWB_COOK)
        virtual b8 serialize(const NWB::Core::Assets::IAsset&, NWB::Core::Assets::AssetBytes&)const override{
            return true;
        }
#endif


    private:
        const NWB::Core::Assets::AssetRegistry& m_registry;
        Name m_leafType;
        Atomic<b8>& m_entered;
        Atomic<b8>& m_release;
        Atomic<b8>& m_destroyedWhileActive;
        mutable Atomic<b8> m_inFlight{false};
    };

    TestArena testArena;
    NWB::Core::Assets::AssetRegistry registry(testArena.arena);
    const Name parentType("probe_parent_asset");
    const Name leafType("probe_leaf_asset");
    Atomic<b8> entered{false};
    Atomic<b8> release{false};
    Atomic<b8> destroyedWhileActive{false};
    Atomic<b8> unregisterFinished{false};

    ASSERT_TRUE(registry.registerCodec(MakeUnique<LeafProbeCodec>(leafType)));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ReentrantBlockingCodec>(parentType, registry, leafType, entered, release, destroyedWhileActive)));

    Expected<UniquePtr<NWB::Core::Assets::IAsset>> parentResult = MakeUnexpected(Failure{});
    Thread deserializeThread([&](){
        parentResult = registry.deserializeAsset(parentType, Name("project/probe/parent"), Span<const u8>{});
    });

    while(!entered.load())
        YieldThread();

    b8 unregistered = false;
    Thread unregisterThread([&](){
        unregistered = registry.unregisterCodec(parentType);
        unregisterFinished.store(true);
    });

    for(u32 spin = 0u; spin < 64u; ++spin)
        YieldThread();
    EXPECT_FALSE(unregisterFinished.load());

    release.store(true);
    deserializeThread.join();
    unregisterThread.join();

    ASSERT_TRUE(parentResult);
    ASSERT_NE(parentResult->get(), nullptr);
    EXPECT_EQ((*parentResult)->assetType(), parentType);
    EXPECT_TRUE(unregistered);
    EXPECT_TRUE(unregisterFinished.load());
    EXPECT_FALSE(destroyedWhileActive.load());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

