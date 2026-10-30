// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/adhd/compiler.h>
#include <core/metascript/parser.h>

#include <global/inplace_function.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdhdCompilerTestsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdhdTestArenaTag{};
using AdhdTestArena = NWB::Tests::TestArena<AdhdTestArenaTag>;

using NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MString;
using NWB::Core::Metascript::MStringView;
using NWB::Core::Metascript::MetaArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MockIncludeResolver final : public NWB::Core::Metascript::IIncludeResolver{
public:
    [[nodiscard]] virtual b8 resolveInclude(const MStringView includePath, const b8, MetaArena& arena, MString& outResolvedKey, MString& outContent)override{
        ++resolveCallCount;
        outResolvedKey = MString(includePath.data(), includePath.size(), arena);
        if(includePath == "shared_service.adhd"){
            static constexpr MStringView s_ServiceHeader =
                "interface IStore<T> {\r\n"
                "    void put(in T value);\r\n"
                "}\r\n"
                "[reflect]\r\n"
                "class SharedService {\r\n"
                "    private i32 m_total = 0;\r\n"
                "    public i32 accumulate(in i32 delta);\r\n"
                "}\r\n";
            outContent = MString(s_ServiceHeader.data(), s_ServiceHeader.size(), arena);
            return true;
        }
        static constexpr MStringView s_HeaderCode = "const u32 kSharedLimit = 16;\r\n";
        outContent = MString(s_HeaderCode.data(), s_HeaderCode.size(), arena);
        return true;
    }


public:
    u32 resolveCallCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

