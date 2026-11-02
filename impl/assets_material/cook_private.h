// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"

#include <core/assets/cook_paths.h>
#include <core/assets/paths.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookArena = MaterialCookArena;
using CookString = MaterialCookString;
using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;
using ScratchPath = ::Path<ScratchArena>;

template<typename T>
using CookVector = MaterialCookVector<T>;

template<typename T>
using ScratchHashSet = HashSet<T, ScratchArena, Hasher<T>, EqualTo<T>>;

using MaterialBindInterfaceLookup = HashMap<Name, NotNull<const MaterialBindEntry*>, ScratchArena, Hasher<Name>, EqualTo<Name>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<MaterialBindInterfaceLookup> BuildMaterialBindInterfaceLookup(Span<const MaterialBindEntry> materialBindEntries, ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
void AppendMaterialBindGeneratedUpperIdentifier(const AStringView text, StringT& inOutText)noexcept(
    noexcept(DeclVal<void(*)(usize)noexcept>()(inOutText.size()))
    && noexcept(static_cast<b8>(inOutText.size() == DeclVal<const usize&>()))
    && noexcept(inOutText += char{})
    && noexcept(inOutText += s_MaterialBindFallbackUpperIdentifier.data())
){
    const usize beginSize = inOutText.size();
    for(const char ch : text)
        inOutText += IsAsciiAlphaNumeric(ch) ? ToAsciiUpper(ch) : '_';
    if(inOutText.size() == beginSize)
        inOutText += s_MaterialBindFallbackUpperIdentifier.data();
}

template<typename StringT>
void AppendMaterialBindGeneratedPascalIdentifier(const AStringView text, StringT& inOutText)noexcept(
    noexcept(DeclVal<void(*)(usize)noexcept>()(inOutText.size()))
    && noexcept(static_cast<b8>(inOutText.size() == DeclVal<const usize&>()))
    && noexcept(inOutText += char{})
    && noexcept(inOutText += DeclVal<const char&>())
    && noexcept(inOutText += s_MaterialBindFallbackPascalIdentifier.data())
){
    const usize beginSize = inOutText.size();
    b8 upperNext = true;
    for(const char ch : text){
        if(ch == '_'){
            upperNext = true;
            continue;
        }

        if(upperNext)
            inOutText += ToAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += s_MaterialBindFallbackPascalIdentifier.data();
}

template<typename ArenaT>
[[nodiscard]] AString<ArenaT> BuildMaterialBindGeneratedSymbol(const InitializerList<AStringView> nameSegments, const AStringView suffix, ArenaT& arena){
    AString<ArenaT> symbol(s_MaterialBindGeneratedSymbolPrefix, arena);
    b8 firstSegment = true;
    for(const AStringView nameSegment : nameSegments){
        if(!firstSegment)
            symbol += '_';
        AppendMaterialBindGeneratedUpperIdentifier(nameSegment, symbol);
        firstSegment = false;
    }
    symbol += suffix;
    return symbol;
}

template<typename ArenaT>
[[nodiscard]] AString<ArenaT> BuildMaterialBindAccessorName(const InitializerList<AStringView> nameSegments, ArenaT& arena){
    AString<ArenaT> functionName(s_MaterialBindAccessorNamePrefix, arena);
    for(const AStringView nameSegment : nameSegments)
        AppendMaterialBindGeneratedPascalIdentifier(nameSegment, functionName);
    return functionName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

