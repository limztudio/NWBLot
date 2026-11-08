# Made values and set faults

First-party C++ uses `Expected<T, E>`, `Unexpected<E>` and
`MakeUnexpected(error)` from `global/expected.h`. The short names wrap the
C++23 std lib kinds at world scope. `Expected<T>` defaults its fault kind to
blank `Failure`. Use that default only when the deal has no deep fault note.
Keep set field fault kinds, `ErrorCode`, note packs, and safely lent still
fault text.

Setup builds a real `std::expected` good/bad probe and needs
`__cpp_lib_expected >= 202202L`. GNU-form Clang/AppleClang picks a good
C++23-or-new form. Clang-cl uses its match front form and target std lib. A
compiler that takes a tongue flag with no good lib help is not enough. The
head asks the same take-in need.

## Give and take-in

A fault-prone maker gives its value straight on good and
`MakeUnexpected(error)` on bad. Many answers from one call go in one clear
pack. Use the make call, not alias-kind auto-find for `Unexpected`.

The result true/false ask tells if the call won. Check it before `*result`,
`result->member`, or `result.error()`. For `Expected<bool, E>`, `*result` is
the true answer: a good false is not the same as a bad fault. Zero, blank
text, blank boxes, and noted no-work results can also be good values.

For example, this read takes a good count with no out flag:

```cpp
#include <global/numeric_text.h>

Expected<u64> ParsePositiveCount(const AStringView text)noexcept{
    const auto count = ParseU64(text);
    if(!count)
        return MakeUnexpected(count.error());
    if(*count == 0u)
        return MakeUnexpected(Failure{});
    return *count;
}
```

Use `.error()` only after failed take-in. Notes stay at the owning fault
edge. A pass-on of a result should not twin logs or make a string just to
move a set cause. Set faults do not catch wild throws. Those keep rising to
the app-door edge under set clean-up and end worker rules.

## Now deals

| Job | Gave result | Good edge |
| --- | --- | --- |
| `FileExists(path)` and host-file kind asks | `Expected<bool, ErrorCode>` | A lost path says false. A disk fault is bad. |
| `FileSize(path)` | `Expected<u64, ErrorCode>` | A blank file has size zero. |
| `ReadPOD<T>(binary, cursor)` | `Expected<T>` | The mark stays caller-owned read state. |
| `IFilesystem::readFile(path, offset, buffer, capacity)` | `Expected<usize>` | A read at end wins with zero copied bytes. |
| `AssetManager::loadSync(type, path)` | `Expected<UniquePtr<IAsset>>` | The passed value owns the made asset. |
| `Telemetry::DecodeEvent(arena, bytes, count)` | `Expected<DecodedEvent, DecodeResult>` | The pack holds the note and used byte count. |
| `IGpuTaskGraphOutputLayerContributor::declareTaskGraphOutputLayer(graph)` | `Expected<GpuTaskGraphOutputLayer>` | A passed blank layer may mean no work. |
| `Device::executeCommandLists(..., queue, submitDesc)` | `Expected<QueueSubmissionReceipt>` | The slip holds the taken token and sent wait count. |
| `CsgDeformPipeline::RebuildSequentialCuts(..., vertices, triangles)` | `Expected<CsgDeformStats, CsgDeformFailure>` | Fault holds its cause and first note counts. |

Host disk jobs sit in `global/filesystem`. Mounted run store sits in
`Core::Filesystem::IFilesystem`. Their fault kinds and doors stay split.
