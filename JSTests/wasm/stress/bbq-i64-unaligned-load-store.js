//@ requireOptions("--useConcurrentJIT=false")
// Regression test for unaligned i64 memory access under BBQ on ARMv7.
// BBQ compiled wasm i64.load/i64.store with ldrd/strd, which fault on unaligned
// addresses on ARMv7 -- but wasm permits unaligned memory access. Round-tripping an
// i64 through an unaligned address therefore crashed (SIGBUS) once BBQ kicked in.

// (module
//   (memory 1)
//   (func (export "roundtrip") (param $off i32) (param $v i64) (result i64)
//     local.get $off local.get $v i64.store
//     local.get $off i64.load))
const bytes = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // \0asm v1
    0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7e, 0x01, 0x7e, // type: (i32,i64)->i64
    0x03, 0x02, 0x01, 0x00, // func 0
    0x05, 0x03, 0x01, 0x00, 0x01, // memory: min 1 page
    0x07, 0x0d, 0x01, 0x09, 0x72, 0x6f, 0x75, 0x6e, 0x64, 0x74, 0x72, 0x69, 0x70, 0x00, 0x00, // export "roundtrip"
    0x0a, 0x10, 0x01, 0x0e, 0x00, 0x20, 0x00, 0x20, 0x01, 0x37, 0x03, 0x00, 0x20, 0x00, 0x29, 0x03, 0x00, 0x0b, // code
]);

const { roundtrip } = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports;

const V = 0x1122334455667788n;
for (let i = 0; i < 100000; i++) {
    // offset 1 is unaligned; ldrd/strd would fault on ARMv7 once BBQ-compiled.
    const got = roundtrip(1, V);
    if (got !== V)
        throw new Error("unaligned i64 roundtrip at iteration " + i + ": got 0x" + got.toString(16));
}
