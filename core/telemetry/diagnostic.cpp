// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "diagnostic.h"

#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_diagnostic{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ByteView{
    using value_type = u8;

    const u8* bytes = nullptr;
    usize byteCount = 0u;

    [[nodiscard]] usize size()const{ return byteCount; }
    [[nodiscard]] const u8* data()const{ return bytes; }
    [[nodiscard]] u8 operator[](const usize index)const{ return bytes[index]; }
};

inline Atomic<DiagnosticCaptureGuard*> g_CaptureGuard{ nullptr };

[[nodiscard]] static AStringView SafeText(const char* const text)noexcept{
    return text ? AStringView(text) : AStringView();
}

[[nodiscard]] static bool TextFitsU32(const AStringView text)noexcept{
    return text.size() <= Limit<u32>::s_Max;
}

[[nodiscard]] static bool AddTextBytes(usize& inOutPayloadBytes, const AStringView text)noexcept{
    return TextFitsU32(text) && AddBinaryReserveBytes(inOutPayloadBytes, text.size());
}

template<typename Container>
static void AppendText(Container& outPayload, const AStringView text){
    if(!text.empty())
        BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, text.data(), text.size());
}

[[nodiscard]] static bool ValidateHeader(const EncodedDiagnosticPayloadHeader& header)noexcept{
    constexpr u16 s_KnownFlags = DiagnosticPayloadFlag::TerminatesProcess;
    return header.magic == s_DiagnosticPayloadMagic
        && header.version == s_DiagnosticPayloadVersion
        && (header.flags & ~s_KnownFlags) == 0u
    ;
}

template<typename StringT>
[[nodiscard]] static bool ReadText(
    const ByteView& payload,
    usize& inOutCursor,
    const u32 byteCount,
    StringT& outText
){
    if(!BinaryDetail::CanReadBytes(payload, inOutCursor, byteCount))
        return false;

    outText.assign(reinterpret_cast<const char*>(payload.data() + inOutCursor), byteCount);
    inOutCursor += byteCount;
    return true;
}

static void CaptureCallback(const DiagnosticEventRecord& record)noexcept{
    DiagnosticCaptureGuard* const guard = g_CaptureGuard.load(MemoryOrder::acquire);
    if(!guard)
        return;

    try{
        if(!guard->capture(record))
            NWB_LOGGER_WARNING(NWB_TEXT("Telemetry: diagnostic event record dropped"));
    }
    catch(...){}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildDiagnosticPayload(
    TelemetryArena&,
    const DiagnosticEventRecord& record,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    const AStringView event = __hidden_telemetry_diagnostic::SafeText(record.event);
    const AStringView category = __hidden_telemetry_diagnostic::SafeText(record.category);
    const AStringView expression = __hidden_telemetry_diagnostic::SafeText(record.expression);
    const AStringView message = __hidden_telemetry_diagnostic::SafeText(record.message);
    const AStringView file = __hidden_telemetry_diagnostic::SafeText(record.file);

    usize payloadBytes = sizeof(EncodedDiagnosticPayloadHeader);
    if(
        !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, event)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, category)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, expression)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, message)
        || !__hidden_telemetry_diagnostic::AddTextBytes(payloadBytes, file)
    )
        return false;

    EncodedDiagnosticPayloadHeader header;
    header.flags = record.terminatesProcess ? DiagnosticPayloadFlag::TerminatesProcess : DiagnosticPayloadFlag::None;
    header.instructionPointer = record.instructionPointer;
    header.line = record.line;
    header.eventBytes = static_cast<u32>(event.size());
    header.categoryBytes = static_cast<u32>(category.size());
    header.expressionBytes = static_cast<u32>(expression.size());
    header.messageBytes = static_cast<u32>(message.size());
    header.fileBytes = static_cast<u32>(file.size());

    outPayload.reserve(payloadBytes);
    AppendPOD(outPayload, header);
    __hidden_telemetry_diagnostic::AppendText(outPayload, event);
    __hidden_telemetry_diagnostic::AppendText(outPayload, category);
    __hidden_telemetry_diagnostic::AppendText(outPayload, expression);
    __hidden_telemetry_diagnostic::AppendText(outPayload, message);
    __hidden_telemetry_diagnostic::AppendText(outPayload, file);

    return outPayload.size() == payloadBytes;
}

