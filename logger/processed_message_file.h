// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


class ProcessedMessageFile{
public:
    using StreamType = BasicOutputFileStream<tchar>;


public:
    explicit ProcessedMessageFile(LogArena& arena)
        : m_arena(arena)
        , m_filePath(arena)
    {}
    ~ProcessedMessageFile()noexcept{ close(); }


public:
    b8 open(BasicStringView<tchar> fileNameBase){
        close();
        if(fileNameBase.empty())
            return false;

        const auto localTime = GetLocalTime();
        if(!localTime)
            return false;

        const auto executableDirectory = ExecutablePaths(m_arena).directory();
        if(!executableDirectory)
            return false;

        static Atomic<u64> sequence{ 0u };
        constexpr usize s_MaxAttempts = 1024u;
        for(usize attempt = 0u; attempt < s_MaxAttempts; ++attempt){
            const LogString fileName = StringFormat(
                m_arena,
                FOUNDATION_TEXT("{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{}_{}.log"),
                fileNameBase,
                localTime->tm_year + s_LocalTimeYearBase,
                localTime->tm_mon + s_LocalTimeMonthBase,
                localTime->tm_mday,
                localTime->tm_hour,
                localTime->tm_min,
                localTime->tm_sec,
                CurrentProcessId(),
                sequence.fetch_add(1u, MemoryOrder::relaxed)
            );
            m_filePath = *executableDirectory / fileName;
            m_stream.open(m_filePath, s_FileOpenWrite | StreamType::noreplace);
            if(m_stream.is_open())
                return true;
            m_stream.clear();
            const auto exists = FileExistsNoFollow(m_filePath);
            if(!exists || !*exists)
                return false;
        }
        return false;
    }

    b8 openByExecutableName(){
        const auto executableName = ExecutablePaths(m_arena).name();
        if(!executableName)
            return false;

        const LogString executableNameString = PathToString<tchar>(m_arena, *executableName);
        return open(executableNameString);
    }

    [[nodiscard]] const Path& path()const noexcept{ return m_filePath; }

    void close(){
        if(!m_stream.is_open())
            return;

        m_stream.close();
        if(!m_stream.good())
            CaptureDiagnosticEvent("logger_processed_message_file", "Processed log stream failed while closing", __FILE__, __LINE__);
    }

    [[nodiscard]] b8 writeLine(BasicStringView<tchar> line){
        if(!m_stream.is_open())
            return false;

        m_stream << line << static_cast<tchar>('\n');
        m_stream.flush();
        return m_stream.good();
    }


private:
    LogArena& m_arena;
    StreamType m_stream;
    Path m_filePath;
};


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
