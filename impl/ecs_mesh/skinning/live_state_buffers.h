// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Borrows the optional runtime resources resolved for this instance's complete runtime handle.
struct MeshSkinningStateBufferResources{
    u32 editRevision = 0u;
    const Core::BufferHandle* skinBuffer = nullptr;
    const Core::BufferHandle* jointPaletteBuffer = nullptr;
    const Core::BufferHandle* bindlessResourceSlotsBuffer = nullptr;
};

// One collection visits live bindings in world order; collector retains no state.
class MeshSkinningStateBufferCollector final : NoCopy{
private:
    using BufferIndex = HashSet<Core::Buffer*, Core::Alloc::ScratchArena>;
    static constexpr usize s_InlineBufferCount = 32u;


public:
    MeshSkinningStateBufferCollector(
        Core::Alloc::ScratchArena& scratchArena,
        Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers
    );
    MeshSkinningStateBufferCollector(MeshSkinningStateBufferCollector&&) = delete;


public:
    void collect(const MeshSkinningRuntimeInstance* instance, const MeshSkinningStateBufferResources& resources);


private:
    void retainBuffer(const Core::BufferHandle& buffer);
    void promoteBufferIndex();


private:
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& m_buffers;
    Core::Alloc::ScratchArena& m_scratchArena;
    Core::Buffer* m_inlineBuffers[s_InlineBufferCount] = {};
    usize m_inlineCount = 0u;
    Optional<BufferIndex> m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

