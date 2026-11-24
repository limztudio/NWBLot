# Project filesystems

`core/filesystem/filesystem.h` defines `NWB::Core::Filesystem::IFilesystem`. Its implementation file contains
only backend-independent cursor helpers. The separate `core/filesystem/factory.h` and `core/filesystem/factory.cpp` composition module owns
`FilesystemFactory` and `CreateFilesystem`, including selection of the default backend. The interface never
includes or constructs a concrete filesystem. The default implementation is `VolumeFileSystem`, which reads and writes the engine's existing segmented volume format. Asset loading, shader
archive loading, pipeline cache persistence, and asset gathering use the interface. The retired `VolumeSession`
wrapper is removed.

## Project selection

Projects configure graphics and storage before device initialization through the loader's `NWB::ConfigureProjectRuntime` entry point. Keep project policy and filesystem implementation in the application namespace. The `.adhd` reference project `CoolStuff/Testbed` configures project sources with `NAMESPACE "NWB::Testbed"` and its loader entry with `NAMESPACE "NWB"` in `CoolStuff/Testbed/CMakeLists.txt`; `NWB::ConfigureProjectRuntime` is implemented in `CoolStuff/Testbed/project_entry.adhd`. This handwritten C++ project example assumes `ExampleApp::ProjectPatchFilesystem` is declared in a project-owned `project_patch_filesystem.h` with `namespace.h` defining `PROJECT_BEGIN`/`PROJECT_END`:

```cpp
#include "namespace.h"
#include "project_patch_filesystem.h"

#include <loader/project_entry.h>
#include <core/graphics/runtime/runtime.h>
#include <core/filesystem/volume_file_system.h>
#include <impl/assets_graphics/bindless/runtime_abi.adhd.h>

PROJECT_BEGIN

namespace __hidden_project_startup{
static b8 ConfigureRuntime(NWB::ProjectStartupContext& context)noexcept{
    if(!context.graphics.setBindlessHeapAbi(NWB::Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
        return false;

    context.filesystemFactory = +[](NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Filesystem::VolumeMountDesc& desc)
        -> Expected<UniquePtr<NWB::Core::Filesystem::IFilesystem>>{
        if(desc.volumeName.view() == AStringView("graphics"))
            return UniquePtr<NWB::Core::Filesystem::IFilesystem>(MakeUnique<ProjectPatchFilesystem>(arena));
        return UniquePtr<NWB::Core::Filesystem::IFilesystem>(MakeUnique<NWB::Core::Filesystem::VolumeFileSystem>(arena));
    };
    return true;
}
};

PROJECT_END

b8 NWB::ConfigureProjectRuntime(NWB::ProjectStartupContext& context)noexcept{
    return ExampleApp::__hidden_project_startup::ConfigureRuntime(context);
}
```

The unary `+` converts the captureless factory lambda to a function pointer for the `noexcept` entry point.

`ExampleApp::ProjectPatchFilesystem` implements `NWB::Core::Filesystem::IFilesystem`. Its local `namespace.h`
defines `PROJECT_BEGIN`/`PROJECT_END` independently of root `engine_namespace.h`; engine APIs supply their
namespaces transitively. Leaving `filesystemFactory`
empty selects `VolumeFileSystem` for every mount. A factory may capture an owning reference to the project's
patch or download service. The engine copies that factory into its graphics device configuration, so captured
services must remain valid until graphics shutdown. Do not capture startup-local references.

The factory receives the mount description to distinguish the read-only `graphics` asset namespace from the
backend's writable pipeline cache namespaces. It returns a fresh, unmounted filesystem for each invocation.
`CreateFilesystem(arena, desc, factory)` owns creation and mounting and returns `Expected<UniquePtr<IFilesystem>>`.
Check success before moving the admitted owner; failed creation or mounting returns an unexpected failure.
The factory uses that same expected owning-result contract, and a null candidate is rejected. A custom factory's failure is propagated without falling back to local files.

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
`unmount` persists pending metadata before releasing the mount and reports failure; a failed unmount keeps
the mounted state and pending metadata available for an explicit retry. The volume backend also flushes
pending metadata before a remount. Writable owners must check `flush` or `unmount` before reporting success
and destroying the object. Destruction only releases owned resources: it performs no persistence, allocation,
or logger callbacks, including during exception unwinding. Uncommitted deferred metadata is not published
by destruction.

