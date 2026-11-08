// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/filesystem/volume_file_system.h>

#include <tests/common/capturing_logger.h>
#include <tests/unit/filesystem/name_cases.adhd.h>

#include <global/filesystem/cleanup.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::FilesystemTest{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Filesystem;
using Path = NWB::Path;

inline constexpr Name s_TestArena = NWB::Tests::FilesystemCases::s_FilesystemNameTestsFilesystem;
inline constexpr i32 s_PublishLogFailure = NWB::Tests::FilesystemCases::s_FilesystemPublishLogFailure;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ThrowingPublishLogger final : public NWB::Core::Common::ILogger{
public:
    explicit ThrowingPublishLogger(NWB::Core::Common::LogArena& arena)noexcept
        : m_arena(arena)
    {}


public:
    virtual NWB::Core::Common::LogArena& arena()noexcept override{ return m_arena; }

    virtual void enqueue(NWB::Core::Common::LogString&&, NWB::Core::Common::LogType::Enum)override{ throw s_PublishLogFailure; }

    virtual void enqueue(const NWB::Core::Common::LogString&, NWB::Core::Common::LogType::Enum)override{ throw s_PublishLogFailure; }


private:
    NWB::Core::Common::LogArena& m_arena;
};

class BlockingRestoreLogger final : public NWB::Core::Common::ILogger{
public:
    BlockingRestoreLogger(NWB::Core::Common::LogArena& arena, const Path& blockedPath, const b8 throwAfterBlock = false)noexcept
        : m_arena(arena)
        , m_blockedPath(blockedPath)
        , m_throwAfterBlock(throwAfterBlock)
    {}

    virtual NWB::Core::Common::LogArena& arena()noexcept override{ return m_arena; }
    virtual void enqueue(NWB::Core::Common::LogString&&, NWB::Core::Common::LogType::Enum)override{ block(); }
    virtual void enqueue(const NWB::Core::Common::LogString&, NWB::Core::Common::LogType::Enum)override{ block(); }
    [[nodiscard]] b8 blocked()const noexcept{ return m_blocked; }

private:
    void block(){
        m_blocked = static_cast<b8>(EnsureDirectories(m_blockedPath));
        if(m_throwAfterBlock)
            throw s_PublishLogFailure;
    }
    NWB::Core::Common::LogArena& m_arena;
    const Path& m_blockedPath;
    b8 m_throwAfterBlock;
    b8 m_blocked = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FilesystemVolumeTest : public testing::Test{
public:
    FilesystemVolumeTest()
        : m_arena(s_TestArena)
        , m_directory(m_arena, "filesystem_unit_test_volume")
        , m_desc(m_arena)
        , m_loggerGuard(m_logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(RemoveAllIfExists(m_directory));
        ASSERT_TRUE(m_desc.volumeName.assign("test"));
        m_desc.mountDirectory = m_directory;
        m_desc.segmentSize = 4096;
        m_desc.metadataSize = 512;
        m_desc.createIfMissing = true;
        m_desc.usage = VolumeUsage::CookWrite;
    }

    virtual void TearDown()override{
        EXPECT_TRUE(RemoveAllIfExists(m_directory));
    }


protected:
    NWB::Core::Alloc::GlobalArena m_arena;
    Path m_directory;
    VolumeMountDesc m_desc;
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerGuard;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

