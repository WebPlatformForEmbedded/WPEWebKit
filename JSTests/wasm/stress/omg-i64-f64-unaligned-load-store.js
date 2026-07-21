//@ requireOptions("--useConcurrentJIT=false", "--useBBQJIT=false", "--thresholdForOMGOptimizeAfterWarmUp=10", "--thresholdForOMGOptimizeSoon=10")
// Regression test for unaligned i64/f64 memory access under OMG on ARMv7.
// Wasm permits unaligned linear-memory access, but ldrd/strd fault (SIGBUS) on
// unaligned addresses on ARMv7. OMG (B3/Air) must lower i64/f64 loads/stores to
// single-word accesses, never a doubleword ldrd/strd. Forcing OMG (BBQ disabled)
// and round-tripping i64/f64 through an unaligned address must not crash.

// (module
//   (memory 1)
//   (func (export "i64rt") (param $off i32) (param $v i64) (result i64)
//     local.get $off local.get $v i64.store
//     local.get $off i64.load)
//   (func (export "f64rt") (param $off i32) (param $v f64) (result f64)
//     local.get $off local.get $v f64.store
//     local.get $off f64.load))
const bytes = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x0d, 0x02,
        0x60, 0x02, 0x7f, 0x7e, 0x01, 0x7e, // (i32,i64)->i64
        0x60, 0x02, 0x7f, 0x7c, 0x01, 0x7c, // (i32,f64)->f64
    0x03, 0x03, 0x02, 0x00, 0x01, // funcs 0,1
    0x05, 0x03, 0x01, 0x00, 0x01, // memory min 1
    0x07, 0x11, 0x02,
        0x05, 0x69, 0x36, 0x34, 0x72, 0x74, 0x00, 0x00, // "i64rt" -> 0
        0x05, 0x66, 0x36, 0x34, 0x72, 0x74, 0x00, 0x01, // "f64rt" -> 1
    0x0a, 0x1f, 0x02,
        0x0e, 0x00, 0x20, 0x00, 0x20, 0x01, 0x37, 0x03, 0x00, 0x20, 0x00, 0x29, 0x03, 0x00, 0x0b,
        0x0e, 0x00, 0x20, 0x00, 0x20, 0x01, 0x39, 0x03, 0x00, 0x20, 0x00, 0x2b, 0x03, 0x00, 0x0b,
]);

const { i64rt, f64rt } = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports;

const V = 0x1122334455667788n;
const D = 1.2345678901234567e-98;
for (let i = 0; i < 100000; i++) {
    // offsets 1,3,5,7 are unaligned; ldrd/strd would fault on ARMv7.
    const off = 1 + ((i % 4) * 2);
    const gi = i64rt(off, V);
    if (gi !== V)
        throw new Error("unaligned i64 roundtrip @" + i + " off " + off + ": got 0x" + gi.toString(16));
    const gd = f64rt(off, D);
    if (gd !== D)
        throw new Error("unaligned f64 roundtrip @" + i + " off " + off + ": got " + gd);
}
