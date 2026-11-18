// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/memory_sdk.h>

#include <core/telemetry/diagnostic.h>
#include <core/telemetry/recorder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::TelemetryCases{

inline constexpr b8 s_CaptureIsNothrow = noexcept(DeclVal<Core::Telemetry::DiagnosticCaptureGuard&>().capture(DeclVal<const DiagnosticEventRecord&>()));

[[nodiscard]] inline const Core::Telemetry::TelemetryArena& TelemetryHeapStorage(const HeapStorage& storage)noexcept{ return storage.arena; }

inline constexpr usize s_PlatformUnitBytes = sizeof(tchar);

class TelemetryPlatformUnits{
public:
    explicit TelemetryPlatformUnits(const Array<u32, 3>& codeUnits)noexcept{
        for(usize index = 0; index < m_units.size(); ++index)
            m_units[index] = static_cast<tchar>(codeUnits[index]);
    }
    [[nodiscard]] TStringView view()const noexcept{ return {m_units.data(), m_units.size()}; }

private:
    Array<tchar, 3> m_units;
};

[[nodiscard]] inline Span<const u8> PlatformUnitBytes(TStringView text)noexcept{
    return {reinterpret_cast<const u8*>(text.data()), text.size() * sizeof(tchar)};
}

[[nodiscard]] inline TStringView BorrowPlatformUnits(Span<const u8> bytes)noexcept{
    return {reinterpret_cast<const tchar*>(bytes.data()), bytes.size() / sizeof(tchar)};
}

inline void TestReplaceWithMovedPayload(Core::Telemetry::TelemetryBytes& destination, Core::Telemetry::TelemetryBytes& source){
    std::destroy_at(&destination);
    std::construct_at(&destination, Move(source));
}

inline b8 AppendMovedTelemetryPayload(Core::Telemetry::Recorder& recorder, const Core::Telemetry::EventHeader& header, Core::Telemetry::TelemetryBytes& bytes){
    return recorder.append(header, Move(bytes));
}

inline b8 RecordMovedPayloadImpl(Core::Telemetry::Recorder& recorder, Core::Telemetry::EventKind::Enum kind, u64 frame, Core::Telemetry::TelemetryBytes& bytes, u32 stream){
    return recorder.recordPayload(kind, frame, Move(bytes), stream);
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

