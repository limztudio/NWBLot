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

#ifndef NWB_ADHD_EXPORT_BRACKET_OPERATOR
#define NWB_ADHD_EXPORT_BRACKET_OPERATOR(openToken, closeToken, symbol, minMemberParams, maxMemberParams, missingCloseError)
#define NWB_ADHD_EXPORT_BRACKET_OPERATOR_AUTO_UNDEF
#endif

#ifndef NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR
#define NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR(prohibitsUnaryZeroMsg, zeroMaxParamsMsg, optionalParamMsg, binaryOrIndexMsg)
#define NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR_AUTO_UNDEF
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "export_global_operators.h"
#include "export_core_operators.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_MEMBER_OPERATOR_ARITY_ERROR
#endif

#ifdef NWB_ADHD_EXPORT_BRACKET_OPERATOR_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BRACKET_OPERATOR_AUTO_UNDEF
#undef NWB_ADHD_EXPORT_BRACKET_OPERATOR
#endif

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

