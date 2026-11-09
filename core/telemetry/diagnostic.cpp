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
inline constexpr StringView s_LifecycleCategory = "telemetry_diagnostic_lifecycle";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 AddTextBytes(usize& inOutPayloadBytes, const AStringView text)noexcept{
    return FitsU32(text.size()) && AddBinaryReserveBytes(inOutPayloadBytes, text.size());
}

[[nodiscard]] static b8 ValidateHeader(const EncodedDiagnosticPayloadHeader& header)noexcept{
    return header.magic == s_DiagnosticPayloadMagic && header.version == s_DiagnosticPayloadVersion && (header.flags & ~DiagnosticPayloadFlag::TerminatesProcess) == 0u;
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

[[nodiscard]] static Expected<usize> BuildCapturedPayload(const DiagnosticEventRecord& record, Array<u8, s_DiagnosticCapturePayloadCapacity>& payload)noexcept{
    const Array<AStringView, s_PayloadTextFieldCount> fields{ record.event, record.category, record.expression, record.message, record.file };
    usize payloadBytes = sizeof(EncodedDiagnosticPayloadHeader);
    for(const AStringView text : fields){
        if(text.size() > payload.size() - payloadBytes)
            return MakeUnexpected(Failure{});
        payloadBytes += text.size();
    }

    const EncodedDiagnosticPayloadHeader header = MakePayloadHeader(record);
    NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
    usize cursor = sizeof(header);
    for(const AStringView text : fields){
        if(!text.empty())
            NWB_MEMCPY(payload.data() + cursor, payload.size() - cursor, text.data(), text.size());
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


b8 BuildDiagnosticPayload(const DiagnosticEventRecord& record, TelemetryArena& arena, TelemetryBytes& outPayload){
    const Array<AStringView, __hidden_telemetry_diagnostic::s_PayloadTextFieldCount> fields{ record.event, record.category, record.expression, record.message, record.file };
    b8 aliasesOutput = false;
    for(const AStringView text : fields){
        const auto alias = ContainerDetail::SourceAliasesDestination(outPayload, Span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
        if(alias){
            if(text.size() > outPayload.size() - *alias){
                outPayload.clear();
                return false;
            }
            aliasesOutput = true;
        }
    }
    if(aliasesOutput){
        TelemetryBytes staged(outPayload.get_allocator().arena());
        const b8 result = BuildDiagnosticPayload(record, arena, staged);
        outPayload = Move(staged);
        return result;
    }
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
        const auto text = ReadBinaryText(encoded, field.byteCount, cursor);
        if(!text)
            return MakeUnexpected(Failure{});
        *field.destination = *text;
    }

    if(cursor != payloadBytes)
        return MakeUnexpected(Failure{});
    return parsedPayload;
}

b8 RecordDiagnostic(Recorder& recorder, const DiagnosticEventRecord& record, const u64 frameIndex, const u32 streamId){
    return recorder.recordBuiltPayload(EventKind::Diagnostic, frameIndex, streamId, [&record](TelemetryArena& arena, TelemetryBytes& payload){
        return BuildDiagnosticPayload(record, arena, payload);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DiagnosticCaptureGuard::DiagnosticCaptureGuard(
    Recorder& recorder,
    const u64 frameIndex,
    const u32 streamId,
    const Atomic<u64>* const sharedFrameIndex,
    const Atomic<u32>* const sharedStreamId
)
    : m_recorder(recorder)
    , m_frameIndex(frameIndex)
    , m_streamId(streamId)
    , m_sharedFrameIndex(sharedFrameIndex)
    , m_sharedStreamId(sharedStreamId)
{
    DiagnosticCaptureGuard* expectedGuard = nullptr;
    if(__hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(expectedGuard, this, MemoryOrder::acq_rel)){
        if(TryClaimDiagnosticEventCallback(__hidden_telemetry_diagnostic::CaptureCallback)){
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

    const b8 callbackOwnershipChanged = !ReleaseDiagnosticEventCallback(__hidden_telemetry_diagnostic::CaptureCallback);
    if(callbackOwnershipChanged)
        NWB_ASSERT(false);

    DiagnosticCaptureGuard* expected = this;
    const b8 guardOwnershipChanged = !__hidden_telemetry_diagnostic::g_CaptureGuard.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel);
    if(guardOwnershipChanged)
        NWB_ASSERT(false);

    if(callbackOwnershipChanged){
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Warning,
            .category = __hidden_telemetry_diagnostic::s_LifecycleCategory,
            .message = "Telemetry: diagnostic callback ownership changed before capture guard destruction",
            .file = __FILE__,
            .line = __LINE__,
        });
    }
    if(guardOwnershipChanged){
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Warning,
            .category = __hidden_telemetry_diagnostic::s_LifecycleCategory,
            .message = "Telemetry: diagnostic capture guard ownership changed before destruction",
            .file = __FILE__,
            .line = __LINE__,
        });
    }
}

void DiagnosticCaptureGuard::capture(const DiagnosticEventRecord& record)noexcept{
    if(!m_recorder.m_diagnosticCaptureEnabled.load(MemoryOrder::acquire))
        return;

    Array<u8, s_DiagnosticCapturePayloadCapacity> payload;
    const auto payloadBytes = __hidden_telemetry_diagnostic::BuildCapturedPayload(record, payload);
    if(!payloadBytes){
        m_recorder.dropDiagnosticCapture();
        return;
    }
    m_recorder.recordCapturedDiagnostic(frameIndex(), Span<const u8>(payload.data(), *payloadBytes), streamId());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

