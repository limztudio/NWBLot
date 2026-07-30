// limztudio@gmail.com


#pragma once


#include <core/global.h>

#include <global/compile.h>
#include <global/platform.h>
#include <global/type.h>
#include <global/allocation_size.h>
#include <global/arena_c_allocator.h>
#include <global/call_traits.h>
#include <global/unique_ptr.h>
#include <global/containers.h>
#include <global/generic.h>
#include <global/not_null.h>
#include <global/simplemath.h>
#include <global/atomic.h>
#include <global/sync.h>
#include <global/thread.h>
#include <global/inplace_function.h>


#define NWB_ALLOC_BEGIN NWB_CORE_BEGIN namespace Alloc{
#define NWB_ALLOC_END }; NWB_CORE_END


NWB_ALLOC_BEGIN


namespace CoreAffinity{
    enum Enum : u8{
        Any,
        Performance,
        Efficiency,
    };
};


extern usize CachelineSize();


NWB_ALLOC_END


