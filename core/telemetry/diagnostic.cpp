// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "diagnostic.h"

#include <core/common/log.h>

#include <global/assert.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_diagnostic{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Atomic<DiagnosticCaptureGuard*> g_CaptureGuard{ nullptr };
inline constexpr usize s_PayloadTextFieldCount = 5u;
inline constexpr StringView s_WarningEventName = "warning";
inline constexpr StringView s_LifecycleCategory = "telemetry_diagnostic_lifecycle";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool AddTextBytes(usize& inOutPayloadBytes, const AStringView text)noexcept{
    return FitsU32(text.size()) && AddBinaryReserveBytes(inOutPayloadBytes, text.size());
}

[[nodiscard]] static bool ValidateHeader(const EncodedDiagnosticPayloadHeader& header)noexcept{
    constexpr u16 s_KnownFlags = DiagnosticPayloadFlag::TerminatesProcess;
    return header.magic == s_DiagnosticPayloadMagic && header.version == s_DiagnosticPayloadVersion && (header.flags & ~s_KnownFlags) == 0u;
}

[[nodiscard]] static Expected<AStringView> ReadText(const BinaryByteView& payload, usize& inOutCursor, const u32 byteCount)noexcept{
    if(!BinaryDetail::CanReadBytes(payload, inOutCursor, byteCount))
        return MakeUnexpected(Failure{});

    const AStringView text(reinterpret_cast<const char*>(payload.data() + inOutCursor), byteCount);
    inOutCursor += byteCount;
    return text;
}

[[nodiscard]] static EncodedDiagnosticPayloadHeader MakePayloadHeader(const DiagnosticEventRecord& record)noexcept{
    EncodedDiagnosticPayloadHeader header;
    header.flags = record.terminatesProcess ? DiagnosticPayloadFlag::TerminatesProcess : DiagnosticPayloadFlag::None;
    header.instructionPointer = record.instructionPointer;
    header.line = record.line;
    header.eventBytes = static_cast<u32>(record.event.size());
    header.categoryBytes = static_cast<u32>(record.category.size());
    header.expressionBytes = static_cast<u32>(record.expression.size());
    header.messageBytes = static_cast<u32>(record.message.size());
    header.fileBytes = static_cast<u32>(record.file.size());
    return header;
}

[[nodiscard]] static Expected<usize> BuildCapturedPayload(const DiagnosticEventRecord& record, u8 (&payload)[s_DiagnosticCapturePayloadCapacity])noexcept{
    const Array<AStringView, s_PayloadTextFieldCount> fields{ record.event, record.category, record.expression, record.message, record.file };
    usize payloadBytes = sizeof(EncodedDiagnosticPayloadHeader);
    for(const AStringView text : fields){
        if(text.size() > sizeof(payload) - payloadBytes)
            return MakeUnexpected(Failure{});
        payloadBytes += text.size();
    }

    const EncodedDiagnosticPayloadHeader header = MakePayloadHeader(record);
    NWB_MEMCPY(payload, sizeof(payload), &header, sizeof(header));
    usize cursor = sizeof(header);
    for(const AStringView text : fields){
        if(!text.empty())
            NWB_MEMCPY(payload + cursor, sizeof(payload) - cursor, text.data(), text.size());
        cursor += text.size();
    }
    return payloadBytes;
}

static void CaptureCallback(const DiagnosticEventRecord& record)noexcept{
    DiagnosticCaptureGuard* const guard = g_CaptureGuard.load(MemoryOrder::acquire);
    if(!guard)
        return;

    guard->capture(record);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildDiagnosticPayload(TelemetryArena&, const DiagnosticEventRecord& record, TelemetryBytes& outPayload){
    outPayload.clear();

    const AStringView event = record.event;
    const AStringView category = record.category;
    const AStringView expression = record.expression;
    const AStringView message = record.message;
    const AStringView file = record.file;

    usize payloadBytes = sizeof(EncodedDiagnosticPayloadHeader);
    if(
        !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, event)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, category)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, expression)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, message)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, file)
    )
        return false;

    const EncodedDiagnosticPayloadHeader header = __hidden_telemetry_diagnostic::MakePayloadHeader(record);

    outPayload.reserve(payloadBytes);
    AppendPOD(outPayload, header);
    AppendTextBytesNoReserveUnchecked(outPayload, event);
    AppendTextBytesNoReserveUnchecked(outPayload, category);
    AppendTextBytesNoReserveUnchecked(outPayload, expression);
    AppendTextBytesNoReserveUnchecked(outPayload, message);
    AppendTextBytesNoReserveUnchecked(outPayload, file);

    return outPayload.size() == payloadBytes;
}

