// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/crash/internal.h>

#include <global/fixed_buffer.h>
#include <global/scope_exit.h>
#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_snapshot_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Crash, MetadataRejectsTruncationAndEmbeddedNullWithoutChangingPublishedValue){
    using namespace NWB::Core::Crash;
    using namespace NWB::Core::Crash::CrashDetail;
    constexpr AStringView key("bounded_metadata");
    ASSERT_TRUE(SetCrashMetadata(key, "original"));
    Array<char, s_MaxShortText> longKey;
    longKey.fill('k');
    Array<char, s_MaxMediumText> longValue;
    longValue.fill('v');
    const Array<char, 3u> embedded{ 'a', '\0', 'b' };
    EXPECT_TRUE(SetCrashMetadata(AStringView(longKey.data(), longKey.size() - 1u), AStringView(longValue.data(), longValue.size() - 1u)));
    EXPECT_FALSE(SetCrashMetadata(AStringView(longKey.data(), longKey.size()), "value"));
    EXPECT_FALSE(SetCrashMetadata(key, AStringView(longValue.data(), longValue.size())));
    EXPECT_FALSE(SetCrashMetadata(AStringView(embedded.data(), embedded.size()), "value"));
    EXPECT_FALSE(SetCrashMetadata(key, AStringView(embedded.data(), embedded.size())));
    const auto request = BuildCrashDumpRequest(CrashReasonKind::ManualDump, 0u, {});
    b8 found = false;
    for(u32 i = 0u; i < request.metadataCount; ++i){
        if(FixedBufferView(request.metadata[i].key) == key){
            found = true;
            EXPECT_EQ(FixedBufferView(request.metadata[i].value), AStringView("original"));
        }
    }
    EXPECT_TRUE(found);
}

TEST(Crash, SnapshotKeepsPublishedMetadataDuringInterruptedWriter){
    constexpr AStringView s_Key("snapshot_interrupted_writer");
    constexpr AStringView s_Published("published_complete_value");
    ASSERT_TRUE(NWB::Core::Crash::SetCrashMetadata(s_Key, s_Published));

    NWB::Core::Crash::CrashDetail::CrashRequest request;
    b8 exercisedWriter = false;
    {
        NothrowScopedLock lock(NWB::Core::Crash::CrashDetail::g_State.mutex);
        for(auto& metadata : NWB::Core::Crash::CrashDetail::g_State.metadata){
            if(!metadata.used || AStringView(metadata.key.data()) != s_Key)
                continue;
            const auto original = metadata;
            ScopeExit restore([&metadata, &original]()noexcept{ metadata = original; });
            CopyFixedBuffer(metadata.value, AStringView("unpublished_partial_value"));
            request = NWB::Core::Crash::CrashDetail::BuildCrashDumpRequest(NWB::Core::Crash::CrashDetail::CrashReasonKind::ManualDump, 0u, {});
            exercisedWriter = true;
            break;
        }
    }
    ASSERT_TRUE(exercisedWriter);
    b8 found = false;
    for(u32 i = 0u; i < request.metadataCount; ++i){
        if(AStringView(request.metadata[i].key.data()) != s_Key)
            continue;
        EXPECT_EQ(AStringView(request.metadata[i].value.data()), s_Published);
        found = true;
    }
    EXPECT_TRUE(found);
}

TEST(Crash, ConcurrentPublicWritersPublishCompleteMetadataAndBreadcrumbRecords){
    constexpr AStringView s_Key("snapshot_concurrent_writer");
    constexpr AStringView s_Category("snapshot_concurrent_breadcrumb");
    constexpr usize s_Iterations = 2000u;
    Array<char, NWB::Core::Crash::CrashDetail::s_MaxMediumText> first = {};
    Array<char, NWB::Core::Crash::CrashDetail::s_MaxMediumText> second = {};
    for(usize i = 0u; i + 1u < first.size(); ++i){
        first[i] = 'a';
        second[i] = 'b';
    }
    const AStringView firstText(first.data(), first.size() - 1u);
    const AStringView secondText(second.data(), second.size() - 1u);
    ASSERT_TRUE(NWB::Core::Crash::SetCrashMetadata(s_Key, firstText));
    Atomic<u32> failures{ 0u };
    Latch start(2u);
    JoiningThread writer([&]()noexcept{
        start.arrive_and_wait();
        for(usize i = 0u; i < s_Iterations; ++i){
            const AStringView text = (i & 1u) != 0u ? firstText : secondText;
            if(!NWB::Core::Crash::SetCrashMetadata(s_Key, text))
                failures.fetch_add(1u, MemoryOrder::relaxed);
            NWB::Core::Crash::AddCrashBreadcrumb(s_Category, text);
        }
    });
    start.arrive_and_wait();
    for(usize iteration = 0u; iteration < s_Iterations; ++iteration){
        const auto request = NWB::Core::Crash::CrashDetail::BuildCrashDumpRequest(NWB::Core::Crash::CrashDetail::CrashReasonKind::ManualDump, 0u, {});
        b8 found = false;
        for(u32 i = 0u; i < request.metadataCount; ++i){
            const auto& metadata = request.metadata[i];
            if(AStringView(metadata.key.data()) != s_Key)
                continue;
            const AStringView value(metadata.value.data());
            if(value != firstText && value != secondText)
                failures.fetch_add(1u, MemoryOrder::relaxed);
            found = true;
        }
        if(!found)
            failures.fetch_add(1u, MemoryOrder::relaxed);
        for(u32 i = 0u; i < request.breadcrumbCount; ++i){
            const auto& breadcrumb = request.breadcrumbs[i];
            if(AStringView(breadcrumb.category) != s_Category)
                continue;
            const AStringView message(breadcrumb.message);
            if(message != firstText && message != secondText)
                failures.fetch_add(1u, MemoryOrder::relaxed);
        }
    }
    writer.join();
    EXPECT_EQ(failures.load(MemoryOrder::relaxed), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

