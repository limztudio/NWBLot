// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/process_memory_map.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_process_memory_map_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, MemoryMapLookupUsesHalfOpenRangesAndRejectsMissingAddresses){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    constexpr AStringView s_Maps = "00001000-00002000 r-xp 00000020 00:00 0 /tmp/first.so\n" "00003000-00004000 r--p 00000000 00:00 0 /tmp/second.so\n";
    ::Vector<LinuxProcessMemoryMapEntry, NWB::Core::Alloc::ScratchArena> entries(scratchArena);
    entries.reserve(2u);
    ParseLinuxProcessMemoryMaps(s_Maps, entries);
    ASSERT_EQ(entries.size(), 2u);
    const auto first = FindLinuxProcessMemoryMapForAddress(entries, 0x1000u);
    const auto last = FindLinuxProcessMemoryMapForAddress(entries, 0x3fffu);
    ASSERT_TRUE(first);
    ASSERT_TRUE(last);
    EXPECT_EQ(first->path, AStringView("/tmp/first.so"));
    EXPECT_EQ(last->path, AStringView("/tmp/second.so"));
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x2000u));
}

TEST(Global, MemoryMapRejectsMalformedOffsetsWithoutDiscardingValidNeighbors){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    constexpr AStringView s_Maps = "1000-2000 r-xp 0 00:00 0 /valid\n" "2000-3000 r-xp invalid 00:00 0 /bad\n" "3000-4000 r-xp 10000000000000000 00:00 0 /overflow\n" "4000-5000 r--p 20 00:00 0\n";
    ::Vector<LinuxProcessMemoryMapEntry, NWB::Core::Alloc::ScratchArena> entries(scratchArena);
    ParseLinuxProcessMemoryMaps(s_Maps, entries);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].fileOffset, 0u);
    EXPECT_EQ(entries[1].fileOffset, 0x20u);
    EXPECT_TRUE(entries[1].path.empty());
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x2000u));
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x3000u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

