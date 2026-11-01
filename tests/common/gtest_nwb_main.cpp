// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/arena_owner_fixture.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/common/application_entry.h>

#include <global/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ArenaOwnerTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ArenaMemoryStats OwnerStats(const Name& name, const ArenaMemorySource::Enum source)noexcept{
    const ArenaMemoryOwnerRecord* record = FirstArenaMemoryOwnerRecord();
    while(record){
        ArenaMemoryOwnerSnapshot snapshot;
        record = ReadArenaMemoryOwnerRecord(*record, snapshot);
        if(snapshot.ownerName == name && snapshot.source == source)
            return snapshot.stats;
    }
    return {};
}

void ExpectMemoryStatsEqual(const ArenaMemoryStats& expected, const ArenaMemoryStats& actual){
    EXPECT_EQ(actual.reservedBytes, expected.reservedBytes);
    EXPECT_EQ(actual.usedBytes, expected.usedBytes);
    EXPECT_EQ(actual.peakUsedBytes, expected.peakUsedBytes);
    EXPECT_EQ(actual.allocationCount, expected.allocationCount);
    EXPECT_EQ(actual.reallocationCount, expected.reallocationCount);
    EXPECT_EQ(actual.deallocationCount, expected.deallocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gtest_nwb_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_ThreadsafeDeathTestStyle = "threadsafe";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Alloc::GlobalArena& Arena(){
    static Core::Alloc::GlobalArena s_Arena(s_DefaultTestArena);
    return s_Arena;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 CapturingLogger::messageCount()const noexcept{
    ScopedLock lock(m_mutex);
    return m_messageCount;
}

u32 CapturingLogger::errorCount()const noexcept{
    ScopedLock lock(m_mutex);
    return m_errorCount;
}

Core::Common::LogType::Enum CapturingLogger::lastType()const noexcept{
    ScopedLock lock(m_mutex);
    return m_lastType;
}

b8 CapturingLogger::sawMessageContaining(const TStringView text)const noexcept{
    ScopedLock lock(m_mutex);

    for(const LogString& message : m_messages){
        if(message.find(text) != LogString::npos)
            return true;
    }
    return false;
}

b8 CapturingLogger::sawErrorContaining(const TStringView text)const noexcept{
    ScopedLock lock(m_mutex);

    for(const LogString& error : m_errors){
        if(error.find(text) != LogString::npos)
            return true;
    }
    return false;
}

void CapturingLogger::record(const TStringView str, const Core::Common::LogType::Enum type){
    ScopedLock lock(m_mutex);

    ++m_messageCount;
    m_lastType = type;
    m_messages.emplace_back(str, m_arena);

    if(type == Core::Common::LogType::Error){
        ++m_errorCount;
        m_errors.emplace_back(str, m_arena);
    }
}

usize CountText(const AStringView text, const AStringView expected)noexcept{
    if(expected.empty())
        return 0u;

    usize count = 0u;
    usize offset = 0u;
    while(offset < text.size()){
        const usize found = text.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}

void RecordUnsignedTestProperty(const AStringView key, const u64 value){
    Array<char, s_TestPropertyNumberBufferBytes> text = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(AInteropString(key), text.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int GoogleTestEntryPoint(const isize argc, tchar** argv, [[maybe_unused]] void* userData){
    Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "test common initialization failed\n";
        return -1;
    }

    int googleTestArgc = static_cast<int>(argc);
    ::testing::InitGoogleTest(&googleTestArgc, argv);
    // Re-exec death tests because common initialization starts workers before suites run.
    GTEST_FLAG_SET(death_test_style, __hidden_gtest_nwb_main::s_ThreadsafeDeathTestStyle.data());
    return RUN_ALL_TESTS();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(::NWB::Tests::GoogleTestEntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

