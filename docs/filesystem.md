# Project filesystems

`core/filesystem/filesystem.h` defines `NWB::Core::Filesystem::IFilesystem`. Its implementation file contains
only backend-independent cursor helpers. The separate `core/filesystem/factory.h/.cpp` composition module owns
`FilesystemFactory` and `CreateFilesystem`, including selection of the default backend. The interface never
includes or constructs a concrete filesystem. The default implementation is `VolumeFileSystem`, which reads and writes the engine's existing segmented volume format. Asset loading, shader
archive loading, pipeline cache persistence, and asset gathering use the interface. The retired `VolumeSession`
wrapper is removed.

## Project selection

Projects configure graphics and storage before device initialization through the loader's `NWB::ConfigureProjectRuntime` entry point. Keep the policy and filesystem implementation in the application namespace; the loader entry only delegates. This Testbed-shaped example assumes `Testbed::ProjectPatchFilesystem` is declared in the project-owned `project_patch_filesystem.h`:

```cpp
#include "namespace.h"
#include "project_patch_filesystem.h"

#include <loader/project_entry.h>
#include <core/graphics/runtime/runtime.h>
#include <core/filesystem/volume_file_system.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>

TESTBED_BEGIN

namespace __hidden_testbed_startup{
static bool ConfigureRuntime(NWB::ProjectStartupContext& context){
    if(!context.graphics.setBindlessHeapAbi(NWB::Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
        return false;

    context.filesystemFactory = [](NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Filesystem::VolumeMountDesc& desc)
        -> UniquePtr<NWB::Core::Filesystem::IFilesystem>{
        if(desc.volumeName.view() == AStringView("graphics"))
            return MakeUnique<ProjectPatchFilesystem>(arena);
        return MakeUnique<NWB::Core::Filesystem::VolumeFileSystem>(arena);
    };
    return true;
}
};

TESTBED_END

bool NWB::ConfigureProjectRuntime(NWB::ProjectStartupContext& context){
    return Testbed::__hidden_testbed_startup::ConfigureRuntime(context);
}
```

`Testbed::ProjectPatchFilesystem` implements `NWB::Core::Filesystem::IFilesystem`. The local `namespace.h`
defines `TESTBED_BEGIN`/`TESTBED_END` independently of root `engine_namespace.h`; engine APIs supply their
namespaces transitively. Other applications use their own namespace and wrappers. Leaving `filesystemFactory`
empty selects `VolumeFileSystem` for every mount. A factory may capture an owning reference to the project's
patch or download service. The engine copies that factory into its graphics device configuration, so captured
services must remain valid until graphics shutdown. Do not capture startup-local references.

The factory receives the mount description to distinguish the read-only `graphics` asset namespace from the
backend's writable pipeline cache namespaces. It returns a fresh, unmounted filesystem for each invocation.
`CreateFilesystem(arena, desc, factory)` owns creation and mounting, and returns an empty `UniquePtr` if either
step fails. A custom factory's failure is propagated. It does not silently fall back to local files.

`ProjectRuntimeContext::filesystem` exposes the mounted asset filesystem to project runtime code. The loader
keeps it alive until project callbacks, asset loading, and the project world have shut down.

## Mount and storage contract

`VolumeMountDesc` names a logical storage namespace using `volumeName`, provides a `mountDirectory` hint, and
specifies `usage` and `createIfMissing`. The segmented backend also consumes `segmentSize`, `metadataSize`, and
`maxSegments`. Custom backends can ignore segmentation hints and map the namespace to their own manifest,
patch database, remote endpoint, or cache directory.

`RuntimeReadOnly` rejects writes and removal. `RuntimeReadWrite` and `CookWrite` permit writes. Creation is
explicit: mounting does not erase an existing namespace. The asset gatherer stages a new volume in its own
empty directory and only publishes it after successful finalization.

`writeFile(path, data, bytes)` replaces the complete file; it does not write at a cursor. Empty files are valid
and use a null pointer with zero bytes. `writeFileDeferred` has the same payload-copy contract, but can defer
metadata publication until `flush` or `unmount`. Callers may reuse their buffer immediately after either
write returns. `flush` persists pending writes; the volume backend also compacts and trims its segments.
`unmount` persists pending metadata before releasing the mount and reports failure. The volume backend
also flushes pending metadata before a remount and attempts to flush in its destructor. Callers that need to
act on persistence failures must explicitly call `flush` or `unmount` before destroying the object.

Paths are canonical engine `Name` keys, including in optimized builds where exact source strings may be absent.
A remote backend should use its own mapping from these keys to download URLs, preserving the asset identities
produced by the builder. `fileExists`, `fileSize`, `fileCount`, `listFiles`, `removeFile`, and
`reserveFileCapacity` complete the storage contract. Capacity reservation is an optional optimization for a
custom implementation and may be a no-op.

## Reading and seeking

`readFile(path, offset, buffer, capacity)` returns `Expected<usize>` with the copied byte count and does not modify shared cursor state. A successful read can be shorter than the requested capacity at EOF. Reading at EOF succeeds with zero bytes; reading beyond EOF returns an unexpected failure. Check admission before reading the count; zero is a successful value. A nonzero capacity requires a valid buffer.

`readFile(path, byteContainer)` obtains the file size, sizes the caller's container, and reads the complete
payload through the virtual range operation. It clears the container on failure. Container overloads require
one-byte element types.

`openFile(path)` returns `Expected<FileCursor>` bound to that filesystem without owning backend resources. Cursor reads return `Expected<usize>` and advance by
the number of bytes actually read. `seekFile(cursor, offset, origin)` supports `Begin`, `Current`, and `End`,
permits positions from zero through EOF, and rejects invalid origins, overflow, and out-of-range positions
without changing the cursor. `closeFile` clears the cursor. Cursors do not retain their filesystem; discard
all cursors when unmounting, remounting, or destroying it.

The default backend serializes individual operations using its internal `Futex`. A cursor is caller-owned
and requires caller synchronization when shared between threads. Whole-container reads consist of a size
query followed by a range read; clients requiring a stable snapshot must prevent concurrent replacement or
removal of that file for the complete operation. Runtime asset volumes are normally immutable while mounted.
Custom backends must synchronize their mutable state and support concurrent reads from independent callers.
Their destructors must release backend resources safely, including after a failed mount or an early runtime
exit. The loader explicitly unmounts after normal project shutdown; pipeline cache operations explicitly
unmount before reporting success.
Remote operations are synchronous at this boundary: they return complete requested data or a failure, while
projects can schedule asset work through the existing asynchronous asset executor.

Host source-file discovery and compiler/cache scratch files continue to use the platform-independent helpers
under `global/filesystem`; they are not virtual runtime asset namespaces.

`fileSize(path)` returns `Expected<u64>`; an empty file succeeds with zero. Whole-container reads retain the caller's arena and capacity. Host helpers such as `FileExists` and `FileSize` return `Expected<bool, ErrorCode>` and `Expected<u64, ErrorCode>`; successful missing-path false is distinct from a host error. See [produced values and expected failures](expected_results.md).
