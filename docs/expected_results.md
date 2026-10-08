# Produced values and expected failures

First-party C++ uses `Expected<T, E>`, `Unexpected<E>` and `MakeUnexpected(error)` from `global/expected.h`. The aliases wrap the C++23 standard-library types at global scope. `Expected<T>` defaults its error type to empty `Failure`; use that default only when the contract has no richer failure detail. Preserve existing domain error enums, `ErrorCode`, diagnostic aggregates and safely borrowed static error text.

Configuration compiles a real `std::expected` success/error probe and requires `__cpp_lib_expected >= 202202L`. GNU-style Clang/AppleClang selects a supported C++23-or-newer mode; clang-cl uses its matching frontend mode and target standard library. A compiler accepting a language flag without the required library support is insufficient. The header enforces the same availability requirement.

## Return and admission

A fallible producer returns its value directly on success and `MakeUnexpected(error)` on failure. Several answers from one operation belong in a coherent aggregate. Use the factory rather than relying on alias template argument deduction for `Unexpected`.

The result's boolean conversion answers whether the operation succeeded. Check it before `*result`, `result->member` or `result.error()`. For `Expected<bool, E>`, `*result` is the actual answer: successful false is distinct from an unexpected error. Zero, empty text, empty containers and documented no-work results can also be successful values.

For example, this parser admits a positive count without an output parameter:

```cpp
#include <global/text_utils.h>

Expected<u64> ParsePositiveCount(const AStringView text)noexcept{
    const auto count = ParseU64(text);
    if(!count)
        return MakeUnexpected(count.error());
    if(*count == 0u)
        return MakeUnexpected(Failure{});
    return *count;
}
```

Use `.error()` only after failed admission. Diagnostics remain at the owning failure boundary; forwarding a result should not duplicate logs or allocate a string merely to transport an existing reason. Expected failures do not catch unexpected exceptions. Those continue unwinding to the application-entry boundary under existing RAII cleanup and terminal worker rules.

## Current contracts

| Operation | Returned result | Meaningful successful boundary |
| --- | --- | --- |
| `FileExists(path)` and host-file type queries | `Expected<bool, ErrorCode>` | A missing path answers false; a filesystem error is unexpected. |
| `FileSize(path)` | `Expected<u64, ErrorCode>` | An empty file has size zero. |
| `ReadPOD<T>(binary, cursor)` | `Expected<T>` | The cursor remains caller-owned parsing state. |
| `IFilesystem::readFile(path, offset, buffer, capacity)` | `Expected<usize>` | A read at EOF succeeds with zero copied bytes. |
| `AssetManager::loadSync(type, path)` | `Expected<UniquePtr<IAsset>>` | The admitted value owns the constructed asset. |
| `Telemetry::DecodeEvent(arena, bytes, count)` | `Expected<DecodedEvent, DecodeResult>` | The aggregate contains the event and consumed byte count. |
| `IGpuTaskGraphOutputLayerContributor::declareTaskGraphOutputLayer(graph)` | `Expected<GpuTaskGraphOutputLayer>` | An admitted empty layer may represent no work. |
| `Device::executeCommandLists(..., queue, submitDesc)` | `Expected<QueueSubmissionReceipt>` | The receipt carries the accepted token and emitted timeline-wait count. |
| `CsgDeformPipeline::RebuildSequentialCuts(..., vertices, triangles)` | `Expected<CsgDeformStats, CsgDeformFailure>` | Failure carries its reason and original diagnostic statistics. |

Host filesystem operations belong to `global/filesystem`; mounted runtime storage belongs to `Core::Filesystem::IFilesystem`. Their error types and interfaces are distinct. `IFilesystem::fileExists` remains a direct predicate, while byte count, file size and cursor producers return expected values.

## Mutation, storage and lifetime

Retain genuine in/out state and reusable caller-owned buffers when an operation intentionally appends, refills retained capacity, updates an existing resource lifecycle or obeys an external SDK/callback signature. A fallible mutator can return `Expected<void, E>`. An independently produced count, offset, handle, lookup answer or report still belongs in the expected value.

Examples include `IAssetBinarySource::readAssetBinary(path, bytes)` retaining the caller's asset arena and buffer capacity, telemetry encoding into reused byte vectors, graph callback state updated during later recording, and native SDK write buffers. A fresh candidate that replaces an output object is a producer; return that candidate with its existing owning arena.

Expected ownership does not extend a borrowed view's lifetime. Returned text and byte views must borrow static or otherwise guaranteed storage through their complete use. Arena-owned returned containers keep their original arena; move them into persistent state only after checking success. Preserve publication order, failure-atomic state changes, partial-progress diagnostics and the complete conditional `noexcept` contract.

Repeated replacements in a scratch or stack arena must respect reclamation order. When the prior value is no longer needed, destroy its owned result before producing the next one. `clear()` retains a container's storage; moving a fresh allocation over that live buffer can leave non-LIFO storage retained until an arena reset. Preserve explicit reusable-buffer state for production operations that need capacity reuse.

Migrate every repository caller with the producer. Retired status-plus-value/error overloads, forwarding adapters and compatibility aliases are removed; old input contracts remain rejected. Infallible producers return their value or aggregate directly. Feature support is the direct `queryFeatureSupport(feature) -> bool` predicate, and `getWaveLaneCounts() -> WaveLaneCountRange` is an infallible capability snapshot.
