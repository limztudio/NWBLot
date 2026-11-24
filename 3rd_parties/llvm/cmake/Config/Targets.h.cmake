/*===------- llvm/Config/Targets.h - LLVM target checks -----------*- C -*-===*/
/*                                                                            */
/* Part of the LLVM Project, under the Apache License v2.0 with LLVM          */
/* Exceptions.                                                                */
/* See https://llvm.org/LICENSE.txt for license information.                  */
/* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    */
/*                                                                            */
/*===----------------------------------------------------------------------===*/

/* This file enables clients to know whether specific targets are enabled. */

#ifndef LLVM_CONFIG_TARGETS_H
#define LLVM_CONFIG_TARGETS_H

#cmakedefine01 LLVM_HAS_AARCH64_TARGET

#cmakedefine01 LLVM_HAS_AMDGPU_TARGET

#cmakedefine01 LLVM_HAS_ARC_TARGET

#cmakedefine01 LLVM_HAS_ARM_TARGET

#cmakedefine01 LLVM_HAS_AVR_TARGET

#cmakedefine01 LLVM_HAS_BPF_TARGET

#cmakedefine01 LLVM_HAS_CSKY_TARGET

#cmakedefine01 LLVM_HAS_DIRECTX_TARGET

#cmakedefine01 LLVM_HAS_HEXAGON_TARGET

#cmakedefine01 LLVM_HAS_LANAI_TARGET

#cmakedefine01 LLVM_HAS_LOONGARCH_TARGET

#cmakedefine01 LLVM_HAS_M68K_TARGET

#cmakedefine01 LLVM_HAS_MIPS_TARGET

#cmakedefine01 LLVM_HAS_MSP430_TARGET

#cmakedefine01 LLVM_HAS_NVPTX_TARGET

#cmakedefine01 LLVM_HAS_POWERPC_TARGET

#cmakedefine01 LLVM_HAS_RISCV_TARGET

#cmakedefine01 LLVM_HAS_SPARC_TARGET

#cmakedefine01 LLVM_HAS_SPIRV_TARGET

#cmakedefine01 LLVM_HAS_SYSTEMZ_TARGET

#cmakedefine01 LLVM_HAS_VE_TARGET

#cmakedefine01 LLVM_HAS_WEBASSEMBLY_TARGET

#cmakedefine01 LLVM_HAS_X86_TARGET

#cmakedefine01 LLVM_HAS_XCORE_TARGET

#cmakedefine01 LLVM_HAS_XTENSA_TARGET

#endif