Expected<DiagnosticPayload> ParseDiagnosticPayload(const void* const payload, const usize payloadBytes)noexcept{
    DiagnosticPayload parsedPayload;

    if(payloadBytes < sizeof(EncodedDiagnosticPayloadHeader) || !payload)
        return MakeUnexpected(Failure{});

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    const auto decodedHeader = ReadPOD<EncodedDiagnosticPayloadHeader>(encoded, cursor);
    if(!decodedHeader)
        return MakeUnexpected(Failure{});
    const EncodedDiagnosticPayloadHeader& header = *decodedHeader;
    if(!__hidden_telemetry_diagnostic::ValidateHeader(header))
        return MakeUnexpected(Failure{});

    parsedPayload.instructionPointer = header.instructionPointer;
    parsedPayload.line = header.line;
    parsedPayload.terminatesProcess = (header.flags & DiagnosticPayloadFlag::TerminatesProcess) != 0u;

    struct TextField{
        u32 byteCount;
        NotNull<AStringView*> destination;
    };
    const Array<TextField, __hidden_telemetry_diagnostic::s_PayloadTextFieldCount> textFields{{
        { header.eventBytes, NotNull(&parsedPayload.event) },
        { header.categoryBytes, NotNull(&parsedPayload.category) },
        { header.expressionBytes, NotNull(&parsedPayload.expression) },
        { header.messageBytes, NotNull(&parsedPayload.message) },
        { header.fileBytes, NotNull(&parsedPayload.file) },
    }};
    for(const TextField& field : textFields){
        const auto text = __hidden_telemetry_diagnostic::ReadText(encoded, cursor, field.byteCount);
        if(!text)
            return MakeUnexpected(Failure{});
        *field.destination = *text;
    }

    if(cursor != payloadBytes)
        return MakeUnexpected(Failure{});
    return parsedPayload;
}

bool RecordDiagnostic(Recorder& recorder, const DiagnosticEventRecord& record, const u64 frameIndex, const u32 streamId){
    return recorder.recordBuiltPayload(EventKind::Diagnostic, frameIndex, streamId, [&record](TelemetryArena& arena, TelemetryBytes& payload){
        return BuildDiagnosticPayload(arena, record, payload);
    }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DiagnosticCaptureGuard::DiagnosticCaptureGuard(Recorder& recorder)
    : m_recorder(recorder)
{
    DiagnosticCaptureGuard* expectedGuard = nullptr;
    if(__hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(expectedGuard, this, MemoryOrder::acq_rel)){
        DiagnosticEventCallback expectedCallback = nullptr;
        if(::DiagnosticDetail::g_EventCallback.compare_exchange_strong(expectedCallback, __hidden_telemetry_diagnostic::CaptureCallback, MemoryOrder::acq_rel)){
            m_installed = true;
        }
        else{
            expectedGuard = this;
            if(!__hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(expectedGuard, nullptr, MemoryOrder::acq_rel)){
                NWB_LOGGER_WARNING(NWB_TEXT("Telemetry: diagnostic capture guard ownership changed while installing callback"));
                NWB_ASSERT(false);
            }
        }
    }
}

DiagnosticCaptureGuard::~DiagnosticCaptureGuard()noexcept{
    if(!m_installed)
        return;

    while(::DiagnosticDetail::g_EventActive.testAndSet(MemoryOrder::acquire)){
        ::DiagnosticDetail::g_EventActive.wait(true, MemoryOrder::relaxed);
    }

    DiagnosticEventCallback expectedCallback = __hidden_telemetry_diagnostic::CaptureCallback;
    const bool callbackOwnershipChanged = !::DiagnosticDetail::g_EventCallback.compare_exchange_strong(expectedCallback, nullptr, MemoryOrder::acq_rel);
    if(callbackOwnershipChanged)
        NWB_ASSERT(false);

    DiagnosticCaptureGuard* expected = this;
    const bool guardOwnershipChanged = !__hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel);
    if(guardOwnershipChanged)
        NWB_ASSERT(false);

    ::DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
    ::DiagnosticDetail::g_EventActive.notifyAll();
    if(callbackOwnershipChanged)
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = __hidden_telemetry_diagnostic::s_WarningEventName, .category = __hidden_telemetry_diagnostic::s_LifecycleCategory, .message = "Telemetry: diagnostic callback ownership changed before capture guard destruction", .file = __FILE__, .line = __LINE__, });
    if(guardOwnershipChanged)
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = __hidden_telemetry_diagnostic::s_WarningEventName, .category = __hidden_telemetry_diagnostic::s_LifecycleCategory, .message = "Telemetry: diagnostic capture guard ownership changed before destruction", .file = __FILE__, .line = __LINE__, });
}

void DiagnosticCaptureGuard::capture(const DiagnosticEventRecord& record)noexcept{
    if(!m_recorder.m_diagnosticCaptureEnabled.load(MemoryOrder::acquire))
        return;

    u8 payload[s_DiagnosticCapturePayloadCapacity];
    const auto payloadBytes = __hidden_telemetry_diagnostic::BuildCapturedPayload(record, payload);
    if(!payloadBytes){
        m_recorder.dropDiagnosticCapture();
        return;
    }
    m_recorder.recordCapturedDiagnostic(frameIndex(), NotNull<const u8*>(payload), *payloadBytes, streamId());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

