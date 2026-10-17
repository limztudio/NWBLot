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


TEST(Crash, SnapshotKeepsPublishedMetadataDuringInterruptedWriter){
    constexpr AStringView s_Key("snapshot_interrupted_writer");
    constexpr AStringView s_Published("published_complete_value");
    ASSERT_TRUE(NWB::Core::Crash::SetCrashMetadata(s_Key, s_Published));

    NWB::Core::Crash::Detail::CrashRequest request;
    bool exercisedWriter = false;
    {
        NothrowScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        for(auto& metadata : NWB::Core::Crash::Detail::g_State.metadata){
            if(!metadata.used || AStringView(metadata.key) != s_Key)
                continue;
            const auto original = metadata;
            ScopeExit restore([&metadata, &original]()noexcept{ metadata = original; });
            CopyFixedBuffer(metadata.value, AStringView("unpublished_partial_value"));
            request = NWB::Core::Crash::Detail::BuildCrashDumpRequest(NWB::Core::Crash::Detail::CrashReasonKind::ManualDump, 0u, {});
            exercisedWriter = true;
            break;
        }
    }
    ASSERT_TRUE(exercisedWriter);
    bool found = false;
    for(u32 i = 0u; i < request.metadataCount; ++i){
        if(AStringView(request.metadata[i].key) != s_Key)
            continue;
        EXPECT_EQ(AStringView(request.metadata[i].value), s_Published);
        found = true;
    }
    EXPECT_TRUE(found);
}

TEST(Crash, ConcurrentPublicWritersPublishCompleteMetadataAndBreadcrumbRecords){
    constexpr AStringView s_Key("snapshot_concurrent_writer");
    constexpr AStringView s_Category("snapshot_concurrent_breadcrumb");
    constexpr usize s_Iterations = 2000u;
    char first[NWB::Core::Crash::Detail::s_MaxMediumText] = {};
    char second[NWB::Core::Crash::Detail::s_MaxMediumText] = {};
    for(usize i = 0u; i + 1u < sizeof(first); ++i){
        first[i] = 'a';
        second[i] = 'b';
    }
    const AStringView firstText(first, sizeof(first) - 1u);
    const AStringView secondText(second, sizeof(second) - 1u);
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
        const auto request = NWB::Core::Crash::Detail::BuildCrashDumpRequest(NWB::Core::Crash::Detail::CrashReasonKind::ManualDump, 0u, {});
        bool found = false;
        for(u32 i = 0u; i < request.metadataCount; ++i){
            const auto& metadata = request.metadata[i];
            if(AStringView(metadata.key) != s_Key)
                continue;
            const AStringView value(metadata.value);
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

