- target always be x64 (in the future I should consider ARM64 but I would never consider 32bit)

- config now would be dbg, opt and fin
  - dbg: not optimized. no inline.
  - opt: optimized. only primitives are inlined. frame pointer available.
  - fin: optimized. inlined. frame pointer is omitted. hardly debuggable.

