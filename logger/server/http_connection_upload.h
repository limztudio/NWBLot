// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>
#include <logger/server/server_values.adhd.h>

#include <core/alloc/module.h>

#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace LoggerServerImpl{


inline constexpr usize s_ConnectionInitialBufferCapacity = ::NWB::Log::s_LogServerValueModuleConnectionInitialBufferCapacity;
inline constexpr AStringView s_UploadDiagnosticCategory = ::NWB::Log::s_LogServerValueModuleUploadDiagnosticCategory;


namespace ConnectionUploadKind{
    enum Enum : u8{
        LogMessage,
        Crash,
        Telemetry,
    };
};

struct ConnectionInfo : NoCopy{
    u8* buffer = nullptr;
    usize size = 0u;
    usize capacity = 0u;
    u32 rejectionStatus = 0u;
    ConnectionUploadKind::Enum uploadKind = ConnectionUploadKind::LogMessage;
    b8 crashUploadOwned = false;
    Path crashUploadPath;
    OutputFileStream crashUploadStream;


public:
    explicit ConnectionInfo(LogArena& arena)
        : crashUploadPath(arena)
    {}
    ~ConnectionInfo()noexcept{
        if(crashUploadOwned)
            discardCrashUpload();
        else if(!closeCrashUploadStream())
            CaptureDiagnosticEvent(s_UploadDiagnosticCategory, "Failed to close crash upload stream", __FILE__, __LINE__);
        Core::Alloc::CoreFree(buffer);
    }


public:
    [[nodiscard]] b8 append(const BinaryByteView uploadData){
        const usize appendSize = uploadData.size();
        if(appendSize == 0u)
            return true;

        if(size > Limit<usize>::s_Max - appendSize)
            return false;

        if(uploadKind == ConnectionUploadKind::Crash)
            return appendCrashUpload(uploadData);

        const usize requiredSize = size + appendSize;
        if(!reserve(requiredSize))
            return false;

        FOUNDATION_MEMCPY(buffer + size, appendSize, uploadData.data(), appendSize);
        size = requiredSize;
        return true;
    }

    [[nodiscard]] b8 appendCrashUpload(const BinaryByteView uploadData){
        if(!crashUploadStream.is_open())
            return false;

        crashUploadStream.write(reinterpret_cast<const char*>(uploadData.data()), static_cast<StreamSize>(uploadData.size()));
        if(!crashUploadStream.good())
            return false;

        size += uploadData.size();
        return true;
    }

    [[nodiscard]] b8 closeCrashUploadStream()noexcept{
        if(crashUploadStream.is_open())
            crashUploadStream.close();

        return crashUploadStream.good();
    }

    void discardCrashUpload()noexcept{
        if(!crashUploadOwned)
            return;
        if(!closeCrashUploadStream())
            CaptureDiagnosticEvent(s_UploadDiagnosticCategory, "Failed to close discarded crash upload stream", __FILE__, __LINE__);
        const auto removed = RemoveFile(crashUploadPath);
        if(!removed){
            const DiagnosticEventText diagnostic = MakeDiagnosticEventText("Failed to remove discarded crash upload '{}', error {}", crashUploadPath.c_str(), removed.error().value());
            CaptureDiagnosticEvent(s_UploadDiagnosticCategory, diagnostic.view(), __FILE__, __LINE__);
        }
        crashUploadOwned = false;
    }

    [[nodiscard]] b8 reserve(const usize requiredSize)noexcept{
        if(requiredSize <= capacity)
            return true;

        const usize newCapacity = ::NextGrowingCapacity(capacity, requiredSize, s_ConnectionInitialBufferCapacity);

        auto* newBuffer = static_cast<u8*>(Core::Alloc::CoreRealloc(buffer, newCapacity));
        if(!newBuffer)
            return false;

        buffer = newBuffer;
        capacity = newCapacity;
        return true;
    }
};


};


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

