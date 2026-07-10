//@ requireOptions("--useConcurrentJIT=false")
// Regression test for array.new with a 32-bit init value under OMG on ARMv7.
// operationWasmArrayNew() takes the init value as a uint64_t. The 32-bit OMG
// generator passed an i32 init as a single word, so the ABI put it in a register
// while the callee read the uint64_t from an uninitialized stack slot -- filling
// the array with a stray value instead of the requested init.

// (module
//   (type $arr (array (mut i32)))
//   (func (export "f") (param i32) (result i32)
//     local.get 0
//     i32.const 3
//     array.new $arr
//     i32.const 1
//     array.get $arr))
const bytes = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // \0asm v1
    0x01, 0x09, 0x02, 0x5e, 0x7f, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, // type: array(mut i32); (i32)->i32
    0x03, 0x02, 0x01, 0x01, // func 0 has type 1
    0x07, 0x05, 0x01, 0x01, 0x66, 0x00, 0x00, // export "f"
    0x0a, 0x10, 0x01, 0x0e, 0x00, 0x20, 0x00, 0x41, 0x03, 0xfb, 0x06, 0x00, 0x41, 0x01, 0xfb, 0x0b, 0x00, 0x0b, // code
]);

const f = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports.f;

for (let i = 0; i < 100000; i++) {
    // f(v) allocates i32[3] filled with v and returns element 1, which must equal v.
    const got = f(0x2a);
    if (got !== 0x2a)
        throw new Error("array.new i32 init: f(0x2a) should be 0x2a, got " + got + " at iteration " + i);
}
