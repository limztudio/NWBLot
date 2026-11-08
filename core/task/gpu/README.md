# Auto queue place

Task notes state work, stores, needs, and run cost. The build step picks a
good real Graphics, Compute, or Move queue before it writes calls. It uses
call skills, store share and hold, need cross, and overlap gain. Small tasks
lean to near. Fit tasks that ask to join their past task stay as one.

Overlap score uses task-link bit sets and per-queue cost groups. It does not
scan each set for each pick. Joined groups drop the join of their part
pasts, nexts, and parts. So only work free of the full pack adds overlap.
A task move 'tween queues news the cost list before later score. If-fix tasks
add zero set normal overlap and queue load, since they run only after normal
work fails. Their queue needs and true-edge joins stay whole. Free graphs and
full-order chains keep their reach short cuts. Graphs with no link rows make
no cost-mask data.

Half graphs use tight per-row word runs when their store plus short edge and
spot lists save near 25% of a full grid. Small rows and wide runs keep full
store. Worst-case store stays square. World task-bit spots stay. Both forms
build the two-way link list in place with capped 64-by-64 tiles, and rank
order gets back firm reach line. Split past and next ends fix exact two-way
word runs before make. End OR loops snap row starts and lengths, so writes do
not block wide ops. Full rows of at least 64 words keep made ends through end
run to skip a second full-grid end scan. Smaller full rows free that store
before grid make. Place rules and note opt-ins for side-by-side call write
stay clear.

Need trim stops past the last direct user rank, or once each direct user is
met. Check and stream notes share a byte-mixed need-pair hash, so tight
count IDs do not blow up the hash table. Firm rank order most often scans
note IDs. Too much re-scan turns on a tier ready map that keeps the exact
low-ready-ID order. Plain maker-first graphs keep the scan path with no map
make.

Queue pick checks the join of real queues that can run any place group call
need, before it builds need reach. When at most one queue can, each good set,
same-kind, or time route uses that queue, and each overlap note is zero.
A square grid build cannot change the result. The skill scan on purpose
over-guesses good routes and stops after two queues. Edge-free graphs keep
their old path with no scan.

Noted and true call-back links check and list before run. Lists of at most
eight call-backs use a free straight find. Bigger lists use a scratch-made
list. Links stay fixed through the job, while call-back-owned context may
move. Calls stay in built task order, and find makes nothing after home
take-in.

Built-in copy, up-load, clear, and fix jobs find their call needs in them.
A plain noted task states its code needs once:

```cpp
struct ExampleComputeTask{
    static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::Compute };
};
```

## Store states and blocks

Stores move through read and write states with queue-home pass. The state
plan keeps each store form, last known state, and wait edges. Less state data
does not mean less work. It means less repeat check.

Home close of maps needs home shut to keep the stated state. Fit shader/depth
reads use the picked back plain read-only plan. A block or ray-box back block
may go back to a known make state on shut, since it has no image plan. A
brought-in read seed then gets a clear local move from that true snap state.
A later bad state or write still joins cross reads, and end outs keep the
newest tracked state.

Read joins may stop at a full writer that has lined the old reads, or at a
full-cover bad read time set by another task. Half edges and task-home state
moves block that read-time cut.

Home block drop turns queue-held step/touch spans into closed caller-owned
lists used by the write call. It does not copy those lists into short boxes
or change plans, spans, hold, or draw-span care.

# Start-state hand-offs

Start-state hand-offs pack at most eight back-to-back sub sets from one
fixed pack source. Each first span checks on its own, and source order stays
'tween packs. Hung block frees on the now base or source use a straight fold.
Join of good halves must not hide an old head fail that would cut a hung
hold-free span. Short snap boxes use caller scratch. The hand-off call owns
copied snap stuff in the graphics field.

# Block byte spans

GPU tasks line up the byte spans noted in
`GpuTaskResourceUse::range.bufferRange`:

```cpp
const GpuTaskResourceUse use{
    .resource = bufferResource,
    .range = GpuTaskResourceRange{ .bufferRange = BufferRange(256u, 128u) },
    .requiredState = ResourceStates::UnorderedAccess,
    .access = GpuTaskResourceAccess::Write,
};
```

This note covers bytes `[256, 384)`. Split spans make no store risks. Cross
reads/writes keep the needed order. State moves, pack state seeds, end outs,
and queue-home hold moves keep the hit spans. This holds when a user spans
many makers.

No span keeps the full-block default.
`BufferRange(offset, BufferRange::s_AllBytes)` covers the rest bytes from that
spot. Blank, run-past, and out-of-edge task spans fail. Ray-boxes keep
full-pack line-up.

The note must cover each byte hit by the task calls and in-task state moves.
For clear home moves, pass the same span as the last flag to
`CommandList::setBufferState(buffer, state, forceMemoryDependency, range)`.
Full-block jobs, with set point/index and draw/mesh side-call state moves,
still need full-block notes. Compute side-call send moves only its flag form
at the sent spot. Built-in up-loads and copies note and move their exact move
spans. Block clears cover the full block.

A hung queue-home free must join with its first byte span. Hand-off sub set
pick turns down cut of a hung free. Free split spans on the maker when users
need free hold spans.

# CPU size cover

CPU size acts sit in plain on unit tests in `tests/unit/task/gpu/` (no
`DISABLED_*` cases): queue place and score, time-past route, store-form
check, need guess, store-state bits and end group, end needs, hold counts,
start-state hand-off check, re-play pre-check, and sub task runs. New cover
aims at past-bug edges through the picked public rules.
