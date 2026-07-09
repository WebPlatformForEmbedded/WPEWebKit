//@ requireOptions("--useConcurrentJIT=false")
// Regression test for test32(Zero/NonZero, op1, op2, dest) with op1 != op2 on ARMv7:
// materializing (op1 & op2) == 0 must use AND, not EOR (EOR computes op1 == op2).
// OMG fuses i32.eqz(i32.and(a, b)) into a single materialized Test32, which is the
// only tier that exercises the distinct-register form.
// https://bugs.webkit.org/show_bug.cgi?id=302016 (regressed in 303329@main)

// (module
//   (func (export "f") (param i32 i32) (result i32)
//     local.get 0
//     local.get 1
//     i32.and
//     i32.eqz))
const bytes = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // \0asm v1
    0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, // type: (i32,i32)->i32
    0x03, 0x02, 0x01, 0x00, // func
    0x07, 0x05, 0x01, 0x01, 0x66, 0x00, 0x00, // export "f"
    0x0a, 0x0a, 0x01, 0x08, 0x00, 0x20, 0x00, 0x20, 0x01, 0x71, 0x45, 0x0b, // code
]);

const f = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports.f;

for (let i = 0; i < 300000; i++) {
    // (2 & 1) == 0, but 2 != 1: EOR-based Zero materialization returns 0 instead of 1.
    let got = f(2, 1);
    if (got !== 1)
        throw new Error("f(2, 1) should be 1 ((2&1)==0), got " + got + " at iteration " + i);
    // (3 & 1) != 0 and 3 != 1: both idioms agree; guards the NonZero path shape too.
    got = f(3, 1);
    if (got !== 0)
        throw new Error("f(3, 1) should be 0 ((3&1)!=0), got " + got + " at iteration " + i);
}