bool ParseDiagnosticPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes,
    DiagnosticPayload& outPayload
){
    outPayload = DiagnosticPayload(arena);

    if(payloadBytes < sizeof(EncodedDiagnosticPayloadHeader) || !payload)
        return false;

    const __hidden_telemetry_diagnostic::ByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    EncodedDiagnosticPayloadHeader header;
    if(!ReadPOD(encoded, cursor, header))
        return false;
    if(!__hidden_telemetry_diagnostic::ValidateHeader(header))
        return false;

    outPayload.instructionPointer = header.instructionPointer;
    outPayload.line = header.line;
    outPayload.terminatesProcess = (header.flags & DiagnosticPayloadFlag::TerminatesProcess) != 0u;

    if(
        !__hidden_telemetry_diagnostic::ReadText(encoded, cursor, header.eventBytes, outPayload.event)
        || !__hidden_telemetry_diagnostic::ReadText(encoded, cursor, header.categoryBytes, outPayload.category)
        || !__hidden_telemetry_diagnostic::ReadText(encoded, cursor, header.expressionBytes, outPayload.expression)
        || !__hidden_telemetry_diagnostic::ReadText(encoded, cursor, header.messageBytes, outPayload.message)
        || !__hidden_telemetry_diagnostic::ReadText(encoded, cursor, header.fileBytes, outPayload.file)
    )
        return false;

    return cursor == payloadBytes;
}

bool RecordDiagnostic(
    Recorder& recorder,
    const DiagnosticEventRecord& record,
    const u64 frameIndex,
    const u32 streamId
){
    if(!recorder.enabled(EventKind::Diagnostic))
        return false;

    TelemetryBytes payload(recorder.arena());
    if(!BuildDiagnosticPayload(recorder.arena(), record, payload))
        return false;

    return recorder.record(EventKind::Diagnostic, PayloadFormat::Binary, frameIndex, payload.data(), payload.size(), streamId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DiagnosticCaptureGuard::DiagnosticCaptureGuard(Recorder& recorder)
    : m_recorder(recorder)
{
    DiagnosticCaptureGuard* expectedGuard = nullptr;
    if(__hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(
        expectedGuard,
        this,
        MemoryOrder::acq_rel
    )){
        DiagnosticEventCallback expectedCallback = nullptr;
        if(::DiagnosticDetail::g_EventCallback.compare_exchange_strong(
            expectedCallback,
            __hidden_telemetry_diagnostic::CaptureCallback,
            MemoryOrder::acq_rel
        )){
            m_installed = true;
        }
        else{
            expectedGuard = this;
            [[maybe_unused]] const bool clearedGuard = __hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(
                expectedGuard,
                nullptr,
                MemoryOrder::acq_rel
            );
        }
    }
}

DiagnosticCaptureGuard::~DiagnosticCaptureGuard(){
    if(!m_installed)
        return;

    DiagnosticEventCallback expectedCallback = __hidden_telemetry_diagnostic::CaptureCallback;
    [[maybe_unused]] const bool clearedCallback = ::DiagnosticDetail::g_EventCallback.compare_exchange_strong(
        expectedCallback,
        nullptr,
        MemoryOrder::acq_rel
    );

    DiagnosticCaptureGuard* expected = this;
    [[maybe_unused]] const bool clearedGuard = __hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(
        expected,
        nullptr,
        MemoryOrder::acq_rel
    );
}

bool DiagnosticCaptureGuard::capture(const DiagnosticEventRecord& record){
    return RecordDiagnostic(m_recorder, record, m_frameIndex, m_streamId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
