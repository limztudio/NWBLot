// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "llvm-c/blake3.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const char* llvm_blake3_version(void){
    return blake3_version();
}

void llvm_blake3_hasher_init(llvm_blake3_hasher* self){
    blake3_hasher_init(self);
}

void llvm_blake3_hasher_init_keyed(llvm_blake3_hasher* self, const uint8_t key[LLVM_BLAKE3_KEY_LEN]){
    blake3_hasher_init_keyed(self, key);
}

void llvm_blake3_hasher_init_derive_key(llvm_blake3_hasher* self, const char* context){
    blake3_hasher_init_derive_key(self, context);
}

void llvm_blake3_hasher_init_derive_key_raw(llvm_blake3_hasher* self, const void* context, size_t context_len){
    blake3_hasher_init_derive_key_raw(self, context, context_len);
}

void llvm_blake3_hasher_update(llvm_blake3_hasher* self, const void* input, size_t input_len){
    blake3_hasher_update(self, input, input_len);
}

void llvm_blake3_hasher_finalize(const llvm_blake3_hasher* self, uint8_t* out, size_t out_len){
    blake3_hasher_finalize(self, out, out_len);
}

void llvm_blake3_hasher_finalize_seek(const llvm_blake3_hasher* self, uint64_t seek, uint8_t* out, size_t out_len){
    blake3_hasher_finalize_seek(self, seek, out, out_len);
}

void llvm_blake3_hasher_reset(llvm_blake3_hasher* self){
    blake3_hasher_reset(self);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