Paths are canonical engine `Name` keys, including in optimized builds where exact source strings may be absent.
A remote backend should use its own mapping from these keys to download URLs, preserving the asset identities
produced by the builder. `fileExists`, `fileSize`, `fileCount`, `removeFile`, and
`reserveFileCapacity` complete the storage contract. Capacity reservation is an optional optimization for a
custom implementation and may be a no-op.

## Reading and seeking

`readFile(path, offset, buffer, capacity)` returns `Expected<usize>` with the copied byte count and does not modify shared cursor state. A successful read can be shorter than the requested capacity at EOF. Reading at EOF succeeds with zero bytes; reading beyond EOF returns an unexpected failure. Check admission before reading the count; zero is a successful value. A nonzero capacity requires a valid buffer.

`readFile(path, byteContainer)` clears and sizes the caller's container through the virtual
`readWholeFile` operation, then checks that the returned byte count matches. It clears the container on
failure. The default `readWholeFile` implementation queries the size and calls the virtual range read;
backends may override it. Container overloads require one-byte element types.

`visitWholeFile` optionally lends a byte span to a synchronous visitor. The default implementation returns
an empty `Optional` when it cannot provide a view. The volume backend can visit an empty file or a file
contained in one mapped segment of a read-only mount; a present boolean carries the visitor's result or a
read failure. A caller must not retain the borrowed span after the callback.

`openFile(path)` returns `Expected<FileCursor>` bound to that filesystem without owning backend resources. Cursor reads return `Expected<usize>` and advance by
the number of bytes actually read. `seekFile(cursor, offset, origin)` supports `Begin`, `Current`, and `End`,
permits positions from zero through EOF, and rejects invalid origins, overflow, and out-of-range positions
without changing the cursor. `closeFile` clears the cursor. Cursors do not retain their filesystem; discard
all cursors when unmounting, remounting, or destroying it.

The default backend uses a reader/writer lock for mutable volume state: reads can share the lock and writes take it exclusively. Some read paths omit the lock for an immutable read-only mount. A cursor is caller-owned
and requires caller synchronization when shared between threads. On mutable volume mounts, the volume
backend holds its shared lock across whole-file lookup, buffer preparation, and copying. The interface's
default implementation performs separate size and range operations, so clients using that implementation
must prevent concurrent replacement or removal when they need a stable snapshot. Runtime asset volumes
are normally immutable while mounted.
Writable access to a volume requires one owner across filesystem instances and processes; their locks do not
coordinate with one another. If compaction cannot restore a failed publication, it unmounts the affected
instance and retains its stage and backup directories for recovery. A later operation using the same work
paths refuses to overwrite those directories.
Custom backends must synchronize their mutable state and support concurrent reads from independent callers.
Their destructors must release backend resources safely, including after a failed mount or an early runtime
exit. The loader explicitly unmounts after normal project shutdown; pipeline cache operations explicitly
unmount before reporting success.
Remote operations are synchronous at this boundary: they return complete requested data or a failure, while
projects can schedule asset work through the existing asynchronous asset executor.

Host source-file discovery and compiler/cache scratch files continue to use the platform-independent helpers
under `global/filesystem`; they are not virtual runtime asset namespaces.

`fileSize(path)` returns `Expected<u64>`; an empty file succeeds with zero. Whole-container reads retain the caller's arena and capacity. Host helpers such as `FileExists` and `FileSize` return `Expected<b8, ErrorCode>` and `Expected<u64, ErrorCode>`; successful missing-path false is distinct from a host error. See [produced values and expected failures](expected_results.md).
