import { instantiate } from "../wabt-wrapper.js"
import * as assert from "../assert.js"

// The i32 operand of the outer add is evaluated first, so it holds a register while the
// dividend is loaded. That pushes the dividend into a register pair overlapping the pair
// the C call passes it in, which is what makes the argument shuffle need a temporary.
let wat = `
(module
    (func (export "divS") (param $x i64) (param $k i32) (result i32)
        (i32.add (local.get $k) (i32.wrap_i64 (i64.div_s (local.get $x) (i64.const 1000)))))
    (func (export "divU") (param $x i64) (param $k i32) (result i32)
        (i32.add (local.get $k) (i32.wrap_i64 (i64.div_u (local.get $x) (i64.const 1000)))))
    (func (export "remS") (param $x i64) (param $k i32) (result i32)
        (i32.add (local.get $k) (i32.wrap_i64 (i64.rem_s (local.get $x) (i64.const 1000)))))
    (func (export "remU") (param $x i64) (param $k i32) (result i32)
        (i32.add (local.get $k) (i32.wrap_i64 (i64.rem_u (local.get $x) (i64.const 1000)))))
    (func (export "divSLeft") (param $x i64) (param $k i32) (result i32)
        (i32.add (local.get $k) (i32.wrap_i64 (i64.div_s (i64.const 1000) (local.get $x)))))
    (func (export "remSLeft") (param $x i64) (param $k i32) (result i32)
        (i32.add (local.get $k) (i32.wrap_i64 (i64.rem_s (i64.const 1000) (local.get $x)))))
    (func (export "divS32") (param $x i32) (param $k i32) (result i32)
        (i32.add (local.get $k) (i32.div_s (local.get $x) (i32.const 1000))))
)
`

async function test() {
    const instance = await instantiate(wat, {}, { simd: true })
    const { divS, divU, remS, remU, divSLeft, remSLeft, divS32 } = instance.exports

    for (let i = 1; i <= 10000; ++i) {
        let x = i * 7919
        assert.eq(divS(BigInt(x), 5), 5 + (x / 1000 | 0))
        assert.eq(divU(BigInt(x), 5), 5 + (x / 1000 | 0))
        assert.eq(remS(BigInt(x), 5), 5 + x % 1000)
        assert.eq(remU(BigInt(x), 5), 5 + x % 1000)
        assert.eq(divSLeft(BigInt(x), 5), 5 + (1000 / x | 0))
        assert.eq(remSLeft(BigInt(x), 5), 5 + 1000 % x)
        assert.eq(divS32(x, 5), 5 + (x / 1000 | 0))
    }

    // A zero dividend with a non-zero constant divisor emits no zero check, so losing the
    // divisor makes the runtime helper divide zero by zero and take SIGFPE.
    for (let i = 0; i < 10000; ++i) {
        assert.eq(divS(0n, 5), 5)
        assert.eq(divU(0n, 5), 5)
        assert.eq(remS(0n, 5), 5)
        assert.eq(remU(0n, 5), 5)
        assert.eq(divS32(0, 5), 5)
    }
}

await assert.asyncTest(test())
