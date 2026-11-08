// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adhd_compiler_test_common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_simd_index_edge_tests{


using namespace NWB::AdhdCompilerTestsDetail;
namespace Compiler = NWB::Core::Metascript::Adhd::Compiler;
using NWB::Core::Metascript::Adhd::CompileResult;
using NWB::Core::Metascript::MStringView;


TEST(AdhdCompiler, KeepsDynamicLaneIndicesDynamicWhileFoldingConstantIndices){
    AdhdTestArena arena;
    static constexpr MStringView s_Source =
        "f32 lanes(mut VectorSingle value, in usize index) {\r\n"
        "    value[2u] = 1.0f;\r\n"
        "    value[index] = 2.0f;\r\n"
        "    return value[2u] + value[index];\r\n"
        "}\r\n";
    CompileResult result(arena.arena);
    ASSERT_TRUE(Compiler::Compile(s_Source, "impl/sim/lane_index_edges.adhd", arena.arena, result));
    const MStringView generated(result.generatedSource.data(), result.generatedSource.size());
    EXPECT_NE(generated.find("::VectorSetZ("), MStringView::npos);
    EXPECT_NE(generated.find("::VectorSetByIndex("), MStringView::npos);
    EXPECT_NE(generated.find("::VectorGetZ("), MStringView::npos);
    EXPECT_NE(generated.find("::VectorGetByIndex("), MStringView::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

