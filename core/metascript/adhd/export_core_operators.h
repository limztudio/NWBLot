// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_ADHD_EXPORT_OPERATOR_TOKEN
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN(tokenType, symbol, isOverloadable, isAssignment, returnsBool, isBitwiseFlagOp, isPrefixUnary, isIncDec, binaryPrecedence, minMemberParams, maxMemberParams, prohibitsUnaryOverload)
#define NWB_ADHD_EXPORT_OPERATOR_TOKEN_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP
#define NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP(tokenType, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD
#define NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD(tokenType, errorMessage)
#define NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_SIMD_BINARY_OP
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP(binaryToken, assignToken, paddedBinarySymbol, isIntLaneFallback, bitwiseIntrinsicSingle, bitwiseIntrinsicDouble)
#define NWB_ADHD_EXPORT_SIMD_BINARY_OP_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_SIMD_BINARY_OP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_SIMD_BINARY_OP
#endif

#ifdef NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_OPERATOR_OVERLOAD
#endif

#ifdef NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_PROHIBITED_UNARY_OP
#endif

#ifdef NWB_ADHD_EXPORT_OPERATOR_TOKEN_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_OPERATOR_TOKEN
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

