// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/parser.h>
#include <core/metascript/literal_decoder.h>
#include <global/algorithm.h>
#include <global/filesystem/utility.h>

#include <tests/common/value_storage.h>
#include <tests/unit/metascript/generated/cpp_unwind.adhd.h>
#include <tests/unit/metascript/generated/owner_result.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::Metascript{

template<typename T>
[[nodiscard]] T& CaptureReturnedValue(TestValueSlot<T>& slot, T value){ return slot.take(value); }

class ReaderWithSpan : public Core::Metascript::IMetaReader{
public:
    Expected<usize> read(Core::Metascript::MChar* data, usize count)final{ return readBytes({reinterpret_cast<u8*>(data), count}); }
    virtual Expected<usize> readBytes(Span<u8> buffer) = 0;
};
struct LerpMoveOnlyOperand{
    i32 value = 10;
    LerpMoveOnlyOperand() = default;
    explicit LerpMoveOnlyOperand(const i32 v) : value(v){}
    LerpMoveOnlyOperand(const LerpMoveOnlyOperand&) = delete;
    LerpMoveOnlyOperand(LerpMoveOnlyOperand&&) = default;
    LerpMoveOnlyOperand operator-(const LerpMoveOnlyOperand& rhs)&&{ return LerpMoveOnlyOperand(value - rhs.value); }
    LerpMoveOnlyOperand operator*(LerpMoveOnlyOperand&& rhs)&&{ return LerpMoveOnlyOperand(value * rhs.value); }
    LerpMoveOnlyOperand operator+(LerpMoveOnlyOperand&& rhs)&{ return LerpMoveOnlyOperand(value + rhs.value); }
    LerpMoveOnlyOperand operator+(LerpMoveOnlyOperand&& rhs)&&{ return LerpMoveOnlyOperand(value + rhs.value + 100); }
};


template<typename T>
[[nodiscard]] b8 DeserializeReflected(T& value, const Core::Metascript::Value& input){ return value.deserializeNwb(input); }

template<typename T, typename Visitor>
[[nodiscard]] b8 VisitReflectedDependencies(const Core::Metascript::Value& input, Visitor&& visitor){ return T::extractNwbAssetDependencies(input, Forward<Visitor>(visitor)); }

inline void AppendMovedValue(Core::Metascript::Value& receiver, Core::Metascript::Value& source){ receiver.append(Move(source)); }

[[nodiscard]] inline Optional<NotNull<const Core::Metascript::Value*>> BorrowStructMetadata(const Core::Metascript::Document& document, AStringView name){
    const auto it = document.structs().find(name);
    if(it == document.structs().end())
        return {};
    return MakeNotNull(&it.value());
}

struct DecodedCharacter{ b8 valid; i64 value; };
[[nodiscard]] inline constexpr DecodedCharacter DecodeConstantCharacter(AStringView text, i64 initial)noexcept{
    const b8 decoded = Core::Metascript::LiteralDecoder::TryDecodeCharLiteral(text, initial);
    return {decoded, initial};
}

inline constexpr auto s_InvalidCharacterResult = DecodeConstantCharacter("'\\U100000000'", 7);
inline constexpr b8 s_InvalidCharacterDecoded = s_InvalidCharacterResult.valid;
inline constexpr i64 s_InvalidCharacterValue = s_InvalidCharacterResult.value;

inline constexpr b8 s_CountsCopyConstructible = IsCopyConstructible_V<OwnedCounts>;
inline constexpr b8 s_CountsCopyAssignable = IsAssignable_V<OwnedCounts&, const OwnedCounts&>;
inline constexpr b8 s_CountsMoveConstructible = IsConstructible_V<OwnedCounts, OwnedCounts&&>;
inline constexpr b8 s_CountsMoveAssignable = IsAssignable_V<OwnedCounts&, OwnedCounts&&>;
inline constexpr b8 s_ReadyNothrow = noexcept(DeclVal<const CppUnwindCallback&>().ready());
inline constexpr b8 s_ConcreteRecordNothrow = noexcept(DeclVal<const CppUnwindCallback&>().record(DeclVal<InplaceFunction<0u, u32()>&>()));
inline constexpr b8 s_VirtualRecordNothrow = noexcept(DeclVal<const IUnwindCallback&>().record(DeclVal<InplaceFunction<0u, u32()>&>()));

template<typename PathT>
[[nodiscard]] DiagnosticEventText FormatOwnedDiagnosticPath(const PathT& path, ArenaBase& storage){
    const auto owned = PathToString<char>(storage, path);
    return MakeDiagnosticEventText(AStringView("{}"), AStringView(owned));
}
template<typename PathT>
[[nodiscard]] DiagnosticEventText FormatDeferredDiagnosticPath(const PathT& path){ return MakeDiagnosticEventText(AStringView("{}"), PathToString<char>(path)); }
template<typename PathT>
[[nodiscard]] DiagnosticEventText FormatWideDiagnosticPath(const PathT& path){ return MakeDiagnosticEventText(WStringView(L"{}"), PathToString<char>(path)); }
[[nodiscard]] inline char DiagnosticTerminator(const DiagnosticEventText& text)noexcept{ return text.value.back(); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

