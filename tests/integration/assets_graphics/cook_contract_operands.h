// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/global.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GraphicsCookContracts{
using StringPointer = const char*;

struct ThrowingConstIdentifierAppend{
    ThrowingConstIdentifierAppend& operator+=(char&&)noexcept;
    ThrowingConstIdentifierAppend& operator+=(const char&);
    ThrowingConstIdentifierAppend& operator+=(const char*)noexcept;


public:
    [[nodiscard]] usize size()const noexcept;
};

struct ThrowingImplicitIdentifierSize{
    explicit operator usize()const noexcept;
    operator u16()const;
};

[[nodiscard]] b8 operator==(const ThrowingImplicitIdentifierSize&, const usize&)noexcept;

struct ThrowingIdentifierSizeAppend{
    ThrowingIdentifierSizeAppend& operator+=(char)noexcept;
    ThrowingIdentifierSizeAppend& operator+=(const char*)noexcept;


public:
    [[nodiscard]] ThrowingImplicitIdentifierSize size()const noexcept;
};

struct NoThrowMeshletBytes{
    [[nodiscard]] usize size()const noexcept;
    [[nodiscard]] const u8* data()const noexcept;
    void clear()noexcept;
    void reserve(usize)noexcept;
    void push_back(u8)noexcept;
};

struct ThrowingMeshletAppendResult{
    ~ThrowingMeshletAppendResult()noexcept(false);
};

struct ThrowingMeshletAppend : NoThrowMeshletBytes{
    ThrowingMeshletAppendResult push_back(u8)noexcept;
};

struct NoThrowMeshletFailure{
    [[nodiscard]] b8 operator()(usize, TStringView)const noexcept;
};

struct ThrowingMeshletFailureBool{
    operator b8()const;
};

struct ThrowingMeshletFailureConversion{
    [[nodiscard]] ThrowingMeshletFailureBool operator()(usize, TStringView)const noexcept;
};

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

