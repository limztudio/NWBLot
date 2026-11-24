/*===-- llvm-c/blake3.h - BLAKE3 C Interface ----------------------*- C -*-===*\
|*                                                                            *|
|* Released into the public domain with CC0 1.0                               *|
|* See '3rd_parties/blake3/LICENSE_CC0' for info.                             *|
|* SPDX-License-Identifier: CC0-1.0                                           *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to LLVM's BLAKE3 implementation.      *|
|* Original BLAKE3 C API: https://github.com/BLAKE3-team/BLAKE3/tree/1.8.7/c  *|
|*                                                                            *|
|* Symbols are prefixed with 'llvm' to avoid a potential conflict with        *|
|* another BLAKE3 version within the same program.                            *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_BLAKE3_H
#define LLVM_C_BLAKE3_H

#include "llvm-c/Visibility.h"
#include <blake3.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LLVM_BLAKE3_VERSION_STRING BLAKE3_VERSION_STRING
#define LLVM_BLAKE3_KEY_LEN BLAKE3_KEY_LEN
#define LLVM_BLAKE3_OUT_LEN BLAKE3_OUT_LEN
#define LLVM_BLAKE3_BLOCK_LEN BLAKE3_BLOCK_LEN
#define LLVM_BLAKE3_CHUNK_LEN BLAKE3_CHUNK_LEN
#define LLVM_BLAKE3_MAX_DEPTH BLAKE3_MAX_DEPTH

typedef blake3_chunk_state llvm_blake3_chunk_state;
typedef blake3_hasher llvm_blake3_hasher;

LLVM_C_ABI const char *llvm_blake3_version(void);
LLVM_C_ABI void llvm_blake3_hasher_init(llvm_blake3_hasher *self);
LLVM_C_ABI void
llvm_blake3_hasher_init_keyed(llvm_blake3_hasher *self,
                              const uint8_t key[LLVM_BLAKE3_KEY_LEN]);
LLVM_C_ABI void llvm_blake3_hasher_init_derive_key(llvm_blake3_hasher *self,
                                                   const char *context);
LLVM_C_ABI void llvm_blake3_hasher_init_derive_key_raw(llvm_blake3_hasher *self,
                                                       const void *context,
                                                       size_t context_len);
LLVM_C_ABI void llvm_blake3_hasher_update(llvm_blake3_hasher *self,
                                          const void *input, size_t input_len);
LLVM_C_ABI void llvm_blake3_hasher_finalize(const llvm_blake3_hasher *self,
                                            uint8_t *out, size_t out_len);
LLVM_C_ABI void llvm_blake3_hasher_finalize_seek(const llvm_blake3_hasher *self,
                                                 uint64_t seek, uint8_t *out,
                                                 size_t out_len);
LLVM_C_ABI void llvm_blake3_hasher_reset(llvm_blake3_hasher *self);

#ifdef __cplusplus
}
#endif

#endif /* LLVM_C_BLAKE3_H */
