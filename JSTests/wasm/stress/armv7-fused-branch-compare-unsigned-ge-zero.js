//@ requireOptions("--useBBQJIT=1", "--useWasmLLInt=0", "--useOMGJIT=0")

import { instantiate } from "../wabt-wrapper.js"
import * as assert from "../assert.js"

// BBQ fuses a comparison into a following br_if/if and emits the inverted condition, so
// the jump it hands back is the "branch not taken" edge. i64.ge_u inverts to Below, and
// every unsigned value is >= 0, so these reach the never-taken case of
// MacroAssemblerARMv7::branch64(Below, hi, lo, TrustedImm64(0)).
let wat = `
(module
    (func (export "geUnsignedZeroBrIf") (param $x i64) (result i32)
        (block $done
            (br_if $done (i64.ge_u (local.get $x) (i64.const 0)))
            (return (i32.const 0))
        )
        (i32.const 1)
    )

    ;; Constant on the left: i64.le_u inverts to Above, which emitBranchI64 commutes to Below.
    (func (export "zeroLeUnsignedBrIf") (param $x i64) (result i32)
        (block $done
            (br_if $done (i64.le_u (i64.const 0) (local.get $x)))
            (return (i32.const 0))
        )
        (i32.const 1)
    )

    ;; The if-fusion path, which stores the jump in ControlData::m_ifBranch.
    (func (export "geUnsignedZeroIf") (param $x i64) (result i32)
        (if (result i32) (i64.ge_u (local.get $x) (i64.const 0))
            (then (i32.const 1))
            (else (i32.const 0))
        )
    )

    ;; i64.lt_u inverts to AboveOrEqual, the always-taken sibling of the case above.
    (func (export "ltUnsignedZeroBrIf") (param $x i64) (result i32)
        (block $done
            (br_if $done (i64.lt_u (local.get $x) (i64.const 0)))
            (return (i32.const 0))
        )
        (i32.const 1)
    )

    ;; A non-zero constant is genuinely conditional and misses the compare-with-zero paths.
    (func (export "geUnsignedTenBrIf") (param $x i64) (result i32)
        (block $done
            (br_if $done (i64.ge_u (local.get $x) (i64.const 10)))
            (return (i32.const 0))
        )
        (i32.const 1)
    )
)
`

async function test() {
    const instance = await instantiate(wat, {}, {})
    const { geUnsignedZeroBrIf, zeroLeUnsignedBrIf, geUnsignedZeroIf, ltUnsignedZeroBrIf, geUnsignedTenBrIf } = instance.exports

    // Cover values whose high word, low word, or neither is zero.
    for (const x of [0n, 1n, 0xffffffffn, 0x100000000n, 0xffffffff00000000n, 0xffffffffffffffffn]) {
        assert.eq(geUnsignedZeroBrIf(x), 1)
        assert.eq(zeroLeUnsignedBrIf(x), 1)
        assert.eq(geUnsignedZeroIf(x), 1)
        assert.eq(ltUnsignedZeroBrIf(x), 0)
    }

    assert.eq(geUnsignedTenBrIf(9n), 0)
    assert.eq(geUnsignedTenBrIf(10n), 1)
    assert.eq(geUnsignedTenBrIf(0xffffffffffffffffn), 1)
}

await assert.asyncTest(test())
