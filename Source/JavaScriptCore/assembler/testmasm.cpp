/*
 * Copyright (C) 2017-2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"

#include "CCallHelpers.h"
#include "CPU.h"
#include "FPRInfo.h"
#include "GPRInfo.h"
#include "InitializeThreading.h"
#include "LinkBuffer.h"
#include "ProbeContext.h"
#include "StackAlignment.h"
#include <limits>
#include <wtf/Compiler.h>
#include <wtf/DataLog.h>
#include <wtf/Function.h>
#include <wtf/Lock.h>
#include <wtf/NumberOfCores.h>
#include <wtf/PtrTag.h>
#include <wtf/Threading.h>
#include <wtf/WTFProcess.h>
#include <wtf/text/StringCommon.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

// We don't have a NO_RETURN_DUE_TO_EXIT, nor should we. That's ridiculous.
static bool hiddenTruthBecauseNoReturnIsStupid() { return true; }

static void usage()
{
    dataLog("Usage: testmasm [<filter>]\n");
    if (hiddenTruthBecauseNoReturnIsStupid())
        exitProcess(1);
}

#if ENABLE(JIT)

static Vector<double> doubleOperands()
{
    return Vector<double> {
        0,
        -0,
        1,
        -1,
        42,
        -42,
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
}


#if CPU(X86_64) || CPU(ARM64) || CPU(RISCV64)
static Vector<float> floatOperands()
{
    return Vector<float> {
        0,
        -0,
        1,
        -1,
        42,
        -42,
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::min(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
}
#endif

template<typename T, typename U>
static T* bitwise_cast_ptr(U* p)
{
    return static_cast<T*>(static_cast<void*>(p));
}

static Vector<int32_t> int32Operands()
{
    return Vector<int32_t> {
        0,
        1,
        -1,
        2,
        -2,
        42,
        -42,
        64,
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::min(),
    };
}

static UNUSED_FUNCTION Vector<int16_t> int16Operands()
{
    return Vector<int16_t> {
        0,
        1,
        -1,
        42,
        -42,
        std::numeric_limits<int16_t>::max(),
        std::numeric_limits<int16_t>::min(),
        static_cast<int16_t>(std::numeric_limits<uint16_t>::max()),
        static_cast<int16_t>(std::numeric_limits<uint16_t>::min())
    };
}

static UNUSED_FUNCTION Vector<int8_t> int8Operands()
{
    return Vector<int8_t> {
        0,
        1,
        -1,
        42,
        -42,
        std::numeric_limits<int8_t>::max(),
        std::numeric_limits<int8_t>::min(),
        static_cast<int8_t>(std::numeric_limits<uint8_t>::max()),
        static_cast<int8_t>(std::numeric_limits<uint8_t>::min())
    };
}

#if CPU(X86_64) || CPU(ARM64)
static Vector<int64_t> int64Operands()
{
    return Vector<int64_t> {
        0,
        1,
        -1,
        2,
        -2,
        42,
        -42,
        64,
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int64_t>::max(),
        std::numeric_limits<int64_t>::min(),
    };
}
#endif

namespace WTF {

static void printInternal(PrintStream& out, void* value)
{
    out.printf("%p", value);
}

} // namespace WTF

namespace JSC {
namespace Probe {

JS_EXPORT_PRIVATE void* probeStateForContext(Probe::Context&);

} // namespace Probe
} // namespace JSC

using namespace JSC;

namespace {

using CPUState = Probe::CPUState;

Lock crashLock;

typedef WTF::Function<void(CCallHelpers&)> Generator;

template<typename T> T nextID(T id) { return static_cast<T>(id + 1); }

#define TESTWORD64 0x0c0defefebeef000
#define TESTWORD32 0x0beef000

#define testWord32(x) (TESTWORD32 + static_cast<uint32_t>(x))
#define testWord64(x) (TESTWORD64 + static_cast<uint64_t>(x))

#if USE(JSVALUE64)
#define testWord(x) testWord64(x)
#else
#define testWord(x) testWord32(x)
#endif

// Nothing fancy for now; we just use the existing WTF assertion machinery.
#define CHECK_EQ(_actual, _expected) do {                               \
        if ((_actual) == (_expected))                                   \
            break;                                                      \
        crashLock.lock();                                               \
        dataLog("FAILED while testing " #_actual ": expected: ", _expected, ", actual: ", _actual, "\n"); \
        WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, "CHECK_EQ("#_actual ", " #_expected ")"); \
        CRASH();                                                        \
    } while (false)

#define CHECK_NOT_EQ(_actual, _expected) do {                               \
        if ((_actual) != (_expected))                                   \
            break;                                                      \
        crashLock.lock();                                               \
        dataLog("FAILED while testing " #_actual ": expected not: ", _expected, ", actual: ", _actual, "\n"); \
        WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, "CHECK_NOT_EQ("#_actual ", " #_expected ")"); \
        CRASH();                                                        \
    } while (false)

bool isPC(MacroAssembler::RegisterID id)
{
#if CPU(ARM_THUMB2)
    return id == ARMRegisters::pc;
#else
    UNUSED_PARAM(id);
    return false;
#endif
}

bool isSP(MacroAssembler::RegisterID id)
{
    return id == MacroAssembler::stackPointerRegister;
}

bool isFP(MacroAssembler::RegisterID id)
{
    return id == MacroAssembler::framePointerRegister;
}

bool isSpecialGPR(MacroAssembler::RegisterID id)
{
    if (isPC(id) || isSP(id) || isFP(id))
        return true;
#if CPU(ARM64)
    if (id == ARM64Registers::x18)
        return true;
#elif CPU(RISCV64)
    if (id == RISCV64Registers::zero || id == RISCV64Registers::ra || id == RISCV64Registers::gp || id == RISCV64Registers::tp)
        return true;
#endif
    return false;
}

MacroAssemblerCodeRef<JSEntryPtrTag> compile(Generator&& generate)
{
    CCallHelpers jit;
    generate(jit);
    LinkBuffer linkBuffer(jit, nullptr);
    return FINALIZE_CODE(linkBuffer, JSEntryPtrTag, nullptr, "testmasm compilation");
}

template<typename T, typename... Arguments>
T invoke(const MacroAssemblerCodeRef<JSEntryPtrTag>& code, Arguments... arguments)
{
    void* executableAddress = untagCFunctionPtr<JSEntryPtrTag>(code.code().taggedPtr());
    T (SYSV_ABI *function)(Arguments...) = std::bit_cast<T(SYSV_ABI *)(Arguments...)>(executableAddress);

#if CPU(RISCV64)
    // RV64 calling convention requires all 32-bit values to be sign-extended into the whole register.
    // JSC JIT is tailored for other ISAs that pass these values in 32-bit-wide registers, which RISC-V
    // doesn't support, so any 32-bit value passed in return-value registers has to be manually sign-extended.
    // This mirrors sign-extension of 32-bit values in argument registers on RV64 in CCallHelpers.h.
    if constexpr (std::is_integral_v<T>) {
        T returnValue = function(arguments...);
        if constexpr (sizeof(T) == 4) {
            asm volatile(
                "sext.w %[out_value], %[in_value]\n\t"
                : [out_value] "=r" (returnValue)
                : [in_value] "r" (returnValue));
        }
        return returnValue;
    }
#endif

    return function(arguments...);
}

template<typename T, typename... Arguments>
T compileAndRun(Generator&& generator, Arguments... arguments)
{
    return invoke<T>(compile(WTFMove(generator)), arguments...);
}

void emitFunctionPrologue(CCallHelpers& jit)
{
    jit.emitFunctionPrologue();
#if CPU(ARM_THUMB2)
    // MacroAssemblerARMv7 uses r6 as a temporary register, which is a
    // callee-saved register, see 5.1.1 of the Procedure Call Standard for
    // the ARM Architecture.
    // http://infocenter.arm.com/help/topic/com.arm.doc.ihi0042f/IHI0042F_aapcs.pdf
    jit.push(ARMRegisters::r6);
#endif
}

void emitFunctionEpilogue(CCallHelpers& jit)
{
#if CPU(ARM_THUMB2)
    jit.pop(ARMRegisters::r6);
#endif
    jit.emitFunctionEpilogue();
}

void testSimple()
{
    CHECK_EQ(compileAndRun<int>([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImm32(42), GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    }), 42);
}

void testGetEffectiveAddress(size_t pointer, ptrdiff_t length, int32_t offset, CCallHelpers::Scale scale)
{
    CHECK_EQ(compileAndRun<size_t>([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImmPtr(std::bit_cast<void*>(pointer)), GPRInfo::regT0);
        jit.move(CCallHelpers::TrustedImmPtr(std::bit_cast<void*>(length)), GPRInfo::regT1);
        jit.getEffectiveAddress(CCallHelpers::BaseIndex(GPRInfo::regT0, GPRInfo::regT1, scale, offset), GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    }), pointer + offset + (static_cast<size_t>(1) << static_cast<int>(scale)) * length);
}

// branchTruncateDoubleToInt32(), when encountering Infinity, -Infinity or a
// Nan, should either yield 0 in dest or fail.
void testBranchTruncateDoubleToInt32(double val, int32_t expected)
{
    const uint64_t valAsUInt = std::bit_cast<uint64_t>(val);
#if CPU(BIG_ENDIAN)
    const bool isBigEndian = true;
#else
    const bool isBigEndian = false;
#endif
    CHECK_EQ(compileAndRun<int>([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.subPtr(CCallHelpers::TrustedImm32(stackAlignmentBytes()), MacroAssembler::stackPointerRegister);
        if (isBigEndian) {
            jit.store32(CCallHelpers::TrustedImm32(valAsUInt >> 32),
                MacroAssembler::Address(MacroAssembler::stackPointerRegister));
            jit.store32(CCallHelpers::TrustedImm32(valAsUInt & 0xffffffff),
                MacroAssembler::Address(MacroAssembler::stackPointerRegister, 4));
        } else {
            jit.store32(CCallHelpers::TrustedImm32(valAsUInt & 0xffffffff),
                MacroAssembler::Address(MacroAssembler::stackPointerRegister));
            jit.store32(CCallHelpers::TrustedImm32(valAsUInt >> 32),
                MacroAssembler::Address(MacroAssembler::stackPointerRegister, 4));
        }
        jit.loadDouble(MacroAssembler::Address(MacroAssembler::stackPointerRegister), FPRInfo::fpRegT0);

        MacroAssembler::Jump done;
        done = jit.branchTruncateDoubleToInt32(FPRInfo::fpRegT0, GPRInfo::returnValueGPR, MacroAssembler::BranchIfTruncateSuccessful);

        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);

        done.link(&jit);
        jit.addPtr(CCallHelpers::TrustedImm32(stackAlignmentBytes()), MacroAssembler::stackPointerRegister);
        emitFunctionEpilogue(jit);
        jit.ret();
    }), expected);
}

void testBranchTest8()
{
    for (auto value : int32Operands()) {
        for (auto value2 : int32Operands()) {
            auto test1 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                auto branch = jit.branchTest8(MacroAssembler::NonZero, CCallHelpers::Address(GPRInfo::argumentGPR0, 1), CCallHelpers::TrustedImm32(value2));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                auto done = jit.jump();
                branch.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                done.link(&jit);

                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto test2 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                auto branch = jit.branchTest8(MacroAssembler::NonZero, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne), CCallHelpers::TrustedImm32(value2));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                auto done = jit.jump();
                branch.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                done.link(&jit);

                emitFunctionEpilogue(jit);
                jit.ret();
            });

            int result = 0;
            if (static_cast<uint8_t>(value) & static_cast<uint8_t>(value2))
                result = 1;

            uint8_t array[] = {
                0,
                static_cast<uint8_t>(value)
            };
            CHECK_EQ(invoke<int>(test1, array), result);
            CHECK_EQ(invoke<int>(test2, array, 1), result);
        }
    }
}

void testBranchTest16()
{
    for (auto value : int32Operands()) {
        for (auto value2 : int32Operands()) {
            auto test1 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                auto branch = jit.branchTest16(MacroAssembler::NonZero, CCallHelpers::Address(GPRInfo::argumentGPR0, 2), CCallHelpers::TrustedImm32(value2));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                auto done = jit.jump();
                branch.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                done.link(&jit);

                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto test2 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                auto branch = jit.branchTest16(MacroAssembler::NonZero, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo), CCallHelpers::TrustedImm32(value2));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                auto done = jit.jump();
                branch.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                done.link(&jit);

                emitFunctionEpilogue(jit);
                jit.ret();
            });

            int result = 0;
            if (static_cast<uint16_t>(value) & static_cast<uint16_t>(value2))
                result = 1;

            uint16_t array[] = {
                0,
                static_cast<uint16_t>(value)
            };
            CHECK_EQ(invoke<int>(test1, array), result);
            CHECK_EQ(invoke<int>(test2, array, 1), result);
        }
    }
}

#if CPU(X86_64)
void testBranchTestBit32RegReg()
{
    for (auto value : int32Operands()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            auto branch = jit.branchTestBit32(MacroAssembler::NonZero, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            branch.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int32Operands())
            CHECK_EQ(invoke<int>(test, value, value2), (value>>(value2%32))&1);
    }
}

void testBranchTestBit32RegImm()
{
    for (auto value : int32Operands()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            auto branch = jit.branchTestBit32(MacroAssembler::NonZero, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(value));
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            branch.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int32Operands())
            CHECK_EQ(invoke<int>(test, value2), (value2>>(value%32))&1);
    }
}

void testBranchTestBit32AddrImm()
{
    for (auto value : int32Operands()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            auto branch = jit.branchTestBit32(MacroAssembler::NonZero, MacroAssembler::Address(GPRInfo::argumentGPR0, 0), CCallHelpers::TrustedImm32(value));
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            branch.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int32Operands())
            CHECK_EQ(invoke<int>(test, &value2), (value2>>(value%32))&1);
    }
}

void testBranchTestBit64RegReg()
{
    for (auto value : int64Operands()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            auto branch = jit.branchTestBit64(MacroAssembler::NonZero, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.move(CCallHelpers::TrustedImm64(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            branch.link(&jit);
            jit.move(CCallHelpers::TrustedImm64(1), GPRInfo::returnValueGPR);
            done.link(&jit);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int64Operands())
            CHECK_EQ(invoke<long int>(test, value, value2), (value>>(value2%64))&1);
    }
}

void testBranchTestBit64RegImm()
{
    for (auto value : int64Operands()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            auto branch = jit.branchTestBit64(MacroAssembler::NonZero, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(value));
            jit.move(CCallHelpers::TrustedImm64(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            branch.link(&jit);
            jit.move(CCallHelpers::TrustedImm64(1), GPRInfo::returnValueGPR);
            done.link(&jit);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int64Operands())
            CHECK_EQ(invoke<long int>(test, value2), (value2>>(value%64))&1);
    }
}

void testBranchTestBit64AddrImm()
{
    for (auto value : int64Operands()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            auto branch = jit.branchTestBit64(MacroAssembler::NonZero, MacroAssembler::Address(GPRInfo::argumentGPR0, 0), CCallHelpers::TrustedImm32(value));
            jit.move(CCallHelpers::TrustedImm64(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            branch.link(&jit);
            jit.move(CCallHelpers::TrustedImm64(1), GPRInfo::returnValueGPR);
            done.link(&jit);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int64Operands())
            CHECK_EQ(invoke<long int>(test, &value2), (value2>>(value%64))&1);
    }
}

#endif

#if CPU(X86_64) || CPU(ARM64)
void testClearBit64()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg scratchGPR = GPRInfo::argumentGPR2;
        jit.clearBit64(GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, scratchGPR);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    constexpr unsigned bitsInWord = sizeof(uint64_t) * 8;

    for (unsigned i = 0; i < bitsInWord; ++i) {
        uint64_t word = std::numeric_limits<uint64_t>::max();
        constexpr uint64_t one = 1;
        CHECK_EQ(invoke<uint64_t>(test, word, i), (word & ~(one << i)));
    }

    for (unsigned i = 0; i < bitsInWord; ++i) {
        uint64_t word = 0;
        CHECK_EQ(invoke<uint64_t>(test, word, i), 0);
    }
}

void testClearBits64WithMask()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.clearBits64WithMask(GPRInfo::argumentGPR1, GPRInfo::argumentGPR0);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : int64Operands()) {
        uint64_t word = std::numeric_limits<uint64_t>::max();
        CHECK_EQ(invoke<uint64_t>(test, word, value), (word & ~value));
    }

    for (auto value : int64Operands()) {
        uint64_t word = 0;
        CHECK_EQ(invoke<uint64_t>(test, word, value), 0);
    }

    uint64_t savedMask = 0;
    auto test2 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.probeDebug([&] (Probe::Context& context) {
            savedMask = context.gpr<uint64_t>(GPRInfo::argumentGPR1);
        });

        jit.clearBits64WithMask(GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, CCallHelpers::ClearBitsAttributes::MustPreserveMask);

        jit.probeDebug([&] (Probe::Context& context) {
            CHECK_EQ(savedMask, context.gpr<uint64_t>(GPRInfo::argumentGPR1));
        });
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : int64Operands()) {
        uint64_t word = std::numeric_limits<uint64_t>::max();
        CHECK_EQ(invoke<uint64_t>(test2, word, value), (word & ~value));
    }

    for (auto value : int64Operands()) {
        uint64_t word = 0;
        CHECK_EQ(invoke<uint64_t>(test2, word, value), 0);
    }
}

void testClearBits64WithMaskTernary()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR2);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR3);
        jit.clearBits64WithMask(GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : int64Operands()) {
        uint64_t word = std::numeric_limits<uint64_t>::max();
        CHECK_EQ(invoke<uint64_t>(test, word, value), (word & ~value));
    }

    for (auto value : int64Operands()) {
        uint64_t word = 0;
        CHECK_EQ(invoke<uint64_t>(test, word, value), 0);
    }

    uint64_t savedMask = 0;
    auto test2 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR2);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR3);

        jit.probeDebug([&] (Probe::Context& context) {
            savedMask = context.gpr<uint64_t>(GPRInfo::argumentGPR2);
        });

        jit.clearBits64WithMask(GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::returnValueGPR, CCallHelpers::ClearBitsAttributes::MustPreserveMask);

        jit.probeDebug([&] (Probe::Context& context) {
            CHECK_EQ(savedMask, context.gpr<uint64_t>(GPRInfo::argumentGPR2));
        });

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : int64Operands()) {
        uint64_t word = std::numeric_limits<uint64_t>::max();
        CHECK_EQ(invoke<uint64_t>(test2, word, value), (word & ~value));
    }

    for (auto value : int64Operands()) {
        uint64_t word = 0;
        CHECK_EQ(invoke<uint64_t>(test2, word, value), 0);
    }
}

static void testCountTrailingZeros64Impl(bool wordCanBeZero)
{
    auto test = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        if (wordCanBeZero)
            jit.countTrailingZeros64(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        else
            jit.countTrailingZeros64WithoutNullCheck(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    constexpr size_t numberOfBits = sizeof(uint64_t) * 8;

    auto expectedNumberOfTrailingZeros = [=] (uint64_t word) -> size_t {
        size_t count = 0;
        for (size_t i = 0; i < numberOfBits; ++i) {
            if (word & 1)
                break;
            word >>= 1;
            count++;
        }
        return count;
    };

    for (auto word : int64Operands()) {
        if (!wordCanBeZero && !word)
            continue;
        CHECK_EQ(invoke<size_t>(test, word), expectedNumberOfTrailingZeros(word));
    }

    for (size_t i = 0; i < numberOfBits; ++i) {
        uint64_t one = 1;
        uint64_t word = one << i;
        CHECK_EQ(invoke<size_t>(test, word), i);
    }
}

void testCountTrailingZeros64()
{
    bool wordCanBeZero = true;
    testCountTrailingZeros64Impl(wordCanBeZero);
}

void testCountTrailingZeros64WithoutNullCheck()
{
    bool wordCanBeZero = false;
    testCountTrailingZeros64Impl(wordCanBeZero);
}

void testShiftAndAdd()
{
    constexpr intptr_t basePointer = 0x1234abcd;

    enum class Reg {
        ArgumentGPR0,
        ArgumentGPR1,
        ArgumentGPR2,
        ArgumentGPR3,
        ScratchGPR
    };

    auto test = [&] (intptr_t index, uint8_t shift, Reg destReg, Reg baseReg, Reg indexReg) {
        auto test = compile([=] (CCallHelpers& jit) {
            CCallHelpers::RegisterID scratchGPR = jit.scratchRegister();

            auto registerIDForReg = [=] (Reg reg) -> CCallHelpers::RegisterID {
                switch (reg) {
                case Reg::ArgumentGPR0: return GPRInfo::argumentGPR0;
                case Reg::ArgumentGPR1: return GPRInfo::argumentGPR1;
                case Reg::ArgumentGPR2: return GPRInfo::argumentGPR2;
                case Reg::ArgumentGPR3: return GPRInfo::argumentGPR3;
                case Reg::ScratchGPR: return scratchGPR;
                }
                RELEASE_ASSERT_NOT_REACHED();
            };

            CCallHelpers::RegisterID destGPR = registerIDForReg(destReg);
            CCallHelpers::RegisterID baseGPR = registerIDForReg(baseReg);
            CCallHelpers::RegisterID indexGPR = registerIDForReg(indexReg);

            emitFunctionPrologue(jit);
            jit.pushPair(scratchGPR, GPRInfo::argumentGPR3);

            jit.move(CCallHelpers::TrustedImmPtr(std::bit_cast<void*>(basePointer)), baseGPR);
            jit.move(CCallHelpers::TrustedImmPtr(std::bit_cast<void*>(index)), indexGPR);
            jit.shiftAndAdd(baseGPR, indexGPR, shift, destGPR);

            jit.probeDebug([=] (Probe::Context& context) {
                if (baseReg != destReg)
                    CHECK_EQ(context.gpr<intptr_t>(baseGPR), basePointer);
                if (indexReg != destReg)
                    CHECK_EQ(context.gpr<intptr_t>(indexGPR), index);
            });
            jit.move(destGPR, GPRInfo::returnValueGPR);

            jit.popPair(scratchGPR, GPRInfo::argumentGPR3);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        CHECK_EQ(invoke<intptr_t>(test), basePointer + (index << shift));
    };

    for (auto index : int32Operands()) {
        for (uint8_t shift = 0; shift < 32; ++shift) {
            test(index, shift, Reg::ScratchGPR, Reg::ScratchGPR, Reg::ArgumentGPR3);     // Scenario: dest == base == scratchRegister.
            test(index, shift, Reg::ArgumentGPR2, Reg::ArgumentGPR2, Reg::ArgumentGPR3); // Scenario: dest == base != scratchRegister.
            test(index, shift, Reg::ScratchGPR, Reg::ArgumentGPR2, Reg::ScratchGPR);     // Scenario: dest == index == scratchRegister.
            test(index, shift, Reg::ArgumentGPR3, Reg::ArgumentGPR2, Reg::ArgumentGPR3); // Scenario: dest == index != scratchRegister.
            test(index, shift, Reg::ArgumentGPR1, Reg::ArgumentGPR2, Reg::ArgumentGPR3); // Scenario: all different registers, no scratchRegister.
            test(index, shift, Reg::ScratchGPR, Reg::ArgumentGPR2, Reg::ArgumentGPR3);   // Scenario: all different registers, dest == scratchRegister.
            test(index, shift, Reg::ArgumentGPR1, Reg::ScratchGPR, Reg::ArgumentGPR3);   // Scenario: all different registers, base == scratchRegister.
            test(index, shift, Reg::ArgumentGPR1, Reg::ArgumentGPR2, Reg::ScratchGPR);   // Scenario: all different registers, index == scratchRegister.
        }
    }
}

void testStore64Imm64AddressPointer()
{
    auto doTest = [] (int64_t value) {
        int64_t dest;
        void* destAddress = &dest;

        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store64(CCallHelpers::TrustedImm64(value), destAddress);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        invoke<size_t>(test);
        CHECK_EQ(dest, value);
    };
    
    for (auto value : int64Operands())
        doTest(value);

    doTest(0x98765555AAAA4321);
    doTest(0xAAAA432198765555);
}

#endif // CPU(X86_64) || CPU(ARM64)

void testCompareDouble(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;

    auto compareDouble = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(-1), GPRInfo::returnValueGPR);
        jit.compareDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto compareDoubleGeneric = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto expectedResult = [&, condition] (double a, double b) -> int {
        auto isUnordered = [] (double x) {
            return x != x;
        };
        switch (condition) {
        case MacroAssembler::DoubleEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a == b);
        case MacroAssembler::DoubleNotEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a != b);
        case MacroAssembler::DoubleGreaterThanAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a > b);
        case MacroAssembler::DoubleGreaterThanOrEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a >= b);
        case MacroAssembler::DoubleLessThanAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a < b);
        case MacroAssembler::DoubleLessThanOrEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a <= b);
        case MacroAssembler::DoubleEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a == b);
        case MacroAssembler::DoubleNotEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a != b);
        case MacroAssembler::DoubleGreaterThanOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a > b);
        case MacroAssembler::DoubleGreaterThanOrEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a >= b);
        case MacroAssembler::DoubleLessThanOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a < b);
        case MacroAssembler::DoubleLessThanOrEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a <= b);
        } // switch
        RELEASE_ASSERT_NOT_REACHED();
    };

    auto operands = doubleOperands();
    for (auto a : operands) {
        for (auto b : operands) {
            arg1 = a;
            arg2 = b;
            CHECK_EQ(invoke<int>(compareDouble), expectedResult(a, b));
            CHECK_EQ(invoke<int>(compareDoubleGeneric), expectedResult(a, b));
        }
    }
}

void testCompareDoubleSameArg(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;

    auto compareDouble = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(-1), GPRInfo::returnValueGPR);
        jit.compareDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto compareDoubleGeneric = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto expectedResult = [&, condition] (double a) -> int {
        auto isUnordered = [] (double x) {
            return x != x;
        };
        switch (condition) {
        case MacroAssembler::DoubleEqualAndOrdered:
            return !isUnordered(a) && (a == a);
        case MacroAssembler::DoubleNotEqualAndOrdered:
            return !isUnordered(a) && (a != a);
        case MacroAssembler::DoubleGreaterThanAndOrdered:
            return !isUnordered(a) && (a > a);
        case MacroAssembler::DoubleGreaterThanOrEqualAndOrdered:
            return !isUnordered(a) && (a >= a);
        case MacroAssembler::DoubleLessThanAndOrdered:
            return !isUnordered(a) && (a < a);
        case MacroAssembler::DoubleLessThanOrEqualAndOrdered:
            return !isUnordered(a) && (a <= a);
        case MacroAssembler::DoubleEqualOrUnordered:
            return isUnordered(a) || (a == a);
        case MacroAssembler::DoubleNotEqualOrUnordered:
            return isUnordered(a) || (a != a);
        case MacroAssembler::DoubleGreaterThanOrUnordered:
            return isUnordered(a) || (a > a);
        case MacroAssembler::DoubleGreaterThanOrEqualOrUnordered:
            return isUnordered(a) || (a >= a);
        case MacroAssembler::DoubleLessThanOrUnordered:
            return isUnordered(a) || (a < a);
        case MacroAssembler::DoubleLessThanOrEqualOrUnordered:
            return isUnordered(a) || (a <= a);
        } // switch
        RELEASE_ASSERT_NOT_REACHED();
    };

    auto operands = doubleOperands();
    for (auto a : operands) {
        arg1 = a;
        CHECK_EQ(invoke<int>(compareDouble), expectedResult(a));
        CHECK_EQ(invoke<int>(compareDoubleGeneric), expectedResult(a));
    }
}

void testMul32WithImmediates()
{
    for (auto immediate : int32Operands()) {
        auto mul = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.mul32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<int>(mul, value), immediate * value);
    }
}

#if CPU(ARM64)
void testMultiplySignExtend32()
{
    for (auto value : int32Operands()) {
        auto mul = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.multiplySignExtend32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int32Operands())
            CHECK_EQ(invoke<long int>(mul, value, value2), ((long int) value) * ((long int) value2));
    }
}

void testMultiplyZeroExtend32()
{
    for (auto nOperand : int32Operands()) {
        auto mul = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.multiplyZeroExtend32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto mOperand : int32Operands()) {
            uint32_t n = nOperand;
            uint32_t m = mOperand;
            CHECK_EQ(invoke<uint64_t>(mul, n, m), static_cast<uint64_t>(n) * static_cast<uint64_t>(m));
        }
    }
}

void testMultiplyAddSignExtend32()
{
    // d = SExt32(n) * SExt32(m) + a
    auto add = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.multiplyAddSignExtend32(GPRInfo::argumentGPR0, 
            GPRInfo::argumentGPR1, 
            GPRInfo::argumentGPR2, 
            GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto a : int64Operands())
                CHECK_EQ(invoke<int64_t>(add, n, m, a), static_cast<int64_t>(n) * static_cast<int64_t>(m) + a);
        }
    }
}

void testMultiplyAddZeroExtend32()
{
    // d = ZExt32(n) * ZExt32(m) + a
    auto add = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.multiplyAddZeroExtend32(GPRInfo::argumentGPR0, 
            GPRInfo::argumentGPR1, 
            GPRInfo::argumentGPR2, 
            GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto a : int64Operands()) {
                uint32_t un = n;
                uint32_t um = m;
                CHECK_EQ(invoke<int64_t>(add, n, m, a), static_cast<int64_t>(un) * static_cast<int64_t>(um) + a);
            }
        }
    }
}

void testSub32Args()
{
    for (auto value : int32Operands()) {
        auto sub = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.sub32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value2 : int32Operands())
            CHECK_EQ(invoke<uint32_t>(sub, value, value2), static_cast<uint32_t>(value - value2));
    }
}

void testSub32Imm()
{
    for (auto immediate : int32Operands()) {
        for (auto immediate2 : int32Operands()) {
            auto sub = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                jit.move(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
                jit.sub32(CCallHelpers::TrustedImm32(immediate2), GPRInfo::returnValueGPR);

                emitFunctionEpilogue(jit);
                jit.ret();
            });
            CHECK_EQ(invoke<uint32_t>(sub), static_cast<uint32_t>(immediate - immediate2));
        }
    }
}

void testSub64Imm32()
{
    for (auto immediate : int64Operands()) {
        for (auto immediate2 : int32Operands()) {
            auto sub = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                jit.move(CCallHelpers::TrustedImm64(immediate), GPRInfo::returnValueGPR);
                jit.sub64(CCallHelpers::TrustedImm32(immediate2), GPRInfo::returnValueGPR);

                emitFunctionEpilogue(jit);
                jit.ret();
            });
            CHECK_EQ(invoke<uint64_t>(sub), static_cast<uint64_t>(immediate - immediate2));
        }
    }
}

void testSub64ArgImm32()
{
    for (auto immediate : int32Operands()) {
        auto sub = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.sub64(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int64Operands())
            CHECK_EQ(invoke<int64_t>(sub, value), static_cast<int64_t>(value - immediate));
    }
}

void testSub64Imm64()
{
    for (auto immediate : int64Operands()) {
        for (auto immediate2 : int64Operands()) {
            auto sub = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                jit.move(CCallHelpers::TrustedImm64(immediate), GPRInfo::returnValueGPR);
                jit.sub64(CCallHelpers::TrustedImm64(immediate2), GPRInfo::returnValueGPR);

                emitFunctionEpilogue(jit);
                jit.ret();
            });
            CHECK_EQ(invoke<uint64_t>(sub), static_cast<uint64_t>(immediate - immediate2));
        }
    }
}

void testSub64ArgImm64()
{
    for (auto immediate : int64Operands()) {
        auto sub = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.sub64(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm64(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int64Operands())
            CHECK_EQ(invoke<int64_t>(sub, value), static_cast<int64_t>(value - immediate));
    }
}

void testMultiplySubSignExtend32()
{
    // d = a - SExt32(n) * SExt32(m)
    auto sub = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.multiplySubSignExtend32(GPRInfo::argumentGPR1, 
            GPRInfo::argumentGPR2,
            GPRInfo::argumentGPR0, 
            GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto a : int64Operands()) {
        for (auto n : int32Operands()) {
            for (auto m : int32Operands())
                CHECK_EQ(invoke<int64_t>(sub, a, n, m), a - static_cast<int64_t>(n) * static_cast<int64_t>(m));
        }
    }
}

void testMultiplySubZeroExtend32()
{
    // d = a - (ZExt32(n) * ZExt32(m))
    auto sub = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.multiplySubZeroExtend32(GPRInfo::argumentGPR1, 
            GPRInfo::argumentGPR2,
            GPRInfo::argumentGPR0, 
            GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto a : int64Operands()) {
        for (auto n : int32Operands()) {
            for (auto m : int32Operands()) {
                uint32_t un = n;
                uint32_t um = m;
                CHECK_EQ(invoke<int64_t>(sub, a, n, m), a - static_cast<int64_t>(un) * static_cast<int64_t>(um));
            }
        }
    }
}

void testMultiplyNegSignExtend32()
{
    // d = - (SExt32(n) * SExt32(m))
    auto neg = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.multiplyNegSignExtend32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto n : int32Operands()) {
        for (auto m : int32Operands())
            CHECK_EQ(invoke<int64_t>(neg, n, m), -(static_cast<int64_t>(n) * static_cast<int64_t>(m)));
    }
}

void testMultiplyNegZeroExtend32()
{
    // d = - ZExt32(n) * ZExt32(m)
    auto neg = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.multiplyNegZeroExtend32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            uint32_t un = n;
            uint32_t um = m;
            CHECK_EQ(invoke<uint64_t>(neg, n, m), -(static_cast<uint64_t>(un) * static_cast<uint64_t>(um)));
        }
    }
}

void testExtractUnsignedBitfield32()
{
    uint32_t src = 0xf0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto ubfx32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractUnsignedBitfield32(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                CHECK_EQ(invoke<uint32_t>(ubfx32, src), ((src >> lsb) & ((1U << width) - 1U)));
            }
        }
    }
}

void testExtractUnsignedBitfield64()
{
    uint64_t src = 0xf0f0f0f0f0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 64) {
                auto ubfx64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractUnsignedBitfield64(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                CHECK_EQ(invoke<uint64_t>(ubfx64, src), ((src >> lsb) & ((1ULL << width) - 1ULL)));
            }
        }
    }
}

void testInsertUnsignedBitfieldInZero32()
{
    uint32_t src = 0xf0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto ubfiz32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.insertUnsignedBitfieldInZero32(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint32_t mask = (1U << width) - 1U;
                CHECK_EQ(invoke<uint32_t>(ubfiz32, src), (src & mask) << lsb);
            }
        }
    }
}

void testInsertUnsignedBitfieldInZero64()
{
    uint64_t src = 0xf0f0f0f0f0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 64) {
                auto ubfiz64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.insertUnsignedBitfieldInZero64(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint64_t mask = (1ULL << width) - 1ULL;
                CHECK_EQ(invoke<uint64_t>(ubfiz64, src), (src & mask) << lsb);
            }
        }
    }
}

void testInsertBitField32()
{
    uint32_t src = 0x0f0f0f0f;
    uint32_t dst = 0xf0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto bfi32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.insertBitField32(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::argumentGPR1);
                    jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint32_t mask1 = (1U << width) - 1U;
                uint32_t mask2 = ~(mask1 << lsb);
                uint32_t rhs = invoke<uint32_t>(bfi32, src, dst);
                uint32_t lhs = ((src & mask1) << lsb) | (dst & mask2);
                CHECK_EQ(rhs, lhs);
            }
        }
    }
}

void testInsertBitField64()
{
    uint64_t src = 0x0f0f0f0f0f0f0f0f;
    uint64_t dst = 0xf0f0f0f0f0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 64) {
                auto bfi64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.insertBitField64(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::argumentGPR1);
                    jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint64_t mask1 = (1ULL << width) - 1ULL;
                uint64_t mask2 = ~(mask1 << lsb);
                uint64_t rhs = invoke<uint64_t>(bfi64, src, dst);
                uint64_t lhs = ((src & mask1) << lsb) | (dst & mask2);
                CHECK_EQ(rhs, lhs);
            }
        }
    }
}

void testExtractInsertBitfieldAtLowEnd32()
{
    uint32_t src = 0xf0f0f0f0;
    uint32_t dst = 0x0f0f0f0f;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto bfxil32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractInsertBitfieldAtLowEnd32(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::argumentGPR1);
                    jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint32_t mask1 = (1U << width) - 1U;
                uint32_t mask2 = ~mask1;
                uint32_t rhs = invoke<uint32_t>(bfxil32, src, dst);
                uint32_t lhs = ((src >> lsb) & mask1) | (dst & mask2);
                CHECK_EQ(rhs, lhs);
            }
        }
    }
}

void testExtractInsertBitfieldAtLowEnd64()
{
    uint64_t src = 0x0f0f0f0f0f0f0f0f;
    uint64_t dst = 0xf0f0f0f0f0f0f0f0;
    Vector<uint64_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 64) {
                auto bfxil64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractInsertBitfieldAtLowEnd64(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::argumentGPR1);
                    jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint64_t mask1 = (1ULL << width) - 1ULL;
                uint64_t mask2 = ~mask1;
                uint64_t rhs = invoke<uint64_t>(bfxil64, src, dst);
                uint64_t lhs = ((src >> lsb) & mask1) | (dst & mask2);
                CHECK_EQ(rhs, lhs);
            }
        }
    }
}

void testClearBitField32()
{
    uint32_t src = std::numeric_limits<uint32_t>::max();
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto bfc32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.clearBitField32(CCallHelpers::TrustedImm32(lsb), CCallHelpers::TrustedImm32(width), GPRInfo::argumentGPR0);
                    jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint32_t mask = ((1U << width) - 1U) << lsb;
                uint32_t rhs = invoke<uint32_t>(bfc32, src);
                uint32_t lhs = src & ~mask;
                CHECK_EQ(rhs, lhs);
            }
        }
    }
}

void testClearBitField64()
{
    uint64_t src = std::numeric_limits<uint64_t>::max();
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto bfc64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.clearBitField64(CCallHelpers::TrustedImm32(lsb), CCallHelpers::TrustedImm32(width), GPRInfo::argumentGPR0);
                    jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });
                uint64_t mask = ((1ULL << width) - 1ULL) << lsb;
                uint64_t rhs = invoke<uint64_t>(bfc64, src);
                uint64_t lhs = src & ~mask;
                CHECK_EQ(rhs, lhs);
            }
        }
    }
}

void testClearBitsWithMask32()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.clearBitsWithMask32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto mask : int32Operands()) {
        uint32_t src = std::numeric_limits<uint32_t>::max();
        CHECK_EQ(invoke<uint32_t>(test, src, mask), (src & ~mask));
        CHECK_EQ(invoke<uint32_t>(test, 0U, mask), 0U);
    }
}

void testClearBitsWithMask64()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.clearBitsWithMask64(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto mask : int64Operands()) {
        uint64_t src = std::numeric_limits<uint64_t>::max();
        CHECK_EQ(invoke<uint64_t>(test, src, mask), (src & ~mask));
        CHECK_EQ(invoke<uint64_t>(test, 0ULL, mask), 0ULL);
    }
}

void testOrNot32()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.orNot32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto mask : int32Operands()) {
        int32_t src = std::numeric_limits<uint32_t>::max();
        CHECK_EQ(invoke<int32_t>(test, src, mask), (src | ~mask));
        CHECK_EQ(invoke<int32_t>(test, 0U, mask), ~mask);
    }
}

void testOrNot64()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.orNot64(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto mask : int64Operands()) {
        int64_t src = std::numeric_limits<uint64_t>::max();
        CHECK_EQ(invoke<int64_t>(test, src, mask), (src | ~mask));
        CHECK_EQ(invoke<int64_t>(test, 0ULL, mask), ~mask);
    }
}

void testInsertSignedBitfieldInZero32()
{
    uint32_t src = 0xf0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto insertSignedBitfieldInZero32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.insertSignedBitfieldInZero32(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t bf = src;
                int32_t mask1 = (1 << width) - 1;
                int32_t mask2 = 1 << (width - 1);
                int32_t bfsx = ((bf & mask1) ^ mask2) - mask2;

                CHECK_EQ(invoke<int32_t>(insertSignedBitfieldInZero32, src), bfsx << lsb);
            }
        }
    }
}

void testInsertSignedBitfieldInZero64()
{
    int64_t src = 0xf0f0f0f0f0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 64) {
                auto insertSignedBitfieldInZero64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.insertSignedBitfieldInZero64(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t bf = src;
                int64_t amount = CHAR_BIT * sizeof(bf) - width;
                int64_t bfsx = (bf << amount) >> amount;

                CHECK_EQ(invoke<int64_t>(insertSignedBitfieldInZero64, src), bfsx << lsb);
            }
        }
    }
}

void testExtractSignedBitfield32()
{
    int32_t src = 0xf0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 32) {
                auto extractSignedBitfield32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractSignedBitfield32(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t bf = src >> lsb;
                int32_t mask1 = (1 << width) - 1;
                int32_t mask2 = 1 << (width - 1);
                int32_t bfsx = ((bf & mask1) ^ mask2) - mask2;

                CHECK_EQ(invoke<int32_t>(extractSignedBitfield32, src), bfsx);
            }
        }
    }
}

void testExtractSignedBitfield64()
{
    int64_t src = 0xf0f0f0f0f0f0f0f0;
    Vector<uint32_t> imms = { 0, 1, 5, 7, 30, 31, 32, 42, 56, 62, 63, 64 };
    for (auto lsb : imms) {
        for (auto width : imms) {
            if (width > 0 && lsb + width < 64) {
                auto extractSignedBitfield64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractSignedBitfield64(GPRInfo::argumentGPR0, 
                        CCallHelpers::TrustedImm32(lsb), 
                        CCallHelpers::TrustedImm32(width), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t bf = src >> lsb;
                int64_t amount = CHAR_BIT * sizeof(bf) - width;
                int64_t bfsx = (bf << amount) >> amount;

                CHECK_EQ(invoke<int64_t>(extractSignedBitfield64, src), bfsx);
            }
        }
    }
}

void testExtractRegister32()
{
    uint32_t datasize = CHAR_BIT * sizeof(uint32_t);

    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (uint32_t lsb = 0; lsb < datasize; ++lsb) {
                auto extractRegister32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractRegister32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(lsb),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                // Test pattern: d = ((n & mask) << highWidth) | (m >>> lowWidth)
                // Where: highWidth = datasize - lowWidth
                //        mask = (1 << lowWidth) - 1
                uint32_t highWidth = datasize - lsb;
                uint32_t mask = (1U << (lsb % 32)) - 1U;
                uint32_t left = (n & mask) << (highWidth % 32);
                uint32_t right = (static_cast<uint32_t>(m) >> (lsb % 32));
                uint32_t rhs = left | right;
                uint32_t lhs = invoke<uint32_t>(extractRegister32, n, m);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testExtractRegister64()
{
    uint64_t datasize = CHAR_BIT * sizeof(uint64_t);

    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (uint32_t lsb = 0; lsb < datasize; ++lsb) {
                auto extractRegister64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.extractRegister64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(lsb),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                // Test pattern: d = ((n & mask) << highWidth) | (m >>> lowWidth)
                // Where: highWidth = datasize - lowWidth
                //        mask = (1 << lowWidth) - 1
                uint64_t highWidth = datasize - lsb;
                uint64_t mask = (1ULL << (lsb % 64)) - 1ULL;
                uint64_t left = (n & mask) << (highWidth % 64);
                uint64_t right = (static_cast<uint64_t>(m) >> (lsb % 64));
                uint64_t rhs = left | right;
                uint64_t lhs = invoke<uint64_t>(extractRegister64, n, m);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAddWithLeftShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto add32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.addLeftShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(add32, n, m);
                int32_t rhs = n + (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAddWithRightShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto add32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.addRightShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(add32, n, m);
                int32_t rhs = n + (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAddWithUnsignedRightShift32()
{
    Vector<uint32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto add32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.addUnsignedRightShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint32_t lhs = invoke<uint32_t>(add32, n, m);
                uint32_t rhs = static_cast<uint32_t>(n) + (static_cast<uint32_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAddWithLeftShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto add64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.addLeftShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(add64, n, m);
                int64_t rhs = n + (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAddWithRightShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto add64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.addRightShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(add64, n, m);
                int64_t rhs = n + (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAddWithUnsignedRightShift64()
{
    Vector<uint32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto add64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.addUnsignedRightShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint64_t lhs = invoke<uint64_t>(add64, n, m);
                uint64_t rhs = static_cast<uint64_t>(n) + (static_cast<uint64_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testSubWithLeftShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto sub32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.subLeftShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(sub32, n, m);
                int32_t rhs = n - (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testSubWithRightShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto sub32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.subRightShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(sub32, n, m);
                int32_t rhs = n - (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testSubWithUnsignedRightShift32()
{
    Vector<uint32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto sub32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.subUnsignedRightShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint32_t lhs = invoke<uint32_t>(sub32, n, m);
                uint32_t rhs = static_cast<uint32_t>(n) - (static_cast<uint32_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testSubWithLeftShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto sub64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.subLeftShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(sub64, n, m);
                int64_t rhs = n - (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testSubWithRightShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto sub64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.subRightShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(sub64, n, m);
                int64_t rhs = n - (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testSubWithUnsignedRightShift64()
{
    Vector<uint32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto sub64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.subUnsignedRightShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint64_t lhs = invoke<uint64_t>(sub64, n, m);
                uint64_t rhs = static_cast<uint64_t>(n) - (static_cast<uint64_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorNot32()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.xorNot32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto mask : int32Operands()) {
        int32_t src = std::numeric_limits<uint32_t>::max();
        CHECK_EQ(invoke<int32_t>(test, src, mask), (src ^ ~mask));
        CHECK_EQ(invoke<int32_t>(test, 0U, mask), ~mask);
    }
}

void testXorNot64()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.xorNot64(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto mask : int64Operands()) {
        int64_t src = std::numeric_limits<uint64_t>::max();
        CHECK_EQ(invoke<int64_t>(test, src, mask), (src ^ ~mask));
        CHECK_EQ(invoke<int64_t>(test, 0ULL, mask), ~mask);
    }
}

void testXorNotWithLeftShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto test = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorNotLeftShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(test, n, m);
                int32_t rhs = n ^ ~(m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorNotWithRightShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto test = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorNotRightShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(test, n, m);
                int32_t rhs = n ^ ~(m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorNotWithUnsignedRightShift32()
{
    Vector<uint32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto test = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorNotUnsignedRightShift32(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint32_t lhs = invoke<uint32_t>(test, n, m);
                uint32_t rhs = static_cast<uint32_t>(n) ^ ~(static_cast<uint32_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorNotWithLeftShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto test = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorNotLeftShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(test, n, m);
                int64_t rhs = n ^ ~(m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorNotWithRightShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto test = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorNotRightShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(test, n, m);
                int64_t rhs = n ^ ~(m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorNotWithUnsignedRightShift64()
{
    Vector<uint32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto test = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorNotUnsignedRightShift64(GPRInfo::argumentGPR0, 
                        GPRInfo::argumentGPR1, 
                        CCallHelpers::TrustedImm32(amount), 
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint64_t lhs = invoke<uint64_t>(test, n, m);
                uint64_t rhs = static_cast<uint64_t>(n) ^ ~(static_cast<uint64_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testStorePrePostIndex32()
{
    int32_t nums[] = { 1, 2, 3 };
    intptr_t addr = std::bit_cast<intptr_t>(&nums[1]);
    int32_t index = sizeof(int32_t);

    auto test1 = [&] (int32_t src) {
        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // *++p1 = 4; return p1;
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::PreIndexAddress(GPRInfo::argumentGPR1, index));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<intptr_t>(store, src, addr);
    };

    int32_t* p1 = std::bit_cast<int32_t*>(test1(4));
    CHECK_EQ(*p1, 4);
    CHECK_EQ(*--p1, nums[1]);

    auto test2 = [&] (int32_t src) {
        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // *p2++ = 5; return p2;
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::PostIndexAddress(GPRInfo::argumentGPR1, index));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<intptr_t>(store, src, addr);
    };

    int32_t* p2 = std::bit_cast<int32_t*>(test2(5));
    CHECK_EQ(*p2, 4);
    CHECK_EQ(*--p2, 5);
}

void testStorePrePostIndex64()
{
    int64_t nums[] = { 1, 2, 3 };
    intptr_t addr = std::bit_cast<intptr_t>(&nums[1]);
    int32_t index = sizeof(int64_t);

    auto test1 = [&] (int64_t src) {
        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // *++p1 = 4; return p1;
            jit.store64(GPRInfo::argumentGPR0, CCallHelpers::PreIndexAddress(GPRInfo::argumentGPR1, index));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<intptr_t>(store, src, addr);
    };

    int64_t* p1 = std::bit_cast<int64_t*>(test1(4));
    CHECK_EQ(*p1, 4);
    CHECK_EQ(*--p1, nums[1]);

    auto test2 = [&] (int64_t src) {
        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // *p2++ = 5; return p2;
            jit.store64(GPRInfo::argumentGPR0, CCallHelpers::PostIndexAddress(GPRInfo::argumentGPR1, index));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<intptr_t>(store, src, addr);
    };

    int64_t* p2 = std::bit_cast<int64_t*>(test2(5));
    CHECK_EQ(*p2, 4);
    CHECK_EQ(*--p2, 5);
}

void testLoadPrePostIndex32()
{
    int32_t nums[] = { 1, 2, 3 };
    int32_t index = sizeof(int32_t);

    auto test1 = [&] (int32_t replace) {
        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // res = *++p1; *p1 = 4; return res;
            jit.load32(CCallHelpers::PreIndexAddress(GPRInfo::argumentGPR0, index), GPRInfo::argumentGPR1);
            jit.store32(CCallHelpers::TrustedImm32(replace), CCallHelpers::Address(GPRInfo::argumentGPR0));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<int32_t>(load, &nums[1]);
    };

    CHECK_EQ(test1(4), 3);
    CHECK_EQ(nums[2], 4);

    auto test2 = [&] (int32_t replace) {
        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // res = *p2++; *p2 = 5; return res;
            jit.load32(CCallHelpers::PostIndexAddress(GPRInfo::argumentGPR0, index), GPRInfo::argumentGPR1);
            jit.store32(CCallHelpers::TrustedImm32(replace), CCallHelpers::Address(GPRInfo::argumentGPR0));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<int32_t>(load, &nums[1]);
    };

    CHECK_EQ(test2(5), 2);
    CHECK_EQ(nums[2], 5);
}

void testLoadPrePostIndex64()
{
    int64_t nums[] = { 1, 2, 3 };
    int32_t index = sizeof(int64_t);

    auto test1 = [&] (int64_t replace) {
        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // res = *++p1; *p1 = 4; return res;
            jit.load64(CCallHelpers::PreIndexAddress(GPRInfo::argumentGPR0, index), GPRInfo::argumentGPR1);
            jit.store64(CCallHelpers::TrustedImm64(replace), CCallHelpers::Address(GPRInfo::argumentGPR0));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<int64_t>(load, &nums[1]);
    };

    CHECK_EQ(test1(4), 3);
    CHECK_EQ(nums[2], 4);

    auto test2 = [&] (int64_t replace) {
        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            // res = *p2++; *p2 = 5; return res;
            jit.load64(CCallHelpers::PostIndexAddress(GPRInfo::argumentGPR0, index), GPRInfo::argumentGPR1);
            jit.store64(CCallHelpers::TrustedImm64(replace), CCallHelpers::Address(GPRInfo::argumentGPR0));
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });
        return invoke<int64_t>(load, &nums[1]);
    };

    CHECK_EQ(test2(5), 2);
    CHECK_EQ(nums[2], 5);
}

void testAndLeftShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto and32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.andLeftShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(and32, n, m);
                int32_t rhs = n & (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAndRightShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto and32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.andRightShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(and32, n, m);
                int32_t rhs = n & (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAndUnsignedRightShift32()
{
    Vector<uint32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto and32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.andUnsignedRightShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint32_t lhs = invoke<uint32_t>(and32, n, m);
                uint32_t rhs = static_cast<uint32_t>(n) & (static_cast<uint32_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAndLeftShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto and64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.andLeftShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(and64, n, m);
                int64_t rhs = n & (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAndRightShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto and64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.andRightShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(and64, n, m);
                int64_t rhs = n & (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testAndUnsignedRightShift64()
{
    Vector<uint32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto and64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.andUnsignedRightShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint64_t lhs = invoke<uint64_t>(and64, n, m);
                uint64_t rhs = static_cast<uint64_t>(n) & (static_cast<uint64_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorLeftShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto xor32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorLeftShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(xor32, n, m);
                int32_t rhs = n ^ (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorRightShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto xor32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorRightShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(xor32, n, m);
                int32_t rhs = n ^ (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorUnsignedRightShift32()
{
    Vector<uint32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto xor32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorUnsignedRightShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint32_t lhs = invoke<uint32_t>(xor32, n, m);
                uint32_t rhs = static_cast<uint32_t>(n) ^ (static_cast<uint32_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorLeftShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto xor64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorLeftShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(xor64, n, m);
                int64_t rhs = n ^ (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorRightShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto xor64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorRightShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(xor64, n, m);
                int64_t rhs = n ^ (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testXorUnsignedRightShift64()
{
    Vector<uint32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto xor64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.xorUnsignedRightShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint64_t lhs = invoke<uint64_t>(xor64, n, m);
                uint64_t rhs = static_cast<uint64_t>(n) ^ (static_cast<uint64_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testOrLeftShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto or32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.orLeftShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(or32, n, m);
                int32_t rhs = n | (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testOrRightShift32()
{
    Vector<int32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto or32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.orRightShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int32_t lhs = invoke<int32_t>(or32, n, m);
                int32_t rhs = n | (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testOrUnsignedRightShift32()
{
    Vector<uint32_t> amounts = { 0, 17, 31 };
    for (auto n : int32Operands()) {
        for (auto m : int32Operands()) {
            for (auto amount : amounts) {
                auto or32 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.orUnsignedRightShift32(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint32_t lhs = invoke<uint32_t>(or32, n, m);
                uint32_t rhs = static_cast<uint32_t>(n) | (static_cast<uint32_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testOrLeftShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto or64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.orLeftShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(or64, n, m);
                int64_t rhs = n | (m << amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testOrRightShift64()
{
    Vector<int32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto or64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.orRightShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                int64_t lhs = invoke<int64_t>(or64, n, m);
                int64_t rhs = n | (m >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testOrUnsignedRightShift64()
{
    Vector<uint32_t> amounts = { 0, 34, 63 };
    for (auto n : int64Operands()) {
        for (auto m : int64Operands()) {
            for (auto amount : amounts) {
                auto or64 = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);

                    jit.orUnsignedRightShift64(GPRInfo::argumentGPR0,
                        GPRInfo::argumentGPR1,
                        CCallHelpers::TrustedImm32(amount),
                        GPRInfo::returnValueGPR);

                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                uint64_t lhs = invoke<uint64_t>(or64, n, m);
                uint64_t rhs = static_cast<uint64_t>(n) | (static_cast<uint64_t>(m) >> amount);
                CHECK_EQ(lhs, rhs);
            }
        }
    }
}

void testZeroExtend48ToWord()
{
    auto zext48First = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.zeroExtend48ToWord(GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto zeroTop16Bits = [] (int64_t value) -> int64_t {
        return value & ((1ull << 48) - 1);
    };

    for (auto a : int64Operands())
        CHECK_EQ(invoke<int64_t>(zext48First, a), zeroTop16Bits(a));

    auto zext48Second = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.zeroExtend48ToWord(GPRInfo::argumentGPR1, GPRInfo::argumentGPR0);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto a : int64Operands())
        CHECK_EQ(invoke<int64_t>(zext48Second, 0, a), zeroTop16Bits(a));
}
#endif

#if CPU(X86_64) || CPU(ARM64) || CPU(RISCV64)
void testCompareFloat(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;

    auto compareFloat = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(-1), GPRInfo::returnValueGPR);
        jit.compareFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto compareFloatGeneric = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto operands = floatOperands();
    for (auto a : operands) {
        for (auto b : operands) {
            arg1 = a;
            arg2 = b;
            CHECK_EQ(invoke<int>(compareFloat), invoke<int>(compareFloatGeneric));
        }
    }
}
#endif // CPU(X86_64) || CPU(ARM64)

#if CPU(X86_64) || CPU(ARM64) || CPU(RISCV64)

template<typename T, typename SelectionType>
void testMoveConditionallyFloatingPoint(MacroAssembler::DoubleCondition condition, const MacroAssemblerCodeRef<JSEntryPtrTag>& testCode, T& arg1, T& arg2, const Vector<T> operands, SelectionType selectionA, SelectionType selectionB)
{
    auto expectedResult = [&, condition] (T a, T b) -> SelectionType {
        auto isUnordered = [] (double x) {
            return x != x;
        };
        switch (condition) {
        case MacroAssembler::DoubleEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a == b) ? selectionA : selectionB;
        case MacroAssembler::DoubleNotEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a != b) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a > b) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanOrEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a >= b) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a < b) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanOrEqualAndOrdered:
            return !isUnordered(a) && !isUnordered(b) && (a <= b) ? selectionA : selectionB;
        case MacroAssembler::DoubleEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a == b) ? selectionA : selectionB;
        case MacroAssembler::DoubleNotEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a != b) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a > b) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanOrEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a >= b) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a < b) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanOrEqualOrUnordered:
            return isUnordered(a) || isUnordered(b) || (a <= b) ? selectionA : selectionB;
        } // switch
        RELEASE_ASSERT_NOT_REACHED();
    };

    for (auto a : operands) {
        for (auto b : operands) {
            arg1 = a;
            arg2 = b;
            CHECK_EQ(invoke<SelectionType>(testCode), expectedResult(a, b));
        }
    }
}

void testMoveConditionallyDouble2(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        RELEASE_ASSERT(destGPR != selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), destGPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, destGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, doubleOperands(), selectionA, selectionB);
}

void testMoveConditionallyDouble3(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;
    unsigned corruptedSelectionA = 0xbbad000a;
    unsigned corruptedSelectionB = 0xbbad000b;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        GPRReg selectionBGPR = GPRInfo::argumentGPR3;
        RELEASE_ASSERT(destGPR != selectionAGPR);
        RELEASE_ASSERT(destGPR != selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(-1), destGPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, selectionBGPR, destGPR);

        auto aIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionAGPR, CCallHelpers::TrustedImm32(selectionA));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionA), destGPR);
        aIsUnchanged.link(&jit);

        auto bIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionBGPR, CCallHelpers::TrustedImm32(selectionB));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionB), destGPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, doubleOperands(), selectionA, selectionB);
}

void testMoveConditionallyDouble3DestSameAsThenCase(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;
    unsigned corruptedSelectionB = 0xbbad000b;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = destGPR;
        GPRReg selectionBGPR = GPRInfo::argumentGPR3;
        RELEASE_ASSERT(destGPR == selectionAGPR);
        RELEASE_ASSERT(destGPR != selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, selectionBGPR, destGPR);

        auto bIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionBGPR, CCallHelpers::TrustedImm32(selectionB));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionB), destGPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, doubleOperands(), selectionA, selectionB);
}

void testMoveConditionallyDouble3DestSameAsElseCase(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;
    unsigned corruptedSelectionA = 0xbbad000a;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        GPRReg selectionBGPR = destGPR;
        RELEASE_ASSERT(destGPR != selectionAGPR);
        RELEASE_ASSERT(destGPR == selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, selectionBGPR, destGPR);

        auto aIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionAGPR, CCallHelpers::TrustedImm32(selectionA));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionA), destGPR);
        aIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, doubleOperands(), selectionA, selectionB);
}

void testMoveConditionallyFloat2(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        RELEASE_ASSERT(destGPR != selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), GPRInfo::returnValueGPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, destGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, floatOperands(), selectionA, selectionB);
}

void testMoveConditionallyFloat3(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;
    unsigned corruptedSelectionA = 0xbbad000a;
    unsigned corruptedSelectionB = 0xbbad000b;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        GPRReg selectionBGPR = GPRInfo::argumentGPR3;
        RELEASE_ASSERT(destGPR != selectionAGPR);
        RELEASE_ASSERT(destGPR != selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(-1), destGPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, selectionBGPR, destGPR);

        auto aIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionAGPR, CCallHelpers::TrustedImm32(selectionA));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionA), destGPR);
        aIsUnchanged.link(&jit);

        auto bIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionBGPR, CCallHelpers::TrustedImm32(selectionB));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionB), destGPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, floatOperands(), selectionA, selectionB);
}

void testMoveConditionallyFloat3DestSameAsThenCase(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;
    unsigned corruptedSelectionB = 0xbbad000b;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = destGPR;
        GPRReg selectionBGPR = GPRInfo::argumentGPR3;
        RELEASE_ASSERT(destGPR == selectionAGPR);
        RELEASE_ASSERT(destGPR != selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, selectionBGPR, destGPR);

        auto bIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionBGPR, CCallHelpers::TrustedImm32(selectionB));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionB), destGPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, floatOperands(), selectionA, selectionB);
}

void testMoveConditionallyFloat3DestSameAsElseCase(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;
    unsigned corruptedSelectionA = 0xbbad000a;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg destGPR = GPRInfo::returnValueGPR;
        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        GPRReg selectionBGPR = destGPR;
        RELEASE_ASSERT(destGPR != selectionAGPR);
        RELEASE_ASSERT(destGPR == selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.moveConditionallyFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1, selectionAGPR, selectionBGPR, destGPR);

        auto aIsUnchanged = jit.branch32(CCallHelpers::Equal, selectionAGPR, CCallHelpers::TrustedImm32(selectionA));
        jit.move(CCallHelpers::TrustedImm32(corruptedSelectionA), destGPR);
        aIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, floatOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyDouble(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;
    double corruptedSelectionA = 55555;
    double corruptedSelectionB = 66666;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        FPRReg destFPR = FPRInfo::returnValueFPR;
        FPRReg selectionAFPR = FPRInfo::fpRegT1;
        FPRReg selectionBFPR = FPRInfo::fpRegT2;
        FPRReg arg1FPR = FPRInfo::fpRegT3;
        FPRReg arg2FPR = FPRInfo::fpRegT4;

        RELEASE_ASSERT(destFPR != selectionAFPR);
        RELEASE_ASSERT(destFPR != selectionBFPR);
        RELEASE_ASSERT(destFPR != arg1FPR);
        RELEASE_ASSERT(destFPR != arg2FPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), arg1FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), arg2FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), selectionAFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), selectionBFPR);
        jit.moveDoubleConditionallyDouble(condition, arg1FPR, arg2FPR, selectionAFPR, selectionBFPR, destFPR);

        FPRReg tempFPR = FPRInfo::fpRegT5;
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), tempFPR);
        auto aIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionAFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionA), destFPR);
        aIsUnchanged.link(&jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), tempFPR);
        auto bIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionBFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionB), destFPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, doubleOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyDoubleDestSameAsThenCase(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;
    double corruptedSelectionB = 66666;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        FPRReg destFPR = FPRInfo::returnValueFPR;
        FPRReg selectionAFPR = destFPR;
        FPRReg selectionBFPR = FPRInfo::fpRegT2;
        FPRReg arg1FPR = FPRInfo::fpRegT3;
        FPRReg arg2FPR = FPRInfo::fpRegT4;

        RELEASE_ASSERT(destFPR == selectionAFPR);
        RELEASE_ASSERT(destFPR != selectionBFPR);
        RELEASE_ASSERT(destFPR != arg1FPR);
        RELEASE_ASSERT(destFPR != arg2FPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), arg1FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), arg2FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), selectionAFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), selectionBFPR);
        jit.moveDoubleConditionallyDouble(condition, arg1FPR, arg2FPR, selectionAFPR, selectionBFPR, destFPR);

        FPRReg tempFPR = FPRInfo::fpRegT5;
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), tempFPR);
        auto bIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionBFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionB), destFPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, doubleOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyDoubleDestSameAsElseCase(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;
    double corruptedSelectionA = 55555;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        FPRReg destFPR = FPRInfo::returnValueFPR;
        FPRReg selectionAFPR = FPRInfo::fpRegT1;
        FPRReg selectionBFPR = destFPR;
        FPRReg arg1FPR = FPRInfo::fpRegT3;
        FPRReg arg2FPR = FPRInfo::fpRegT4;

        RELEASE_ASSERT(destFPR != selectionAFPR);
        RELEASE_ASSERT(destFPR == selectionBFPR);
        RELEASE_ASSERT(destFPR != arg1FPR);
        RELEASE_ASSERT(destFPR != arg2FPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), arg1FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), arg2FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), selectionAFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), selectionBFPR);
        jit.moveDoubleConditionallyDouble(condition, arg1FPR, arg2FPR, selectionAFPR, selectionBFPR, destFPR);

        FPRReg tempFPR = FPRInfo::fpRegT5;
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), tempFPR);
        auto aIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionAFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionA), destFPR);
        aIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, doubleOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyFloat(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;
    double corruptedSelectionA = 55555;
    double corruptedSelectionB = 66666;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        FPRReg destFPR = FPRInfo::returnValueFPR;
        FPRReg selectionAFPR = FPRInfo::fpRegT1;
        FPRReg selectionBFPR = FPRInfo::fpRegT2;
        FPRReg arg1FPR = FPRInfo::fpRegT3;
        FPRReg arg2FPR = FPRInfo::fpRegT4;

        RELEASE_ASSERT(destFPR != selectionAFPR);
        RELEASE_ASSERT(destFPR != selectionBFPR);
        RELEASE_ASSERT(destFPR != arg1FPR);
        RELEASE_ASSERT(destFPR != arg2FPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), arg1FPR);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), arg2FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), selectionAFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), selectionBFPR);
        jit.moveDoubleConditionallyFloat(condition, arg1FPR, arg2FPR, selectionAFPR, selectionBFPR, destFPR);

        FPRReg tempFPR = FPRInfo::fpRegT5;
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), tempFPR);
        auto aIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionAFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionA), destFPR);
        aIsUnchanged.link(&jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), tempFPR);
        auto bIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionBFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionB), destFPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, floatOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyFloatDestSameAsThenCase(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;
    double corruptedSelectionB = 66666;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        FPRReg destFPR = FPRInfo::returnValueFPR;
        FPRReg selectionAFPR = destFPR;
        FPRReg selectionBFPR = FPRInfo::fpRegT2;
        FPRReg arg1FPR = FPRInfo::fpRegT3;
        FPRReg arg2FPR = FPRInfo::fpRegT4;

        RELEASE_ASSERT(destFPR == selectionAFPR);
        RELEASE_ASSERT(destFPR != selectionBFPR);
        RELEASE_ASSERT(destFPR != arg1FPR);
        RELEASE_ASSERT(destFPR != arg2FPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), arg1FPR);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), arg2FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), selectionAFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), selectionBFPR);
        jit.moveDoubleConditionallyFloat(condition, arg1FPR, arg2FPR, selectionAFPR, selectionBFPR, destFPR);

        FPRReg tempFPR = FPRInfo::fpRegT5;
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), tempFPR);
        auto bIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionBFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionB), destFPR);
        bIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, floatOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyFloatDestSameAsElseCase(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    float arg2 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;
    double corruptedSelectionA = 55555;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        FPRReg destFPR = FPRInfo::returnValueFPR;
        FPRReg selectionAFPR = FPRInfo::fpRegT1;
        FPRReg selectionBFPR = destFPR;
        FPRReg arg1FPR = FPRInfo::fpRegT3;
        FPRReg arg2FPR = FPRInfo::fpRegT4;

        RELEASE_ASSERT(destFPR != selectionAFPR);
        RELEASE_ASSERT(destFPR == selectionBFPR);
        RELEASE_ASSERT(destFPR != arg1FPR);
        RELEASE_ASSERT(destFPR != arg2FPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), arg1FPR);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), arg2FPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), selectionAFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), selectionBFPR);
        jit.moveDoubleConditionallyFloat(condition, arg1FPR, arg2FPR, selectionAFPR, selectionBFPR, destFPR);

        FPRReg tempFPR = FPRInfo::fpRegT5;
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), tempFPR);
        auto aIsUnchanged = jit.branchDouble(CCallHelpers::DoubleEqualAndOrdered, selectionAFPR, tempFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&corruptedSelectionA), destFPR);
        aIsUnchanged.link(&jit);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPoint(condition, testCode, arg1, arg2, floatOperands(), selectionA, selectionB);
}

template<typename T, typename SelectionType>
void testMoveConditionallyFloatingPointSameArg(MacroAssembler::DoubleCondition condition, const MacroAssemblerCodeRef<JSEntryPtrTag>& testCode, T& arg1, const Vector<T> operands, SelectionType selectionA, SelectionType selectionB)
{
    auto expectedResult = [&, condition] (T a) -> SelectionType {
        auto isUnordered = [] (double x) {
            return x != x;
        };
        switch (condition) {
        case MacroAssembler::DoubleEqualAndOrdered:
            return !isUnordered(a) && (a == a) ? selectionA : selectionB;
        case MacroAssembler::DoubleNotEqualAndOrdered:
            return !isUnordered(a) && (a != a) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanAndOrdered:
            return !isUnordered(a) && (a > a) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanOrEqualAndOrdered:
            return !isUnordered(a) && (a >= a) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanAndOrdered:
            return !isUnordered(a) && (a < a) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanOrEqualAndOrdered:
            return !isUnordered(a) && (a <= a) ? selectionA : selectionB;
        case MacroAssembler::DoubleEqualOrUnordered:
            return isUnordered(a) || (a == a) ? selectionA : selectionB;
        case MacroAssembler::DoubleNotEqualOrUnordered:
            return isUnordered(a) || (a != a) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanOrUnordered:
            return isUnordered(a) || (a > a) ? selectionA : selectionB;
        case MacroAssembler::DoubleGreaterThanOrEqualOrUnordered:
            return isUnordered(a) || (a >= a) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanOrUnordered:
            return isUnordered(a) || (a < a) ? selectionA : selectionB;
        case MacroAssembler::DoubleLessThanOrEqualOrUnordered:
            return isUnordered(a) || (a <= a) ? selectionA : selectionB;
        } // switch
        RELEASE_ASSERT_NOT_REACHED();
    };

    for (auto a : operands) {
        arg1 = a;
        CHECK_EQ(invoke<SelectionType>(testCode), expectedResult(a));
    }
}

void testMoveConditionallyDouble2SameArg(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        RELEASE_ASSERT(GPRInfo::returnValueGPR != selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), GPRInfo::returnValueGPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.moveConditionallyDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0, selectionAGPR, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPointSameArg(condition, testCode, arg1, doubleOperands(), selectionA, selectionB);
}

void testMoveConditionallyDouble3SameArg(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        GPRReg selectionBGPR = GPRInfo::argumentGPR3;
        RELEASE_ASSERT(GPRInfo::returnValueGPR != selectionAGPR);
        RELEASE_ASSERT(GPRInfo::returnValueGPR != selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(-1), GPRInfo::returnValueGPR);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.moveConditionallyDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0, selectionAGPR, selectionBGPR, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPointSameArg(condition, testCode, arg1, doubleOperands(), selectionA, selectionB);
}

void testMoveConditionallyFloat2SameArg(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        RELEASE_ASSERT(GPRInfo::returnValueGPR != selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), GPRInfo::returnValueGPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.moveConditionallyFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0, selectionAGPR, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPointSameArg(condition, testCode, arg1, floatOperands(), selectionA, selectionB);
}

void testMoveConditionallyFloat3SameArg(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    unsigned selectionA = 42;
    unsigned selectionB = 17;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        GPRReg selectionAGPR = GPRInfo::argumentGPR2;
        GPRReg selectionBGPR = GPRInfo::argumentGPR3;
        RELEASE_ASSERT(GPRInfo::returnValueGPR != selectionAGPR);
        RELEASE_ASSERT(GPRInfo::returnValueGPR != selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionA), selectionAGPR);
        jit.move(CCallHelpers::TrustedImm32(selectionB), selectionBGPR);
        jit.move(CCallHelpers::TrustedImm32(-1), GPRInfo::returnValueGPR);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.moveConditionallyFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0, selectionAGPR, selectionBGPR, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPointSameArg(condition, testCode, arg1, floatOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyDoubleSameArg(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), FPRInfo::fpRegT2);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), FPRInfo::fpRegT3);
        jit.moveDoubleConditionallyDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0, FPRInfo::fpRegT2, FPRInfo::fpRegT3, FPRInfo::returnValueFPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPointSameArg(condition, testCode, arg1, doubleOperands(), selectionA, selectionB);
}

void testMoveDoubleConditionallyFloatSameArg(MacroAssembler::DoubleCondition condition)
{
    float arg1 = 0;
    double selectionA = 42.0;
    double selectionB = 17.0;

    auto testCode = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionA), FPRInfo::fpRegT2);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&selectionB), FPRInfo::fpRegT3);
        jit.moveDoubleConditionallyFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0, FPRInfo::fpRegT2, FPRInfo::fpRegT3, FPRInfo::returnValueFPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    testMoveConditionallyFloatingPointSameArg(condition, testCode, arg1, floatOperands(), selectionA, selectionB);
}

void testSignExtend8To64()
{
    auto code = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.signExtend8To64(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto a : int8Operands()) {
        int64_t expectedResult = static_cast<int64_t>(a);
        CHECK_EQ(invoke<int64_t>(code, a), expectedResult);
    }
}

void testSignExtend16To64()
{
    auto code = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.signExtend16To64(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto a : int16Operands()) {
        int64_t expectedResult = static_cast<int64_t>(a);
        CHECK_EQ(invoke<int64_t>(code, a), expectedResult);
    }
}

#endif // CPU(X86_64) || CPU(ARM64) || CPU(RISCV64)

#if CPU(ARM64)

void testAtomicStrongCASFill8()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.atomicStrongCAS8(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR2));
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    uint8_t data[] = {
        0xff, 0xff,
    };
    uint32_t result = invoke<uint32_t>(test, 0xffffffffffffffffULL, 0, data);
    CHECK_EQ(result, 0xff);
    CHECK_EQ(data[0], 0);
    CHECK_EQ(data[1], 0xff);
}

void testAtomicStrongCASFill16()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.atomicStrongCAS16(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR2));
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    uint16_t data[] = {
        0xffff, 0xffff,
    };
    uint32_t result = invoke<uint32_t>(test, 0xffffffffffffffffULL, 0, data);
    CHECK_EQ(result, 0xffff);
    CHECK_EQ(data[0], 0);
    CHECK_EQ(data[1], 0xffff);
}

#endif // CPU(ARM64)

void testLoadStorePair32()
{
    constexpr uint32_t initialValue = 0x55aabb80u;
    constexpr uint32_t value1 = 42;
    constexpr uint32_t value2 = 0xcfbb1357u;

    uint32_t buffer[10];

    auto initBuffer = [&] {
        for (unsigned i = 0; i < 10; ++i)
            buffer[i] = initialValue + i;
    };

    struct Pair {
        uint32_t value1;
        uint32_t value2;
    };

    Pair pair;
    auto initPair = [&] {
        pair = { 0, 0 };
    };

    // Test loadPair32.
    auto testLoadPair = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR0;
        constexpr GPRReg pairGPR = GPRInfo::argumentGPR1;
        jit.loadPair32(bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(uint32_t)), GPRInfo::regT2, GPRInfo::regT3);

        jit.store32(GPRInfo::regT2, CCallHelpers::Address(pairGPR, 0));
        jit.store32(GPRInfo::regT3, CCallHelpers::Address(pairGPR, sizeof(uint32_t)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testLoadPair0 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, 0);
    });

    initBuffer();

    initPair();
    invoke<void>(testLoadPair0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4);
    CHECK_EQ(pair.value2, initialValue + 5);

    initPair();
    buffer[4] = value1;
    buffer[5] = value2;
    invoke<void>(testLoadPair0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value1);
    CHECK_EQ(pair.value2, value2);

    auto testLoadPairMinus2 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, -2);
    });

    initPair();
    invoke<void>(testLoadPairMinus2, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4 - 2);
    CHECK_EQ(pair.value2, initialValue + 5 - 2);

    initPair();
    buffer[4 - 2] = value2;
    buffer[5 - 2] = value1;
    invoke<void>(testLoadPairMinus2, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value2);
    CHECK_EQ(pair.value2, value1);

    auto testLoadPairPlus3 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, 3);
    });

    initPair();
    invoke<void>(testLoadPairPlus3, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4 + 3);
    CHECK_EQ(pair.value2, initialValue + 5 + 3);

    initPair();
    buffer[4 + 3] = value1;
    buffer[5 + 3] = value2;
    invoke<void>(testLoadPairPlus3, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value1);
    CHECK_EQ(pair.value2, value2);

    // Test loadPair32 using a buffer register as a destination.
    auto testLoadPairUsingBufferRegisterAsDestination = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR0;
        constexpr GPRReg pairGPR = GPRInfo::argumentGPR1;
        jit.loadPair32(bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(uint32_t)), GPRInfo::argumentGPR0, GPRInfo::regT2);

        jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(pairGPR, 0));
        jit.store32(GPRInfo::regT2, CCallHelpers::Address(pairGPR, sizeof(uint32_t)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testLoadPairUsingBufferRegisterAsDestination0 = compile([&] (CCallHelpers& jit) {
        testLoadPairUsingBufferRegisterAsDestination(jit, 0);
    });

    initBuffer();

    initPair();
    invoke<void>(testLoadPairUsingBufferRegisterAsDestination0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4);
    CHECK_EQ(pair.value2, initialValue + 5);

    // Test storePair32.
    auto testStorePair = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR2;
        jit.storePair32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(unsigned)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testStorePair0 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, 0);
    });

    initBuffer();
    invoke<void>(testStorePair0, value1, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], value1);
    CHECK_EQ(buffer[5], value2);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairMinus2 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, -2);
    });

    initBuffer();
    invoke<void>(testStorePairMinus2, value1, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], value1);
    CHECK_EQ(buffer[3], value2);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairPlus3 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, 3);
    });

    initBuffer();
    invoke<void>(testStorePairPlus3, value1, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], value1);
    CHECK_EQ(buffer[8], value2);
    CHECK_EQ(buffer[9], initialValue + 9);

    // Test storePair32 from 1 register.
    auto testStorePairFromOneReg = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR1;
        jit.storePair32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR0, bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(unsigned)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testStorePairFromOneReg0 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, 0);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneReg0, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], value2);
    CHECK_EQ(buffer[5], value2);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairFromOneRegMinus2 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, -2);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneRegMinus2, value1, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], value1);
    CHECK_EQ(buffer[3], value1);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairFromOneRegPlus3 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, 3);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneRegPlus3, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], value2);
    CHECK_EQ(buffer[8], value2);
    CHECK_EQ(buffer[9], initialValue + 9);
}

void testSub32ArgImm()
{
    for (auto immediate : int32Operands()) {
        auto sub = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.sub32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<uint32_t>(sub, value), static_cast<uint32_t>(value - immediate));
    }
}

#if CPU(ARM_THUMB2)
void testSub32ImmArg()
{
    for (auto immediate : { 0, 1, 32, 0xff, 0x100, 0x101, 0x1ff, 0x200, 0x3ff, 0x400, 0x555,
        0xabc, 0xfff, 0x1000, -1, -0x101, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min() }) {
        auto sub = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.sub32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<uint32_t>(sub, value), static_cast<uint32_t>(immediate - value));
    }
}
#endif

static Vector<int32_t> aluImmediates()
{
    return Vector<int32_t> {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 0x7f, 0xfe, 0xff, 0x100, 0x101, 0x1fc, 0x1fd, 0x1ff,
        0x200, 0x3fc, 0x3fd, 0x3ff, 0x400, 0x555, 0xabc, 0xffc, 0xfff, 0x1000, 0x1001,
        0x10001, 0xff00, 0x00ff00ff, static_cast<int32_t>(0xff00ff00),
        -1, -2, -6, -0x101, -0x1000,
        std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min()
    };
}

static Vector<uint32_t> aluValues()
{
    return Vector<uint32_t> {
        0u, 1u, 2u, 7u, 0xffu, 0x100u, 0xfffu, 0x1000u, 0x12345678u,
        0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu
    };
}

void testALUAdd32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto aliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.add32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto distinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.add32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            uint32_t expected = value + static_cast<uint32_t>(immediate);
            CHECK_EQ(invoke<uint32_t>(aliased, value), expected);
            CHECK_EQ(invoke<uint32_t>(distinct, value), expected);
        }
    }
}

void testALUSub32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto aliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.sub32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto distinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.sub32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto reversed = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.sub32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto reversedAliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.sub32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            uint32_t expected = value - static_cast<uint32_t>(immediate);
            uint32_t expectedReversed = static_cast<uint32_t>(immediate) - value;
            CHECK_EQ(invoke<uint32_t>(aliased, value), expected);
            CHECK_EQ(invoke<uint32_t>(distinct, value), expected);
            CHECK_EQ(invoke<uint32_t>(reversed, value), expectedReversed);
            CHECK_EQ(invoke<uint32_t>(reversedAliased, value), expectedReversed);
        }
    }
}

void testALUAnd32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto aliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.and32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto distinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.and32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            uint32_t expected = value & static_cast<uint32_t>(immediate);
            CHECK_EQ(invoke<uint32_t>(aliased, value), expected);
            CHECK_EQ(invoke<uint32_t>(distinct, value), expected);
        }
    }
}

void testALUOr32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto aliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.or32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto distinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.or32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            uint32_t expected = value | static_cast<uint32_t>(immediate);
            CHECK_EQ(invoke<uint32_t>(aliased, value), expected);
            CHECK_EQ(invoke<uint32_t>(distinct, value), expected);
        }
    }
}

void testALUXor32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto aliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.xor32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto distinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.xor32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            uint32_t expected = value ^ static_cast<uint32_t>(immediate);
            CHECK_EQ(invoke<uint32_t>(aliased, value), expected);
            CHECK_EQ(invoke<uint32_t>(distinct, value), expected);
        }
    }
}

void testALURegisterAliasing()
{
    auto addSame = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.add32(GPRInfo::returnValueGPR, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto addDestIsLeft = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
        jit.add32(GPRInfo::returnValueGPR, GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto addDestIsRight = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
        jit.add32(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto subSame = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.sub32(GPRInfo::returnValueGPR, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto subDestIsRight = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
        jit.sub32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR2, GPRInfo::argumentGPR2);
        jit.move(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto andSame = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.and32(GPRInfo::returnValueGPR, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto xorSame = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.xor32(GPRInfo::returnValueGPR, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto orDestIsRight = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
        jit.or32(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto left : aluValues()) {
        CHECK_EQ(invoke<uint32_t>(addSame, left), static_cast<uint32_t>(left + left));
        CHECK_EQ(invoke<uint32_t>(subSame, left), 0u);
        CHECK_EQ(invoke<uint32_t>(andSame, left), left);
        CHECK_EQ(invoke<uint32_t>(xorSame, left), 0u);
        for (auto right : aluValues()) {
            CHECK_EQ(invoke<uint32_t>(addDestIsLeft, left, right), static_cast<uint32_t>(left + right));
            CHECK_EQ(invoke<uint32_t>(addDestIsRight, left, right), static_cast<uint32_t>(left + right));
            CHECK_EQ(invoke<uint32_t>(subDestIsRight, left, right), static_cast<uint32_t>(left - right));
            CHECK_EQ(invoke<uint32_t>(orDestIsRight, left, right), left | right);
        }
    }
}

void testALUNeg32AndNot32()
{
    auto negAliased = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.neg32(GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto notAliased = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.not32(GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto notDistinct = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
        jit.not32(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto xorMinusOne = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.xor32(CCallHelpers::TrustedImm32(-1), GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : aluValues()) {
        CHECK_EQ(invoke<uint32_t>(negAliased, value), static_cast<uint32_t>(0u - value));
        CHECK_EQ(invoke<uint32_t>(notAliased, value), ~value);
        CHECK_EQ(invoke<uint32_t>(notDistinct, value), ~value);
        CHECK_EQ(invoke<uint32_t>(xorMinusOne, value), ~value);
    }
}

void testALUMul32()
{
    auto mulAliased = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.mul32(GPRInfo::returnValueGPR, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto mulDestIsLeft = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
        jit.mul32(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto mulDestIsRight = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.move(GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
        jit.mul32(GPRInfo::returnValueGPR, GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto left : aluValues()) {
        CHECK_EQ(invoke<uint32_t>(mulAliased, left), static_cast<uint32_t>(left * left));
        for (auto right : aluValues()) {
            CHECK_EQ(invoke<uint32_t>(mulDestIsLeft, left, right), static_cast<uint32_t>(left * right));
            CHECK_EQ(invoke<uint32_t>(mulDestIsRight, left, right), static_cast<uint32_t>(left * right));
        }
    }
}

void testALUMul32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.mul32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues())
            CHECK_EQ(invoke<uint32_t>(test, value), static_cast<uint32_t>(value * static_cast<uint32_t>(immediate)));
    }
}

void testALUCountLeadingZeros32()
{
    auto aliased = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.countLeadingZeros32(GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto distinct = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
        jit.countLeadingZeros32(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : aluValues()) {
        uint32_t expected = 32;
        for (uint32_t bit = 0; bit < 32; ++bit) {
            if (value & (1u << (31 - bit))) {
                expected = bit;
                break;
            }
        }
        CHECK_EQ(invoke<uint32_t>(aliased, value), expected);
        CHECK_EQ(invoke<uint32_t>(distinct, value), expected);
    }
}

void testALUShift32Boundaries()
{
    for (int32_t shiftAmount : { 0, 1, 2, 15, 16, 30, 31 }) {
        auto lshiftAliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.lshift32(CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto lshiftDistinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.lshift32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rshiftAliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.rshift32(CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rshiftDistinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.rshift32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto urshiftAliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.urshift32(CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto urshiftDistinct = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.urshift32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto lshiftByRegister = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR2);
            jit.move(CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::argumentGPR1);
            jit.lshift32(GPRInfo::argumentGPR2, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rshiftByRegister = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR2);
            jit.move(CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::argumentGPR1);
            jit.rshift32(GPRInfo::argumentGPR2, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto urshiftByRegister = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR2);
            jit.move(CCallHelpers::TrustedImm32(shiftAmount), GPRInfo::argumentGPR1);
            jit.urshift32(GPRInfo::argumentGPR2, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            uint32_t expectedLeft = shiftAmount ? (value << shiftAmount) : value;
            uint32_t expectedLogicalRight = shiftAmount ? (value >> shiftAmount) : value;
            uint32_t expectedArithmeticRight = shiftAmount
                ? static_cast<uint32_t>(static_cast<int32_t>(value) >> shiftAmount)
                : value;

            CHECK_EQ(invoke<uint32_t>(lshiftAliased, value), expectedLeft);
            CHECK_EQ(invoke<uint32_t>(lshiftDistinct, value), expectedLeft);
            CHECK_EQ(invoke<uint32_t>(lshiftByRegister, value), expectedLeft);
            CHECK_EQ(invoke<uint32_t>(rshiftAliased, value), expectedArithmeticRight);
            CHECK_EQ(invoke<uint32_t>(rshiftDistinct, value), expectedArithmeticRight);
            CHECK_EQ(invoke<uint32_t>(rshiftByRegister, value), expectedArithmeticRight);
            CHECK_EQ(invoke<uint32_t>(urshiftAliased, value), expectedLogicalRight);
            CHECK_EQ(invoke<uint32_t>(urshiftDistinct, value), expectedLogicalRight);
            CHECK_EQ(invoke<uint32_t>(urshiftByRegister, value), expectedLogicalRight);
        }
    }
}

void testALUBranchAdd32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            auto overflow = jit.branchAdd32(CCallHelpers::Overflow, GPRInfo::argumentGPR1,
                CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            overflow.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            int64_t wide = static_cast<int64_t>(static_cast<int32_t>(value)) + static_cast<int64_t>(immediate);
            uint32_t expected = (wide < INT32_MIN || wide > INT32_MAX) ? 1 : 0;
            CHECK_EQ(invoke<uint32_t>(test, value), expected);
        }
    }
}

void testALUBranchSub32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            auto overflow = jit.branchSub32(CCallHelpers::Overflow, GPRInfo::argumentGPR1,
                CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            overflow.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto result = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            auto overflow = jit.branchSub32(CCallHelpers::Overflow, GPRInfo::argumentGPR1,
                CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            overflow.link(&jit);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            int64_t wide = static_cast<int64_t>(static_cast<int32_t>(value)) - static_cast<int64_t>(immediate);
            uint32_t expected = (wide < INT32_MIN || wide > INT32_MAX) ? 1 : 0;
            CHECK_EQ(invoke<uint32_t>(test, value), expected);
            CHECK_EQ(invoke<uint32_t>(result, value), value - static_cast<uint32_t>(immediate));
        }
    }
}

void testALUBranchNeg32()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        auto overflow = jit.branchNeg32(CCallHelpers::Overflow, GPRInfo::returnValueGPR);
        overflow.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : aluValues())
        CHECK_EQ(invoke<uint32_t>(test, value), static_cast<uint32_t>(0u - value));
}

void testALUBranchMul32Overflow()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR2);
        auto overflow = jit.branchMul32(CCallHelpers::Overflow, GPRInfo::argumentGPR1,
            GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        auto done = jit.jump();
        overflow.link(&jit);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        done.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto left : aluValues()) {
        for (auto right : aluValues()) {
            int64_t wide = static_cast<int64_t>(static_cast<int32_t>(left)) * static_cast<int64_t>(static_cast<int32_t>(right));
            uint32_t expected = (wide < INT32_MIN || wide > INT32_MAX) ? 1 : 0;
            CHECK_EQ(invoke<uint32_t>(test, left, right), expected);
        }
    }
}

void testALUCompare32Immediate()
{
    for (auto immediate : aluImmediates()) {
        auto equal = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare32(CCallHelpers::Equal, GPRInfo::argumentGPR0,
                CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto below = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare32(CCallHelpers::Below, GPRInfo::argumentGPR0,
                CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto lessThan = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare32(CCallHelpers::LessThan, GPRInfo::argumentGPR0,
                CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testNonZero = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.test32(CCallHelpers::NonZero, GPRInfo::argumentGPR0,
                CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : aluValues()) {
            CHECK_EQ(invoke<uint32_t>(equal, value), static_cast<uint32_t>(value == static_cast<uint32_t>(immediate)));
            CHECK_EQ(invoke<uint32_t>(below, value), static_cast<uint32_t>(value < static_cast<uint32_t>(immediate)));
            CHECK_EQ(invoke<uint32_t>(lessThan, value), static_cast<uint32_t>(static_cast<int32_t>(value) < immediate));
            CHECK_EQ(invoke<uint32_t>(testNonZero, value), static_cast<uint32_t>(!!(value & static_cast<uint32_t>(immediate))));
        }
    }
}

#if CPU(ARM_THUMB2)
void testALUARMv7Add32ToStackPointerImmediate()
{
    for (int32_t immediate : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xfd, 0xff, 0x100, 0x1fc, 0x1fd,
        0x1ff, 0x200, 0x3fc, 0x3fd, 0x3ff, 0x400, 0xffd, 0xfff, -1, -6, -0x400 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(MacroAssembler::stackPointerRegister, GPRInfo::argumentGPR1);
            jit.add32(CCallHelpers::TrustedImm32(immediate), MacroAssembler::stackPointerRegister);
            jit.move(MacroAssembler::stackPointerRegister, GPRInfo::returnValueGPR);
            jit.move(GPRInfo::argumentGPR1, MacroAssembler::stackPointerRegister);
            jit.sub32(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        CHECK_EQ(invoke<int32_t>(test), immediate);
    }
}

void testALUARMv7Sub32FromStackPointerImmediate()
{
    for (int32_t immediate : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xfd, 0xff, 0x100, 0x1fc, 0x1fd,
        0x1ff, 0x200, 0x3fc, 0x3fd, 0x3ff, 0x400, 0xffd, 0xfff, -1, -6, -0x400 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(MacroAssembler::stackPointerRegister, GPRInfo::argumentGPR1);
            jit.sub32(CCallHelpers::TrustedImm32(immediate), MacroAssembler::stackPointerRegister);
            jit.move(MacroAssembler::stackPointerRegister, GPRInfo::returnValueGPR);
            jit.move(GPRInfo::argumentGPR1, MacroAssembler::stackPointerRegister);
            jit.sub32(GPRInfo::returnValueGPR, GPRInfo::argumentGPR1);
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        CHECK_EQ(invoke<int32_t>(test), immediate);
    }
}

void testALUARMv7Add32FromStackPointerImmediate()
{
    for (int32_t immediate : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xfd, 0xff, 0x100, 0x1fc, 0x1fd,
        0x1ff, 0x200, 0x3fc, 0x3fd, 0x3ff, 0x400, 0xffd, 0xfff, -1, -6, -0x400 }) {
        auto lowDestination = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(MacroAssembler::stackPointerRegister, GPRInfo::argumentGPR1);
            jit.add32(CCallHelpers::TrustedImm32(immediate), MacroAssembler::stackPointerRegister, GPRInfo::returnValueGPR);
            jit.sub32(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto highDestination = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.push(ARMRegisters::r8);
            jit.push(ARMRegisters::r10);
            jit.move(MacroAssembler::stackPointerRegister, GPRInfo::argumentGPR1);
            jit.add32(CCallHelpers::TrustedImm32(immediate), MacroAssembler::stackPointerRegister, ARMRegisters::r8);
            jit.move(ARMRegisters::r8, GPRInfo::returnValueGPR);
            jit.sub32(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            jit.pop(ARMRegisters::r10);
            jit.pop(ARMRegisters::r8);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        CHECK_EQ(invoke<int32_t>(lowDestination), immediate);
        CHECK_EQ(invoke<int32_t>(highDestination), immediate);
    }
}

void testALUARMv7HighRegisterOperands()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.push(ARMRegisters::r8);
        jit.push(ARMRegisters::r10);
        jit.move(GPRInfo::argumentGPR0, ARMRegisters::r8);
        jit.move(GPRInfo::argumentGPR1, ARMRegisters::r10);
        jit.add32(ARMRegisters::r10, ARMRegisters::r8, ARMRegisters::r8);
        jit.sub32(ARMRegisters::r10, ARMRegisters::r8, ARMRegisters::r8);
        jit.and32(ARMRegisters::r10, ARMRegisters::r8, ARMRegisters::r10);
        jit.or32(ARMRegisters::r8, ARMRegisters::r10, ARMRegisters::r10);
        jit.xor32(ARMRegisters::r8, ARMRegisters::r10, ARMRegisters::r10);
        jit.add32(ARMRegisters::r10, ARMRegisters::r8, GPRInfo::returnValueGPR);
        jit.pop(ARMRegisters::r10);
        jit.pop(ARMRegisters::r8);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto left : aluValues()) {
        for (auto right : aluValues()) {
            uint32_t r8 = left;
            uint32_t r10 = right;
            r8 = r10 + r8;
            r8 = r10 - r8;
            r10 = r10 & r8;
            r10 = r8 | r10;
            r10 = r8 ^ r10;
            CHECK_EQ(invoke<uint32_t>(test, left, right), static_cast<uint32_t>(r10 + r8));
        }
    }
}

void testALUARMv7RotateRight32MultipleOf32()
{
    for (int32_t rotation : { 0, 1, 31, 32, 33, 63, 64, -32, -64 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.rotateRight32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(rotation), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto aliased = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.rotateRight32(CCallHelpers::TrustedImm32(rotation), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        uint32_t amount = static_cast<uint32_t>(rotation) & 0x1f;
        for (auto value : aluValues()) {
            uint32_t expected = amount ? ((value >> amount) | (value << (32 - amount))) : value;
            CHECK_EQ(invoke<uint32_t>(test, value), expected);
            CHECK_EQ(invoke<uint32_t>(aliased, value), expected);
        }
    }
}

void testALUARMv7RotateLeft32()
{
    for (int32_t rotation : { 0, 1, 31, 32, 33, -32 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.rotateLeft32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(rotation), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        uint32_t amount = static_cast<uint32_t>(rotation) & 0x1f;
        for (auto value : aluValues()) {
            uint32_t expected = amount ? ((value << amount) | (value >> (32 - amount))) : value;
            CHECK_EQ(invoke<uint32_t>(test, value), expected);
        }
    }
}

void testALUARMv7TruncateDoubleToInt64Negative()
{
    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.push(ARMRegisters::r8);
        jit.push(ARMRegisters::r10);
        jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0), FPRInfo::fpRegT0);
        jit.truncateDoubleToInt64(FPRInfo::fpRegT0, ARMRegisters::r8, ARMRegisters::r10, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        jit.move(ARMRegisters::r8, GPRInfo::returnValueGPR);
        jit.move(ARMRegisters::r10, GPRInfo::returnValueGPR2);
        jit.pop(ARMRegisters::r10);
        jit.pop(ARMRegisters::r8);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (double value : { -1.0, -2.5, -1000.0, -4294967296.0, -4294967297.0, 0.0, 1.0, 1000.0 }) {
        double storage = value;
        int64_t expected = static_cast<int64_t>(value);
        CHECK_EQ(invoke<int64_t>(test, &storage), expected);
    }
}
#endif

#if CPU(ARM_THUMB2)

static MacroAssemblerCodeRef<JSEntryPtrTag> armv7CompileBranchOverPadding(unsigned paddingBytes, bool conditional)
{
    return compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        CCallHelpers::Jump taken;
        if (conditional)
            taken = jit.branch32(CCallHelpers::Equal, GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(0));
        else
            taken = jit.jump();
        jit.move(CCallHelpers::TrustedImm32(2), GPRInfo::returnValueGPR);
        for (unsigned i = 0; i < paddingBytes / 2; ++i)
            jit.nop();
        taken.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });
}

void testARMv7BranchRangeConditional()
{
    for (unsigned padding : { 0u, 2u, 4u, 244u, 248u, 250u, 252u, 254u, 256u, 258u, 260u, 262u, 512u,
        2040u, 2044u, 2046u, 2048u, 2050u, 2052u, 4096u, 8192u }) {
        auto code = armv7CompileBranchOverPadding(padding, true);
        CHECK_EQ(invoke<int>(code, 0, 0), 1);
        CHECK_EQ(invoke<int>(code, 0, 1), 2);
        CHECK_EQ(invoke<int>(code, 0, -1), 2);
    }
}

void testARMv7BranchRangeUnconditional()
{
    for (unsigned padding : { 0u, 2u, 4u, 244u, 250u, 254u, 256u, 258u, 262u, 512u,
        2038u, 2042u, 2044u, 2046u, 2048u, 2050u, 2052u, 2054u, 4096u, 8192u }) {
        auto code = armv7CompileBranchOverPadding(padding, false);
        CHECK_EQ(invoke<int>(code, 0, 0), 1);
        CHECK_EQ(invoke<int>(code, 0, 1), 1);
    }
}

void testARMv7BranchRangeConditionalLong()
{
    for (unsigned padding : { 1048572u, 1048576u, 1048580u }) {
        auto code = armv7CompileBranchOverPadding(padding, true);
        CHECK_EQ(invoke<int>(code, 0, 0), 1);
        CHECK_EQ(invoke<int>(code, 0, 1), 2);
    }
}

void testARMv7BranchRangeUnconditionalLong()
{
    for (unsigned padding : { 1048572u, 1048576u, 1048580u }) {
        auto code = armv7CompileBranchOverPadding(padding, false);
        CHECK_EQ(invoke<int>(code, 0, 0), 1);
    }
}

void testARMv7BranchRangeBackward()
{
    for (unsigned padding : { 0u, 2u, 244u, 250u, 254u, 256u, 258u, 262u, 2040u, 2046u, 2048u, 2052u, 4096u }) {
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto top = jit.label();
            jit.add32(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            for (unsigned i = 0; i < padding / 2; ++i)
                jit.nop();
            auto back = jit.branch32(CCallHelpers::LessThan, GPRInfo::returnValueGPR, CCallHelpers::TrustedImm32(5));
            back.linkTo(top, &jit);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        CHECK_EQ(invoke<int>(code), 5);
    }
}

void testARMv7BranchRangeManyJumpsToOneLabel()
{
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        CCallHelpers::JumpList done;
        for (unsigned i = 0; i < 64; ++i) {
            done.append(jit.branch32(CCallHelpers::Equal, GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(static_cast<int32_t>(i))));
            jit.add32(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            for (unsigned j = 0; j < 64; ++j)
                jit.nop();
        }
        done.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    for (int32_t i = 0; i < 64; ++i)
        CHECK_EQ(invoke<int>(code, 0, i), i);
    CHECK_EQ(invoke<int>(code, 0, 1000), 64);
}

void testARMv7LoadDoubleOffsets()
{
    double buffer[768];
    for (unsigned i = 0; i < 768; ++i)
        buffer[i] = static_cast<double>(i) + 0.5;

    for (int offset : { -2052, -2048, -1028, -1024, -1023, -1021, -1020, -1016, -8, -4, -3, -1,
        0, 1, 3, 4, 8, 1016, 1020, 1021, 1023, 1024, 1028, 2048, 2052 }) {
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), FPRInfo::fpRegT0);
            jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR1));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        char* base = reinterpret_cast<char*>(&buffer[384]) - offset;
        double out = 0;
        invoke<void>(code, base, &out);
        CHECK_EQ(out, buffer[384]);
    }
}

void testARMv7StoreDoubleOffsets()
{
    double buffer[768];
    double source = 1234.5;

    for (int offset : { -2052, -2048, -1028, -1024, -1023, -1021, -1020, -1016, -8, -4, -3, -1,
        0, 1, 3, 4, 8, 1016, 1020, 1021, 1023, 1024, 1028, 2048, 2052 }) {
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0), FPRInfo::fpRegT0);
            jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR1, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        buffer[384] = 0;
        char* base = reinterpret_cast<char*>(&buffer[384]) - offset;
        invoke<void>(code, &source, base);
        CHECK_EQ(buffer[384], source);
    }
}

void testARMv7LoadStoreFloatOffsets()
{
    float buffer[1536];
    for (unsigned i = 0; i < 1536; ++i)
        buffer[i] = static_cast<float>(i) + 0.5f;

    for (int offset : { -2052, -2048, -1024, -1023, -1021, -1020, -8, -4, -3, -1,
        0, 1, 3, 4, 8, 1016, 1020, 1021, 1023, 1024, 1028, 2048 }) {
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadFloat(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), FPRInfo::fpRegT0);
            jit.storeFloat(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR1, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        float outBuffer[1536];
        for (unsigned i = 0; i < 1536; ++i)
            outBuffer[i] = 0;
        char* inBase = reinterpret_cast<char*>(&buffer[768]) - offset;
        char* outBase = reinterpret_cast<char*>(&outBuffer[768]) - offset;
        invoke<void>(code, inBase, outBase);
        CHECK_EQ(outBuffer[768], buffer[768]);
    }
}

void testARMv7DoubleArithAliasing()
{
    MacroAssembler::FPRegisterID dstReg[5] = { FPRInfo::fpRegT2, FPRInfo::fpRegT0, FPRInfo::fpRegT1, FPRInfo::fpRegT0, FPRInfo::fpRegT2 };
    MacroAssembler::FPRegisterID lhsReg[5] = { FPRInfo::fpRegT0, FPRInfo::fpRegT0, FPRInfo::fpRegT0, FPRInfo::fpRegT0, FPRInfo::fpRegT0 };
    MacroAssembler::FPRegisterID rhsReg[5] = { FPRInfo::fpRegT1, FPRInfo::fpRegT1, FPRInfo::fpRegT1, FPRInfo::fpRegT0, FPRInfo::fpRegT0 };

    double io[3];
    for (unsigned op = 0; op < 4; ++op) {
        for (unsigned m = 0; m < 5; ++m) {
            MacroAssembler::FPRegisterID d = dstReg[m];
            MacroAssembler::FPRegisterID l = lhsReg[m];
            MacroAssembler::FPRegisterID r = rhsReg[m];
            auto code = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
                jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 8), FPRInfo::fpRegT1);
                switch (op) {
                case 0:
                    jit.addDouble(l, r, d);
                    break;
                case 1:
                    jit.subDouble(l, r, d);
                    break;
                case 2:
                    jit.mulDouble(l, r, d);
                    break;
                default:
                    jit.divDouble(l, r, d);
                    break;
                }
                jit.storeDouble(d, CCallHelpers::Address(GPRInfo::argumentGPR0, 16));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            for (double a : { 0.0, -0.0, 1.0, -1.0, 3.5, -7.25, 1e300, 1e-300 }) {
                for (double b : { 1.0, -1.0, 2.0, 0.5, -4.75, 1e300 }) {
                    io[0] = a;
                    io[1] = b;
                    io[2] = 99.0;
                    double lv = a;
                    double rv = (r == FPRInfo::fpRegT0) ? a : b;
                    double expected = 0;
                    switch (op) {
                    case 0:
                        expected = lv + rv;
                        break;
                    case 1:
                        expected = lv - rv;
                        break;
                    case 2:
                        expected = lv * rv;
                        break;
                    default:
                        expected = lv / rv;
                        break;
                    }
                    invoke<void>(code, io);
                    if (expected == expected)
                        CHECK_EQ(io[2], expected);
                    else
                        CHECK_EQ(static_cast<int>(io[2] == io[2]), 0);
                }
            }
        }
    }
}

void testARMv7FloatArithAliasing()
{
    MacroAssembler::FPRegisterID dstReg[5] = { FPRInfo::fpRegT2, FPRInfo::fpRegT0, FPRInfo::fpRegT1, FPRInfo::fpRegT0, FPRInfo::fpRegT2 };
    MacroAssembler::FPRegisterID rhsReg[5] = { FPRInfo::fpRegT1, FPRInfo::fpRegT1, FPRInfo::fpRegT1, FPRInfo::fpRegT0, FPRInfo::fpRegT0 };

    float io[3];
    for (unsigned op = 0; op < 4; ++op) {
        for (unsigned m = 0; m < 5; ++m) {
            MacroAssembler::FPRegisterID d = dstReg[m];
            MacroAssembler::FPRegisterID l = FPRInfo::fpRegT0;
            MacroAssembler::FPRegisterID r = rhsReg[m];
            auto code = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.loadFloat(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
                jit.loadFloat(CCallHelpers::Address(GPRInfo::argumentGPR0, 4), FPRInfo::fpRegT1);
                switch (op) {
                case 0:
                    jit.addFloat(l, r, d);
                    break;
                case 1:
                    jit.subFloat(l, r, d);
                    break;
                case 2:
                    jit.mulFloat(l, r, d);
                    break;
                default:
                    jit.divFloat(l, r, d);
                    break;
                }
                jit.storeFloat(d, CCallHelpers::Address(GPRInfo::argumentGPR0, 8));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            for (float a : { 0.0f, -0.0f, 1.0f, -1.0f, 3.5f, -7.25f, 1e30f }) {
                for (float b : { 1.0f, -1.0f, 2.0f, 0.5f, -4.75f }) {
                    io[0] = a;
                    io[1] = b;
                    io[2] = 99.0f;
                    float rv = (r == FPRInfo::fpRegT0) ? a : b;
                    float expected = 0;
                    switch (op) {
                    case 0:
                        expected = a + rv;
                        break;
                    case 1:
                        expected = a - rv;
                        break;
                    case 2:
                        expected = a * rv;
                        break;
                    default:
                        expected = a / rv;
                        break;
                    }
                    invoke<void>(code, io);
                    if (expected == expected)
                        CHECK_EQ(io[2], expected);
                    else
                        CHECK_EQ(static_cast<int>(io[2] == io[2]), 0);
                }
            }
        }
    }
}

void testARMv7AbsNegSqrtDouble()
{
    double io[2];
    for (unsigned op = 0; op < 3; ++op) {
        for (unsigned aliased = 0; aliased < 2; ++aliased) {
            MacroAssembler::FPRegisterID dest = aliased ? FPRInfo::fpRegT0 : FPRInfo::fpRegT1;
            auto code = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
                switch (op) {
                case 0:
                    jit.absDouble(FPRInfo::fpRegT0, dest);
                    break;
                case 1:
                    jit.negateDouble(FPRInfo::fpRegT0, dest);
                    break;
                default:
                    jit.sqrtDouble(FPRInfo::fpRegT0, dest);
                    break;
                }
                jit.storeDouble(dest, CCallHelpers::Address(GPRInfo::argumentGPR0, 8));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            for (double a : { 0.0, -0.0, 1.0, -1.0, 4.0, 2.25, -2.25, 1e300, -1e300, 1e-300 }) {
                io[0] = a;
                io[1] = 99.0;
                invoke<void>(code, io);
                if (!op)
                    CHECK_EQ(std::bit_cast<uint64_t>(io[1]), std::bit_cast<uint64_t>(a) & 0x7fffffffffffffffull);
                else if (op == 1)
                    CHECK_EQ(std::bit_cast<uint64_t>(io[1]), std::bit_cast<uint64_t>(a) ^ 0x8000000000000000ull);
                else if (a == 0.0 || a == 1.0 || a == 4.0 || a == 2.25)
                    CHECK_EQ(io[1] * io[1], a);
            }
        }
    }
}

void testARMv7AbsNegSqrtFloat()
{
    float io[2];
    for (unsigned op = 0; op < 3; ++op) {
        for (unsigned aliased = 0; aliased < 2; ++aliased) {
            MacroAssembler::FPRegisterID dest = aliased ? FPRInfo::fpRegT0 : FPRInfo::fpRegT1;
            auto code = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.loadFloat(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
                switch (op) {
                case 0:
                    jit.absFloat(FPRInfo::fpRegT0, dest);
                    break;
                case 1:
                    jit.negateFloat(FPRInfo::fpRegT0, dest);
                    break;
                default:
                    jit.sqrtFloat(FPRInfo::fpRegT0, dest);
                    break;
                }
                jit.storeFloat(dest, CCallHelpers::Address(GPRInfo::argumentGPR0, 4));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            for (float a : { 0.0f, -0.0f, 1.0f, -1.0f, 4.0f, 2.25f, -2.25f, 1e30f }) {
                io[0] = a;
                io[1] = 99.0f;
                invoke<void>(code, io);
                if (!op)
                    CHECK_EQ(std::bit_cast<uint32_t>(io[1]), std::bit_cast<uint32_t>(a) & 0x7fffffffu);
                else if (op == 1)
                    CHECK_EQ(std::bit_cast<uint32_t>(io[1]), std::bit_cast<uint32_t>(a) ^ 0x80000000u);
                else if (a == 0.0f || a == 1.0f || a == 4.0f || a == 2.25f)
                    CHECK_EQ(io[1] * io[1], a);
            }
        }
    }
}

void testARMv7MoveDoubleAliasing()
{
    double io[2];
    for (unsigned aliased = 0; aliased < 2; ++aliased) {
        MacroAssembler::FPRegisterID dest = aliased ? FPRInfo::fpRegT0 : FPRInfo::fpRegT3;
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
            jit.moveDouble(FPRInfo::fpRegT0, dest);
            jit.storeDouble(dest, CCallHelpers::Address(GPRInfo::argumentGPR0, 8));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        for (double a : { 0.0, -0.0, 1.0, -1.0, 1e300, -1e-300 }) {
            io[0] = a;
            io[1] = 99.0;
            invoke<void>(code, io);
            CHECK_EQ(std::bit_cast<uint64_t>(io[1]), std::bit_cast<uint64_t>(a));
        }
    }
}

void testARMv7ConvertInt32ToDouble()
{
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.convertInt32ToDouble(GPRInfo::argumentGPR1, FPRInfo::fpRegT0);
        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR0));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    double out = 0;
    for (int32_t value : { 0, 1, -1, 2, -2, 255, 256, 4095, 4096, 65535, 65536, 0x7ffffffe,
        std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min(), -65536 }) {
        out = 99;
        invoke<void>(code, &out, value);
        CHECK_EQ(out, static_cast<double>(value));
    }
}

void testARMv7ConvertUInt32ToDouble()
{
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.convertUInt32ToDouble(GPRInfo::argumentGPR1, FPRInfo::fpRegT0);
        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR0));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    double out = 0;
    for (uint32_t value : { 0u, 1u, 255u, 256u, 65535u, 65536u, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu }) {
        out = 99;
        invoke<void>(code, &out, value);
        CHECK_EQ(out, static_cast<double>(value));
    }
}

void testARMv7ConvertInt32ToFloat()
{
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.convertInt32ToFloat(GPRInfo::argumentGPR1, FPRInfo::fpRegT0);
        jit.storeFloat(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR0));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    float out = 0;
    for (int32_t value : { 0, 1, -1, 255, 256, 65535, 65536, 0x1000000,
        std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min() }) {
        out = 99;
        invoke<void>(code, &out, value);
        CHECK_EQ(out, static_cast<float>(value));
    }
}

void testARMv7ConvertUInt32ToFloat()
{
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.convertUInt32ToFloat(GPRInfo::argumentGPR1, FPRInfo::fpRegT0);
        jit.storeFloat(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR0));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    float out = 0;
    for (uint32_t value : { 0u, 1u, 255u, 65536u, 0x7fffffffu, 0x80000000u, 0xffffffffu }) {
        out = 99;
        invoke<void>(code, &out, value);
        CHECK_EQ(out, static_cast<float>(value));
    }
}

void testARMv7TruncateDoubleToInt32()
{
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0), FPRInfo::fpRegT0);
        jit.truncateDoubleToInt32(FPRInfo::fpRegT0, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    double in = 0;
    for (double value : { 0.0, -0.0, 1.0, -1.0, 1.9, -1.9, 42.5, -42.5, 2147483647.0, -2147483648.0,
        0.5, -0.5, 65536.25, -65536.25 }) {
        in = value;
        CHECK_EQ(invoke<int32_t>(code, &in), static_cast<int32_t>(value));
    }
}

void testARMv7TruncateDoubleToUint32()
{
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0), FPRInfo::fpRegT0);
        jit.truncateDoubleToUint32(FPRInfo::fpRegT0, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    double in = 0;
    for (double value : { 0.0, 1.0, 1.9, 42.5, 65536.25, 2147483647.0, 2147483648.0, 4294967295.0 }) {
        in = value;
        CHECK_EQ(invoke<uint32_t>(code, &in), static_cast<uint32_t>(value));
    }
}

void testARMv7ConvertDoubleFloatRoundTrip()
{
    double io[2];
    for (unsigned aliased = 0; aliased < 2; ++aliased) {
        MacroAssembler::FPRegisterID mid = aliased ? FPRInfo::fpRegT0 : FPRInfo::fpRegT1;
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
            jit.convertDoubleToFloat(FPRInfo::fpRegT0, mid);
            jit.convertFloatToDouble(mid, FPRInfo::fpRegT2);
            jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::Address(GPRInfo::argumentGPR0, 8));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        for (double a : { 0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 3.25, 65536.0, 1e30, -1e30 }) {
            io[0] = a;
            io[1] = 99.0;
            invoke<void>(code, io);
            CHECK_EQ(io[1], static_cast<double>(static_cast<float>(a)));
        }
    }
}

void testARMv7BranchDoubleConditions()
{
    MacroAssembler::DoubleCondition conds[12] = {
        MacroAssembler::DoubleEqualAndOrdered,
        MacroAssembler::DoubleNotEqualAndOrdered,
        MacroAssembler::DoubleGreaterThanAndOrdered,
        MacroAssembler::DoubleGreaterThanOrEqualAndOrdered,
        MacroAssembler::DoubleLessThanAndOrdered,
        MacroAssembler::DoubleLessThanOrEqualAndOrdered,
        MacroAssembler::DoubleEqualOrUnordered,
        MacroAssembler::DoubleNotEqualOrUnordered,
        MacroAssembler::DoubleGreaterThanOrUnordered,
        MacroAssembler::DoubleGreaterThanOrEqualOrUnordered,
        MacroAssembler::DoubleLessThanOrUnordered,
        MacroAssembler::DoubleLessThanOrEqualOrUnordered
    };

    double io[2];
    for (unsigned c = 0; c < 12; ++c) {
        MacroAssembler::DoubleCondition cond = conds[c];
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 8), FPRInfo::fpRegT1);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto taken = jit.branchDouble(cond, FPRInfo::fpRegT0, FPRInfo::fpRegT1);
            auto done = jit.jump();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        for (double a : { 0.0, -0.0, 1.0, -1.0, 42.0, std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
            for (double b : { 0.0, 1.0, -1.0, 42.0, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN() }) {
                io[0] = a;
                io[1] = b;
                bool unordered = !(a == a) || !(b == b);
                bool expected = false;
                switch (c) {
                case 0:
                    expected = a == b;
                    break;
                case 1:
                    expected = !unordered && a != b;
                    break;
                case 2:
                    expected = a > b;
                    break;
                case 3:
                    expected = a >= b;
                    break;
                case 4:
                    expected = a < b;
                    break;
                case 5:
                    expected = a <= b;
                    break;
                case 6:
                    expected = unordered || a == b;
                    break;
                case 7:
                    expected = unordered || a != b;
                    break;
                case 8:
                    expected = unordered || a > b;
                    break;
                case 9:
                    expected = unordered || a >= b;
                    break;
                case 10:
                    expected = unordered || a < b;
                    break;
                default:
                    expected = unordered || a <= b;
                    break;
                }
                CHECK_EQ(invoke<int>(code, io), expected ? 1 : 0);
            }
        }
    }
}

void testARMv7BranchDoubleWithZero()
{
    MacroAssembler::DoubleCondition conds[6] = {
        MacroAssembler::DoubleEqualAndOrdered,
        MacroAssembler::DoubleGreaterThanAndOrdered,
        MacroAssembler::DoubleGreaterThanOrEqualAndOrdered,
        MacroAssembler::DoubleLessThanAndOrdered,
        MacroAssembler::DoubleLessThanOrEqualAndOrdered,
        MacroAssembler::DoubleNotEqualOrUnordered
    };

    double in = 0;
    for (unsigned c = 0; c < 6; ++c) {
        MacroAssembler::DoubleCondition cond = conds[c];
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0), FPRInfo::fpRegT0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto taken = jit.branchDoubleWithZero(cond, FPRInfo::fpRegT0);
            auto done = jit.jump();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        for (double a : { 0.0, -0.0, 1.0, -1.0, std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
            in = a;
            bool unordered = !(a == a);
            bool expected = false;
            switch (c) {
            case 0:
                expected = a == 0;
                break;
            case 1:
                expected = a > 0;
                break;
            case 2:
                expected = a >= 0;
                break;
            case 3:
                expected = a < 0;
                break;
            case 4:
                expected = a <= 0;
                break;
            default:
                expected = unordered || a != 0;
                break;
            }
            CHECK_EQ(invoke<int>(code, &in), expected ? 1 : 0);
        }
    }
}

void testARMv7CompareDouble()
{
    MacroAssembler::DoubleCondition conds[12] = {
        MacroAssembler::DoubleEqualAndOrdered,
        MacroAssembler::DoubleNotEqualAndOrdered,
        MacroAssembler::DoubleGreaterThanAndOrdered,
        MacroAssembler::DoubleGreaterThanOrEqualAndOrdered,
        MacroAssembler::DoubleLessThanAndOrdered,
        MacroAssembler::DoubleLessThanOrEqualAndOrdered,
        MacroAssembler::DoubleEqualOrUnordered,
        MacroAssembler::DoubleNotEqualOrUnordered,
        MacroAssembler::DoubleGreaterThanOrUnordered,
        MacroAssembler::DoubleGreaterThanOrEqualOrUnordered,
        MacroAssembler::DoubleLessThanOrUnordered,
        MacroAssembler::DoubleLessThanOrEqualOrUnordered
    };

    double io[2];
    for (unsigned c = 0; c < 12; ++c) {
        MacroAssembler::DoubleCondition cond = conds[c];
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), FPRInfo::fpRegT0);
            jit.loadDouble(CCallHelpers::Address(GPRInfo::argumentGPR0, 8), FPRInfo::fpRegT1);
            jit.compareDouble(cond, FPRInfo::fpRegT0, FPRInfo::fpRegT1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        for (double a : { 0.0, 1.0, -1.0, 42.0, std::numeric_limits<double>::quiet_NaN() }) {
            for (double b : { 0.0, 1.0, -1.0, 42.0, std::numeric_limits<double>::quiet_NaN() }) {
                io[0] = a;
                io[1] = b;
                bool unordered = !(a == a) || !(b == b);
                bool expected = false;
                switch (c) {
                case 0:
                    expected = a == b;
                    break;
                case 1:
                    expected = !unordered && a != b;
                    break;
                case 2:
                    expected = a > b;
                    break;
                case 3:
                    expected = a >= b;
                    break;
                case 4:
                    expected = a < b;
                    break;
                case 5:
                    expected = a <= b;
                    break;
                case 6:
                    expected = unordered || a == b;
                    break;
                case 7:
                    expected = unordered || a != b;
                    break;
                case 8:
                    expected = unordered || a > b;
                    break;
                case 9:
                    expected = unordered || a >= b;
                    break;
                case 10:
                    expected = unordered || a < b;
                    break;
                default:
                    expected = unordered || a <= b;
                    break;
                }
                CHECK_EQ(invoke<int>(code, io), expected ? 1 : 0);
            }
        }
    }
}

void testARMv7LoadStorePair64Double()
{
    double buffer[8];
    for (int32_t offset : { 0, 8, 16, -16 }) {
        for (unsigned consecutive = 0; consecutive < 2; ++consecutive) {
            MacroAssembler::FPRegisterID d1 = FPRInfo::fpRegT2;
            MacroAssembler::FPRegisterID d2 = consecutive ? FPRInfo::fpRegT3 : FPRInfo::fpRegT5;
            auto code = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.loadPair64(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(offset), d1, d2);
                jit.storePair64(d1, d2, GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(offset));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            for (unsigned i = 0; i < 8; ++i)
                buffer[i] = static_cast<double>(i) + 0.25;
            double out[8];
            for (unsigned i = 0; i < 8; ++i)
                out[i] = 0;
            char* inBase = reinterpret_cast<char*>(&buffer[4]) - offset;
            char* outBase = reinterpret_cast<char*>(&out[4]) - offset;
            invoke<void>(code, inBase, outBase);
            CHECK_EQ(out[4], buffer[4]);
            CHECK_EQ(out[5], buffer[5]);
        }
    }
}

void testARMv7MoveDoubleBits()
{
    uint32_t io[4];
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.load32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), GPRInfo::argumentGPR2);
        jit.load32(CCallHelpers::Address(GPRInfo::argumentGPR0, 4), GPRInfo::argumentGPR3);
        jit.move64ToDouble(GPRInfo::argumentGPR3, GPRInfo::argumentGPR2, FPRInfo::fpRegT0);
        jit.moveDoubleTo64(FPRInfo::fpRegT0, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2);
        jit.store32(GPRInfo::argumentGPR2, CCallHelpers::Address(GPRInfo::argumentGPR0, 8));
        jit.store32(GPRInfo::argumentGPR3, CCallHelpers::Address(GPRInfo::argumentGPR0, 12));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    for (uint32_t lo : { 0u, 1u, 0xffffffffu, 0x80000000u, 0x7ff80000u }) {
        for (uint32_t hi : { 0u, 1u, 0xffffffffu, 0x80000000u, 0x7ff80000u }) {
            io[0] = lo;
            io[1] = hi;
            io[2] = 0;
            io[3] = 0;
            invoke<void>(code, io);
            CHECK_EQ(io[2], lo);
            CHECK_EQ(io[3], hi);
        }
    }
}

void testARMv7Move32ToFloatBits()
{
    uint32_t io[2];
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.load32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), GPRInfo::argumentGPR2);
        jit.move32ToFloat(GPRInfo::argumentGPR2, FPRInfo::fpRegT0);
        jit.moveFloatTo32(FPRInfo::fpRegT0, GPRInfo::argumentGPR3);
        jit.store32(GPRInfo::argumentGPR3, CCallHelpers::Address(GPRInfo::argumentGPR0, 4));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    for (uint32_t value : { 0u, 1u, 0x3f800000u, 0x80000000u, 0x7f800000u, 0x7fc00000u, 0xffffffffu }) {
        io[0] = value;
        io[1] = 0;
        invoke<void>(code, io);
        CHECK_EQ(io[1], value);
    }
}

void testARMv7MoveZeroToDoubleAndFloat()
{
    double outDouble = 1.0;
    float outFloat = 1.0f;
    auto code = compile([=] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.moveZeroToDouble(FPRInfo::fpRegT0);
        jit.moveZeroToFloat(FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::argumentGPR0));
        jit.storeFloat(FPRInfo::fpRegT1, CCallHelpers::Address(GPRInfo::argumentGPR1));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(code, &outDouble, &outFloat);
    CHECK_EQ(std::bit_cast<uint64_t>(outDouble), static_cast<uint64_t>(0));
    CHECK_EQ(std::bit_cast<uint32_t>(outFloat), static_cast<uint32_t>(0));
}

#endif

void testAsmMemStoreLoad32OffsetBoundaries()
{
    alignas(8) uint8_t buffer[8448];
    uint8_t* base = buffer + 4096;

    for (auto offset : { 0, 4, 8, 124, 128, 252, 256, 1016, 1020, 1024, 4088, 4092, 4096, -4, -8, -252, -255, -256, -1020, -1024 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store32(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            jit.load32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : { 0u, 1u, 0x0000ffffu, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u }) {
            for (unsigned i = 0; i < sizeof(buffer); ++i)
                buffer[i] = 0xa5;
            CHECK_EQ(invoke<uint32_t>(test, base, value), value);
            CHECK_EQ(*bitwise_cast_ptr<uint32_t>(base + offset), value);
        }
    }
}

void testAsmMemStoreLoad16OffsetBoundaries()
{
    alignas(8) uint8_t buffer[8448];
    uint8_t* base = buffer + 4096;

    for (auto offset : { 0, 2, 30, 62, 64, 126, 128, 254, 256, 1020, 1024, 4094, 4096, -2, -30, -62, -254, -256, -1024 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store16(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            jit.load16(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testSigned = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store16(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            jit.load16SignedExtendTo32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : { 0u, 1u, 0x00ffu, 0x0100u, 0x7fffu, 0x8000u, 0xffffu, 0x1234u }) {
            for (unsigned i = 0; i < sizeof(buffer); ++i)
                buffer[i] = 0xa5;
            CHECK_EQ(invoke<uint32_t>(test, base, value), value);
            CHECK_EQ(*bitwise_cast_ptr<uint16_t>(base + offset), static_cast<uint16_t>(value));
            CHECK_EQ(invoke<int32_t>(testSigned, base, value), static_cast<int32_t>(static_cast<int16_t>(value)));
        }
    }
}

void testAsmMemStoreLoad8OffsetBoundaries()
{
    alignas(8) uint8_t buffer[8448];
    uint8_t* base = buffer + 4096;

    for (auto offset : { 0, 1, 30, 31, 32, 63, 64, 127, 128, 254, 255, 256, 4094, 4095, 4096, -1, -31, -32, -255, -256, -1024 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store8(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            jit.load8(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testSigned = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store8(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            jit.load8SignedExtendTo32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : { 0u, 1u, 0x7fu, 0x80u, 0xffu, 0x5au }) {
            for (unsigned i = 0; i < sizeof(buffer); ++i)
                buffer[i] = 0xa5;
            CHECK_EQ(invoke<uint32_t>(test, base, value), value);
            CHECK_EQ(static_cast<uint32_t>(*(base + offset)), value);
            CHECK_EQ(invoke<int32_t>(testSigned, base, value), static_cast<int32_t>(static_cast<int8_t>(value)));
        }
    }
}

void testAsmMemStoreLoad32BaseIndexScales()
{
    alignas(8) uint8_t buffer[16640];
    uint8_t* base = buffer + 8192;

    for (auto scale : { CCallHelpers::TimesOne, CCallHelpers::TimesTwo, CCallHelpers::TimesFour, CCallHelpers::TimesEight }) {
        for (auto offset : { 0, 4, 252, 256, 1020, 1024, 4092, 4096, -4, -252, -256, -1024 }) {
            auto test = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store32(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset));
                jit.load32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto index : { 0, 4, 128 }) {
                for (auto value : { 0u, 1u, 0x80000000u, 0xffffffffu, 0x12345678u }) {
                    for (unsigned i = 0; i < sizeof(buffer); ++i)
                        buffer[i] = 0xa5;
                    uint8_t* effective = base + (static_cast<intptr_t>(index) << static_cast<int>(scale)) + offset;
                    CHECK_EQ(invoke<uint32_t>(test, base, static_cast<intptr_t>(index), value), value);
                    CHECK_EQ(*bitwise_cast_ptr<uint32_t>(effective), value);
                }
            }
        }
    }
}

void testAsmMemStoreLoad8BaseIndexScales()
{
    alignas(8) uint8_t buffer[16640];
    uint8_t* base = buffer + 8192;

    for (auto scale : { CCallHelpers::TimesOne, CCallHelpers::TimesTwo, CCallHelpers::TimesFour, CCallHelpers::TimesEight }) {
        for (auto offset : { 0, 1, 31, 32, 255, 256, 4095, 4096, -1, -255, -256 }) {
            auto test = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store8(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset));
                jit.load8(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto index : { 0, 3, 128 }) {
                for (auto value : { 0u, 1u, 0x7fu, 0x80u, 0xffu }) {
                    for (unsigned i = 0; i < sizeof(buffer); ++i)
                        buffer[i] = 0xa5;
                    uint8_t* effective = base + (static_cast<intptr_t>(index) << static_cast<int>(scale)) + offset;
                    CHECK_EQ(invoke<uint32_t>(test, base, static_cast<intptr_t>(index), value), value);
                    CHECK_EQ(static_cast<uint32_t>(*effective), value);
                }
            }
        }
    }
}

void testAsmMemLoadPair32OffsetBoundaries()
{
    alignas(8) uint8_t buffer[8448];
    uint8_t* base = buffer + 4096;

    for (auto offset : { 0, 4, 8, 1012, 1016, 1020, 1024, 2048, 4084, 4088, 4092, -4, -8, -252, -256, -1020, -1024 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(offset), GPRInfo::argumentGPR2, GPRInfo::argumentGPR3);
            jit.store32(GPRInfo::argumentGPR2, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::argumentGPR3, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (unsigned i = 0; i < sizeof(buffer); ++i)
            buffer[i] = 0xa5;
        uint32_t* words = bitwise_cast_ptr<uint32_t>(base + offset);
        words[0] = 0x11223344u;
        words[1] = 0x55667788u;

        uint32_t out[2] = { 0, 0 };
        invoke<void>(test, base, &out[0]);
        CHECK_EQ(out[0], 0x11223344u);
        CHECK_EQ(out[1], 0x55667788u);
    }
}

void testAsmMemLoadPair32Aliasing()
{
    alignas(8) uint8_t buffer[8448];
    uint8_t* base = buffer + 4096;

    for (auto offset : { 0, 4, 1020, 1024, 4088, 4092, -4, -256, -1024 }) {
        auto aliasLow = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(offset), GPRInfo::argumentGPR0, GPRInfo::argumentGPR3);
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::argumentGPR3, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto aliasHigh = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(offset), GPRInfo::argumentGPR3, GPRInfo::argumentGPR0);
            jit.store32(GPRInfo::argumentGPR3, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (unsigned i = 0; i < sizeof(buffer); ++i)
            buffer[i] = 0xa5;
        uint32_t* words = bitwise_cast_ptr<uint32_t>(base + offset);
        words[0] = 0x0badf00du;
        words[1] = 0xfeedfaceu;

        uint32_t out[2] = { 0, 0 };
        invoke<void>(aliasLow, base, &out[0]);
        CHECK_EQ(out[0], 0x0badf00du);
        CHECK_EQ(out[1], 0xfeedfaceu);

        out[0] = 0;
        out[1] = 0;
        invoke<void>(aliasHigh, base, &out[0]);
        CHECK_EQ(out[0], 0x0badf00du);
        CHECK_EQ(out[1], 0xfeedfaceu);
    }
}

void testAsmMemStorePair32OffsetBoundaries()
{
    alignas(8) uint8_t buffer[8448];
    uint8_t* base = buffer + 4096;

    for (auto offset : { 0, 4, 8, 1016, 1020, 1024, 4084, 4088, 4092, -4, -8, -252, -256, -1020, -1024 }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.storePair32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, CCallHelpers::TrustedImm32(offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (unsigned i = 0; i < sizeof(buffer); ++i)
            buffer[i] = 0xa5;
        invoke<void>(test, 0x11223344u, 0x55667788u, base);
        uint32_t* words = bitwise_cast_ptr<uint32_t>(base + offset);
        CHECK_EQ(words[0], 0x11223344u);
        CHECK_EQ(words[1], 0x55667788u);
    }
}

void testAsmMemShift32ImmediateBoundaries()
{
    for (auto shift : { 0, 1, 2, 15, 16, 30, 31, 32, 33, 63, 64, 96, -1, -31, -32 }) {
        auto lshift = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.lshift32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rshift = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.rshift32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto urshift = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.urshift32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto lshiftInPlace = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.lshift32(CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        unsigned amount = static_cast<unsigned>(shift) & 31;
        for (auto value : { 0u, 1u, 2u, 0x0000ffffu, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u }) {
            CHECK_EQ(invoke<uint32_t>(lshift, value), static_cast<uint32_t>(value << amount));
            CHECK_EQ(invoke<uint32_t>(rshift, value), static_cast<uint32_t>(static_cast<int32_t>(value) >> amount));
            CHECK_EQ(invoke<uint32_t>(urshift, value), static_cast<uint32_t>(value >> amount));
            CHECK_EQ(invoke<uint32_t>(lshiftInPlace, value), static_cast<uint32_t>(value << amount));
        }
    }
}

void testAsmMemShift32RegisterBoundaries()
{
    auto lshift = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.lshift32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto rshift = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.rshift32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto urshift = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.urshift32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto lshiftAliased = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.lshift32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto shift : { 0u, 1u, 15u, 31u, 32u, 33u, 63u, 64u, 0xffffffffu }) {
        unsigned amount = shift & 31;
        for (auto value : { 0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u }) {
            CHECK_EQ(invoke<uint32_t>(lshift, value, shift), static_cast<uint32_t>(value << amount));
            CHECK_EQ(invoke<uint32_t>(rshift, value, shift), static_cast<uint32_t>(static_cast<int32_t>(value) >> amount));
            CHECK_EQ(invoke<uint32_t>(urshift, value, shift), static_cast<uint32_t>(value >> amount));
        }
    }

    for (auto value : { 0u, 1u, 3u, 31u, 32u, 33u, 0xffffffffu })
        CHECK_EQ(invoke<uint32_t>(lshiftAliased, value), static_cast<uint32_t>(value << (value & 31)));
}

void testAsmMemCountZeros32Boundaries()
{
    auto clz = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.countLeadingZeros32(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto ctz = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.countTrailingZeros32(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto clzAliased = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.countLeadingZeros32(GPRInfo::returnValueGPR, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : { 0u, 1u, 2u, 3u, 0x0000ffffu, 0x00010000u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u }) {
        uint32_t expectedClz = 32;
        for (unsigned i = 0; i < 32; ++i) {
            if (value & (1u << (31 - i))) {
                expectedClz = i;
                break;
            }
        }

        uint32_t expectedCtz = 32;
        for (unsigned i = 0; i < 32; ++i) {
            if (value & (1u << i)) {
                expectedCtz = i;
                break;
            }
        }

        CHECK_EQ(invoke<uint32_t>(clz, value), expectedClz);
        CHECK_EQ(invoke<uint32_t>(ctz, value), expectedCtz);
        CHECK_EQ(invoke<uint32_t>(clzAliased, value), expectedClz);
    }
}

#if CPU(ARM_THUMB2)

void testAsmMemRotateRight32ImmediateBoundaries()
{
    for (auto shift : { 0, 1, 2, 15, 16, 31, 32, 33, 63, 64, 96, -1, -31, -32 }) {
        auto rotate = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.rotateRight32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rotateInPlace = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.rotateRight32(CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        unsigned amount = static_cast<unsigned>(shift) & 31;
        for (auto value : { 0u, 1u, 2u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u, 0xdeadbeefu }) {
            uint32_t expected = amount ? ((value >> amount) | (value << (32 - amount))) : value;
            CHECK_EQ(invoke<uint32_t>(rotate, value), expected);
            CHECK_EQ(invoke<uint32_t>(rotateInPlace, value), expected);
        }
    }
}

void testAsmMemRotate32RegisterBoundaries()
{
    auto rotateRight = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.rotateRight32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto rotateLeft = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.rotateLeft32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto shift : { 0u, 1u, 15u, 31u, 32u, 33u, 63u, 64u, 0xffffffffu }) {
        unsigned amount = shift & 31;
        for (auto value : { 0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u, 0xdeadbeefu }) {
            uint32_t expectedRight = amount ? ((value >> amount) | (value << (32 - amount))) : value;
            uint32_t expectedLeft = amount ? ((value << amount) | (value >> (32 - amount))) : value;
            CHECK_EQ(invoke<uint32_t>(rotateRight, value, shift), expectedRight);
            CHECK_EQ(invoke<uint32_t>(rotateLeft, value, shift), expectedLeft);
        }
    }
}

void testAsmMemRotateLeft32ImmediateBoundaries()
{
    for (auto shift : { 0, 1, 15, 31, 32, 33, 63, 64, -1 }) {
        auto rotate = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.rotateLeft32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        unsigned amount = static_cast<unsigned>(shift) & 31;
        for (auto value : { 0u, 1u, 0x80000000u, 0xffffffffu, 0x12345678u }) {
            uint32_t expected = amount ? ((value << amount) | (value >> (32 - amount))) : value;
            CHECK_EQ(invoke<uint32_t>(rotate, value), expected);
        }
    }
}

#endif

#if CPU(ARM_THUMB2)

static Vector<int32_t> armv7ImmediateOperands()
{
    return Vector<int32_t> {
        0,
        1,
        2,
        7,
        8,
        0xff,
        0x100,
        0x101,
        0x1ff,
        0x200,
        0x3ff,
        0x400,
        0x555,
        0xabc,
        0xfff,
        0x1000,
        0x1001,
        0xff00,
        0xff0000,
        static_cast<int32_t>(0xff000000),
        0x00ff00ff,
        static_cast<int32_t>(0xff00ff00),
        0x01010101,
        static_cast<int32_t>(0xf000000f),
        -1,
        -2,
        -0x100,
        -0x101,
        -0x1000,
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::min(),
    };
}

static Vector<int32_t> armv7ShiftAmounts()
{
    return Vector<int32_t> { 0, 1, 2, 15, 16, 17, 30, 31, 32, 33, 63, 64, -1, -31, -32 };
}

void testARMv7ImmAdd32ArgImm()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto add = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.add32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<uint32_t>(add, 0, value), static_cast<uint32_t>(value) + static_cast<uint32_t>(immediate));
    }
}

void testARMv7ImmAdd32ImmDest()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto add = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.add32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<uint32_t>(add, value), static_cast<uint32_t>(value) + static_cast<uint32_t>(immediate));
    }
}

void testARMv7ImmAddSubRoundTrip()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto roundTrip = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.add32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);
            jit.sub32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<uint32_t>(roundTrip, value), static_cast<uint32_t>(value));
    }
}

void testARMv7ImmAnd32ArgImm()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto andOp = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.and32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto andSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.and32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands()) {
            uint32_t expected = static_cast<uint32_t>(value) & static_cast<uint32_t>(immediate);
            CHECK_EQ(invoke<uint32_t>(andOp, 0, value), expected);
            CHECK_EQ(invoke<uint32_t>(andSame, value), expected);
        }
    }
}

void testARMv7ImmOr32ArgImm()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto orOp = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.or32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto orSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.or32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands()) {
            uint32_t expected = static_cast<uint32_t>(value) | static_cast<uint32_t>(immediate);
            CHECK_EQ(invoke<uint32_t>(orOp, 0, value), expected);
            CHECK_EQ(invoke<uint32_t>(orSame, value), expected);
        }
    }
}

void testARMv7ImmXor32ArgImm()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto xorOp = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.xor32(CCallHelpers::TrustedImm32(immediate), GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto xorSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.xor32(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands()) {
            uint32_t expected = static_cast<uint32_t>(value) ^ static_cast<uint32_t>(immediate);
            CHECK_EQ(invoke<uint32_t>(xorOp, 0, value), expected);
            CHECK_EQ(invoke<uint32_t>(xorSame, value), expected);
        }
    }
}

void testARMv7ImmMove32()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto moveOp = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.move(CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        CHECK_EQ(invoke<uint32_t>(moveOp), static_cast<uint32_t>(immediate));
    }
}

void testARMv7ImmBranch32()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto branch = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            auto equal = jit.branch32(CCallHelpers::Equal, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(immediate));
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            auto done = jit.jump();
            equal.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            done.link(&jit);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<int>(branch, value), value == immediate ? 1 : 0);
        CHECK_EQ(invoke<int>(branch, immediate), 1);
    }
}

void testARMv7ImmCompare32()
{
    for (int32_t immediate : armv7ImmediateOperands()) {
        auto compare = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.compare32(CCallHelpers::LessThan, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(immediate), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands())
            CHECK_EQ(invoke<int>(compare, value), value < immediate ? 1 : 0);
    }
}

void testARMv7ImmRotateRight32()
{
    for (int32_t amount : armv7ShiftAmounts()) {
        auto rotate = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.rotateRight32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(amount), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rotateSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.rotateRight32(CCallHelpers::TrustedImm32(amount), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        unsigned shift = static_cast<unsigned>(amount) & 31;
        for (auto value : int32Operands()) {
            uint32_t unsignedValue = static_cast<uint32_t>(value);
            uint32_t expected = shift ? ((unsignedValue >> shift) | (unsignedValue << (32 - shift))) : unsignedValue;
            CHECK_EQ(invoke<uint32_t>(rotate, 0, value), expected);
            CHECK_EQ(invoke<uint32_t>(rotateSame, value), expected);
        }
    }
}

void testARMv7ImmRotateLeft32()
{
    for (int32_t amount : armv7ShiftAmounts()) {
        auto rotate = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.rotateLeft32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(amount), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        unsigned shift = static_cast<unsigned>(amount) & 31;
        for (auto value : int32Operands()) {
            uint32_t unsignedValue = static_cast<uint32_t>(value);
            uint32_t expected = shift ? ((unsignedValue << shift) | (unsignedValue >> (32 - shift))) : unsignedValue;
            CHECK_EQ(invoke<uint32_t>(rotate, 0, value), expected);
        }
    }
}

void testARMv7ImmShift32Amounts()
{
    for (int32_t amount : armv7ShiftAmounts()) {
        auto left = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.lshift32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(amount), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto arithmeticRight = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.rshift32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(amount), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto logicalRight = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.urshift32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(amount), GPRInfo::returnValueGPR);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        unsigned shift = static_cast<unsigned>(amount) & 31;
        for (auto value : int32Operands()) {
            uint32_t unsignedValue = static_cast<uint32_t>(value);
            CHECK_EQ(invoke<uint32_t>(left, 0, value), unsignedValue << shift);
            CHECK_EQ(invoke<int32_t>(arithmeticRight, 0, value), value >> shift);
            CHECK_EQ(invoke<uint32_t>(logicalRight, 0, value), unsignedValue >> shift);
        }
    }
}

#endif



void testRotateRight32Immediate()
{
    for (auto shift : { 0, 1, 4, 7, 15, 16, 30, 31, 32, 33, 47, 63, 64, 96, 128,
        -1, -7, -31, -32, -33, -64 }) {
        auto rotateToOther = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.rotateRight32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rotateInPlace = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.rotateRight32(CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto rotateSameRegister = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.rotateRight32(GPRInfo::returnValueGPR, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands()) {
            uint32_t v = static_cast<uint32_t>(value);
            uint32_t n = static_cast<uint32_t>(shift) & 31;
            uint32_t expected = n ? ((v >> n) | (v << (32 - n))) : v;
            CHECK_EQ(invoke<uint32_t>(rotateToOther, value), expected);
            CHECK_EQ(invoke<uint32_t>(rotateInPlace, value), expected);
            CHECK_EQ(invoke<uint32_t>(rotateSameRegister, value), expected);
        }
    }
}

void testShift32Immediate()
{
    for (auto shift : { 0, 1, 4, 15, 16, 30, 31, 32, 33, 63, 64, -1, -31, -32, -33 }) {
        auto left = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.lshift32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto leftInPlace = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.lshift32(CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto arithmeticRight = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.rshift32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto arithmeticRightInPlace = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.rshift32(CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto logicalRight = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.urshift32(GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto logicalRightInPlace = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            jit.urshift32(CCallHelpers::TrustedImm32(shift), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto value : int32Operands()) {
            uint32_t v = static_cast<uint32_t>(value);
            uint32_t n = static_cast<uint32_t>(shift) & 31;
            CHECK_EQ(invoke<uint32_t>(left, value), static_cast<uint32_t>(v << n));
            CHECK_EQ(invoke<uint32_t>(leftInPlace, value), static_cast<uint32_t>(v << n));
            CHECK_EQ(invoke<int32_t>(arithmeticRight, value), static_cast<int32_t>(value >> n));
            CHECK_EQ(invoke<int32_t>(arithmeticRightInPlace, value), static_cast<int32_t>(value >> n));
            CHECK_EQ(invoke<uint32_t>(logicalRight, value), static_cast<uint32_t>(v >> n));
            CHECK_EQ(invoke<uint32_t>(logicalRightInPlace, value), static_cast<uint32_t>(v >> n));
        }
    }
}

void testOrAbsoluteAddressAdjacent()
{
    uint32_t words[4];
    uint16_t halves[4];
    uint8_t bytes[4];

    auto reset = [&] {
        for (unsigned i = 0; i < 4; ++i) {
            words[i] = 0x11110000u * (i + 1);
            halves[i] = static_cast<uint16_t>(0x1100 * (i + 1));
            bytes[i] = static_cast<uint8_t>(0x10 * (i + 1));
        }
    };

    reset();
    auto orImm32 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.or32(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&words[0]));
        jit.or32(CCallHelpers::TrustedImm32(2), CCallHelpers::AbsoluteAddress(&words[1]));
        jit.or32(CCallHelpers::TrustedImm32(0x12345678), CCallHelpers::AbsoluteAddress(&words[2]));
        jit.or32(CCallHelpers::TrustedImm32(4), CCallHelpers::AbsoluteAddress(&words[3]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(orImm32);
    CHECK_EQ(words[0], 0x11110000u | 1);
    CHECK_EQ(words[1], 0x22220000u | 2);
    CHECK_EQ(words[2], 0x33330000u | 0x12345678u);
    CHECK_EQ(words[3], 0x44440000u | 4);

    reset();
    auto orReg32 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImm32(0xf), GPRInfo::argumentGPR0);
        jit.or32(GPRInfo::argumentGPR0, CCallHelpers::AbsoluteAddress(&words[0]));
        jit.or32(GPRInfo::argumentGPR0, CCallHelpers::AbsoluteAddress(&words[1]));
        jit.or32(GPRInfo::argumentGPR0, CCallHelpers::AbsoluteAddress(&words[3]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(orReg32);
    CHECK_EQ(words[0], 0x11110000u | 0xf);
    CHECK_EQ(words[1], 0x22220000u | 0xf);
    CHECK_EQ(words[2], 0x33330000u);
    CHECK_EQ(words[3], 0x44440000u | 0xf);

    reset();
    auto orImm16 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.or16(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&halves[0]));
        jit.or16(CCallHelpers::TrustedImm32(2), CCallHelpers::AbsoluteAddress(&halves[1]));
        jit.or16(CCallHelpers::TrustedImm32(0x4444), CCallHelpers::AbsoluteAddress(&halves[2]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(orImm16);
    CHECK_EQ(static_cast<unsigned>(halves[0]), static_cast<unsigned>(0x1100 | 1));
    CHECK_EQ(static_cast<unsigned>(halves[1]), static_cast<unsigned>(0x2200 | 2));
    CHECK_EQ(static_cast<unsigned>(halves[2]), static_cast<unsigned>(0x3300 | 0x4444));
    CHECK_EQ(static_cast<unsigned>(halves[3]), static_cast<unsigned>(0x4400));

    reset();
    auto orReg16 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImm32(0xf), GPRInfo::argumentGPR0);
        jit.or16(GPRInfo::argumentGPR0, CCallHelpers::AbsoluteAddress(&halves[0]));
        jit.or16(GPRInfo::argumentGPR0, CCallHelpers::AbsoluteAddress(&halves[2]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(orReg16);
    CHECK_EQ(static_cast<unsigned>(halves[0]), static_cast<unsigned>(0x1100 | 0xf));
    CHECK_EQ(static_cast<unsigned>(halves[1]), static_cast<unsigned>(0x2200));
    CHECK_EQ(static_cast<unsigned>(halves[2]), static_cast<unsigned>(0x3300 | 0xf));
    CHECK_EQ(static_cast<unsigned>(halves[3]), static_cast<unsigned>(0x4400));

    reset();
    auto orImm8 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.or8(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&bytes[0]));
        jit.or8(CCallHelpers::TrustedImm32(2), CCallHelpers::AbsoluteAddress(&bytes[1]));
        jit.or8(CCallHelpers::TrustedImm32(4), CCallHelpers::AbsoluteAddress(&bytes[3]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(orImm8);
    CHECK_EQ(static_cast<unsigned>(bytes[0]), static_cast<unsigned>(0x10 | 1));
    CHECK_EQ(static_cast<unsigned>(bytes[1]), static_cast<unsigned>(0x20 | 2));
    CHECK_EQ(static_cast<unsigned>(bytes[2]), static_cast<unsigned>(0x30));
    CHECK_EQ(static_cast<unsigned>(bytes[3]), static_cast<unsigned>(0x40 | 4));
}

void testAdd64ImmAbsoluteAddressAdjacent()
{
    uint64_t counters[3];

    auto reset = [&] {
        counters[0] = 0;
        counters[1] = 0x00000000ffffffffull;
        counters[2] = 0x1000000000000000ull;
    };

    reset();
    auto smallImmediates = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.add64(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&counters[0]));
        jit.add64(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&counters[1]));
        jit.add64(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&counters[2]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(smallImmediates);
    CHECK_EQ(counters[0], 1ull);
    CHECK_EQ(counters[1], 0x0000000100000000ull);
    CHECK_EQ(counters[2], 0x1000000000000001ull);

    reset();
    auto largeImmediates = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.add64(CCallHelpers::TrustedImm32(0x12345678), CCallHelpers::AbsoluteAddress(&counters[0]));
        jit.add64(CCallHelpers::TrustedImm32(0x12345678), CCallHelpers::AbsoluteAddress(&counters[1]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(largeImmediates);
    CHECK_EQ(counters[0], 0x12345678ull);
    CHECK_EQ(counters[1], 0x0000000112345677ull);

    reset();
    auto negativeImmediates = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.add64(CCallHelpers::TrustedImm32(-1), CCallHelpers::AbsoluteAddress(&counters[0]));
        jit.add64(CCallHelpers::TrustedImm32(-1), CCallHelpers::AbsoluteAddress(&counters[1]));
        jit.add64(CCallHelpers::TrustedImm32(-0x12345678), CCallHelpers::AbsoluteAddress(&counters[2]));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(negativeImmediates);
    CHECK_EQ(counters[0], 0xffffffffffffffffull);
    CHECK_EQ(counters[1], 0x00000000fffffffeull);
    CHECK_EQ(counters[2], 0x1000000000000000ull - 0x12345678ull);
}


#if CPU(ARM_THUMB2)

static uint32_t masmaluClz32(uint32_t value)
{
    uint32_t count = 0;
    while (count < 32 && !(value & 0x80000000u)) {
        ++count;
        value <<= 1;
    }
    return count;
}

static uint32_t masmaluCtz32(uint32_t value)
{
    uint32_t count = 0;
    while (count < 32 && !(value & 1)) {
        ++count;
        value >>= 1;
    }
    return count;
}

void testArmv7Arith32Aliasing()
{
    uint32_t inLeft = 0;
    uint32_t inRight = 0;
    uint32_t out = 0;
    uint32_t effective[16];

    for (auto operation : { 0, 1, 2, 3, 4, 5 }) {
        for (auto left : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
            for (auto right : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2, ARMRegisters::r3 }) {
                    auto code = compile([&] (CCallHelpers& jit) {
                        emitFunctionPrologue(jit);
                        jit.load32(&inLeft, left);
                        jit.load32(&inRight, right);
                        switch (operation) {
                        case 0:
                            jit.add32(left, right, dest);
                            break;
                        case 1:
                            jit.sub32(left, right, dest);
                            break;
                        case 2:
                            jit.and32(left, right, dest);
                            break;
                        case 3:
                            jit.or32(left, right, dest);
                            break;
                        case 4:
                            jit.xor32(left, right, dest);
                            break;
                        default:
                            jit.mul32(left, right, dest);
                            break;
                        }
                        jit.store32(dest, &out);
                        emitFunctionEpilogue(jit);
                        jit.ret();
                    });

                    for (auto a : int32Operands()) {
                        for (auto b : { 0, 1, -1, 3, 42, -42, 0x7fffffff }) {
                            inLeft = static_cast<uint32_t>(a);
                            inRight = static_cast<uint32_t>(b);
                            out = 0;
                            effective[left] = inLeft;
                            effective[right] = inRight;
                            uint32_t x = effective[left];
                            uint32_t y = effective[right];
                            uint32_t expected = 0;
                            switch (operation) {
                            case 0:
                                expected = x + y;
                                break;
                            case 1:
                                expected = x - y;
                                break;
                            case 2:
                                expected = x & y;
                                break;
                            case 3:
                                expected = x | y;
                                break;
                            case 4:
                                expected = x ^ y;
                                break;
                            default:
                                expected = x * y;
                                break;
                            }
                            invoke<void>(code);
                            CHECK_EQ(out, expected);
                        }
                    }
                }
            }
        }
    }
}

void testArmv7Arith32ImmediateAliasing()
{
    uint32_t in = 0;
    uint32_t out = 0;

    for (auto immediate : { 0, 1, 2, 7, 8, 0xff, 0x100, 0x101, 0x1ff, 0x200, 0x3ff,
        0x400, 0xabc, 0xfff, 0x1000, 0x1001, 0x10000, 0x12345678, -1, -2, -0xff,
        -0x1000, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min() }) {
        for (auto operation : { 0, 1, 2, 3, 4, 5, 6 }) {
            for (auto src : { ARMRegisters::r0, ARMRegisters::r1 }) {
                for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                    auto code = compile([&] (CCallHelpers& jit) {
                        emitFunctionPrologue(jit);
                        jit.load32(&in, src);
                        switch (operation) {
                        case 0:
                            jit.add32(CCallHelpers::TrustedImm32(immediate), src, dest);
                            break;
                        case 1:
                            jit.and32(CCallHelpers::TrustedImm32(immediate), src, dest);
                            break;
                        case 2:
                            jit.or32(CCallHelpers::TrustedImm32(immediate), src, dest);
                            break;
                        case 3:
                            jit.xor32(CCallHelpers::TrustedImm32(immediate), src, dest);
                            break;
                        case 4:
                            jit.sub32(src, CCallHelpers::TrustedImm32(immediate), dest);
                            break;
                        case 5:
                            jit.sub32(CCallHelpers::TrustedImm32(immediate), src, dest);
                            break;
                        default:
                            jit.mul32(CCallHelpers::TrustedImm32(immediate), src, dest);
                            break;
                        }
                        jit.store32(dest, &out);
                        emitFunctionEpilogue(jit);
                        jit.ret();
                    });

                    for (auto value : int32Operands()) {
                        in = static_cast<uint32_t>(value);
                        out = 0;
                        uint32_t v = in;
                        uint32_t i = static_cast<uint32_t>(immediate);
                        uint32_t expected = 0;
                        switch (operation) {
                        case 0:
                            expected = v + i;
                            break;
                        case 1:
                            expected = v & i;
                            break;
                        case 2:
                            expected = v | i;
                            break;
                        case 3:
                            expected = v ^ i;
                            break;
                        case 4:
                            expected = v - i;
                            break;
                        case 5:
                            expected = i - v;
                            break;
                        default:
                            expected = v * i;
                            break;
                        }
                        invoke<void>(code);
                        CHECK_EQ(out, expected);
                    }
                }
            }
        }
    }
}

void testArmv7Arith32InPlaceImmediate()
{
    uint32_t in = 0;
    uint32_t out = 0;

    for (auto immediate : { 0, 1, 0xff, 0x100, 0x3ff, 0x400, 0xfff, 0x1000, 0x12345678,
        -1, -0x1000, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min() }) {
        for (auto operation : { 0, 1, 2, 3, 4 }) {
            for (auto dest : { ARMRegisters::r0, ARMRegisters::r1 }) {
                auto code = compile([&] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);
                    jit.load32(&in, dest);
                    switch (operation) {
                    case 0:
                        jit.add32(CCallHelpers::TrustedImm32(immediate), dest);
                        break;
                    case 1:
                        jit.and32(CCallHelpers::TrustedImm32(immediate), dest);
                        break;
                    case 2:
                        jit.or32(CCallHelpers::TrustedImm32(immediate), dest);
                        break;
                    case 3:
                        jit.xor32(CCallHelpers::TrustedImm32(immediate), dest);
                        break;
                    default:
                        jit.sub32(CCallHelpers::TrustedImm32(immediate), dest);
                        break;
                    }
                    jit.store32(dest, &out);
                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                for (auto value : int32Operands()) {
                    in = static_cast<uint32_t>(value);
                    out = 0;
                    uint32_t v = in;
                    uint32_t i = static_cast<uint32_t>(immediate);
                    uint32_t expected = 0;
                    switch (operation) {
                    case 0:
                        expected = v + i;
                        break;
                    case 1:
                        expected = v & i;
                        break;
                    case 2:
                        expected = v | i;
                        break;
                    case 3:
                        expected = v ^ i;
                        break;
                    default:
                        expected = v - i;
                        break;
                    }
                    invoke<void>(code);
                    CHECK_EQ(out, expected);
                }
            }
        }
    }
}

void testArmv7Neg32Not32Aliasing()
{
    uint32_t in = 0;
    uint32_t out = 0;

    for (auto src : { ARMRegisters::r0, ARMRegisters::r1 }) {
        for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
            auto negate = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(&in, src);
                jit.neg32(src, dest);
                jit.store32(dest, &out);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto invert = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(&in, src);
                jit.not32(src, dest);
                jit.store32(dest, &out);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto negateInPlace = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(&in, dest);
                jit.neg32(dest);
                jit.store32(dest, &out);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto invertInPlace = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(&in, dest);
                jit.not32(dest);
                jit.store32(dest, &out);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto value : int32Operands()) {
                in = static_cast<uint32_t>(value);
                out = 0;
                invoke<void>(negate);
                CHECK_EQ(out, static_cast<uint32_t>(0) - static_cast<uint32_t>(value));
                out = 0;
                invoke<void>(invert);
                CHECK_EQ(out, ~static_cast<uint32_t>(value));
                out = 0;
                invoke<void>(negateInPlace);
                CHECK_EQ(out, static_cast<uint32_t>(0) - static_cast<uint32_t>(value));
                out = 0;
                invoke<void>(invertInPlace);
                CHECK_EQ(out, ~static_cast<uint32_t>(value));
            }
        }
    }
}

void testArmv7CountZeros32Aliasing()
{
    uint32_t in = 0;
    uint32_t out = 0;

    for (auto src : { ARMRegisters::r0, ARMRegisters::r1 }) {
        for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
            auto leading = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(&in, src);
                jit.countLeadingZeros32(src, dest);
                jit.store32(dest, &out);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto trailing = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(&in, src);
                jit.countTrailingZeros32(src, dest);
                jit.store32(dest, &out);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto value : { 0u, 1u, 2u, 3u, 0x80000000u, 0xffffffffu, 0x00010000u,
                0x00008000u, 0x40000000u, 0x0000ffffu }) {
                in = value;
                out = 0;
                invoke<void>(leading);
                CHECK_EQ(out, masmaluClz32(value));
                out = 0;
                invoke<void>(trailing);
                CHECK_EQ(out, masmaluCtz32(value));
            }
        }
    }
}

void testArmv7Shift32RegisterAliasing()
{
    uint32_t inValue = 0;
    uint32_t inShift = 0;
    uint32_t out = 0;
    uint32_t effective[16];

    for (auto operation : { 0, 1, 2 }) {
        for (auto src : { ARMRegisters::r0, ARMRegisters::r1 }) {
            for (auto shiftAmount : { ARMRegisters::r0, ARMRegisters::r1 }) {
                for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                    auto code = compile([&] (CCallHelpers& jit) {
                        emitFunctionPrologue(jit);
                        jit.load32(&inValue, src);
                        jit.load32(&inShift, shiftAmount);
                        switch (operation) {
                        case 0:
                            jit.lshift32(src, shiftAmount, dest);
                            break;
                        case 1:
                            jit.rshift32(src, shiftAmount, dest);
                            break;
                        default:
                            jit.urshift32(src, shiftAmount, dest);
                            break;
                        }
                        jit.store32(dest, &out);
                        emitFunctionEpilogue(jit);
                        jit.ret();
                    });

                    for (auto value : int32Operands()) {
                        for (auto shift : { 0, 1, 5, 31, 32, 33, 63, 64, 255, 256, -1 }) {
                            inValue = static_cast<uint32_t>(value);
                            inShift = static_cast<uint32_t>(shift);
                            out = 0;
                            effective[src] = inValue;
                            effective[shiftAmount] = inShift;
                            uint32_t v = effective[src];
                            uint32_t n = effective[shiftAmount] & 31;
                            uint32_t expected = 0;
                            switch (operation) {
                            case 0:
                                expected = v << n;
                                break;
                            case 1:
                                expected = static_cast<uint32_t>(static_cast<int32_t>(v) >> n);
                                break;
                            default:
                                expected = v >> n;
                                break;
                            }
                            invoke<void>(code);
                            CHECK_EQ(out, expected);
                        }
                    }
                }
            }
        }
    }
}

void testArmv7Shift32ImmediateValueRegisterAmount()
{
    uint32_t inShift = 0;
    uint32_t out = 0;

    for (auto immediate : { 0, 1, 0xff, 0x100, 0x3ff, 0x400, 0xfff, 0x1000, 0x12345678,
        -1, std::numeric_limits<int32_t>::min() }) {
        for (auto operation : { 0, 1 }) {
            for (auto shiftAmount : { ARMRegisters::r0, ARMRegisters::r1 }) {
                for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                    auto code = compile([&] (CCallHelpers& jit) {
                        emitFunctionPrologue(jit);
                        jit.load32(&inShift, shiftAmount);
                        if (!operation)
                            jit.lshift32(CCallHelpers::TrustedImm32(immediate), shiftAmount, dest);
                        else
                            jit.rshift32(CCallHelpers::TrustedImm32(immediate), shiftAmount, dest);
                        jit.store32(dest, &out);
                        emitFunctionEpilogue(jit);
                        jit.ret();
                    });

                    for (auto shift : { 0, 1, 5, 31, 32, 33, 63, 255, -1 }) {
                        inShift = static_cast<uint32_t>(shift);
                        out = 0;
                        uint32_t n = inShift & 31;
                        uint32_t v = static_cast<uint32_t>(immediate);
                        uint32_t expected = operation
                            ? static_cast<uint32_t>(static_cast<int32_t>(v) >> n)
                            : (v << n);
                        invoke<void>(code);
                        CHECK_EQ(out, expected);
                    }
                }
            }
        }
    }
}

void testArmv7ShiftUnchecked()
{
    uint32_t inValue = 0;
    uint32_t inShift = 0;
    uint32_t out = 0;
    uint32_t effective[16];

    for (auto operation : { 0, 1, 2 }) {
        for (auto src : { ARMRegisters::r0, ARMRegisters::r1 }) {
            for (auto shiftAmount : { ARMRegisters::r0, ARMRegisters::r1 }) {
                for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                    auto code = compile([&] (CCallHelpers& jit) {
                        emitFunctionPrologue(jit);
                        jit.load32(&inValue, src);
                        jit.load32(&inShift, shiftAmount);
                        switch (operation) {
                        case 0:
                            jit.lshiftUnchecked(src, shiftAmount, dest);
                            break;
                        case 1:
                            jit.rshiftUnchecked(src, shiftAmount, dest);
                            break;
                        default:
                            jit.urshiftUnchecked(src, shiftAmount, dest);
                            break;
                        }
                        jit.store32(dest, &out);
                        emitFunctionEpilogue(jit);
                        jit.ret();
                    });

                    for (auto value : int32Operands()) {
                        for (auto shift : { 0, 1, 5, 30, 31 }) {
                            inValue = static_cast<uint32_t>(value);
                            inShift = static_cast<uint32_t>(shift);
                            out = 0;
                            effective[src] = inValue;
                            effective[shiftAmount] = inShift;
                            uint32_t v = effective[src];
                            uint32_t n = effective[shiftAmount] & 31;
                            uint32_t expected = 0;
                            switch (operation) {
                            case 0:
                                expected = v << n;
                                break;
                            case 1:
                                expected = static_cast<uint32_t>(static_cast<int32_t>(v) >> n);
                                break;
                            default:
                                expected = v >> n;
                                break;
                            }
                            invoke<void>(code);
                            CHECK_EQ(out, expected);
                        }
                    }
                }
            }
        }
    }
}

void testArmv7RotateLeft32()
{
    uint32_t inValue = 0;
    uint32_t inShift = 0;
    uint32_t out = 0;
    uint32_t effective[16];

    for (auto src : { ARMRegisters::r0, ARMRegisters::r1 }) {
        for (auto shiftAmount : { ARMRegisters::r0, ARMRegisters::r1 }) {
            for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                auto code = compile([&] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);
                    jit.load32(&inValue, src);
                    jit.load32(&inShift, shiftAmount);
                    jit.rotateLeft32(src, shiftAmount, dest);
                    jit.store32(dest, &out);
                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                for (auto value : int32Operands()) {
                    for (auto shift : { 0, 1, 5, 31, 32, 33, 63, 255, -1 }) {
                        inValue = static_cast<uint32_t>(value);
                        inShift = static_cast<uint32_t>(shift);
                        out = 0;
                        effective[src] = inValue;
                        effective[shiftAmount] = inShift;
                        uint32_t v = effective[src];
                        uint32_t n = effective[shiftAmount] & 31;
                        uint32_t expected = n ? ((v << n) | (v >> (32 - n))) : v;
                        invoke<void>(code);
                        CHECK_EQ(out, expected);
                    }
                }
            }
        }
    }
}

void testArmv7RotateLeft32Immediate()
{
    uint32_t in = 0;
    uint32_t out = 0;

    for (auto shift : { 0, 1, 5, 31, 32, 33, 63, 64, 96, 255, -1, -31, -32, -33 }) {
        for (auto src : { ARMRegisters::r0, ARMRegisters::r1 }) {
            for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                auto code = compile([&] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);
                    jit.load32(&in, src);
                    jit.rotateLeft32(src, CCallHelpers::TrustedImm32(shift), dest);
                    jit.store32(dest, &out);
                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                for (auto value : int32Operands()) {
                    in = static_cast<uint32_t>(value);
                    out = 0;
                    uint32_t v = in;
                    uint32_t n = static_cast<uint32_t>(shift) & 31;
                    uint32_t expected = n ? ((v << n) | (v >> (32 - n))) : v;
                    invoke<void>(code);
                    CHECK_EQ(out, expected);
                }
            }
        }
    }
}

void testArmv7Add64Sub64RegisterPairs()
{
    uint32_t inA[2];
    uint32_t inB[2];
    uint32_t outHi = 0;
    uint32_t outLo = 0;
    uint32_t effective[16];

    for (auto subtract : { false, true }) {
        for (auto op2Hi : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
            for (auto op2Lo : { ARMRegisters::r1, ARMRegisters::r2, ARMRegisters::r3 }) {
                for (auto destHi : { ARMRegisters::r0, ARMRegisters::r2, ARMRegisters::r3 }) {
                    for (auto destLo : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2, ARMRegisters::r3 }) {
                        if (destHi == destLo)
                            continue;

                        auto op1Hi = ARMRegisters::r0;
                        auto op1Lo = ARMRegisters::r1;

                        auto code = compile([&] (CCallHelpers& jit) {
                            emitFunctionPrologue(jit);
                            jit.load32(&inA[1], op1Hi);
                            jit.load32(&inA[0], op1Lo);
                            jit.load32(&inB[1], op2Hi);
                            jit.load32(&inB[0], op2Lo);
                            if (subtract)
                                jit.sub64(op1Hi, op1Lo, op2Hi, op2Lo, destHi, destLo);
                            else
                                jit.add64(op1Hi, op1Lo, op2Hi, op2Lo, destHi, destLo);
                            jit.store32(destLo, &outLo);
                            jit.store32(destHi, &outHi);
                            emitFunctionEpilogue(jit);
                            jit.ret();
                        });

                        for (auto pair : { 0, 1, 2, 3, 4 }) {
                            switch (pair) {
                            case 0:
                                inA[1] = 0;
                                inA[0] = 1;
                                inB[1] = 0;
                                inB[0] = 2;
                                break;
                            case 1:
                                inA[1] = 0;
                                inA[0] = 0xffffffffu;
                                inB[1] = 0;
                                inB[0] = 1;
                                break;
                            case 2:
                                inA[1] = 0xffffffffu;
                                inA[0] = 0xffffffffu;
                                inB[1] = 0;
                                inB[0] = 1;
                                break;
                            case 3:
                                inA[1] = 0x12345678u;
                                inA[0] = 0x9abcdef0u;
                                inB[1] = 0x0fedcba9u;
                                inB[0] = 0x87654321u;
                                break;
                            default:
                                inA[1] = 0;
                                inA[0] = 0;
                                inB[1] = 0;
                                inB[0] = 1;
                                break;
                            }

                            outHi = 0;
                            outLo = 0;
                            effective[op1Hi] = inA[1];
                            effective[op1Lo] = inA[0];
                            effective[op2Hi] = inB[1];
                            effective[op2Lo] = inB[0];
                            uint64_t a = (static_cast<uint64_t>(effective[op1Hi]) << 32) | effective[op1Lo];
                            uint64_t b = (static_cast<uint64_t>(effective[op2Hi]) << 32) | effective[op2Lo];
                            uint64_t expected = subtract ? (a - b) : (a + b);

                            invoke<void>(code);
                            CHECK_EQ((static_cast<uint64_t>(outHi) << 32) | outLo, expected);
                        }
                    }
                }
            }
        }
    }
}

void testArmv7CountZeros64RegisterPairs()
{
    uint32_t inHi = 0;
    uint32_t inLo = 0;
    uint32_t outHi = 0;
    uint32_t outLo = 0;
    uint32_t effective[16];

    for (auto trailing : { false, true }) {
        for (auto srcHi : { ARMRegisters::r0, ARMRegisters::r1 }) {
            for (auto srcLo : { ARMRegisters::r0, ARMRegisters::r1 }) {
                for (auto destHi : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2, ARMRegisters::r3 }) {
                    for (auto destLo : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2, ARMRegisters::r3 }) {
                        if (destHi == destLo)
                            continue;

                        auto code = compile([&] (CCallHelpers& jit) {
                            emitFunctionPrologue(jit);
                            jit.load32(&inHi, srcHi);
                            jit.load32(&inLo, srcLo);
                            if (trailing)
                                jit.countTrailingZeros64(srcHi, srcLo, destHi, destLo);
                            else
                                jit.countLeadingZeros64(srcHi, srcLo, destHi, destLo);
                            jit.store32(destLo, &outLo);
                            jit.store32(destHi, &outHi);
                            emitFunctionEpilogue(jit);
                            jit.ret();
                        });

                        for (auto hi : { 0u, 1u, 0x80000000u, 0xffffffffu, 0x00010000u }) {
                            for (auto lo : { 0u, 1u, 0x80000000u, 0xffffffffu, 0x00010000u }) {
                                inHi = hi;
                                inLo = lo;
                                outHi = 0;
                                outLo = 0;
                                effective[srcHi] = inHi;
                                effective[srcLo] = inLo;
                                uint32_t h = effective[srcHi];
                                uint32_t l = effective[srcLo];
                                uint32_t expected;
                                if (trailing) {
                                    uint32_t low = masmaluCtz32(l);
                                    expected = low != 32 ? low : 32 + masmaluCtz32(h);
                                } else {
                                    uint32_t high = masmaluClz32(h);
                                    expected = high != 32 ? high : 32 + masmaluClz32(l);
                                }
                                invoke<void>(code);
                                CHECK_EQ(outLo, expected);
                                CHECK_EQ(outHi, 0u);
                            }
                        }
                    }
                }
            }
        }
    }
}

void testArmv7AddUnsignedRightShift32()
{
    uint32_t in1 = 0;
    uint32_t in2 = 0;
    uint32_t out = 0;
    uint32_t effective[16];

    for (auto amount : { 0, 1, 5, 31, 32, 33, -1 }) {
        for (auto src1 : { ARMRegisters::r0, ARMRegisters::r1 }) {
            for (auto src2 : { ARMRegisters::r0, ARMRegisters::r1 }) {
                for (auto dest : { ARMRegisters::r0, ARMRegisters::r1, ARMRegisters::r2 }) {
                    auto code = compile([&] (CCallHelpers& jit) {
                        emitFunctionPrologue(jit);
                        jit.load32(&in1, src1);
                        jit.load32(&in2, src2);
                        jit.addUnsignedRightShift32(src1, src2, CCallHelpers::TrustedImm32(amount), dest);
                        jit.store32(dest, &out);
                        emitFunctionEpilogue(jit);
                        jit.ret();
                    });

                    for (auto value : int32Operands()) {
                        in1 = static_cast<uint32_t>(value);
                        in2 = 0xdeadbeefu;
                        out = 0;
                        effective[src1] = in1;
                        effective[src2] = in2;
                        uint32_t n = static_cast<uint32_t>(amount) & 31;
                        uint32_t expected = effective[src1] + (effective[src2] >> n);
                        invoke<void>(code);
                        CHECK_EQ(out, expected);
                    }
                }
            }
        }
    }
}

void testArmv7Add32Sub32MemoryOperands()
{
    uint32_t words[4];
    uint32_t out = 0;

    auto reset = [&] {
        for (unsigned i = 0; i < 4; ++i)
            words[i] = 0x11110000u * (i + 1);
    };

    for (auto dest : { ARMRegisters::r0, ARMRegisters::r1 }) {
        reset();
        auto addAbsolute = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(CCallHelpers::TrustedImm32(7), dest);
            jit.add32(CCallHelpers::AbsoluteAddress(&words[0]), dest);
            jit.add32(CCallHelpers::AbsoluteAddress(&words[2]), dest);
            jit.store32(dest, &out);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        out = 0;
        invoke<void>(addAbsolute);
        CHECK_EQ(out, 7u + 0x11110000u + 0x33330000u);

        reset();
        auto memoryImmediates = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.add32(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&words[0]));
            jit.add32(CCallHelpers::TrustedImm32(0x12345678), CCallHelpers::AbsoluteAddress(&words[1]));
            jit.sub32(CCallHelpers::TrustedImm32(1), CCallHelpers::AbsoluteAddress(&words[2]));
            jit.sub32(CCallHelpers::TrustedImm32(0x12345678), CCallHelpers::AbsoluteAddress(&words[3]));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        invoke<void>(memoryImmediates);
        CHECK_EQ(words[0], 0x11110000u + 1);
        CHECK_EQ(words[1], 0x22220000u + 0x12345678u);
        CHECK_EQ(words[2], 0x33330000u - 1);
        CHECK_EQ(words[3], 0x44440000u - 0x12345678u);
    }
}

#endif

static bool evaluateRelational32(MacroAssembler::RelationalCondition cond, int32_t a, int32_t b)
{
    switch (cond) {
    case MacroAssembler::Equal: return a == b;
    case MacroAssembler::NotEqual: return a != b;
    case MacroAssembler::Above: return static_cast<uint32_t>(a) > static_cast<uint32_t>(b);
    case MacroAssembler::AboveOrEqual: return static_cast<uint32_t>(a) >= static_cast<uint32_t>(b);
    case MacroAssembler::Below: return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
    case MacroAssembler::BelowOrEqual: return static_cast<uint32_t>(a) <= static_cast<uint32_t>(b);
    case MacroAssembler::GreaterThan: return a > b;
    case MacroAssembler::GreaterThanOrEqual: return a >= b;
    case MacroAssembler::LessThan: return a < b;
    case MacroAssembler::LessThanOrEqual: return a <= b;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return false;
}

static bool evaluateResult32(MacroAssembler::ResultCondition cond, int32_t result, bool carry, bool overflow)
{
    switch (cond) {
    case MacroAssembler::Zero: return !result;
    case MacroAssembler::NonZero: return !!result;
    case MacroAssembler::Signed: return result < 0;
    case MacroAssembler::PositiveOrZero: return result >= 0;
    case MacroAssembler::Carry: return carry;
    case MacroAssembler::Overflow: return overflow;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return false;
}

static Vector<MacroAssembler::RelationalCondition> masmbrRelationalConditions()
{
    return Vector<MacroAssembler::RelationalCondition> {
        MacroAssembler::Equal,
        MacroAssembler::NotEqual,
        MacroAssembler::Above,
        MacroAssembler::AboveOrEqual,
        MacroAssembler::Below,
        MacroAssembler::BelowOrEqual,
        MacroAssembler::GreaterThan,
        MacroAssembler::GreaterThanOrEqual,
        MacroAssembler::LessThan,
        MacroAssembler::LessThanOrEqual,
    };
}

static Vector<int32_t> masmbrBoundary32()
{
    return Vector<int32_t> {
        0, 1, -1, 2, -2, 0x7f, 0x80, 0xff, 0x100, 0x3ff, 0x400, 0xfff, 0x1000, 0x1001,
        -0xff, -0x100, -0x1000, 0x7ffffffe,
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::min(),
        static_cast<int32_t>(0x80000001u),
        static_cast<int32_t>(0xfffffffeu),
    };
}

void testBranch32RegReg()
{
    for (auto cond : masmbrRelationalConditions()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branch32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            for (auto b : masmbrBoundary32())
                CHECK_EQ(invoke<int>(test, a, b), evaluateRelational32(cond, a, b) ? 1 : 0);
            CHECK_EQ(invoke<int>(test, a, a), evaluateRelational32(cond, a, a) ? 1 : 0);
        }
    }

    for (auto cond : masmbrRelationalConditions()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branch32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32())
            CHECK_EQ(invoke<int>(test, a), evaluateRelational32(cond, a, a) ? 1 : 0);
    }
}

void testBranch32RegImm()
{
    for (auto cond : masmbrRelationalConditions()) {
        for (auto imm : masmbrBoundary32()) {
            auto test = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branch32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(imm));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32())
                CHECK_EQ(invoke<int>(test, a), evaluateRelational32(cond, a, imm) ? 1 : 0);
        }
    }
}

void testBranch32Address()
{
    for (auto cond : masmbrRelationalConditions()) {
        for (auto offset : { 0, 4, 0x100, 0xffc, 0x1000, 0x1004 }) {
            auto testAddrImm = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branch32(cond, CCallHelpers::Address(GPRInfo::argumentGPR0, offset), CCallHelpers::TrustedImm32(0x1234));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto testAddrReg = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branch32(cond, CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::argumentGPR1);
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            auto testRegAddr = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branch32(cond, GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            Vector<uint8_t> buffer(0x1010 + sizeof(int32_t));
            for (auto a : masmbrBoundary32()) {
                *std::bit_cast<int32_t*>(buffer.data() + offset) = a;
                CHECK_EQ(invoke<int>(testAddrImm, buffer.data()), evaluateRelational32(cond, a, 0x1234) ? 1 : 0);
                for (auto b : masmbrBoundary32()) {
                    CHECK_EQ(invoke<int>(testAddrReg, buffer.data(), b), evaluateRelational32(cond, a, b) ? 1 : 0);
                    CHECK_EQ(invoke<int>(testRegAddr, buffer.data(), b), evaluateRelational32(cond, b, a) ? 1 : 0);
                }
            }
        }
    }
}

void testBranch8And16()
{
    for (auto cond : masmbrRelationalConditions()) {
        for (auto imm : { 0, 1, 0x7f, 0x80, 0xfe, 0xff }) {
            auto test8 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branch8(cond, CCallHelpers::Address(GPRInfo::argumentGPR0, 3), CCallHelpers::TrustedImm32(imm));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            uint8_t buffer[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
            for (auto v : { 0, 1, 0x7f, 0x80, 0xfe, 0xff }) {
                buffer[3] = static_cast<uint8_t>(v);
                int32_t left = (cond == MacroAssembler::GreaterThan || cond == MacroAssembler::GreaterThanOrEqual
                    || cond == MacroAssembler::LessThan || cond == MacroAssembler::LessThanOrEqual)
                    ? static_cast<int32_t>(static_cast<int8_t>(v)) : static_cast<int32_t>(static_cast<uint8_t>(v));
                int32_t right = (cond == MacroAssembler::GreaterThan || cond == MacroAssembler::GreaterThanOrEqual
                    || cond == MacroAssembler::LessThan || cond == MacroAssembler::LessThanOrEqual)
                    ? static_cast<int32_t>(static_cast<int8_t>(imm)) : static_cast<int32_t>(static_cast<uint8_t>(imm));
                CHECK_EQ(invoke<int>(test8, buffer), evaluateRelational32(cond, left, right) ? 1 : 0);
            }
        }
    }

    for (auto cond : masmbrRelationalConditions()) {
        for (auto imm : { 0, 1, 0x7fff, 0x8000, 0xfffe, 0xffff }) {
            auto test16 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branch16(cond, CCallHelpers::Address(GPRInfo::argumentGPR0, 2), CCallHelpers::TrustedImm32(imm));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            uint16_t buffer[4] = { 0, 0, 0, 0 };
            for (auto v : { 0, 1, 0x7fff, 0x8000, 0xfffe, 0xffff }) {
                buffer[1] = static_cast<uint16_t>(v);
                bool isSigned = (cond == MacroAssembler::GreaterThan || cond == MacroAssembler::GreaterThanOrEqual
                    || cond == MacroAssembler::LessThan || cond == MacroAssembler::LessThanOrEqual);
                int32_t left = isSigned ? static_cast<int32_t>(static_cast<int16_t>(v)) : static_cast<int32_t>(static_cast<uint16_t>(v));
                int32_t right = isSigned ? static_cast<int32_t>(static_cast<int16_t>(imm)) : static_cast<int32_t>(static_cast<uint16_t>(imm));
                CHECK_EQ(invoke<int>(test16, buffer), evaluateRelational32(cond, left, right) ? 1 : 0);
            }
        }
    }
}

void testCompare32Setter()
{
    for (auto cond : masmbrRelationalConditions()) {
        auto testNoAlias = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(CCallHelpers::TrustedImm32(0x5a5a5a5a), GPRInfo::argumentGPR2);
            jit.compare32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
            jit.move(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsLeft = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsRight = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR1);
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testAllSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            CHECK_EQ(invoke<int>(testAllSame, a), evaluateRelational32(cond, a, a) ? 1 : 0);
            for (auto b : masmbrBoundary32()) {
                int expected = evaluateRelational32(cond, a, b) ? 1 : 0;
                CHECK_EQ(invoke<int>(testNoAlias, a, b), expected);
                CHECK_EQ(invoke<int>(testDestIsLeft, a, b), expected);
                CHECK_EQ(invoke<int>(testDestIsRight, a, b), expected);
            }
        }
    }
}

void testCompare32SetterImm()
{
    for (auto cond : masmbrRelationalConditions()) {
        for (auto imm : masmbrBoundary32()) {
            auto test = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.compare32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(imm), GPRInfo::argumentGPR0);
                jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32())
                CHECK_EQ(invoke<int>(test, a), evaluateRelational32(cond, a, imm) ? 1 : 0);
        }
    }
}

void testBranchTest32Masmbr()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero }) {
        auto testRegReg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchTest32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testSameReg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchTest32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            CHECK_EQ(invoke<int>(testSameReg, a), evaluateResult32(cond, a, false, false) ? 1 : 0);
            for (auto b : masmbrBoundary32())
                CHECK_EQ(invoke<int>(testRegReg, a, b), evaluateResult32(cond, a & b, false, false) ? 1 : 0);
        }

        for (auto mask : masmbrBoundary32()) {
            auto testRegImm = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branchTest32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(mask));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32())
                CHECK_EQ(invoke<int>(testRegImm, a), evaluateResult32(cond, a & mask, false, false) ? 1 : 0);
        }

        auto testDefaultMask = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchTest32(cond, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32())
            CHECK_EQ(invoke<int>(testDefaultMask, a), evaluateResult32(cond, a, false, false) ? 1 : 0);
    }
}

void testTest32Setter()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero }) {
        auto testRegReg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.test32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            for (auto b : masmbrBoundary32())
                CHECK_EQ(invoke<int>(testRegReg, a, b), evaluateResult32(cond, a & b, false, false) ? 1 : 0);
        }

        for (auto mask : masmbrBoundary32()) {
            auto testRegImm = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.test32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(mask), GPRInfo::argumentGPR1);
                jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32())
                CHECK_EQ(invoke<int>(testRegImm, a), evaluateResult32(cond, a & mask, false, false) ? 1 : 0);
        }
    }
}

void testBranchAdd32Masmbr()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero, MacroAssembler::Overflow }) {
        auto testThreeArg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchAdd32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsOp1 = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchAdd32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testAllSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchAdd32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            int64_t wideSame = static_cast<int64_t>(a) + static_cast<int64_t>(a);
            int32_t sumSame = static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(a));
            CHECK_EQ(invoke<int>(testAllSame, a), evaluateResult32(cond, sumSame, false, wideSame != sumSame) ? 1 : 0);

            for (auto b : masmbrBoundary32()) {
                int64_t wide = static_cast<int64_t>(a) + static_cast<int64_t>(b);
                int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
                int expected = evaluateResult32(cond, sum, false, wide != sum) ? 1 : 0;
                CHECK_EQ(invoke<int>(testThreeArg, a, b), expected);
                CHECK_EQ(invoke<int>(testDestIsOp1, a, b), expected);
            }
        }

        for (auto imm : masmbrBoundary32()) {
            auto testImm = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branchAdd32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(imm), GPRInfo::argumentGPR0);
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32()) {
                int64_t wide = static_cast<int64_t>(a) + static_cast<int64_t>(imm);
                int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(imm));
                CHECK_EQ(invoke<int>(testImm, a), evaluateResult32(cond, sum, false, wide != sum) ? 1 : 0);
            }
        }
    }
}

void testBranchAdd32Address()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero, MacroAssembler::Overflow }) {
        for (auto offset : { 0, 4, 0x100, 0xffc, 0x1000, 5000 }) {
            for (auto imm : { 1, -1, 0xff, 0x100, 0x1000, 5100, 0x12345 }) {
                auto test = compile([=] (CCallHelpers& jit) {
                    emitFunctionPrologue(jit);
                    auto taken = jit.branchAdd32(cond, CCallHelpers::TrustedImm32(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
                    jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                    emitFunctionEpilogue(jit);
                    jit.ret();
                    taken.link(&jit);
                    jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                    emitFunctionEpilogue(jit);
                    jit.ret();
                });

                Vector<uint8_t> buffer(6000 + sizeof(int32_t));
                for (auto a : masmbrBoundary32()) {
                    *std::bit_cast<int32_t*>(buffer.data() + offset) = a;
                    int64_t wide = static_cast<int64_t>(a) + static_cast<int64_t>(imm);
                    int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(imm));
                    CHECK_EQ(invoke<int>(test, buffer.data()), evaluateResult32(cond, sum, false, wide != sum) ? 1 : 0);
                    CHECK_EQ(*std::bit_cast<int32_t*>(buffer.data() + offset), sum);
                }
            }
        }
    }
}

void testBranchSub32Masmbr()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero, MacroAssembler::Overflow }) {
        auto testThreeArg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchSub32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testAllSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchSub32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            CHECK_EQ(invoke<int>(testAllSame, a), evaluateResult32(cond, 0, false, false) ? 1 : 0);
            for (auto b : masmbrBoundary32()) {
                int64_t wide = static_cast<int64_t>(a) - static_cast<int64_t>(b);
                int32_t diff = static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
                CHECK_EQ(invoke<int>(testThreeArg, a, b), evaluateResult32(cond, diff, false, wide != diff) ? 1 : 0);
            }
        }

        for (auto imm : masmbrBoundary32()) {
            auto testImm = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branchSub32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(imm), GPRInfo::argumentGPR0);
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32()) {
                int64_t wide = static_cast<int64_t>(a) - static_cast<int64_t>(imm);
                int32_t diff = static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(imm));
                CHECK_EQ(invoke<int>(testImm, a), evaluateResult32(cond, diff, false, wide != diff) ? 1 : 0);
            }
        }
    }
}

void testBranchMul32Masmbr()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero, MacroAssembler::Overflow }) {
        auto testThreeArg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchMul32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsSrc2 = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchMul32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR1);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testAllSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchMul32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            int64_t wideSame = static_cast<int64_t>(a) * static_cast<int64_t>(a);
            int32_t prodSame = static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(a));
            CHECK_EQ(invoke<int>(testAllSame, a), evaluateResult32(cond, prodSame, false, wideSame != prodSame) ? 1 : 0);

            for (auto b : masmbrBoundary32()) {
                int64_t wide = static_cast<int64_t>(a) * static_cast<int64_t>(b);
                int32_t prod = static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
                int expected = evaluateResult32(cond, prod, false, wide != prod) ? 1 : 0;
                CHECK_EQ(invoke<int>(testThreeArg, a, b), expected);
                CHECK_EQ(invoke<int>(testDestIsSrc2, a, b), expected);
            }
        }

        for (auto imm : masmbrBoundary32()) {
            auto testImm = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branchMul32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(imm), GPRInfo::argumentGPR1);
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32()) {
                int64_t wide = static_cast<int64_t>(a) * static_cast<int64_t>(imm);
                int32_t prod = static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(imm));
                CHECK_EQ(invoke<int>(testImm, a), evaluateResult32(cond, prod, false, wide != prod) ? 1 : 0);
            }
        }
    }
}

void testBranchNegAndOr32Masmbr()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero, MacroAssembler::Overflow }) {
        auto testNeg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchNeg32(cond, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testNegValue = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.branchNeg32(cond, GPRInfo::argumentGPR0).link(&jit);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            int64_t wide = -static_cast<int64_t>(a);
            int32_t neg = static_cast<int32_t>(0u - static_cast<uint32_t>(a));
            CHECK_EQ(invoke<int>(testNeg, a), evaluateResult32(cond, neg, false, wide != neg) ? 1 : 0);
            CHECK_EQ(invoke<int32_t>(testNegValue, a), neg);
        }
    }

    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero }) {
        auto testOr = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchOr32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testOrSame = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchOr32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            CHECK_EQ(invoke<int>(testOrSame, a), evaluateResult32(cond, a, false, false) ? 1 : 0);
            for (auto b : masmbrBoundary32())
                CHECK_EQ(invoke<int>(testOr, a, b), evaluateResult32(cond, a | b, false, false) ? 1 : 0);
        }
    }
}

void testMoveConditionally32Masmbr()
{
    for (auto cond : masmbrRelationalConditions()) {
        auto testNoAlias = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.moveConditionally32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testThenIsDest = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.moveConditionally32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2);
            jit.move(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testElseIsDest = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.moveConditionally32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::argumentGPR3);
            jit.move(GPRInfo::argumentGPR3, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testThenEqualsElse = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.moveConditionally32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            for (auto b : masmbrBoundary32()) {
                int32_t thenCase = 0x1234;
                int32_t elseCase = -0x4321;
                int32_t expected = evaluateRelational32(cond, a, b) ? thenCase : elseCase;
                CHECK_EQ(invoke<int32_t>(testNoAlias, a, b, thenCase, elseCase), expected);
                CHECK_EQ(invoke<int32_t>(testThenIsDest, a, b, thenCase, elseCase), expected);
                CHECK_EQ(invoke<int32_t>(testElseIsDest, a, b, thenCase, elseCase), expected);
                CHECK_EQ(invoke<int32_t>(testThenEqualsElse, a, b, thenCase, elseCase), thenCase);
            }
        }
    }
}

void testMoveConditionallyTest32Masmbr()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero }) {
        auto testNoAlias = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.moveConditionallyTest32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testThenIsDest = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.moveConditionallyTest32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2);
            jit.move(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testElseIsDest = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.moveConditionallyTest32(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, GPRInfo::argumentGPR3);
            jit.move(GPRInfo::argumentGPR3, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary32()) {
            for (auto b : masmbrBoundary32()) {
                int32_t thenCase = 0x1234;
                int32_t elseCase = -0x4321;
                int32_t expected = evaluateResult32(cond, a & b, false, false) ? thenCase : elseCase;
                CHECK_EQ(invoke<int32_t>(testNoAlias, a, b, thenCase, elseCase), expected);
                CHECK_EQ(invoke<int32_t>(testThenIsDest, a, b, thenCase, elseCase), expected);
                CHECK_EQ(invoke<int32_t>(testElseIsDest, a, b, thenCase, elseCase), expected);
            }
        }
    }
}

void testPatchableBranchMasmbr()
{
    for (auto cond : masmbrRelationalConditions()) {
        for (auto imm : { 0, 1, -1, 0xff, 0x100, 0x1000 }) {
            auto test = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.patchableBranch32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(imm));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.m_jump.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32())
                CHECK_EQ(invoke<int>(test, a), evaluateRelational32(cond, a, imm) ? 1 : 0);
        }
    }

    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero }) {
        for (auto mask : { -1, 0, 1, 0xff, 0x100, static_cast<int>(0x80000000u) }) {
            auto test = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.patchableBranchTest32(cond, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm32(mask));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.m_jump.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary32())
                CHECK_EQ(invoke<int>(test, a), evaluateResult32(cond, a & mask, false, false) ? 1 : 0);
        }
    }
}

#if CPU(ARM_THUMB2)
static bool evaluateRelational64(MacroAssembler::RelationalCondition cond, int64_t a, int64_t b)
{
    switch (cond) {
    case MacroAssembler::Equal: return a == b;
    case MacroAssembler::NotEqual: return a != b;
    case MacroAssembler::Above: return static_cast<uint64_t>(a) > static_cast<uint64_t>(b);
    case MacroAssembler::AboveOrEqual: return static_cast<uint64_t>(a) >= static_cast<uint64_t>(b);
    case MacroAssembler::Below: return static_cast<uint64_t>(a) < static_cast<uint64_t>(b);
    case MacroAssembler::BelowOrEqual: return static_cast<uint64_t>(a) <= static_cast<uint64_t>(b);
    case MacroAssembler::GreaterThan: return a > b;
    case MacroAssembler::GreaterThanOrEqual: return a >= b;
    case MacroAssembler::LessThan: return a < b;
    case MacroAssembler::LessThanOrEqual: return a <= b;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return false;
}

static Vector<int64_t> masmbrBoundary64()
{
    return Vector<int64_t> {
        0LL,
        1LL,
        -1LL,
        2LL,
        -2LL,
        static_cast<int64_t>(std::numeric_limits<int32_t>::max()),
        static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
        0x00000000ffffffffLL,
        0x0000000100000000LL,
        0x0000000100000001LL,
        0x00000001ffffffffLL,
        0x0000000200000000LL,
        0x7fffffff00000000LL,
        0x7fffffff00000001LL,
        0x7ffffffffffffffeLL,
        std::numeric_limits<int64_t>::max(),
        std::numeric_limits<int64_t>::min(),
        static_cast<int64_t>(0x8000000000000001ULL),
        static_cast<int64_t>(0xffffffff00000000ULL),
        static_cast<int64_t>(0xfffffffffffffffeULL),
    };
}

void testBranch64ARMv7()
{
    for (auto cond : masmbrRelationalConditions()) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branch64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testSameOperand = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branch64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary64()) {
            int32_t aLo = static_cast<int32_t>(a);
            int32_t aHi = static_cast<int32_t>(a >> 32);
            CHECK_EQ(invoke<int>(testSameOperand, aLo, aHi), evaluateRelational64(cond, a, a) ? 1 : 0);

            for (auto b : masmbrBoundary64()) {
                int32_t bLo = static_cast<int32_t>(b);
                int32_t bHi = static_cast<int32_t>(b >> 32);
                CHECK_EQ(invoke<int>(test, aLo, aHi, bLo, bHi), evaluateRelational64(cond, a, b) ? 1 : 0);
            }
        }
    }
}

void testBranch64ImmARMv7()
{
    for (auto cond : masmbrRelationalConditions()) {
        for (auto imm : masmbrBoundary64()) {
            auto test = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                auto taken = jit.branch64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, CCallHelpers::TrustedImm64(imm));
                jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
                taken.link(&jit);
                jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto a : masmbrBoundary64()) {
                int32_t aLo = static_cast<int32_t>(a);
                int32_t aHi = static_cast<int32_t>(a >> 32);
                CHECK_EQ(invoke<int>(test, aLo, aHi), evaluateRelational64(cond, a, imm) ? 1 : 0);
            }
        }
    }
}

void testCompare64ARMv7()
{
    for (auto cond : masmbrRelationalConditions()) {
        auto testNoAlias = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.push(GPRInfo::regT4);
            jit.move(CCallHelpers::TrustedImm32(0x5a5a5a5a), GPRInfo::regT4);
            jit.compare64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2, GPRInfo::regT4);
            jit.move(GPRInfo::regT4, GPRInfo::returnValueGPR);
            jit.pop(GPRInfo::regT4);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsLhsLo = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2, GPRInfo::argumentGPR0);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsLhsHi = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2, GPRInfo::argumentGPR1);
            jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsRhsLo = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2, GPRInfo::argumentGPR2);
            jit.move(GPRInfo::argumentGPR2, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testDestIsRhsHi = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR3, GPRInfo::argumentGPR2, GPRInfo::argumentGPR3);
            jit.move(GPRInfo::argumentGPR3, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testSameOperand = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.compare64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary64()) {
            int32_t aLo = static_cast<int32_t>(a);
            int32_t aHi = static_cast<int32_t>(a >> 32);
            CHECK_EQ(invoke<int32_t>(testSameOperand, aLo, aHi), evaluateRelational64(cond, a, a) ? 1 : 0);

            for (auto b : masmbrBoundary64()) {
                int32_t bLo = static_cast<int32_t>(b);
                int32_t bHi = static_cast<int32_t>(b >> 32);
                int32_t expected = evaluateRelational64(cond, a, b) ? 1 : 0;
                CHECK_EQ(invoke<int32_t>(testNoAlias, aLo, aHi, bLo, bHi), expected);
                CHECK_EQ(invoke<int32_t>(testDestIsLhsLo, aLo, aHi, bLo, bHi), expected);
                CHECK_EQ(invoke<int32_t>(testDestIsLhsHi, aLo, aHi, bLo, bHi), expected);
                CHECK_EQ(invoke<int32_t>(testDestIsRhsLo, aLo, aHi, bLo, bHi), expected);
                CHECK_EQ(invoke<int32_t>(testDestIsRhsHi, aLo, aHi, bLo, bHi), expected);
            }
        }
    }
}

void testBranchTest64ARMv7()
{
    for (auto cond : { MacroAssembler::Zero, MacroAssembler::NonZero, MacroAssembler::Signed, MacroAssembler::PositiveOrZero }) {
        auto test = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchTest64(cond, GPRInfo::argumentGPR1, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        auto testSameReg = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            auto taken = jit.branchTest64(cond, GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
            jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
            taken.link(&jit);
            jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto a : masmbrBoundary64()) {
            int32_t aLo = static_cast<int32_t>(a);
            int32_t aHi = static_cast<int32_t>(a >> 32);
            bool expected = false;
            switch (cond) {
            case MacroAssembler::Zero: expected = !a; break;
            case MacroAssembler::NonZero: expected = !!a; break;
            case MacroAssembler::Signed: expected = a < 0; break;
            case MacroAssembler::PositiveOrZero: expected = a >= 0; break;
            default: RELEASE_ASSERT_NOT_REACHED();
            }
            CHECK_EQ(invoke<int>(test, aLo, aHi), expected ? 1 : 0);

            bool expectedSame = false;
            int64_t doubled = (static_cast<int64_t>(aLo) << 32) | static_cast<uint32_t>(aLo);
            switch (cond) {
            case MacroAssembler::Zero: expectedSame = !doubled; break;
            case MacroAssembler::NonZero: expectedSame = !!doubled; break;
            case MacroAssembler::Signed: expectedSame = doubled < 0; break;
            case MacroAssembler::PositiveOrZero: expectedSame = doubled >= 0; break;
            default: RELEASE_ASSERT_NOT_REACHED();
            }
            CHECK_EQ(invoke<int>(testSameReg, aLo), expectedSame ? 1 : 0);
        }
    }
}
#endif

#if CPU(ARM_THUMB2)

static Vector<uint64_t> armv7DoubleBitPatterns()
{
    return Vector<uint64_t> {
        0x0000000000000000ull,
        0x8000000000000000ull,
        0x3ff0000000000000ull,
        0xbff0000000000000ull,
        0x0000000000000001ull,
        0x8000000000000001ull,
        0x000fffffffffffffull,
        0x0010000000000000ull,
        0x7fefffffffffffffull,
        0xffefffffffffffffull,
        0x7ff0000000000000ull,
        0xfff0000000000000ull,
        0x7ff8000000000000ull,
        0xfff8000000000000ull,
        0x7ff800000abcdef1ull,
        0xfff123456789abcdull,
        0x0123456789abcdefull,
        0xfedcba9876543210ull,
        0xffffffffffffffffull,
    };
}

static Vector<uint32_t> armv7FloatBitPatterns()
{
    return Vector<uint32_t> {
        0x00000000u,
        0x80000000u,
        0x3f800000u,
        0xbf800000u,
        0x00000001u,
        0x80000001u,
        0x007fffffu,
        0x00800000u,
        0x7f7fffffu,
        0xff7fffffu,
        0x7f800000u,
        0xff800000u,
        0x7fc00000u,
        0xffc00000u,
        0x7fc0abcdu,
        0x4b000001u,
        0x4f000000u,
        0xcf000000u,
    };
}

static Vector<double> armv7EdgeDoubles()
{
    return Vector<double> {
        0.0,
        -0.0,
        1.0,
        -1.0,
        0.5,
        -0.5,
        1.5,
        -1.5,
        2.5,
        -2.5,
        0.3,
        -0.3,
        std::numeric_limits<double>::denorm_min(),
        -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::min(),
        -std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
        -std::numeric_limits<double>::quiet_NaN(),
        std::bit_cast<double>(0x7ff800000abcdef1ull),
        2147483646.0,
        2147483647.0,
        2147483648.0,
        -2147483648.0,
        -2147483649.0,
        4294967295.0,
        4294967296.0,
        2147483647.5,
        -2147483648.5,
    };
}

static bool armv7IsNaN(double value)
{
    return value != value;
}

static bool armv7IsNegativeZero(double value)
{
    return std::bit_cast<uint64_t>(value) == 0x8000000000000000ull;
}

static int32_t armv7SaturatingToInt32(double value)
{
    if (armv7IsNaN(value))
        return 0;
    if (value >= 2147483647.0)
        return std::numeric_limits<int32_t>::max();
    if (value <= -2147483648.0)
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(value);
}

static uint32_t armv7SaturatingToUint32(double value)
{
    if (armv7IsNaN(value))
        return 0;
    if (value >= 4294967295.0)
        return std::numeric_limits<uint32_t>::max();
    if (value <= 0.0)
        return 0;
    return static_cast<uint32_t>(value);
}

static int32_t armv7NearestEvenToInt32(double value)
{
    if (armv7IsNaN(value))
        return 0;
    if (value >= 2147483647.0)
        return std::numeric_limits<int32_t>::max();
    if (value <= -2147483648.0)
        return std::numeric_limits<int32_t>::min();

    int32_t towardZero = static_cast<int32_t>(value);
    double truncated = static_cast<double>(towardZero);
    int32_t low = towardZero;
    if (value < truncated)
        low = towardZero - 1;
    double fraction = value - static_cast<double>(low);
    if (fraction > 0.5)
        return low + 1;
    if (fraction < 0.5)
        return low;
    return (low & 1) ? low + 1 : low;
}

static void armv7CheckDouble(uint64_t actualBits, double expected)
{
    double actual = std::bit_cast<double>(actualBits);
    if (armv7IsNaN(expected)) {
        CHECK_EQ(armv7IsNaN(actual), true);
        return;
    }
    CHECK_EQ(actualBits, std::bit_cast<uint64_t>(expected));
}

static void armv7CheckFloat(uint32_t actualBits, float expected)
{
    float actual = std::bit_cast<float>(actualBits);
    if (expected != expected) {
        CHECK_EQ(actual != actual, true);
        return;
    }
    CHECK_EQ(actualBits, std::bit_cast<uint32_t>(expected));
}

static void armv7StoreFloatToPointer(CCallHelpers& jit, FPRReg src, void* address)
{
    jit.move(CCallHelpers::TrustedImmPtr(address), GPRInfo::regT3);
    jit.storeFloat(src, CCallHelpers::Address(GPRInfo::regT3));
}

static int armv7ExpectedDoubleCondition(MacroAssembler::DoubleCondition condition, double a, double b)
{
    bool unordered = armv7IsNaN(a) || armv7IsNaN(b);
    switch (condition) {
    case MacroAssembler::DoubleEqualAndOrdered:
        return !unordered && (a == b);
    case MacroAssembler::DoubleNotEqualAndOrdered:
        return !unordered && (a != b);
    case MacroAssembler::DoubleGreaterThanAndOrdered:
        return !unordered && (a > b);
    case MacroAssembler::DoubleGreaterThanOrEqualAndOrdered:
        return !unordered && (a >= b);
    case MacroAssembler::DoubleLessThanAndOrdered:
        return !unordered && (a < b);
    case MacroAssembler::DoubleLessThanOrEqualAndOrdered:
        return !unordered && (a <= b);
    case MacroAssembler::DoubleEqualOrUnordered:
        return unordered || (a == b);
    case MacroAssembler::DoubleNotEqualOrUnordered:
        return unordered || (a != b);
    case MacroAssembler::DoubleGreaterThanOrUnordered:
        return unordered || (a > b);
    case MacroAssembler::DoubleGreaterThanOrEqualOrUnordered:
        return unordered || (a >= b);
    case MacroAssembler::DoubleLessThanOrUnordered:
        return unordered || (a < b);
    case MacroAssembler::DoubleLessThanOrEqualOrUnordered:
        return unordered || (a <= b);
    }
    RELEASE_ASSERT_NOT_REACHED();
}

void testARMv7FPMoveDoubleInts()
{
    uint64_t input = 0;
    uint64_t viaInts = 0;
    uint64_t via64 = 0;
    uint64_t hiInserted = 0;
    uint32_t lo = 0;
    uint32_t hi = 0;
    uint32_t hiOnly = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);

        jit.moveDoubleToInts(FPRInfo::fpRegT0, GPRInfo::regT0, GPRInfo::regT1);
        jit.store32(GPRInfo::regT0, &lo);
        jit.store32(GPRInfo::regT1, &hi);
        jit.moveIntsToDouble(GPRInfo::regT0, GPRInfo::regT1, FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&viaInts));

        jit.moveDoubleTo64(FPRInfo::fpRegT0, GPRInfo::regT2, GPRInfo::regT3);
        jit.move64ToDouble(GPRInfo::regT2, GPRInfo::regT3, FPRInfo::fpRegT2);
        jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::TrustedImmPtr(&via64));

        jit.moveDoubleHiTo32(FPRInfo::fpRegT0, GPRInfo::regT0);
        jit.store32(GPRInfo::regT0, &hiOnly);
        jit.moveZeroToDouble(FPRInfo::fpRegT3);
        jit.move32ToDoubleHi(GPRInfo::regT0, FPRInfo::fpRegT3);
        jit.storeDouble(FPRInfo::fpRegT3, CCallHelpers::TrustedImmPtr(&hiInserted));

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto bits : armv7DoubleBitPatterns()) {
        input = bits;
        invoke<void>(test);
        CHECK_EQ(lo, static_cast<uint32_t>(bits));
        CHECK_EQ(hi, static_cast<uint32_t>(bits >> 32));
        CHECK_EQ(viaInts, bits);
        CHECK_EQ(via64, bits);
        CHECK_EQ(hiOnly, static_cast<uint32_t>(bits >> 32));
        CHECK_EQ(hiInserted, (bits >> 32) << 32);
    }
}

void testARMv7FPMoveDoubleAliasing()
{
    uint64_t input = 0;
    uint64_t moved = 0;
    uint64_t movedOrNop = 0;
    uint64_t selfMoved = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.moveDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&moved));
        jit.moveDoubleOrNop(FPRInfo::fpRegT0, FPRInfo::fpRegT2);
        jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::TrustedImmPtr(&movedOrNop));
        jit.moveDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT0);
        jit.moveDoubleOrNop(FPRInfo::fpRegT0, FPRInfo::fpRegT0);
        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::TrustedImmPtr(&selfMoved));
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto bits : armv7DoubleBitPatterns()) {
        input = bits;
        invoke<void>(test);
        CHECK_EQ(moved, bits);
        CHECK_EQ(movedOrNop, bits);
        CHECK_EQ(selfMoved, bits);
    }
}

void testARMv7FPMoveFloatAndImmediates()
{
    for (auto bits : armv7FloatBitPatterns()) {
        uint32_t input = bits;
        uint32_t roundTrip = ~bits;
        uint32_t fromImm = ~bits;
        uint32_t zeroFloat = 0xdeadbeefu;

        auto test = compile([&, bits] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.loadFloat(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
            jit.moveFloatTo32(FPRInfo::fpRegT0, GPRInfo::regT0);
            jit.move32ToFloat(GPRInfo::regT0, FPRInfo::fpRegT1);
            armv7StoreFloatToPointer(jit, FPRInfo::fpRegT1, &roundTrip);

            jit.move32ToFloat(CCallHelpers::TrustedImm32(static_cast<int32_t>(bits)), FPRInfo::fpRegT2);
            armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &fromImm);

            jit.moveZeroToFloat(FPRInfo::fpRegT3);
            armv7StoreFloatToPointer(jit, FPRInfo::fpRegT3, &zeroFloat);

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        invoke<void>(test);
        CHECK_EQ(roundTrip, bits);
        CHECK_EQ(fromImm, bits);
        CHECK_EQ(zeroFloat, 0u);
    }
}

void testARMv7FPMove64ToDoubleImmediate()
{
    for (auto bits : armv7DoubleBitPatterns()) {
        uint64_t result = ~bits;
        uint64_t zeroDouble = 0xffffffffffffffffull;

        auto test = compile([&, bits] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move64ToDouble(CCallHelpers::TrustedImm64(static_cast<int64_t>(bits)), FPRInfo::fpRegT0);
            jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::TrustedImmPtr(&result));
            jit.moveZeroToDouble(FPRInfo::fpRegT1);
            jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&zeroDouble));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        invoke<void>(test);
        CHECK_EQ(result, bits);
        CHECK_EQ(zeroDouble, 0ull);
    }
}

void testARMv7FPLoadStoreOffsets()
{
    Vector<uint64_t> storage(0x8000);
    uint8_t* buffer = std::bit_cast<uint8_t*>(storage.data());
    uint8_t* base = buffer + 0x20000;

    for (int32_t offset : { 0, 4, 8, 1016, 1020, 1024, 2048, -4, -1020, -1024, -2048, 0x12348, -0x12348 }) {
        for (int32_t skew : { 0, 2 }) {
            uint8_t* skewedBase = base - skew;
            int32_t skewedOffset = offset + skew;
            double source = 1.0 + offset;
            uint32_t floatSource = 0x40490fdbu;
            uint64_t loaded = 0;
            uint32_t loadedFloat = 0;

            auto test = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.move(CCallHelpers::TrustedImmPtr(skewedBase), GPRInfo::regT0);

                jit.loadDouble(CCallHelpers::TrustedImmPtr(&source), FPRInfo::fpRegT0);
                jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::Address(GPRInfo::regT0, skewedOffset));
                jit.loadDouble(CCallHelpers::Address(GPRInfo::regT0, skewedOffset), FPRInfo::fpRegT1);
                jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&loaded));

                jit.move(CCallHelpers::TrustedImmPtr(skewedBase), GPRInfo::regT0);
                jit.loadFloat(CCallHelpers::TrustedImmPtr(&floatSource), FPRInfo::fpRegT2);
                jit.storeFloat(FPRInfo::fpRegT2, CCallHelpers::Address(GPRInfo::regT0, skewedOffset));
                jit.loadFloat(CCallHelpers::Address(GPRInfo::regT0, skewedOffset), FPRInfo::fpRegT3);
                armv7StoreFloatToPointer(jit, FPRInfo::fpRegT3, &loadedFloat);

                emitFunctionEpilogue(jit);
                jit.ret();
            });

            invoke<void>(test);
            CHECK_EQ(loaded, std::bit_cast<uint64_t>(source));
            CHECK_EQ(loadedFloat, floatSource);
        }
    }
}

void testARMv7FPLoadStoreBaseIndex()
{
    Vector<uint64_t> storage(0x400);
    uint8_t* base = std::bit_cast<uint8_t*>(storage.data());

    for (auto scale : { CCallHelpers::TimesOne, CCallHelpers::TimesTwo, CCallHelpers::TimesFour, CCallHelpers::TimesEight }) {
        for (int32_t offset : { 0, 8, 1024 }) {
            int32_t index = 8;
            double source = 12345.678;
            uint32_t floatSource = 0x40490fdbu;
            uint64_t loaded = 0;
            uint32_t loadedFloat = 0;

            auto test = compile([&] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.move(CCallHelpers::TrustedImmPtr(base), GPRInfo::regT0);
                jit.move(CCallHelpers::TrustedImm32(index), GPRInfo::regT1);

                jit.loadDouble(CCallHelpers::TrustedImmPtr(&source), FPRInfo::fpRegT0);
                jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::BaseIndex(GPRInfo::regT0, GPRInfo::regT1, scale, offset));
                jit.loadDouble(CCallHelpers::BaseIndex(GPRInfo::regT0, GPRInfo::regT1, scale, offset), FPRInfo::fpRegT1);
                jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&loaded));

                jit.move(CCallHelpers::TrustedImmPtr(base), GPRInfo::regT0);
                jit.move(CCallHelpers::TrustedImm32(index), GPRInfo::regT1);
                jit.loadFloat(CCallHelpers::TrustedImmPtr(&floatSource), FPRInfo::fpRegT2);
                jit.storeFloat(FPRInfo::fpRegT2, CCallHelpers::BaseIndex(GPRInfo::regT0, GPRInfo::regT1, scale, offset + 8));
                jit.loadFloat(CCallHelpers::BaseIndex(GPRInfo::regT0, GPRInfo::regT1, scale, offset + 8), FPRInfo::fpRegT3);
                armv7StoreFloatToPointer(jit, FPRInfo::fpRegT3, &loadedFloat);

                emitFunctionEpilogue(jit);
                jit.ret();
            });

            invoke<void>(test);
            CHECK_EQ(loaded, std::bit_cast<uint64_t>(source));
            CHECK_EQ(loadedFloat, floatSource);

            uint8_t* slot = base + (index << static_cast<int32_t>(scale)) + offset;
            CHECK_EQ(*std::bit_cast<uint64_t*>(slot), std::bit_cast<uint64_t>(source));
            CHECK_EQ(*std::bit_cast<uint32_t*>(slot + 8), floatSource);
        }
    }
}

void testARMv7BranchDoubleEdges(MacroAssembler::DoubleCondition condition)
{
    double arg1 = 0;
    double arg2 = 0;

    auto branchTest = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto withZeroTest = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchDoubleWithZero(condition, FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto sameRegisterTest = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchDouble(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto operands = armv7EdgeDoubles();
    for (auto a : operands) {
        arg1 = a;
        arg2 = 0;
        CHECK_EQ(invoke<int>(withZeroTest), armv7ExpectedDoubleCondition(condition, a, 0.0));
        CHECK_EQ(invoke<int>(sameRegisterTest), armv7ExpectedDoubleCondition(condition, a, a));
        for (auto b : operands) {
            arg1 = a;
            arg2 = b;
            CHECK_EQ(invoke<int>(branchTest), armv7ExpectedDoubleCondition(condition, a, b));
        }
    }
}

void testARMv7BranchFloatEdges(MacroAssembler::DoubleCondition condition)
{
    uint32_t arg1 = 0;
    uint32_t arg2 = 0;

    auto branchTest = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchFloat(condition, FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto withZeroTest = compile([&, condition] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchFloatWithZero(condition, FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto operands = armv7FloatBitPatterns();
    for (auto a : operands) {
        arg1 = a;
        arg2 = 0;
        double aValue = static_cast<double>(std::bit_cast<float>(a));
        CHECK_EQ(invoke<int>(withZeroTest), armv7ExpectedDoubleCondition(condition, aValue, 0.0));
        for (auto b : operands) {
            arg1 = a;
            arg2 = b;
            double bValue = static_cast<double>(std::bit_cast<float>(b));
            CHECK_EQ(invoke<int>(branchTest), armv7ExpectedDoubleCondition(condition, aValue, bValue));
        }
    }
}

void testARMv7BranchDoubleNonZeroAndZeroOrNaN()
{
    double input = 0;

    auto nonZeroTest = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchDoubleNonZero(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto zeroOrNaNTest = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::returnValueGPR);
        auto jump = jit.branchDoubleZeroOrNaN(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::returnValueGPR);
        jump.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : armv7EdgeDoubles()) {
        input = value;
        CHECK_EQ(invoke<int>(nonZeroTest), !armv7IsNaN(value) && value != 0.0);
        CHECK_EQ(invoke<int>(zeroOrNaNTest), armv7IsNaN(value) || value == 0.0);
    }
}

void testARMv7TruncateDoubleAndFloatToInt32()
{
    double input = 0;
    int32_t signedResult = 0;
    uint32_t unsignedResult = 0;
    int32_t floatSigned = 0;
    uint32_t floatUnsigned = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);

        jit.truncateDoubleToInt32(FPRInfo::fpRegT0, GPRInfo::regT0);
        jit.store32(GPRInfo::regT0, &signedResult);
        jit.truncateDoubleToUint32(FPRInfo::fpRegT0, GPRInfo::regT0);
        jit.store32(GPRInfo::regT0, &unsignedResult);

        jit.convertDoubleToFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.truncateFloatToInt32(FPRInfo::fpRegT1, GPRInfo::regT0);
        jit.store32(GPRInfo::regT0, &floatSigned);
        jit.truncateFloatToUint32(FPRInfo::fpRegT1, GPRInfo::regT0);
        jit.store32(GPRInfo::regT0, &floatUnsigned);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : armv7EdgeDoubles()) {
        input = value;
        invoke<void>(test);
        CHECK_EQ(signedResult, armv7SaturatingToInt32(value));
        CHECK_EQ(unsignedResult, armv7SaturatingToUint32(value));
        double asFloat = static_cast<double>(static_cast<float>(value));
        CHECK_EQ(floatSigned, armv7SaturatingToInt32(asFloat));
        CHECK_EQ(floatUnsigned, armv7SaturatingToUint32(asFloat));
    }
}

void testARMv7BranchTruncateDoubleToInt32()
{
    double input = 0;
    int32_t dest = 0;

    auto failedTest = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::regT2);
        auto failure = jit.branchTruncateDoubleToInt32(FPRInfo::fpRegT0, GPRInfo::regT1, CCallHelpers::BranchIfTruncateFailed);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::regT2);
        failure.link(&jit);
        jit.store32(GPRInfo::regT1, &dest);
        jit.move(GPRInfo::regT2, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto successfulTest = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::regT2);
        auto success = jit.branchTruncateDoubleToInt32(FPRInfo::fpRegT0, GPRInfo::regT1, CCallHelpers::BranchIfTruncateSuccessful);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::regT2);
        success.link(&jit);
        jit.store32(GPRInfo::regT1, &dest);
        jit.move(GPRInfo::regT2, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : armv7EdgeDoubles()) {
        input = value;
        int32_t truncated = armv7SaturatingToInt32(value);
        int reportedFailure = truncated == 0
            || truncated == -1
            || truncated == std::numeric_limits<int32_t>::max()
            || truncated == std::numeric_limits<int32_t>::min();

        dest = 0x5a5a5a5a;
        CHECK_EQ(invoke<int>(failedTest), reportedFailure);
        CHECK_EQ(dest, truncated);

        dest = 0x5a5a5a5a;
        CHECK_EQ(invoke<int>(successfulTest), reportedFailure);
        CHECK_EQ(dest, truncated);
    }
}

void testARMv7BranchConvertDoubleToInt32(bool negZeroCheck)
{
    double input = 0;
    int32_t dest = 0;

    auto test = compile([&, negZeroCheck] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(1), GPRInfo::regT2);
        CCallHelpers::JumpList failureCases;
        jit.branchConvertDoubleToInt32(FPRInfo::fpRegT0, GPRInfo::regT1, failureCases, FPRInfo::fpRegT3, negZeroCheck);
        jit.store32(GPRInfo::regT1, &dest);
        auto done = jit.jump();
        failureCases.link(&jit);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::regT2);
        done.link(&jit);
        jit.move(GPRInfo::regT2, GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : armv7EdgeDoubles()) {
        input = value;
        dest = 0x5a5a5a5a;

        bool exact = !armv7IsNaN(value)
            && value >= -2147483648.0
            && value <= 2147483647.0
            && static_cast<double>(static_cast<int32_t>(value)) == value;
        int expected = exact && !(negZeroCheck && armv7IsNegativeZero(value));

        CHECK_EQ(invoke<int>(test), expected);
        if (expected)
            CHECK_EQ(dest, static_cast<int32_t>(value));
    }
}

void testARMv7ConvertInt32AndUint32ToFloatingPoint()
{
    int32_t input = 0;
    uint64_t signedDouble = 0;
    uint64_t unsignedDouble = 0;
    uint32_t signedFloat = 0;
    uint32_t unsignedFloat = 0;
    uint64_t fromAddress = 0;
    uint64_t fromAbsolute = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImmPtr(&input), GPRInfo::regT0);
        jit.load32(CCallHelpers::Address(GPRInfo::regT0), GPRInfo::regT1);

        jit.convertInt32ToDouble(GPRInfo::regT1, FPRInfo::fpRegT0);
        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::TrustedImmPtr(&signedDouble));
        jit.convertUInt32ToDouble(GPRInfo::regT1, FPRInfo::fpRegT0);
        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::TrustedImmPtr(&unsignedDouble));

        jit.convertInt32ToFloat(GPRInfo::regT1, FPRInfo::fpRegT0);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT0, &signedFloat);
        jit.convertUInt32ToFloat(GPRInfo::regT1, FPRInfo::fpRegT0);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT0, &unsignedFloat);

        jit.move(CCallHelpers::TrustedImmPtr(&input), GPRInfo::regT0);
        jit.convertInt32ToDouble(CCallHelpers::Address(GPRInfo::regT0), FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&fromAddress));
        jit.convertInt32ToDouble(CCallHelpers::AbsoluteAddress(&input), FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&fromAbsolute));

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    Vector<int32_t> values = int32Operands();
    values.append(16777217);
    values.append(-16777217);
    values.append(0x40000001);
    values.append(static_cast<int32_t>(0x80000001u));
    values.append(static_cast<int32_t>(0xffffff81u));
    values.append(static_cast<int32_t>(0x7fffff81u));

    for (auto value : values) {
        input = value;
        invoke<void>(test);
        armv7CheckDouble(signedDouble, static_cast<double>(value));
        armv7CheckDouble(unsignedDouble, static_cast<double>(static_cast<uint32_t>(value)));
        armv7CheckFloat(signedFloat, static_cast<float>(value));
        armv7CheckFloat(unsignedFloat, static_cast<float>(static_cast<uint32_t>(value)));
        armv7CheckDouble(fromAddress, static_cast<double>(value));
        armv7CheckDouble(fromAbsolute, static_cast<double>(value));
    }
}

void testARMv7ConvertInt32ImmediateToDouble()
{
    for (auto value : int32Operands()) {
        uint64_t result = 0xffffffffffffffffull;

        auto test = compile([&, value] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.convertInt32ToDouble(CCallHelpers::TrustedImm32(value), FPRInfo::fpRegT0);
            jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::TrustedImmPtr(&result));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        invoke<void>(test);
        armv7CheckDouble(result, static_cast<double>(value));
    }
}

void testARMv7ConvertFloatDouble()
{
    uint32_t floatInput = 0;
    double doubleInput = 0;
    uint64_t toDouble = 0;
    uint64_t toDoubleAliased = 0;
    uint32_t toFloat = 0;
    uint32_t toFloatAliased = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&floatInput), FPRInfo::fpRegT0);
        jit.convertFloatToDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&toDouble));
        jit.convertFloatToDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT0);
        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::TrustedImmPtr(&toDoubleAliased));

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&doubleInput), FPRInfo::fpRegT2);
        jit.convertDoubleToFloat(FPRInfo::fpRegT2, FPRInfo::fpRegT1);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT1, &toFloat);
        jit.convertDoubleToFloat(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &toFloatAliased);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto floats = armv7FloatBitPatterns();
    auto doubles = armv7EdgeDoubles();
    for (auto bits : floats) {
        for (auto value : doubles) {
            floatInput = bits;
            doubleInput = value;
            invoke<void>(test);

            float asFloat = std::bit_cast<float>(bits);
            armv7CheckDouble(toDouble, static_cast<double>(asFloat));
            CHECK_EQ(toDoubleAliased, toDouble);
            armv7CheckFloat(toFloat, static_cast<float>(value));
            CHECK_EQ(toFloatAliased, toFloat);
        }
    }
}

static double armv7ApplyBinary(int operation, double left, double right)
{
    switch (operation) {
    case 0:
        return left + right;
    case 1:
        return left - right;
    case 2:
        return left * right;
    default:
        return left / right;
    }
}

void testARMv7DoubleBinaryAliasing()
{
    double lhs = 0;
    double rhs = 0;
    uint64_t result = 0;

    for (int operation = 0; operation < 4; ++operation) {
        for (int aliasing = 0; aliasing < 4; ++aliasing) {
            auto test = compile([&, operation, aliasing] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);

                FPRReg a = FPRInfo::fpRegT0;
                FPRReg b = FPRInfo::fpRegT1;
                FPRReg dest = FPRInfo::fpRegT2;
                if (aliasing == 1)
                    dest = a;
                else if (aliasing == 2)
                    dest = b;
                else if (aliasing == 3) {
                    b = a;
                    dest = a;
                }

                jit.loadDouble(CCallHelpers::TrustedImmPtr(&lhs), a);
                jit.loadDouble(CCallHelpers::TrustedImmPtr(&rhs), b);

                switch (operation) {
                case 0:
                    jit.addDouble(a, b, dest);
                    break;
                case 1:
                    jit.subDouble(a, b, dest);
                    break;
                case 2:
                    jit.mulDouble(a, b, dest);
                    break;
                default:
                    jit.divDouble(a, b, dest);
                    break;
                }

                jit.storeDouble(dest, CCallHelpers::TrustedImmPtr(&result));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            for (auto x : armv7EdgeDoubles()) {
                for (auto y : armv7EdgeDoubles()) {
                    lhs = x;
                    rhs = y;
                    invoke<void>(test);
                    armv7CheckDouble(result, armv7ApplyBinary(operation, (aliasing == 3) ? y : x, y));
                }
            }
        }
    }
}

void testARMv7DoubleAccumulateForms()
{
    double lhs = 0;
    double rhs = 0;
    uint64_t registerForm = 0;
    uint64_t selfForm = 0;
    uint64_t addressForm = 0;
    uint64_t absoluteForm = 0;

    for (int operation = 0; operation < 4; ++operation) {
        auto test = compile([&, operation] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);

            jit.loadDouble(CCallHelpers::TrustedImmPtr(&lhs), FPRInfo::fpRegT0);
            jit.loadDouble(CCallHelpers::TrustedImmPtr(&rhs), FPRInfo::fpRegT1);
            switch (operation) {
            case 0:
                jit.addDouble(FPRInfo::fpRegT1, FPRInfo::fpRegT0);
                break;
            case 1:
                jit.subDouble(FPRInfo::fpRegT1, FPRInfo::fpRegT0);
                break;
            case 2:
                jit.mulDouble(FPRInfo::fpRegT1, FPRInfo::fpRegT0);
                break;
            default:
                jit.divDouble(FPRInfo::fpRegT1, FPRInfo::fpRegT0);
                break;
            }
            jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::TrustedImmPtr(&registerForm));

            jit.loadDouble(CCallHelpers::TrustedImmPtr(&lhs), FPRInfo::fpRegT2);
            switch (operation) {
            case 0:
                jit.addDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
                break;
            case 1:
                jit.subDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
                break;
            case 2:
                jit.mulDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
                break;
            default:
                jit.divDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
                break;
            }
            jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::TrustedImmPtr(&selfForm));

            jit.move(CCallHelpers::TrustedImmPtr(&rhs), GPRInfo::regT0);
            jit.loadDouble(CCallHelpers::TrustedImmPtr(&lhs), FPRInfo::fpRegT3);
            switch (operation) {
            case 0:
                jit.addDouble(CCallHelpers::Address(GPRInfo::regT0), FPRInfo::fpRegT3);
                break;
            case 1:
                jit.subDouble(CCallHelpers::Address(GPRInfo::regT0), FPRInfo::fpRegT3);
                break;
            default:
                jit.mulDouble(CCallHelpers::Address(GPRInfo::regT0), FPRInfo::fpRegT3);
                break;
            }
            jit.storeDouble(FPRInfo::fpRegT3, CCallHelpers::TrustedImmPtr(&addressForm));

            jit.loadDouble(CCallHelpers::TrustedImmPtr(&lhs), FPRInfo::fpRegT3);
            jit.addDouble(CCallHelpers::AbsoluteAddress(&rhs), FPRInfo::fpRegT3);
            jit.storeDouble(FPRInfo::fpRegT3, CCallHelpers::TrustedImmPtr(&absoluteForm));

            emitFunctionEpilogue(jit);
            jit.ret();
        });

        for (auto x : armv7EdgeDoubles()) {
            for (auto y : armv7EdgeDoubles()) {
                lhs = x;
                rhs = y;
                invoke<void>(test);
                armv7CheckDouble(registerForm, armv7ApplyBinary(operation, x, y));
                armv7CheckDouble(selfForm, armv7ApplyBinary(operation, x, x));
                armv7CheckDouble(addressForm, armv7ApplyBinary(operation > 2 ? 2 : operation, x, y));
                armv7CheckDouble(absoluteForm, x + y);
            }
        }
    }
}

void testARMv7DoubleUnaryAliasing()
{
    double input = 0;
    uint64_t sqrtSeparate = 0;
    uint64_t sqrtAliased = 0;
    uint64_t absSeparate = 0;
    uint64_t absAliased = 0;
    uint64_t negSeparate = 0;
    uint64_t negAliased = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.sqrtDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&sqrtSeparate));
        jit.absDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&absSeparate));
        jit.negateDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&negSeparate));

        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT2);
        jit.sqrtDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
        jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::TrustedImmPtr(&sqrtAliased));
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT2);
        jit.absDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
        jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::TrustedImmPtr(&absAliased));
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT2);
        jit.negateDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
        jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::TrustedImmPtr(&negAliased));

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : armv7EdgeDoubles()) {
        input = value;
        invoke<void>(test);

        uint64_t bits = std::bit_cast<uint64_t>(value);
        CHECK_EQ(absSeparate, bits & 0x7fffffffffffffffull);
        CHECK_EQ(absAliased, bits & 0x7fffffffffffffffull);
        CHECK_EQ(negSeparate, bits ^ 0x8000000000000000ull);
        CHECK_EQ(negAliased, bits ^ 0x8000000000000000ull);
        CHECK_EQ(sqrtAliased, sqrtSeparate);

        if (armv7IsNaN(value) || value < 0.0)
            CHECK_EQ(armv7IsNaN(std::bit_cast<double>(sqrtSeparate)), true);
        else if (value == 0.0)
            CHECK_EQ(sqrtSeparate, bits);
    }
}

void testARMv7FloatArithmeticIsSinglePrecision()
{
    uint32_t lhsBits = 0;
    uint32_t rhsBits = 0;
    uint32_t addResult = 0;
    uint32_t subResult = 0;
    uint32_t mulResult = 0;
    uint32_t divResult = 0;
    uint32_t sqrtResult = 0;
    uint32_t absResult = 0;
    uint32_t negResult = 0;
    uint32_t addAliased = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&lhsBits), FPRInfo::fpRegT0);
        jit.loadFloat(CCallHelpers::TrustedImmPtr(&rhsBits), FPRInfo::fpRegT1);

        jit.addFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &addResult);
        jit.subFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &subResult);
        jit.mulFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &mulResult);
        jit.divFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &divResult);

        jit.sqrtFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &sqrtResult);
        jit.absFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &absResult);
        jit.negateFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &negResult);

        jit.loadFloat(CCallHelpers::TrustedImmPtr(&lhsBits), FPRInfo::fpRegT2);
        jit.addFloat(FPRInfo::fpRegT2, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        armv7StoreFloatToPointer(jit, FPRInfo::fpRegT2, &addAliased);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto operands = armv7FloatBitPatterns();
    for (auto a : operands) {
        for (auto b : operands) {
            lhsBits = a;
            rhsBits = b;
            invoke<void>(test);

            float x = std::bit_cast<float>(a);
            float y = std::bit_cast<float>(b);
            armv7CheckFloat(addResult, x + y);
            armv7CheckFloat(subResult, x - y);
            armv7CheckFloat(mulResult, x * y);
            armv7CheckFloat(divResult, x / y);
            CHECK_EQ(absResult, a & 0x7fffffffu);
            CHECK_EQ(negResult, a ^ 0x80000000u);
            CHECK_EQ(addAliased, addResult);
            if (x != x || x < 0.0f)
                CHECK_EQ(std::bit_cast<float>(sqrtResult) != std::bit_cast<float>(sqrtResult), true);
        }
    }

    lhsBits = 0x3f800000u;
    rhsBits = 0x33000000u;
    invoke<void>(test);
    CHECK_EQ(addResult, 0x3f800000u);
    CHECK_EQ(mulResult, 0x33000000u);

    lhsBits = 0x7f7fffffu;
    rhsBits = 0x40000000u;
    invoke<void>(test);
    CHECK_EQ(mulResult, 0x7f800000u);
}

void testARMv7RoundTowardNearestIntDouble()
{
    double input = 0;
    uint64_t result = 0;
    uint64_t aliased = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.roundTowardNearestIntDouble(FPRInfo::fpRegT0, FPRInfo::fpRegT1);
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::TrustedImmPtr(&result));
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT2);
        jit.roundTowardNearestIntDouble(FPRInfo::fpRegT2, FPRInfo::fpRegT2);
        jit.storeDouble(FPRInfo::fpRegT2, CCallHelpers::TrustedImmPtr(&aliased));
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : armv7EdgeDoubles()) {
        input = value;
        invoke<void>(test);
        double expected = static_cast<double>(armv7NearestEvenToInt32(value));
        CHECK_EQ(std::bit_cast<double>(result), expected);
        CHECK_EQ(aliased, result);
    }

    for (auto value : { 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 3.5, -3.5, 254.5, 255.5, 0.49999999999999994, 1.0000000000000002 }) {
        input = value;
        invoke<void>(test);
        CHECK_EQ(std::bit_cast<double>(result), static_cast<double>(armv7NearestEvenToInt32(value)));
    }
}

void testARMv7TruncateDoubleToInt64()
{
    double input = 0;
    uint32_t lo = 0;
    uint32_t hi = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.truncateDoubleToInt64(FPRInfo::fpRegT0, GPRInfo::regT0, GPRInfo::regT1, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        jit.store32(GPRInfo::regT0, &lo);
        jit.store32(GPRInfo::regT1, &hi);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto floatTest = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.convertDoubleToFloat(FPRInfo::fpRegT0, FPRInfo::fpRegT3);
        jit.truncateFloatToInt64(FPRInfo::fpRegT3, GPRInfo::regT0, GPRInfo::regT1, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        jit.store32(GPRInfo::regT0, &lo);
        jit.store32(GPRInfo::regT1, &hi);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : { 0.0, -0.0, 1.0, -1.0, 0.9, -0.9, 1.5, -1.5, 42.0, -42.0,
        2147483647.0, -2147483648.0, 2147483648.0, -2147483649.0,
        4294967295.0, 4294967296.0, -4294967296.0, -4294967297.0,
        4503599627370496.0, -4503599627370496.0, 1099511627776.0, -1099511627776.0 }) {
        input = value;
        invoke<void>(test);
        uint64_t actual = (static_cast<uint64_t>(hi) << 32) | lo;
        CHECK_EQ(actual, static_cast<uint64_t>(static_cast<int64_t>(value)));

        float asFloat = static_cast<float>(value);
        invoke<void>(floatTest);
        actual = (static_cast<uint64_t>(hi) << 32) | lo;
        CHECK_EQ(actual, static_cast<uint64_t>(static_cast<int64_t>(asFloat)));
    }
}

void testARMv7TruncateDoubleToUint64()
{
    double input = 0;
    uint32_t lo = 0;
    uint32_t hi = 0;

    auto test = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&input), FPRInfo::fpRegT0);
        jit.truncateDoubleToUint64(FPRInfo::fpRegT0, GPRInfo::regT0, GPRInfo::regT1, FPRInfo::fpRegT1, FPRInfo::fpRegT2);
        jit.store32(GPRInfo::regT0, &lo);
        jit.store32(GPRInfo::regT1, &hi);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto value : { 0.0, -0.0, 1.0, 0.9, 42.0, 4294967295.0, 4294967296.0, 4294967297.0,
        4503599627370496.0, 1099511627776.0, 18446744073709549568.0, 9223372036854775808.0 }) {
        input = value;
        invoke<void>(test);
        uint64_t actual = (static_cast<uint64_t>(hi) << 32) | lo;
        CHECK_EQ(actual, static_cast<uint64_t>(value));
    }

    for (auto value : { -0.0, -1.0, -42.0, -4294967296.0, std::numeric_limits<double>::quiet_NaN(),
        -std::numeric_limits<double>::infinity() }) {
        input = value;
        invoke<void>(test);
        uint64_t actual = (static_cast<uint64_t>(hi) << 32) | lo;
        CHECK_EQ(actual, 0ull);
    }
}

void testARMv7TransferDouble()
{
    Vector<uint64_t> storage(16);
    uint8_t* base = std::bit_cast<uint8_t*>(storage.data());

    for (auto bits : armv7DoubleBitPatterns()) {
        *std::bit_cast<uint64_t*>(base) = bits;
        *std::bit_cast<uint64_t*>(base + 8) = 0;
        *std::bit_cast<uint32_t*>(base + 16) = static_cast<uint32_t>(bits);
        *std::bit_cast<uint32_t*>(base + 20) = 0;

        auto test = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(CCallHelpers::TrustedImmPtr(base), GPRInfo::regT0);
            jit.transferDouble(CCallHelpers::Address(GPRInfo::regT0, 0), CCallHelpers::Address(GPRInfo::regT0, 8));
            jit.transferDouble(CCallHelpers::Address(GPRInfo::regT0, 8), CCallHelpers::Address(GPRInfo::regT0, 8));
            jit.transferFloat(CCallHelpers::Address(GPRInfo::regT0, 16), CCallHelpers::Address(GPRInfo::regT0, 20));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        invoke<void>(test);
        CHECK_EQ(*std::bit_cast<uint64_t*>(base + 8), bits);
        CHECK_EQ(*std::bit_cast<uint32_t*>(base + 20), static_cast<uint32_t>(bits));
    }
}

#endif

#if CPU(ARM_THUMB2)

void testARMv7Load32StoreOffsetBoundaries()
{
    constexpr int words = 5000;
    constexpr int mid = 2500;
    static uint32_t buffer[words];

    auto initBuffer = [&] {
        for (int i = 0; i < words; ++i)
            buffer[i] = 0xa5000000u + static_cast<uint32_t>(i);
    };

    for (auto offset : { -8192, -4100, -4096, -1028, -1024, -1020, -260, -256, -252, -8, -4, 0, 4, 252, 256, 1020, 1024, 4088, 4092, 4096, 8188 }) {
        int slot = mid + offset / 4;

        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        CHECK_EQ(invoke<uint32_t>(load, &buffer[mid]), buffer[slot]);

        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store32(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(store, &buffer[mid], 0x5eed1234u);
        CHECK_EQ(buffer[slot], 0x5eed1234u);
        CHECK_EQ(buffer[slot - 1], 0xa5000000u + static_cast<uint32_t>(slot - 1));
        CHECK_EQ(buffer[slot + 1], 0xa5000000u + static_cast<uint32_t>(slot + 1));

        auto storeImm = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store32(CCallHelpers::TrustedImm32(0x0badf00d), CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(storeImm, &buffer[mid]);
        CHECK_EQ(buffer[slot], 0x0badf00du);
        CHECK_EQ(buffer[slot - 1], 0xa5000000u + static_cast<uint32_t>(slot - 1));
        CHECK_EQ(buffer[slot + 1], 0xa5000000u + static_cast<uint32_t>(slot + 1));
    }
}

void testARMv7Load8Load16OffsetBoundaries()
{
    constexpr int size = 20000;
    constexpr int mid = 10000;
    static uint8_t buffer[size];

    auto initBuffer = [&] {
        for (int i = 0; i < size; ++i)
            buffer[i] = static_cast<uint8_t>(i * 7 + 3);
    };

    for (auto offset : { -8192, -4096, -1024, -257, -256, -255, -254, -2, -1, 0, 1, 2, 254, 255, 256, 4094, 4095, 4096, 8191 }) {
        int slot = mid + offset;

        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load8(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        CHECK_EQ(invoke<uint32_t>(load, &buffer[mid]), static_cast<uint32_t>(buffer[slot]));

        auto loadSigned = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load8SignedExtendTo32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        CHECK_EQ(invoke<int32_t>(loadSigned, &buffer[mid]), static_cast<int32_t>(static_cast<int8_t>(buffer[slot])));

        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store8(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(store, &buffer[mid], 0xa7u);
        CHECK_EQ(buffer[slot], static_cast<uint8_t>(0xa7));
        CHECK_EQ(buffer[slot - 1], static_cast<uint8_t>((slot - 1) * 7 + 3));
        CHECK_EQ(buffer[slot + 1], static_cast<uint8_t>((slot + 1) * 7 + 3));

        auto storeImm = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store8(CCallHelpers::TrustedImm32(0x3c), CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(storeImm, &buffer[mid]);
        CHECK_EQ(buffer[slot], static_cast<uint8_t>(0x3c));
    }

    for (auto offset : { -8192, -4096, -1024, -256, -254, -2, 0, 2, 254, 256, 4094, 4096, 8190 }) {
        int slot = mid + offset;

        initBuffer();
        uint32_t expected = static_cast<uint32_t>(buffer[slot]) | (static_cast<uint32_t>(buffer[slot + 1]) << 8);

        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load16(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        CHECK_EQ(invoke<uint32_t>(load, &buffer[mid]), expected);

        auto loadSigned = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load16SignedExtendTo32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        CHECK_EQ(invoke<int32_t>(loadSigned, &buffer[mid]), static_cast<int32_t>(static_cast<int16_t>(expected)));

        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store16(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(store, &buffer[mid], 0xbeefu);
        CHECK_EQ(buffer[slot], static_cast<uint8_t>(0xef));
        CHECK_EQ(buffer[slot + 1], static_cast<uint8_t>(0xbe));
        CHECK_EQ(buffer[slot - 1], static_cast<uint8_t>((slot - 1) * 7 + 3));
        CHECK_EQ(buffer[slot + 2], static_cast<uint8_t>((slot + 2) * 7 + 3));

        auto storeImm = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store16(CCallHelpers::TrustedImm32(0x1234), CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(storeImm, &buffer[mid]);
        CHECK_EQ(buffer[slot], static_cast<uint8_t>(0x34));
        CHECK_EQ(buffer[slot + 1], static_cast<uint8_t>(0x12));
    }
}

void testARMv7LoadStoreBaseIndexScales()
{
    constexpr int words = 8192;
    static uint32_t buffer[words];
    constexpr int index = 100;

    auto initBuffer = [&] {
        for (int i = 0; i < words; ++i)
            buffer[i] = 0xc3000000u + static_cast<uint32_t>(i);
    };

    for (int shift = 0; shift < 4; ++shift) {
        auto scale = static_cast<CCallHelpers::Scale>(shift);
        for (auto offset : { 0, 4, 252, 256, 1020, 1024, 4092, 4096, 8192 }) {
            int byteOffset = index * (1 << shift) + offset;
            uint32_t* slot = bitwise_cast_ptr<uint32_t>(reinterpret_cast<uint8_t*>(&buffer[0]) + byteOffset);

            auto load = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            CHECK_EQ(invoke<uint32_t>(load, &buffer[0], index), *slot);

            auto loadDestIsBase = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::argumentGPR0);
                jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            CHECK_EQ(invoke<uint32_t>(loadDestIsBase, &buffer[0], index), *slot);

            auto loadDestIsIndex = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::argumentGPR1);
                jit.move(GPRInfo::argumentGPR1, GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            CHECK_EQ(invoke<uint32_t>(loadDestIsIndex, &buffer[0], index), *slot);

            auto load16BaseIndex = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load16(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            CHECK_EQ(invoke<uint32_t>(load16BaseIndex, &buffer[0], index), *slot & 0xffffu);

            auto load8BaseIndex = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.load8(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::returnValueGPR);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            CHECK_EQ(invoke<uint32_t>(load8BaseIndex, &buffer[0], index), *slot & 0xffu);

            auto store = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store32(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            invoke<void>(store, &buffer[0], index, 0x77665544u);
            CHECK_EQ(*slot, 0x77665544u);

            auto storeImm = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store32(CCallHelpers::TrustedImm32(0x11223344), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            invoke<void>(storeImm, &buffer[0], index);
            CHECK_EQ(*slot, 0x11223344u);

            auto store16 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store16(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            uint32_t before = *slot;
            invoke<void>(store16, &buffer[0], index, 0xdeadu);
            CHECK_EQ(*slot, (before & 0xffff0000u) | 0xdeadu);

            auto store8 = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store8(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            before = *slot;
            invoke<void>(store8, &buffer[0], index, 0x9au);
            CHECK_EQ(*slot, (before & 0xffffff00u) | 0x9au);
        }
    }
}

void testARMv7LoadPair32OffsetBoundaries()
{
    constexpr int words = 5000;
    constexpr int mid = 2500;
    static uint32_t buffer[words];

    auto initBuffer = [&] {
        for (int i = 0; i < words; ++i)
            buffer[i] = 0x77000000u + static_cast<uint32_t>(i);
    };

    for (auto offset : { -8192, -4096, -4092, -1028, -1024, -1020, -1016, -256, -252, -8, -4, 0, 4, 8, 252, 256, 1016, 1020, 1024, 4084, 4088, 4092 }) {
        int slot = mid + offset / 4;
        uint32_t out[2];

        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::regT2, GPRInfo::regT3);
            jit.store32(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::regT3, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        out[0] = 0;
        out[1] = 0;
        invoke<void>(load, &buffer[mid], &out[0]);
        CHECK_EQ(out[0], buffer[slot]);
        CHECK_EQ(out[1], buffer[slot + 1]);

        auto loadUnaligned = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32Unaligned(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::regT2, GPRInfo::regT3);
            jit.store32(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::regT3, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        out[0] = 0;
        out[1] = 0;
        invoke<void>(loadUnaligned, &buffer[mid], &out[0]);
        CHECK_EQ(out[0], buffer[slot]);
        CHECK_EQ(out[1], buffer[slot + 1]);

        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.storePair32(GPRInfo::argumentGPR1, GPRInfo::argumentGPR2, CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(store, &buffer[mid], 0x01020304u, 0x05060708u);
        CHECK_EQ(buffer[slot], 0x01020304u);
        CHECK_EQ(buffer[slot + 1], 0x05060708u);
        CHECK_EQ(buffer[slot - 1], 0x77000000u + static_cast<uint32_t>(slot - 1));
        CHECK_EQ(buffer[slot + 2], 0x77000000u + static_cast<uint32_t>(slot + 2));

        auto storeImms = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.storePair32(CCallHelpers::TrustedImm32(0x0a0b0c0d), CCallHelpers::TrustedImm32(0x0e0f1011), CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(storeImms, &buffer[mid]);
        CHECK_EQ(buffer[slot], 0x0a0b0c0du);
        CHECK_EQ(buffer[slot + 1], 0x0e0f1011u);
        CHECK_EQ(buffer[slot - 1], 0x77000000u + static_cast<uint32_t>(slot - 1));
        CHECK_EQ(buffer[slot + 2], 0x77000000u + static_cast<uint32_t>(slot + 2));

        auto storeRegImm = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.storePair32(GPRInfo::argumentGPR1, CCallHelpers::TrustedImm32(0x12131415), CCallHelpers::Address(GPRInfo::argumentGPR0, offset));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(storeRegImm, &buffer[mid], 0x16171819u);
        CHECK_EQ(buffer[slot], 0x16171819u);
        CHECK_EQ(buffer[slot + 1], 0x12131415u);
    }
}

void testARMv7LoadPair32Aliasing()
{
    constexpr int words = 5000;
    constexpr int mid = 2500;
    static uint32_t buffer[words];

    auto initBuffer = [&] {
        for (int i = 0; i < words; ++i)
            buffer[i] = 0x88000000u + static_cast<uint32_t>(i);
    };

    for (auto offset : { -1024, -1020, -256, -8, 0, 4, 1016, 1020, 1024, 4088, 4092 }) {
        int slot = mid + offset / 4;
        uint32_t out[2];

        auto destIsBaseFirst = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::argumentGPR0, GPRInfo::regT2);
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        out[0] = 0;
        out[1] = 0;
        invoke<void>(destIsBaseFirst, &buffer[mid], &out[0]);
        CHECK_EQ(out[0], buffer[slot]);
        CHECK_EQ(out[1], buffer[slot + 1]);

        auto destIsBaseSecond = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::regT2, GPRInfo::argumentGPR0);
            jit.store32(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        out[0] = 0;
        out[1] = 0;
        invoke<void>(destIsBaseSecond, &buffer[mid], &out[0]);
        CHECK_EQ(out[0], buffer[slot]);
        CHECK_EQ(out[1], buffer[slot + 1]);

        auto unalignedDestIsBase = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32Unaligned(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), GPRInfo::argumentGPR0, GPRInfo::regT2);
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            jit.store32(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR1, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        out[0] = 0;
        out[1] = 0;
        invoke<void>(unalignedDestIsBase, &buffer[mid], &out[0]);
        CHECK_EQ(out[0], buffer[slot]);
        CHECK_EQ(out[1], buffer[slot + 1]);
    }

    for (int shift = 0; shift < 4; ++shift) {
        auto scale = static_cast<CCallHelpers::Scale>(shift);
        for (auto offset : { 0, 4, 1020, 1024, 4092 }) {
            constexpr int index = 64;
            int byteOffset = index * (1 << shift) + offset;
            uint32_t* slot = bitwise_cast_ptr<uint32_t>(reinterpret_cast<uint8_t*>(&buffer[0]) + byteOffset);
            uint32_t out[2];

            auto load = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.push(ARMRegisters::r4);
                jit.push(ARMRegisters::r5);
                jit.loadPair32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), ARMRegisters::r4, ARMRegisters::r5);
                jit.store32(ARMRegisters::r4, CCallHelpers::Address(GPRInfo::argumentGPR2, 0));
                jit.store32(ARMRegisters::r5, CCallHelpers::Address(GPRInfo::argumentGPR2, 4));
                jit.pop(ARMRegisters::r5);
                jit.pop(ARMRegisters::r4);
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            out[0] = 0;
            out[1] = 0;
            invoke<void>(load, &buffer[0], index, &out[0]);
            CHECK_EQ(out[0], slot[0]);
            CHECK_EQ(out[1], slot[1]);

            auto loadDestIsBase = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.loadPair32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::argumentGPR0, GPRInfo::regT3);
                jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(GPRInfo::argumentGPR2, 0));
                jit.store32(GPRInfo::regT3, CCallHelpers::Address(GPRInfo::argumentGPR2, 4));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            out[0] = 0;
            out[1] = 0;
            invoke<void>(loadDestIsBase, &buffer[0], index, &out[0]);
            CHECK_EQ(out[0], slot[0]);
            CHECK_EQ(out[1], slot[1]);

            auto loadDestIsIndex = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.loadPair32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset), GPRInfo::regT3, GPRInfo::argumentGPR1);
                jit.store32(GPRInfo::regT3, CCallHelpers::Address(GPRInfo::argumentGPR2, 0));
                jit.store32(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR2, 4));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            out[0] = 0;
            out[1] = 0;
            invoke<void>(loadDestIsIndex, &buffer[0], index, &out[0]);
            CHECK_EQ(out[0], slot[0]);
            CHECK_EQ(out[1], slot[1]);

            auto store = compile([=] (CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.storePair32(GPRInfo::regT2, GPRInfo::regT3, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, offset));
                emitFunctionEpilogue(jit);
                jit.ret();
            });

            initBuffer();
            invoke<void>(store, &buffer[0], index, 0xaabbccddu, 0x11223344u);
            CHECK_EQ(slot[0], 0xaabbccddu);
            CHECK_EQ(slot[1], 0x11223344u);
        }
    }
}

void testARMv7CachedTempRegisterInvalidatedByMemoryOps()
{
    constexpr int words = 4096;
    constexpr int mid = 2048;
    static uint32_t buffer[words];
    for (int i = 0; i < words; ++i)
        buffer[i] = 0xffffffffu - static_cast<uint32_t>(i);

    constexpr uint32_t marker = 0x12345678u;

    auto check = [&] (auto emitMemoryOp, MacroAssembler::RegisterID temp) {
        auto code = compile([&] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.move(CCallHelpers::TrustedImm32(marker), temp);
            emitMemoryOp(jit, temp);
            jit.move(CCallHelpers::TrustedImm32(marker), temp);
            jit.move(temp, GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        CHECK_EQ(invoke<uint32_t>(code, &buffer[mid], 0u), marker);
    };

    for (auto temp : { ARMRegisters::ip, ARMRegisters::r6 }) {
        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load8(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load8SignedExtendTo32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load16(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load16(CCallHelpers::Address(GPRInfo::argumentGPR0, 4096), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load16SignedExtendTo32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load16(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load16(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, 8), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.load16Unaligned(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.convertibleLoadPtr(CCallHelpers::Address(GPRInfo::argumentGPR0, 4), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.loadPair32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest, GPRInfo::regT2);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.loadPair32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), GPRInfo::regT2, dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.loadLink32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.loadLink8(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.loadLink16(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), dest);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID result) {
            jit.loadLink32(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), GPRInfo::regT2);
            jit.storeCond32(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR0, 0), result);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID result) {
            jit.loadLink8(CCallHelpers::Address(GPRInfo::argumentGPR0, 0), GPRInfo::regT2);
            jit.storeCond8(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR0, 0), result);
        }, temp);

        check([] (CCallHelpers& jit, MacroAssembler::RegisterID dest) {
            jit.getEffectiveAddress(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, 0), dest);
        }, temp);
    }
}

void testARMv7TransferMemory()
{
    constexpr int words = 3000;
    constexpr int mid = 1000;
    static uint32_t buffer[words];

    auto initBuffer = [&] {
        for (int i = 0; i < words; ++i)
            buffer[i] = 0x99000000u + static_cast<uint32_t>(i);
    };

    for (auto offset : { -1024, -256, -4, 0, 4, 1020, 4092, 4096 }) {
        int srcSlot = mid + offset / 4;

        auto transfer = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.transfer32(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        uint32_t out = 0;
        invoke<void>(transfer, &buffer[mid], &out);
        CHECK_EQ(out, buffer[srcSlot]);

        auto transfer64 = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.transfer64(CCallHelpers::Address(GPRInfo::argumentGPR0, offset), CCallHelpers::Address(GPRInfo::argumentGPR1, 0));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        uint32_t out64[2];
        out64[0] = 0;
        out64[1] = 0;
        invoke<void>(transfer64, &buffer[mid], &out64[0]);
        CHECK_EQ(out64[0], buffer[srcSlot]);
        CHECK_EQ(out64[1], buffer[srcSlot + 1]);
    }

    for (int shift = 0; shift < 4; ++shift) {
        auto scale = static_cast<CCallHelpers::Scale>(shift);
        constexpr int index = 32;
        int byteOffset = index * (1 << shift);
        uint32_t* slot = bitwise_cast_ptr<uint32_t>(reinterpret_cast<uint8_t*>(&buffer[0]) + byteOffset);

        auto transfer = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.transfer32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, scale, 0), CCallHelpers::BaseIndex(GPRInfo::argumentGPR2, GPRInfo::argumentGPR3, CCallHelpers::TimesOne, 0));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        uint32_t out = 0;
        invoke<void>(transfer, &buffer[0], index, &out, 0);
        CHECK_EQ(out, *slot);
    }
}

void testARMv7SwapRegisters()
{
    for (auto variant : { 0, 1, 2 }) {
        auto code = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            if (!variant)
                jit.swap(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            else if (variant == 1) {
                jit.swap(GPRInfo::argumentGPR0, GPRInfo::argumentGPR0);
                jit.swap(GPRInfo::argumentGPR1, GPRInfo::argumentGPR1);
                jit.swap(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            } else {
                jit.swap(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
                jit.swap(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
                jit.swap(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
            }
            jit.store32(GPRInfo::argumentGPR0, CCallHelpers::Address(GPRInfo::argumentGPR2, 0));
            jit.store32(GPRInfo::argumentGPR1, CCallHelpers::Address(GPRInfo::argumentGPR2, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        uint32_t out[2];
        out[0] = 0;
        out[1] = 0;
        invoke<void>(code, 0x11112222u, 0x33334444u, &out[0]);
        CHECK_EQ(out[0], 0x33334444u);
        CHECK_EQ(out[1], 0x11112222u);
    }
}

void testARMv7LoadStoreAbsoluteAddress()
{
    static uint32_t buffer[16];

    auto initBuffer = [&] {
        for (int i = 0; i < 16; ++i)
            buffer[i] = 0x44000000u + static_cast<uint32_t>(i);
    };

    for (auto index : { 0, 1, 2, 15 }) {
        auto load = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load32(&buffer[index], GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        CHECK_EQ(invoke<uint32_t>(load), buffer[index]);

        auto load8 = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load8(&buffer[index], GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        CHECK_EQ(invoke<uint32_t>(load8), buffer[index] & 0xffu);

        auto load16 = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load16(&buffer[index], GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        CHECK_EQ(invoke<uint32_t>(load16), buffer[index] & 0xffffu);

        auto store = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store32(GPRInfo::argumentGPR0, &buffer[index]);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(store, 0xfeedbeefu);
        CHECK_EQ(buffer[index], 0xfeedbeefu);

        auto storeImm = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store32(CCallHelpers::TrustedImm32(0x0c0d0e0f), &buffer[index]);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(storeImm);
        CHECK_EQ(buffer[index], 0x0c0d0e0fu);

        uint32_t out[2];

        auto loadPair = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadPair32(CCallHelpers::AbsoluteAddress(&buffer[0]), GPRInfo::regT2, GPRInfo::regT3);
            jit.store32(GPRInfo::regT2, CCallHelpers::Address(GPRInfo::argumentGPR0, 0));
            jit.store32(GPRInfo::regT3, CCallHelpers::Address(GPRInfo::argumentGPR0, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        out[0] = 0;
        out[1] = 0;
        invoke<void>(loadPair, &out[0]);
        CHECK_EQ(out[0], buffer[0]);
        CHECK_EQ(out[1], buffer[1]);

        auto storePair = compile([=] (CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.storePair32(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, &buffer[0]);
            emitFunctionEpilogue(jit);
            jit.ret();
        });

        initBuffer();
        invoke<void>(storePair, 0x21222324u, 0x25262728u);
        CHECK_EQ(buffer[0], 0x21222324u);
        CHECK_EQ(buffer[1], 0x25262728u);
    }
}

#endif

#if CPU(ARM_THUMB2)
void testARMv7StorePair32AbsoluteTempAliasing()
{
    static uint32_t buffer[8];
    for (auto& word : buffer)
        word = 0;

    auto test = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(CCallHelpers::TrustedImmPtr(&buffer[0]), MacroAssembler::dataTempRegister);
        jit.move(CCallHelpers::TrustedImm32(0x11111111), GPRInfo::regT2);
        jit.storePair32(GPRInfo::regT2, CCallHelpers::TrustedImm32(0x22222222), &buffer[2]);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    invoke<void>(test);
    CHECK_EQ(buffer[2], 0x11111111u);
    CHECK_EQ(buffer[3], 0x22222222u);
}
#endif

#if CPU(ARM64)
void testLoadStorePair64Int64()
{
    constexpr uint64_t initialValue = 0x5555aaaabbbb8800ull;
    constexpr uint64_t value1 = 42;
    constexpr uint64_t value2 = 0xcafebabe12345678ull;

    uint64_t buffer[10];

    auto initBuffer = [&] {
        for (unsigned i = 0; i < 10; ++i)
            buffer[i] = initialValue + i;
    };

    struct Pair {
        uint64_t value1;
        uint64_t value2;
    };

    Pair pair;
    auto initPair = [&] {
        pair = { 0, 0 };
    };

    // Test loadPair64.
    auto testLoadPair = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR0;
        constexpr GPRReg pairGPR = GPRInfo::argumentGPR1;
        jit.loadPair64(bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(CPURegister)), GPRInfo::regT2, GPRInfo::regT3);

        jit.store64(GPRInfo::regT2, CCallHelpers::Address(pairGPR, 0));
        jit.store64(GPRInfo::regT3, CCallHelpers::Address(pairGPR, sizeof(uint64_t)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testLoadPair0 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, 0);
    });

    initBuffer();

    initPair();
    invoke<void>(testLoadPair0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4);
    CHECK_EQ(pair.value2, initialValue + 5);

    initPair();
    buffer[4] = value1;
    buffer[5] = value2;
    invoke<void>(testLoadPair0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value1);
    CHECK_EQ(pair.value2, value2);

    auto testLoadPairMinus2 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, -2);
    });

    initPair();
    invoke<void>(testLoadPairMinus2, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4 - 2);
    CHECK_EQ(pair.value2, initialValue + 5 - 2);

    initPair();
    buffer[4 - 2] = value2;
    buffer[5 - 2] = value1;
    invoke<void>(testLoadPairMinus2, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value2);
    CHECK_EQ(pair.value2, value1);

    auto testLoadPairPlus3 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, 3);
    });

    initPair();
    invoke<void>(testLoadPairPlus3, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4 + 3);
    CHECK_EQ(pair.value2, initialValue + 5 + 3);

    initPair();
    buffer[4 + 3] = value1;
    buffer[5 + 3] = value2;
    invoke<void>(testLoadPairPlus3, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value1);
    CHECK_EQ(pair.value2, value2);

    // Test loadPair64 using a buffer register as a destination.
    auto testLoadPairUsingBufferRegisterAsDestination = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR0;
        constexpr GPRReg pairGPR = GPRInfo::argumentGPR1;
        jit.loadPair64(bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(CPURegister)), GPRInfo::argumentGPR0, GPRInfo::regT2);

        jit.store64(GPRInfo::argumentGPR0, CCallHelpers::Address(pairGPR, 0));
        jit.store64(GPRInfo::regT2, CCallHelpers::Address(pairGPR, sizeof(uint64_t)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testLoadPairUsingBufferRegisterAsDestination0 = compile([&] (CCallHelpers& jit) {
        testLoadPairUsingBufferRegisterAsDestination(jit, 0);
    });

    initBuffer();

    initPair();
    invoke<void>(testLoadPairUsingBufferRegisterAsDestination0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4);
    CHECK_EQ(pair.value2, initialValue + 5);

    // Test storePair64.
    auto testStorePair = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR2;
        jit.storePair64(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(CPURegister)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testStorePair0 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, 0);
    });

    initBuffer();
    invoke<void>(testStorePair0, value1, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], value1);
    CHECK_EQ(buffer[5], value2);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairMinus2 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, -2);
    });

    initBuffer();
    invoke<void>(testStorePairMinus2, value1, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], value1);
    CHECK_EQ(buffer[3], value2);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairPlus3 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, 3);
    });

    initBuffer();
    invoke<void>(testStorePairPlus3, value1, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], value1);
    CHECK_EQ(buffer[8], value2);
    CHECK_EQ(buffer[9], initialValue + 9);

    // Test storePair64 from 1 register.
    auto testStorePairFromOneReg = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR1;
        jit.storePair64(GPRInfo::argumentGPR0, GPRInfo::argumentGPR0, bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(CPURegister)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testStorePairFromOneReg0 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, 0);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneReg0, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], value2);
    CHECK_EQ(buffer[5], value2);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairFromOneRegMinus2 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, -2);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneRegMinus2, value1, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], value1);
    CHECK_EQ(buffer[3], value1);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairFromOneRegPlus3 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, 3);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneRegPlus3, value2, &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], value2);
    CHECK_EQ(buffer[8], value2);
    CHECK_EQ(buffer[9], initialValue + 9);
}

void testLoadStorePair64Double()
{
    constexpr double initialValue = 10000.275;
    constexpr double value1 = 42.89;
    constexpr double value2 = -555.321;

    double buffer[10];

    auto initBuffer = [&] {
        for (unsigned i = 0; i < 10; ++i)
            buffer[i] = initialValue + i;
    };

    struct Pair {
        double value1;
        double value2;
    };

    Pair pair;
    auto initPair = [&] {
        pair = { 0, 0 };
    };

    // Test loadPair64.
    auto testLoadPair = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR0;
        constexpr GPRReg pairGPR = GPRInfo::argumentGPR1;
        jit.loadPair64(bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(double)), FPRInfo::fpRegT0, FPRInfo::fpRegT1);

        jit.storeDouble(FPRInfo::fpRegT0, CCallHelpers::Address(pairGPR, 0));
        jit.storeDouble(FPRInfo::fpRegT1, CCallHelpers::Address(pairGPR, sizeof(uint64_t)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testLoadPair0 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, 0);
    });

    initBuffer();

    initPair();
    invoke<void>(testLoadPair0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4);
    CHECK_EQ(pair.value2, initialValue + 5);

    initPair();
    buffer[4] = value1;
    buffer[5] = value2;
    invoke<void>(testLoadPair0, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value1);
    CHECK_EQ(pair.value2, value2);

    auto testLoadPairMinus2 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, -2);
    });

    initPair();
    invoke<void>(testLoadPairMinus2, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4 - 2);
    CHECK_EQ(pair.value2, initialValue + 5 - 2);

    initPair();
    buffer[4 - 2] = value2;
    buffer[5 - 2] = value1;
    invoke<void>(testLoadPairMinus2, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value2);
    CHECK_EQ(pair.value2, value1);

    auto testLoadPairPlus3 = compile([&] (CCallHelpers& jit) {
        testLoadPair(jit, 3);
    });

    initPair();
    invoke<void>(testLoadPairPlus3, &buffer[4], &pair);
    CHECK_EQ(pair.value1, initialValue + 4 + 3);
    CHECK_EQ(pair.value2, initialValue + 5 + 3);

    initPair();
    buffer[4 + 3] = value1;
    buffer[5 + 3] = value2;
    invoke<void>(testLoadPairPlus3, &buffer[4], &pair);
    CHECK_EQ(pair.value1, value1);
    CHECK_EQ(pair.value2, value2);

    // Test storePair64.
    auto testStorePair = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR2;
        jit.move64ToDouble(GPRInfo::argumentGPR0, FPRInfo::fpRegT0);
        jit.move64ToDouble(GPRInfo::argumentGPR1, FPRInfo::fpRegT1);
        jit.storePair64(FPRInfo::fpRegT0, FPRInfo::fpRegT1, bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(double)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto asInt64 = [] (double value) {
        return std::bit_cast<int64_t>(value);
    };

    auto testStorePair0 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, 0);
    });

    initBuffer();
    invoke<void>(testStorePair0, asInt64(value1), asInt64(value2), &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], value1);
    CHECK_EQ(buffer[5], value2);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairMinus2 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, -2);
    });

    initBuffer();
    invoke<void>(testStorePairMinus2, asInt64(value1), asInt64(value2), &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], value1);
    CHECK_EQ(buffer[3], value2);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairPlus3 = compile([&] (CCallHelpers& jit) {
        testStorePair(jit, 3);
    });

    initBuffer();
    invoke<void>(testStorePairPlus3, asInt64(value1), asInt64(value2), &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], value1);
    CHECK_EQ(buffer[8], value2);
    CHECK_EQ(buffer[9], initialValue + 9);

    // Test storePair64 from 1 register.
    auto testStorePairFromOneReg = [] (CCallHelpers& jit, int offset) {
        emitFunctionPrologue(jit);

        constexpr GPRReg bufferGPR = GPRInfo::argumentGPR1;
        jit.move64ToDouble(GPRInfo::argumentGPR0, FPRInfo::fpRegT0);
        jit.storePair64(FPRInfo::fpRegT0, FPRInfo::fpRegT0, bufferGPR, CCallHelpers::TrustedImm32(offset * sizeof(double)));

        emitFunctionEpilogue(jit);
        jit.ret();
    };

    auto testStorePairFromOneReg0 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, 0);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneReg0, asInt64(value2), &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], value2);
    CHECK_EQ(buffer[5], value2);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairFromOneRegMinus2 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, -2);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneRegMinus2, asInt64(value1), &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], value1);
    CHECK_EQ(buffer[3], value1);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], initialValue + 7);
    CHECK_EQ(buffer[8], initialValue + 8);
    CHECK_EQ(buffer[9], initialValue + 9);

    auto testStorePairFromOneRegPlus3 = compile([&] (CCallHelpers& jit) {
        testStorePairFromOneReg(jit, 3);
    });

    initBuffer();
    invoke<void>(testStorePairFromOneRegPlus3, asInt64(value2), &buffer[4]);
    CHECK_EQ(buffer[0], initialValue + 0);
    CHECK_EQ(buffer[1], initialValue + 1);
    CHECK_EQ(buffer[2], initialValue + 2);
    CHECK_EQ(buffer[3], initialValue + 3);
    CHECK_EQ(buffer[4], initialValue + 4);
    CHECK_EQ(buffer[5], initialValue + 5);
    CHECK_EQ(buffer[6], initialValue + 6);
    CHECK_EQ(buffer[7], value2);
    CHECK_EQ(buffer[8], value2);
    CHECK_EQ(buffer[9], initialValue + 9);
}
#endif // CPU(ARM64)

void testProbeReadsArgumentRegisters()
{
    bool probeWasCalled = false;
    compileAndRun<void>([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.pushPair(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
        jit.pushPair(GPRInfo::argumentGPR2, GPRInfo::argumentGPR3);

        jit.move(CCallHelpers::TrustedImm32(testWord32(0)), GPRInfo::argumentGPR0);
        jit.convertInt32ToDouble(GPRInfo::argumentGPR0, FPRInfo::fpRegT0);
        jit.move(CCallHelpers::TrustedImm32(testWord32(1)), GPRInfo::argumentGPR0);
        jit.convertInt32ToDouble(GPRInfo::argumentGPR0, FPRInfo::fpRegT1);
#if USE(JSVALUE64)
        jit.move(CCallHelpers::TrustedImm64(testWord(0)), GPRInfo::argumentGPR0);
        jit.move(CCallHelpers::TrustedImm64(testWord(1)), GPRInfo::argumentGPR1);
        jit.move(CCallHelpers::TrustedImm64(testWord(2)), GPRInfo::argumentGPR2);
        jit.move(CCallHelpers::TrustedImm64(testWord(3)), GPRInfo::argumentGPR3);
#else
        jit.move(CCallHelpers::TrustedImm32(testWord(0)), GPRInfo::argumentGPR0);
        jit.move(CCallHelpers::TrustedImm32(testWord(1)), GPRInfo::argumentGPR1);
        jit.move(CCallHelpers::TrustedImm32(testWord(2)), GPRInfo::argumentGPR2);
        jit.move(CCallHelpers::TrustedImm32(testWord(3)), GPRInfo::argumentGPR3);
#endif

        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeWasCalled = true;
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR0), testWord(0));
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR1), testWord(1));
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR2), testWord(2));
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR3), testWord(3));

            CHECK_EQ(cpu.fpr(FPRInfo::fpRegT0), testWord32(0));
            CHECK_EQ(cpu.fpr(FPRInfo::fpRegT1), testWord32(1));
        });

        jit.popPair(GPRInfo::argumentGPR2, GPRInfo::argumentGPR3);
        jit.popPair(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);

        emitFunctionEpilogue(jit);
        jit.ret();
    });
    CHECK_EQ(probeWasCalled, true);
}

void testProbeWritesArgumentRegisters()
{
    // This test relies on testProbeReadsArgumentRegisters() having already validated
    // that we can read from argument registers. We'll use that ability to validate
    // that our writes did take effect.
    unsigned probeCallCount = 0;
    compileAndRun<void>([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        jit.pushPair(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);
        jit.pushPair(GPRInfo::argumentGPR2, GPRInfo::argumentGPR3);

        // Pre-initialize with non-expected values.
#if USE(JSVALUE64)
        jit.move(CCallHelpers::TrustedImm64(0), GPRInfo::argumentGPR0);
        jit.move(CCallHelpers::TrustedImm64(0), GPRInfo::argumentGPR1);
        jit.move(CCallHelpers::TrustedImm64(0), GPRInfo::argumentGPR2);
        jit.move(CCallHelpers::TrustedImm64(0), GPRInfo::argumentGPR3);
#else
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::argumentGPR0);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::argumentGPR1);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::argumentGPR2);
        jit.move(CCallHelpers::TrustedImm32(0), GPRInfo::argumentGPR3);
#endif
        jit.convertInt32ToDouble(GPRInfo::argumentGPR0, FPRInfo::fpRegT0);
        jit.convertInt32ToDouble(GPRInfo::argumentGPR0, FPRInfo::fpRegT1);

        // Write expected values.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            cpu.gpr(GPRInfo::argumentGPR0) = testWord(0);
            cpu.gpr(GPRInfo::argumentGPR1) = testWord(1);
            cpu.gpr(GPRInfo::argumentGPR2) = testWord(2);
            cpu.gpr(GPRInfo::argumentGPR3) = testWord(3);
            
            cpu.fpr(FPRInfo::fpRegT0) = std::bit_cast<double>(testWord64(0));
            cpu.fpr(FPRInfo::fpRegT1) = std::bit_cast<double>(testWord64(1));
        });

        // Validate that expected values were written.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR0), testWord(0));
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR1), testWord(1));
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR2), testWord(2));
            CHECK_EQ(cpu.gpr(GPRInfo::argumentGPR3), testWord(3));

            CHECK_EQ(cpu.fpr<uint64_t>(FPRInfo::fpRegT0), testWord64(0));
            CHECK_EQ(cpu.fpr<uint64_t>(FPRInfo::fpRegT1), testWord64(1));
        });

        jit.popPair(GPRInfo::argumentGPR2, GPRInfo::argumentGPR3);
        jit.popPair(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1);

        emitFunctionEpilogue(jit);
        jit.ret();
    });
    CHECK_EQ(probeCallCount, 2);
}

static NEVER_INLINE NOT_TAIL_CALLED int testFunctionToTrashGPRs(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)
{
    if (j > 0)
        return testFunctionToTrashGPRs(a + 1, b + a, c + b, d + 5, e - a, f * 1.5, g ^ a, h - b, i, j - 1);
    return a + 1;
}
static NEVER_INLINE NOT_TAIL_CALLED double testFunctionToTrashFPRs(double a, double b, double c, double d, double e, double f, double g, double h, double i, double j)
{
    if (j > 0)
        return testFunctionToTrashFPRs(a + 1, b + a, c + b, d + 5, e - a, f * 1.5, pow(g, a), h - b, i, j - 1);
    return a + 1;
}

void testProbePreservesGPRS()
{
    // This test relies on testProbeReadsArgumentRegisters() and testProbeWritesArgumentRegisters()
    // having already validated that we can read and write from registers. We'll use these abilities
    // to validate that the probe preserves register values.
    unsigned probeCallCount = 0;
    CPUState originalState;

    compileAndRun<void>([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        // Write expected values into the registers (except for sp, fp, and pc).
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                originalState.gpr(id) = cpu.gpr(id);
                if (isSpecialGPR(id))
                    continue;
                cpu.gpr(id) = testWord(static_cast<int>(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id)) {
                originalState.fpr(id) = cpu.fpr(id);
                cpu.fpr(id) = std::bit_cast<double>(testWord64(id));
            }
        });

        // Invoke the probe to call a lot of functions and trash register values.
        jit.probeDebug([&] (Probe::Context&) {
            probeCallCount++;
            CHECK_EQ(testFunctionToTrashGPRs(0, 1, 2, 3, 4, 5, 6, 7, 8, 9), 10);
            CHECK_EQ(testFunctionToTrashFPRs(0, 1, 2, 3, 4, 5, 6, 7, 8, 9), 10);
        });

        // Validate that the registers have the expected values.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isSP(id) || isFP(id)) {
                    CHECK_EQ(cpu.gpr(id), originalState.gpr(id));
                    continue;
                }
                if (isSpecialGPR(id))
                    continue;
                CHECK_EQ(cpu.gpr(id), testWord(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id)) {
                CHECK_EQ(cpu.fpr<uint64_t>(id), testWord64(id));
            }
        });

        // Restore the original state.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isSpecialGPR(id))
                    continue;
                cpu.gpr(id) = originalState.gpr(id);
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id))
                cpu.fpr(id) = originalState.fpr(id);
        });

        // Validate that the original state was restored.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isSpecialGPR(id))
                    continue;
                CHECK_EQ(cpu.gpr(id), originalState.gpr(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id))
                CHECK_EQ(cpu.fpr<uint64_t>(id), originalState.fpr<uint64_t>(id));
        });

        emitFunctionEpilogue(jit);
        jit.ret();
    });
    CHECK_EQ(probeCallCount, 5);
}

void testProbeModifiesStackPointer(WTF::Function<void*(Probe::Context&)> computeModifiedStackPointer)
{
    unsigned probeCallCount = 0;
    CPUState originalState;
    void* originalSP { nullptr };
    void* modifiedSP { nullptr };
#if !CPU(RISCV64)
    uintptr_t modifiedFlags { 0 };
#endif
    
#if CPU(X86_64)
    auto flagsSPR = X86Registers::eflags;
    uintptr_t flagsMask = 0xc5;
#elif CPU(ARM_THUMB2)
    auto flagsSPR = ARMRegisters::apsr;
    uintptr_t flagsMask = 0xf8000000;
#elif CPU(ARM64)
    auto flagsSPR = ARM64Registers::nzcv;
    uintptr_t flagsMask = 0xf0000000;
#endif

    compileAndRun<void>([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        // Preserve original stack pointer and modify the sp, and
        // write expected values into other registers (except for fp, and pc).
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                originalState.gpr(id) = cpu.gpr(id);
                if (isSpecialGPR(id))
                    continue;
                cpu.gpr(id) = testWord(static_cast<int>(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id)) {
                originalState.fpr(id) = cpu.fpr(id);
                cpu.fpr(id) = std::bit_cast<double>(testWord64(id));
            }

#if !(CPU(RISCV64))
            originalState.spr(flagsSPR) = cpu.spr(flagsSPR);
            modifiedFlags = originalState.spr(flagsSPR) ^ flagsMask;
            cpu.spr(flagsSPR) = modifiedFlags;
#endif

            originalSP = cpu.sp();
            modifiedSP = computeModifiedStackPointer(context);
            cpu.sp() = modifiedSP;
        });

        // Validate that the registers have the expected values.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isFP(id)) {
                    CHECK_EQ(cpu.gpr(id), originalState.gpr(id));
                    continue;
                }
                if (isSpecialGPR(id))
                    continue;
                CHECK_EQ(cpu.gpr(id), testWord(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id))
                CHECK_EQ(cpu.fpr<uint64_t>(id), testWord64(id));
#if !CPU(RISCV64)
            CHECK_EQ(cpu.spr(flagsSPR) & flagsMask, modifiedFlags & flagsMask);
#endif
            CHECK_EQ(cpu.sp(), modifiedSP);
        });

        // Restore the original state.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isSpecialGPR(id))
                    continue;
                cpu.gpr(id) = originalState.gpr(id);
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id))
                cpu.fpr(id) = originalState.fpr(id);
#if !CPU(RISCV64)
            cpu.spr(flagsSPR) = originalState.spr(flagsSPR);
#endif
            cpu.sp() = originalSP;
        });

        // Validate that the original state was restored.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isSpecialGPR(id))
                    continue;
                CHECK_EQ(cpu.gpr(id), originalState.gpr(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id))
                CHECK_EQ(cpu.fpr<uint64_t>(id), originalState.fpr<uint64_t>(id));
#if !CPU(RISCV64)
            CHECK_EQ(cpu.spr(flagsSPR) & flagsMask, originalState.spr(flagsSPR) & flagsMask);
#endif
            CHECK_EQ(cpu.sp(), originalSP);
        });

        emitFunctionEpilogue(jit);
        jit.ret();
    });
    CHECK_EQ(probeCallCount, 4);
}

void testProbeModifiesStackPointerToInsideProbeStateOnStack()
{
    size_t increment = sizeof(uintptr_t);
#if CPU(ARM64)
    // The ARM64 probe uses ldp and stp which require 16 byte alignment.
    increment = 2 * sizeof(uintptr_t);
#endif
    for (size_t offset = 0; offset < sizeof(Probe::State); offset += increment) {
        testProbeModifiesStackPointer([=] (Probe::Context& context) -> void* {
            return static_cast<uint8_t*>(probeStateForContext(context)) + offset;

        });
    }
}

void testProbeModifiesStackPointerToNBytesBelowSP()
{
    size_t increment = sizeof(uintptr_t);
#if CPU(ARM64)
    // The ARM64 probe uses ldp and stp which require 16 byte alignment.
    increment = 2 * sizeof(uintptr_t);
#endif
    for (size_t offset = 0; offset < 1 * KB; offset += increment) {
        testProbeModifiesStackPointer([=] (Probe::Context& context) -> void* {
            return context.cpu.sp<uint8_t*>() - offset;
        });
    }
}

void testProbeModifiesProgramCounter()
{
    // This test relies on testProbeReadsArgumentRegisters() and testProbeWritesArgumentRegisters()
    // having already validated that we can read and write from registers. We'll use these abilities
    // to validate that the probe preserves register values.
    unsigned probeCallCount = 0;
    bool continuationWasReached = false;

    MacroAssemblerCodeRef<JSEntryPtrTag> continuation = compile([&] (CCallHelpers& jit) {
        // Validate that we reached the continuation.
        jit.probeDebug([&] (Probe::Context&) {
            probeCallCount++;
            continuationWasReached = true;
        });

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    compileAndRun<void>([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        // Write expected values into the registers.
        jit.probeDebug([&] (Probe::Context& context) {
            probeCallCount++;
            context.cpu.pc() = retagCodePtr<JSEntryPtrTag, JITProbePCPtrTag>(continuation.code().taggedPtr());
        });

        jit.breakpoint(); // We should never get here.
    });
    CHECK_EQ(probeCallCount, 2);
    CHECK_EQ(continuationWasReached, true);
}

void testProbeModifiesStackValues()
{
    unsigned probeCallCount = 0;
    CPUState originalState;
    void* originalSP { nullptr };
    void* newSP { nullptr };
#if !CPU(RISCV64)
    uintptr_t modifiedFlags { 0 };
#endif
    size_t numberOfExtraEntriesToWrite { 10 }; // ARM64 requires that this be 2 word aligned.

#if CPU(X86_64)
    MacroAssembler::SPRegisterID flagsSPR = X86Registers::eflags;
    uintptr_t flagsMask = 0xc5;
#elif CPU(ARM_THUMB2)
    MacroAssembler::SPRegisterID flagsSPR = ARMRegisters::apsr;
    uintptr_t flagsMask = 0xf8000000;
#elif CPU(ARM64)
    MacroAssembler::SPRegisterID flagsSPR = ARM64Registers::nzcv;
    uintptr_t flagsMask = 0xf0000000;
#endif

    compileAndRun<void>([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);

        // Write expected values into the registers.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            auto& stack = context.stack();
            probeCallCount++;

            // Preserve the original CPU state.
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                originalState.gpr(id) = cpu.gpr(id);
                if (isSpecialGPR(id))
                    continue;
                cpu.gpr(id) = testWord(static_cast<int>(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id)) {
                originalState.fpr(id) = cpu.fpr(id);
                cpu.fpr(id) = std::bit_cast<double>(testWord64(id));
            }
#if !CPU(RISCV64)
            originalState.spr(flagsSPR) = cpu.spr(flagsSPR);
            modifiedFlags = originalState.spr(flagsSPR) ^ flagsMask;
            cpu.spr(flagsSPR) = modifiedFlags;
#endif

            // Ensure that we'll be writing over the regions of the stack where the Probe::State is.
            originalSP = cpu.sp();
            newSP = static_cast<uintptr_t*>(probeStateForContext(context)) - numberOfExtraEntriesToWrite;
            cpu.sp() = newSP;

            // Fill the stack with values.
            uintptr_t* p = static_cast<uintptr_t*>(newSP);
            int count = 0;
            stack.set<double>(p++, 1.234567);
            if (is32Bit())
                p++; // On 32-bit targets, a double takes up 2 uintptr_t.
            while (p < static_cast<uintptr_t*>(originalSP))
                stack.set<uintptr_t>(p++, testWord(count++));
        });

        // Validate that the registers and stack have the expected values.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            auto& stack = context.stack();
            probeCallCount++;

            // Validate the register values.
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isFP(id)) {
                    CHECK_EQ(cpu.gpr(id), originalState.gpr(id));
                    continue;
                }
                if (isSpecialGPR(id))
                    continue;
                CHECK_EQ(cpu.gpr(id), testWord(id));
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id))
                CHECK_EQ(cpu.fpr<uint64_t>(id), testWord64(id));
#if !CPU(RISCV64)
            CHECK_EQ(cpu.spr(flagsSPR) & flagsMask, modifiedFlags & flagsMask);
#endif
            CHECK_EQ(cpu.sp(), newSP);

            // Validate the stack values.
            uintptr_t* p = static_cast<uintptr_t*>(newSP);
            int count = 0;
            CHECK_EQ(stack.get<double>(p++), 1.234567);
            if (is32Bit())
                p++; // On 32-bit targets, a double takes up 2 uintptr_t.
            while (p < static_cast<uintptr_t*>(originalSP))
                CHECK_EQ(stack.get<uintptr_t>(p++), testWord(count++));
        });

        // Restore the original state.
        jit.probeDebug([&] (Probe::Context& context) {
            auto& cpu = context.cpu;
            probeCallCount++;
            for (auto id = CCallHelpers::firstRegister(); id <= CCallHelpers::lastRegister(); id = nextID(id)) {
                if (isSpecialGPR(id))
                    continue;
                cpu.gpr(id) = originalState.gpr(id);
            }
            for (auto id = CCallHelpers::firstFPRegister(); id <= CCallHelpers::lastFPRegister(); id = nextID(id))
                cpu.fpr(id) = originalState.fpr(id);
#if !CPU(RISCV64)
            cpu.spr(flagsSPR) = originalState.spr(flagsSPR);
#endif
            cpu.sp() = originalSP;
        });

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    CHECK_EQ(probeCallCount, 3);
}

void testOrImmMem()
{
    // FIXME: this does not test that the or does not touch beyond its width.
    // I am not sure how to do such a test without a lot of complexity (running multiple threads, with a race on the high bits of the memory location).
    uint64_t memoryLocation = 0x12341234;
    auto or32 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.or32(CCallHelpers::TrustedImm32(42), CCallHelpers::AbsoluteAddress(&memoryLocation));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(or32);
    CHECK_EQ(memoryLocation, 0x12341234 | 42);

    memoryLocation = 0x12341234;
    auto or16 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.or16(CCallHelpers::TrustedImm32(42), CCallHelpers::AbsoluteAddress(&memoryLocation));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(or16);
    CHECK_EQ(memoryLocation, 0x12341234 | 42);

    memoryLocation = 0x12341234;
    auto or8 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.or8(CCallHelpers::TrustedImm32(42), CCallHelpers::AbsoluteAddress(&memoryLocation));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(or8);
    CHECK_EQ(memoryLocation, 0x12341234 | 42);

    memoryLocation = 0x12341234;
    auto or16InvalidLogicalImmInARM64 = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.or16(CCallHelpers::TrustedImm32(0), CCallHelpers::AbsoluteAddress(&memoryLocation));
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    invoke<void>(or16InvalidLogicalImmInARM64);
    CHECK_EQ(memoryLocation, 0x12341234);
}

void testAndOrDouble()
{
    double arg1, arg2;

    auto andDouble = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT1);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT2);

        jit.andDouble(FPRInfo::fpRegT1, FPRInfo::fpRegT2, FPRInfo::returnValueFPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    auto operands = doubleOperands();
    for (auto a : operands) {
        for (auto b : operands) {
            arg1 = a;
            arg2 = b;
            uint64_t expectedResult = std::bit_cast<uint64_t>(arg1) & std::bit_cast<uint64_t>(arg2);
            CHECK_EQ(std::bit_cast<uint64_t>(invoke<double>(andDouble)), expectedResult);
        }
    }

    auto orDouble = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT1);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT2);

        jit.orDouble(FPRInfo::fpRegT1, FPRInfo::fpRegT2, FPRInfo::returnValueFPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    for (auto a : operands) {
        for (auto b : operands) {
            arg1 = a;
            arg2 = b;
            uint64_t expectedResult = std::bit_cast<uint64_t>(arg1) | std::bit_cast<uint64_t>(arg2);
            CHECK_EQ(std::bit_cast<uint64_t>(invoke<double>(orDouble)), expectedResult);
        }
    }
}

void testByteSwap()
{
#if CPU(X86_64) || CPU(ARM64)
    auto byteSwap16 = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.byteSwap16(GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    CHECK_EQ(invoke<uint64_t>(byteSwap16, 0xaabbccddee001122), static_cast<uint64_t>(0x2211));
    CHECK_EQ(invoke<uint64_t>(byteSwap16, 0xaabbccddee00ffaa), static_cast<uint64_t>(0xaaff));

    auto byteSwap32 = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.byteSwap32(GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    CHECK_EQ(invoke<uint64_t>(byteSwap32, 0xaabbccddee001122), static_cast<uint64_t>(0x221100ee));
    CHECK_EQ(invoke<uint64_t>(byteSwap32, 0xaabbccddee00ffaa), static_cast<uint64_t>(0xaaff00ee));

    auto byteSwap64 = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.move(GPRInfo::argumentGPR0, GPRInfo::returnValueGPR);
        jit.byteSwap64(GPRInfo::returnValueGPR);
        emitFunctionEpilogue(jit);
        jit.ret();
    });
    CHECK_EQ(invoke<uint64_t>(byteSwap64, 0xaabbccddee001122), static_cast<uint64_t>(0x221100eeddccbbaa));
    CHECK_EQ(invoke<uint64_t>(byteSwap64, 0xaabbccddee00ffaa), static_cast<uint64_t>(0xaaff00eeddccbbaa));
#endif
}

void testMoveDoubleConditionally32()
{
#if CPU(X86_64) | CPU(ARM64)
    double arg1 = 0;
    double arg2 = 0;
    const double zero = -0;

    const double chosenDouble = 6.00000059604644775390625;
    CHECK_EQ(static_cast<double>(static_cast<float>(chosenDouble)) == chosenDouble, false);

    auto sel = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&zero), FPRInfo::returnValueFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT1);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT2);

        jit.move(MacroAssembler::TrustedImm32(-1), GPRInfo::regT0);
        jit.moveDoubleConditionally32(MacroAssembler::Equal, GPRInfo::regT0, GPRInfo::regT0, FPRInfo::fpRegT1, FPRInfo::fpRegT2, FPRInfo::returnValueFPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    arg1 = chosenDouble;
    arg2 = 43;
    CHECK_EQ(invoke<double>(sel), chosenDouble);

    arg1 = 43;
    arg2 = chosenDouble;
    CHECK_EQ(invoke<double>(sel), 43.0);

#endif
}

void testMoveDoubleConditionally64()
{
#if CPU(X86_64) | CPU(ARM64)
    double arg1 = 0;
    double arg2 = 0;
    const double zero = -0;

    const double chosenDouble = 6.00000059604644775390625;
    CHECK_EQ(static_cast<double>(static_cast<float>(chosenDouble)) == chosenDouble, false);

    auto sel = compile([&] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&zero), FPRInfo::returnValueFPR);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg1), FPRInfo::fpRegT1);
        jit.loadDouble(CCallHelpers::TrustedImmPtr(&arg2), FPRInfo::fpRegT2);

        jit.move(MacroAssembler::TrustedImm64(-1), GPRInfo::regT0);
        jit.moveDoubleConditionally64(MacroAssembler::Equal, GPRInfo::regT0, GPRInfo::regT0, FPRInfo::fpRegT1, FPRInfo::fpRegT2, FPRInfo::returnValueFPR);

        emitFunctionEpilogue(jit);
        jit.ret();
    });

    arg1 = chosenDouble;
    arg2 = 43;
    CHECK_EQ(invoke<double>(sel), chosenDouble);

    arg1 = 43;
    arg2 = chosenDouble;
    CHECK_EQ(invoke<double>(sel), 43.0);

#endif
}

void testLoadBaseIndex()
{
#if CPU(ARM64) || CPU(X86_64) || CPU(RISCV64)
    // load64
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load64(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, -8), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint64_t array[] = { UINT64_MAX - 1, UINT64_MAX - 2, UINT64_MAX - 3, UINT64_MAX - 4, UINT64_MAX - 5, };
        CHECK_EQ(invoke<uint64_t>(test, array, static_cast<UCPURegister>(3)), UINT64_MAX - 3);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load64(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, 8), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint64_t array[] = { UINT64_MAX - 1, UINT64_MAX - 2, UINT64_MAX - 3, UINT64_MAX - 4, UINT64_MAX - 5, };
        CHECK_EQ(invoke<uint64_t>(test, array, static_cast<UCPURegister>(3)), UINT64_MAX - 5);
    }
#endif

    // load32
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, -4), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint32_t array[] = { UINT32_MAX - 1, UINT32_MAX - 2, UINT32_MAX - 3, UINT32_MAX - 4, UINT32_MAX - 5, };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), UINT32_MAX - 3);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, 4), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint32_t array[] = { UINT32_MAX - 1, UINT32_MAX - 2, UINT32_MAX - 3, UINT32_MAX - 4, UINT32_MAX - 5, };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), UINT32_MAX - 5);
    }

    // load16
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load16(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, -2), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint16_t array[] = { UINT16_MAX - 1, UINT16_MAX - 2, UINT16_MAX - 3, UINT16_MAX - 4, UINT16_MAX - 5, };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), UINT16_MAX - 3);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load16(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, 2), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint16_t array[] = { UINT16_MAX - 1, UINT16_MAX - 2, UINT16_MAX - 3, UINT16_MAX - 4, static_cast<uint16_t>(-1), };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), 0xffff);
    }

    // load16SignedExtendTo32
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load16SignedExtendTo32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, -2), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint16_t array[] = { 1, 2, 0x7ff3, 4, 5, };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), 0x7ff3);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load16SignedExtendTo32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, 2), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint16_t array[] = { UINT16_MAX - 1, UINT16_MAX - 2, UINT16_MAX - 3, UINT16_MAX - 4, static_cast<uint16_t>(-1), };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), static_cast<uint32_t>(-1));
    }

    // load8
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load8(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, -1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint8_t array[] = { UINT8_MAX - 1, UINT8_MAX - 2, UINT8_MAX - 3, UINT8_MAX - 4, UINT8_MAX - 5, };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), UINT8_MAX - 3);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load8(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, 1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint8_t array[] = { UINT8_MAX - 1, UINT8_MAX - 2, UINT8_MAX - 3, UINT8_MAX - 4, static_cast<uint8_t>(-1), };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), 0xff);
    }

    // load8SignedExtendTo32
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load8SignedExtendTo32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, -1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint8_t array[] = { 1, 2, 0x73, 4, 5, };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), 0x73);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.load8SignedExtendTo32(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, 1), GPRInfo::returnValueGPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint8_t array[] = { UINT8_MAX - 1, UINT8_MAX - 2, UINT8_MAX - 3, UINT8_MAX - 4, static_cast<uint8_t>(-1), };
        CHECK_EQ(invoke<uint32_t>(test, array, static_cast<UCPURegister>(3)), static_cast<uint32_t>(-1));
    }

    // loadDouble
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, -8), FPRInfo::returnValueFPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        double array[] = { 1, 2, 3, 4, 5, };
        CHECK_EQ(invoke<double>(test, array, static_cast<UCPURegister>(3)), 3.0);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadDouble(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, 8), FPRInfo::returnValueFPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        double array[] = { 1, 2, 3, 4, 5, };
        CHECK_EQ(invoke<double>(test, array, static_cast<UCPURegister>(3)), 5.0);
    }

    // loadFloat
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadFloat(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, -4), FPRInfo::returnValueFPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        float array[] = { 1, 2, 3, 4, 5, };
        CHECK_EQ(invoke<float>(test, array, static_cast<UCPURegister>(3)), 3.0);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.loadFloat(CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, 4), FPRInfo::returnValueFPR);
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        float array[] = { 1, 2, 3, 4, 5, };
        CHECK_EQ(invoke<float>(test, array, static_cast<UCPURegister>(3)), 5.0);
    }
}

void testStoreImmediateAddress()
{
#if CPU(ARM64) || CPU(X86_64)
    // store64
    for (auto imm : int64Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store64(CCallHelpers::TrustedImm64(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, -16));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint64_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array + 3);
            CHECK_EQ(array[1], static_cast<uint64_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store64(CCallHelpers::TrustedImm64(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, 16));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint64_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array);
            CHECK_EQ(array[2], static_cast<uint64_t>(imm));
        }
    }

    // store32
    for (auto imm : int32Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store32(CCallHelpers::TrustedImm32(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, -8));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint32_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array + 3);
            CHECK_EQ(array[1], static_cast<uint32_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store32(CCallHelpers::TrustedImm32(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, 8));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint32_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array);
            CHECK_EQ(array[2], static_cast<uint32_t>(imm));
        }
    }

    // store16
    for (auto imm : int16Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store16(CCallHelpers::TrustedImm32(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, -4));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint16_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array + 3);
            CHECK_EQ(array[1], static_cast<uint16_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store16(CCallHelpers::TrustedImm32(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, 4));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint16_t array[] = { 1, 2, 3, 4, static_cast<uint16_t>(-1), };
            invoke<void>(test, array);
            CHECK_EQ(array[2], static_cast<uint16_t>(imm));
        }
    }

    // store8
    for (auto imm : int8Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store8(CCallHelpers::TrustedImm32(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, -2));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint8_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array + 3);
            CHECK_EQ(array[1], static_cast<uint8_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store8(CCallHelpers::TrustedImm32(imm), CCallHelpers::Address(GPRInfo::argumentGPR0, 2));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint8_t array[] = { 1, 2, 3, 4, static_cast<uint8_t>(-1), };
            invoke<void>(test, array);
            CHECK_EQ(array[2], static_cast<uint8_t>(imm));
        }
    }
#endif
}

void testStoreBaseIndex()
{
#if CPU(ARM64) || CPU(X86_64) || CPU(RISCV64)
    // store64
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store64(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, -8));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint64_t array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, 3, UINT64_MAX - 42);
        CHECK_EQ(array[2], UINT64_MAX - 42);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store64(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, 8));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint64_t array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, 3, UINT64_MAX - 42);
        CHECK_EQ(array[4], UINT64_MAX - 42);
    }
#endif

    // store32
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store32(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, -4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint32_t array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, 3, UINT32_MAX - 42);
        CHECK_EQ(array[2], UINT32_MAX - 42);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store32(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint32_t array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, 3, UINT32_MAX - 42);
        CHECK_EQ(array[4], UINT32_MAX - 42);
    }

    // store16
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store16(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, -2));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint16_t array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, 3, UINT16_MAX - 42);
        CHECK_EQ(array[2], UINT16_MAX - 42);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store16(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, 2));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint16_t array[] = { 1, 2, 3, 4, static_cast<uint16_t>(-1), };
        invoke<void>(test, array, 3, UINT16_MAX - 42);
        CHECK_EQ(array[4], UINT16_MAX - 42);
    }

    // store8
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store8(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, -1));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint8_t array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, 3, UINT8_MAX - 42);
        CHECK_EQ(array[2], UINT8_MAX - 42);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            jit.store8(GPRInfo::argumentGPR2, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, 1));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        uint8_t array[] = { 1, 2, 3, 4, static_cast<uint8_t>(-1), };
        invoke<void>(test, array, 3, UINT8_MAX - 42);
        CHECK_EQ(array[4], UINT8_MAX - 42);
    }

    // storeDouble
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            constexpr FPRReg inputFPR = FPRInfo::argumentFPR0;
            jit.storeDouble(inputFPR, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, -8));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        double array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, static_cast<UCPURegister>(3), 42.0);
        CHECK_EQ(array[2], 42.0);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            constexpr FPRReg inputFPR = FPRInfo::argumentFPR0;
            jit.storeDouble(inputFPR, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, 8));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        double array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, static_cast<UCPURegister>(3), 42.0);
        CHECK_EQ(array[4], 42.0);
    }

    // storeFloat
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            constexpr FPRReg inputFPR = FPRInfo::argumentFPR0;
            jit.storeFloat(inputFPR, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, -4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        float array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, static_cast<UCPURegister>(3), 42.0f);
        CHECK_EQ(array[2], 42.0f);
    }
    {
        auto test = compile([=](CCallHelpers& jit) {
            emitFunctionPrologue(jit);
            constexpr FPRReg inputFPR = FPRInfo::argumentFPR0;
            jit.storeFloat(inputFPR, CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, 4));
            emitFunctionEpilogue(jit);
            jit.ret();
        });
        float array[] = { 1, 2, 3, 4, 5, };
        invoke<void>(test, array, static_cast<UCPURegister>(3), 42.0f);
        CHECK_EQ(array[4], 42.0f);
    }
}

void testStoreImmediateBaseIndex()
{
#if CPU(ARM64) || CPU(X86_64)
    // store64
    for (auto imm : int64Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store64(CCallHelpers::TrustedImm64(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, -8));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint64_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[2], static_cast<uint64_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store64(CCallHelpers::TrustedImm64(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesEight, 8));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint64_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[4], static_cast<uint64_t>(imm));
        }
    }

    // store32
    for (auto imm : int32Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store32(CCallHelpers::TrustedImm32(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, -4));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint32_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[2], static_cast<uint32_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store32(CCallHelpers::TrustedImm32(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesFour, 4));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint32_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[4], static_cast<uint32_t>(imm));
        }
    }

    // store16
    for (auto imm : int16Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store16(CCallHelpers::TrustedImm32(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, -2));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint16_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[2], static_cast<uint16_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store16(CCallHelpers::TrustedImm32(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesTwo, 2));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint16_t array[] = { 1, 2, 3, 4, static_cast<uint16_t>(-1), };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[4], static_cast<uint16_t>(imm));
        }
    }

    // store8
    for (auto imm : int8Operands()) {
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store8(CCallHelpers::TrustedImm32(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, -1));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint8_t array[] = { 1, 2, 3, 4, 5, };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[2], static_cast<uint8_t>(imm));
        }
        {
            auto test = compile([=](CCallHelpers& jit) {
                emitFunctionPrologue(jit);
                jit.store8(CCallHelpers::TrustedImm32(imm), CCallHelpers::BaseIndex(GPRInfo::argumentGPR0, GPRInfo::argumentGPR1, CCallHelpers::TimesOne, 1));
                emitFunctionEpilogue(jit);
                jit.ret();
            });
            uint8_t array[] = { 1, 2, 3, 4, static_cast<uint8_t>(-1), };
            invoke<void>(test, array, 3);
            CHECK_EQ(array[4], static_cast<uint8_t>(imm));
        }
    }
#endif
}

static void testBranchIfType()
{
    using JSC::JSType;
    struct CellLike {
        uint32_t structureID;
        uint8_t indexingType;
        JSType type;
    };
    CHECK_EQ(JSCell::typeInfoTypeOffset(), OBJECT_OFFSETOF(CellLike, type));

    auto isType = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        auto isType = jit.branchIfType(GPRInfo::argumentGPR0, JSC::JSTypeRange { JSType(FirstTypedArrayType), JSType(LastTypedArrayTypeExcludingDataView) });
        jit.move(CCallHelpers::TrustedImm32(false), GPRInfo::returnValueGPR);
        auto done = jit.jump();
        isType.link(&jit);
        jit.move(CCallHelpers::TrustedImm32(true), GPRInfo::returnValueGPR);
        done.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    CellLike cell;
    for (unsigned i = JSC::FirstTypedArrayType; i <= JSC::LastTypedArrayTypeExcludingDataView; ++i) {
        cell.type = JSType(i);
        CHECK_EQ(invoke<bool>(isType, &cell), true);
    }

    cell.type = JSType(LastTypedArrayType);
    CHECK_EQ(invoke<bool>(isType, &cell), false);
    cell.type = JSType(FirstTypedArrayType - 1);
    CHECK_EQ(invoke<bool>(isType, &cell), false);
}

static void testBranchIfNotType()
{
    using JSC::JSType;
    struct CellLike {
        uint32_t structureID;
        uint8_t indexingType;
        JSType type;
    };
    CHECK_EQ(JSCell::typeInfoTypeOffset(), OBJECT_OFFSETOF(CellLike, type));

    auto isNotType = compile([] (CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        auto isNotType = jit.branchIfNotType(GPRInfo::argumentGPR0, JSC::JSTypeRange { JSType(FirstTypedArrayType), JSType(LastTypedArrayTypeExcludingDataView) });
        jit.move(CCallHelpers::TrustedImm32(false), GPRInfo::returnValueGPR);
        auto done = jit.jump();
        isNotType.link(&jit);
        jit.move(CCallHelpers::TrustedImm32(true), GPRInfo::returnValueGPR);
        done.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    CellLike cell;
    for (unsigned i = JSC::FirstTypedArrayType; i <= JSC::LastTypedArrayTypeExcludingDataView; ++i) {
        cell.type = JSType(i);
        CHECK_EQ(invoke<bool>(isNotType, &cell), false);
    }

    cell.type = JSType(LastTypedArrayType);
    CHECK_EQ(invoke<bool>(isNotType, &cell), true);
    cell.type = JSType(FirstTypedArrayType - 1);
    CHECK_EQ(invoke<bool>(isNotType, &cell), true);
}

#if CPU(X86_64) || CPU(ARM64)
static void testBranchConvertDoubleToInt52()
{
    auto toInt52 = compile([](CCallHelpers& jit) {
        emitFunctionPrologue(jit);
        CCallHelpers::JumpList failureCases;
        jit.branchConvertDoubleToInt52(FPRInfo::argumentFPR0, GPRInfo::returnValueGPR, failureCases, GPRInfo::returnValueGPR2, FPRInfo::argumentFPR1);
        auto done = jit.jump();
        failureCases.link(&jit);
        jit.move(CCallHelpers::TrustedImm64(1ULL << 52), GPRInfo::returnValueGPR);
        done.link(&jit);
        emitFunctionEpilogue(jit);
        jit.ret();
    });

    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(1LL << 50))), (1LL << 50));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>((1LL << 50) - 1))), ((1LL << 50) - 1));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>((1LL << 51) - 1))), ((1LL << 51) - 1));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-(1LL << 51)))), (-(1LL << 51)));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(1))), 1LL);
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-1))), -1LL);
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(0))), 0LL);

    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(1LL << 51))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>((1LL << 51) + 1))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>((1LL << 51) + 42))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-((1LL << 51) + 1)))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-((1LL << 51) + 42)))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(1LL << 52))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>((1LL << 52) + 1))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>((1LL << 52) + 42))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-((1LL << 52) + 1)))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-((1LL << 52) + 42)))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, -static_cast<double>(0))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(std::numeric_limits<double>::infinity()))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-std::numeric_limits<double>::infinity()))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(std::numeric_limits<double>::quiet_NaN()))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(42.195))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(0.3))), (1LL << 52));
    CHECK_EQ((invoke<int64_t>(toInt52, static_cast<double>(-0.1))), (1LL << 52));
}
#endif

static void testGPRInfoConsistency()
{
    for (unsigned index = 0; index < GPRInfo::numberOfRegisters; ++index) {
        GPRReg reg = GPRInfo::toRegister(index);
        CHECK_EQ(GPRInfo::toIndex(reg), index);
    }
    for (auto reg = CCallHelpers::firstRegister(); reg <= CCallHelpers::lastRegister(); reg = nextID(reg)) {
        if (isSpecialGPR(reg))
            continue;
        unsigned index = GPRInfo::toIndex(reg);
        if (index == GPRInfo::InvalidIndex) {
            CHECK_EQ(index >= GPRInfo::numberOfRegisters, true);
            continue;
        }
        CHECK_EQ(index < GPRInfo::numberOfRegisters, true);
    }
}

#define RUN(test) do {                          \
        if (!shouldRun(#test))                  \
            break;                              \
        numberOfTests++;                        \
        tasks.append(                           \
            createSharedTask<void()>(           \
                [&] () {                        \
                    dataLog(#test "...\n");     \
                    test;                       \
                    dataLog(#test ": OK!\n");   \
                }));                            \
    } while (false);

// Using WTF_IGNORES_THREAD_SAFETY_ANALYSIS because the function is still holding crashLock when exiting.
void run(const char* filter) WTF_IGNORES_THREAD_SAFETY_ANALYSIS
{
    JSC::initialize();
    unsigned numberOfTests = 0;

    Deque<RefPtr<SharedTask<void()>>> tasks;

    auto shouldRun = [&] (const char* testName) -> bool {
        return !filter || WTF::findIgnoringASCIICaseWithoutLength(testName, filter) != WTF::notFound;
    };

    RUN(testSimple());
    RUN(testGetEffectiveAddress(0xff00, 42, 8, CCallHelpers::TimesEight));
    RUN(testGetEffectiveAddress(0xff00, -200, -300, CCallHelpers::TimesEight));
    RUN(testBranchTruncateDoubleToInt32(0, 0));
    RUN(testBranchTruncateDoubleToInt32(42, 42));
    RUN(testBranchTruncateDoubleToInt32(42.7, 42));
    RUN(testBranchTruncateDoubleToInt32(-1234, -1234));
    RUN(testBranchTruncateDoubleToInt32(-1234.56, -1234));
    RUN(testBranchTruncateDoubleToInt32(std::numeric_limits<double>::infinity(), 0));
    RUN(testBranchTruncateDoubleToInt32(-std::numeric_limits<double>::infinity(), 0));
    RUN(testBranchTruncateDoubleToInt32(std::numeric_limits<double>::quiet_NaN(), 0));
    RUN(testBranchTruncateDoubleToInt32(std::numeric_limits<double>::signaling_NaN(), 0));
    RUN(testBranchTruncateDoubleToInt32(std::numeric_limits<double>::max(), 0));
    RUN(testBranchTruncateDoubleToInt32(-std::numeric_limits<double>::max(), 0));
    // We run this last one to make sure that we don't use flags that were not
    // reset to check a conversion result
    RUN(testBranchTruncateDoubleToInt32(123, 123));

#define FOR_EACH_DOUBLE_CONDITION_RUN(__test) \
    do { \
        RUN(__test(MacroAssembler::DoubleEqualAndOrdered)); \
        RUN(__test(MacroAssembler::DoubleNotEqualAndOrdered)); \
        RUN(__test(MacroAssembler::DoubleGreaterThanAndOrdered)); \
        RUN(__test(MacroAssembler::DoubleGreaterThanOrEqualAndOrdered)); \
        RUN(__test(MacroAssembler::DoubleLessThanAndOrdered)); \
        RUN(__test(MacroAssembler::DoubleLessThanOrEqualAndOrdered)); \
        RUN(__test(MacroAssembler::DoubleEqualOrUnordered)); \
        RUN(__test(MacroAssembler::DoubleNotEqualOrUnordered)); \
        RUN(__test(MacroAssembler::DoubleGreaterThanOrUnordered)); \
        RUN(__test(MacroAssembler::DoubleGreaterThanOrEqualOrUnordered)); \
        RUN(__test(MacroAssembler::DoubleLessThanOrUnordered)); \
        RUN(__test(MacroAssembler::DoubleLessThanOrEqualOrUnordered)); \
    } while (false)

    FOR_EACH_DOUBLE_CONDITION_RUN(testCompareDouble);
    FOR_EACH_DOUBLE_CONDITION_RUN(testCompareDoubleSameArg);

    RUN(testMul32WithImmediates());
    RUN(testLoadStorePair32());
    RUN(testSub32ArgImm());
#if CPU(ARM_THUMB2)
    RUN(testSub32ImmArg());
#endif
#if CPU(ARM_THUMB2)
    RUN(testARMv7StorePair32AbsoluteTempAliasing());
#endif
RUN(testALUAdd32Immediate());
    RUN(testALUSub32Immediate());
    RUN(testALUAnd32Immediate());
    RUN(testALUOr32Immediate());
    RUN(testALUXor32Immediate());
    RUN(testALURegisterAliasing());
    RUN(testALUNeg32AndNot32());
    RUN(testALUMul32());
    RUN(testALUMul32Immediate());
    RUN(testALUCountLeadingZeros32());
    RUN(testALUShift32Boundaries());
    RUN(testALUBranchAdd32Immediate());
    RUN(testALUBranchSub32Immediate());
    RUN(testALUBranchNeg32());
    RUN(testALUBranchMul32Overflow());
    RUN(testALUCompare32Immediate());
#if CPU(ARM_THUMB2)
    RUN(testALUARMv7Add32ToStackPointerImmediate());
    RUN(testALUARMv7Sub32FromStackPointerImmediate());
    RUN(testALUARMv7Add32FromStackPointerImmediate());
    RUN(testALUARMv7HighRegisterOperands());
    RUN(testALUARMv7RotateRight32MultipleOf32());
    RUN(testALUARMv7RotateLeft32());
    RUN(testALUARMv7TruncateDoubleToInt64Negative());
#endif
#if CPU(ARM_THUMB2)
    RUN(testARMv7BranchRangeConditional());
    RUN(testARMv7BranchRangeUnconditional());
    RUN(testARMv7BranchRangeConditionalLong());
    RUN(testARMv7BranchRangeUnconditionalLong());
    RUN(testARMv7BranchRangeBackward());
    RUN(testARMv7BranchRangeManyJumpsToOneLabel());
    RUN(testARMv7LoadDoubleOffsets());
    RUN(testARMv7StoreDoubleOffsets());
    RUN(testARMv7LoadStoreFloatOffsets());
    RUN(testARMv7DoubleArithAliasing());
    RUN(testARMv7FloatArithAliasing());
    RUN(testARMv7AbsNegSqrtDouble());
    RUN(testARMv7AbsNegSqrtFloat());
    RUN(testARMv7MoveDoubleAliasing());
    RUN(testARMv7ConvertInt32ToDouble());
    RUN(testARMv7ConvertUInt32ToDouble());
    RUN(testARMv7ConvertInt32ToFloat());
    RUN(testARMv7ConvertUInt32ToFloat());
    RUN(testARMv7TruncateDoubleToInt32());
    RUN(testARMv7TruncateDoubleToUint32());
    RUN(testARMv7ConvertDoubleFloatRoundTrip());
    RUN(testARMv7BranchDoubleConditions());
    RUN(testARMv7BranchDoubleWithZero());
    RUN(testARMv7CompareDouble());
    RUN(testARMv7LoadStorePair64Double());
    RUN(testARMv7MoveDoubleBits());
    RUN(testARMv7Move32ToFloatBits());
    RUN(testARMv7MoveZeroToDoubleAndFloat());
#endif
RUN(testAsmMemStoreLoad32OffsetBoundaries());
    RUN(testAsmMemStoreLoad16OffsetBoundaries());
    RUN(testAsmMemStoreLoad8OffsetBoundaries());
    RUN(testAsmMemStoreLoad32BaseIndexScales());
    RUN(testAsmMemStoreLoad8BaseIndexScales());
    RUN(testAsmMemLoadPair32OffsetBoundaries());
    RUN(testAsmMemLoadPair32Aliasing());
    RUN(testAsmMemStorePair32OffsetBoundaries());
    RUN(testAsmMemShift32ImmediateBoundaries());
    RUN(testAsmMemShift32RegisterBoundaries());
    RUN(testAsmMemCountZeros32Boundaries());

#if CPU(ARM_THUMB2)
    RUN(testAsmMemRotateRight32ImmediateBoundaries());
    RUN(testAsmMemRotate32RegisterBoundaries());
    RUN(testAsmMemRotateLeft32ImmediateBoundaries());
#endif
#if CPU(ARM_THUMB2)
    RUN(testARMv7ImmAdd32ArgImm());
    RUN(testARMv7ImmAdd32ImmDest());
    RUN(testARMv7ImmAddSubRoundTrip());
    RUN(testARMv7ImmAnd32ArgImm());
    RUN(testARMv7ImmOr32ArgImm());
    RUN(testARMv7ImmXor32ArgImm());
    RUN(testARMv7ImmMove32());
    RUN(testARMv7ImmBranch32());
    RUN(testARMv7ImmCompare32());
    RUN(testARMv7ImmRotateRight32());
    RUN(testARMv7ImmRotateLeft32());
    RUN(testARMv7ImmShift32Amounts());
#endif
RUN(testRotateRight32Immediate());
RUN(testShift32Immediate());
RUN(testOrAbsoluteAddressAdjacent());
RUN(testAdd64ImmAbsoluteAddressAdjacent());

#if CPU(ARM_THUMB2)
RUN(testArmv7Arith32Aliasing());
RUN(testArmv7Arith32ImmediateAliasing());
RUN(testArmv7Arith32InPlaceImmediate());
RUN(testArmv7Neg32Not32Aliasing());
RUN(testArmv7CountZeros32Aliasing());
RUN(testArmv7Shift32RegisterAliasing());
RUN(testArmv7Shift32ImmediateValueRegisterAmount());
RUN(testArmv7ShiftUnchecked());
RUN(testArmv7RotateLeft32());
RUN(testArmv7RotateLeft32Immediate());
RUN(testArmv7Add64Sub64RegisterPairs());
RUN(testArmv7CountZeros64RegisterPairs());
RUN(testArmv7AddUnsignedRightShift32());
RUN(testArmv7Add32Sub32MemoryOperands());
#endif
RUN(testBranch32RegReg());
    RUN(testBranch32RegImm());
    RUN(testBranch32Address());
    RUN(testBranch8And16());
    RUN(testCompare32Setter());
    RUN(testCompare32SetterImm());
    RUN(testBranchTest32Masmbr());
    RUN(testTest32Setter());
    RUN(testBranchAdd32Masmbr());
    RUN(testBranchAdd32Address());
    RUN(testBranchSub32Masmbr());
    RUN(testBranchMul32Masmbr());
    RUN(testBranchNegAndOr32Masmbr());
    RUN(testMoveConditionally32Masmbr());
    RUN(testMoveConditionallyTest32Masmbr());
    RUN(testPatchableBranchMasmbr());

#if CPU(ARM_THUMB2)
    RUN(testBranch64ARMv7());
    RUN(testBranch64ImmARMv7());
    RUN(testCompare64ARMv7());
    RUN(testBranchTest64ARMv7());
#endif
#if CPU(ARM_THUMB2)
    RUN(testARMv7FPMoveDoubleInts());
    RUN(testARMv7FPMoveDoubleAliasing());
    RUN(testARMv7FPMoveFloatAndImmediates());
    RUN(testARMv7FPMove64ToDoubleImmediate());
    RUN(testARMv7FPLoadStoreOffsets());
    RUN(testARMv7FPLoadStoreBaseIndex());
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleEqualAndOrdered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleNotEqualAndOrdered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleGreaterThanAndOrdered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleGreaterThanOrEqualAndOrdered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleLessThanAndOrdered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleLessThanOrEqualAndOrdered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleEqualOrUnordered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleNotEqualOrUnordered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleGreaterThanOrUnordered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleGreaterThanOrEqualOrUnordered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleLessThanOrUnordered));
    RUN(testARMv7BranchDoubleEdges(MacroAssembler::DoubleLessThanOrEqualOrUnordered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleEqualAndOrdered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleNotEqualAndOrdered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleGreaterThanAndOrdered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleGreaterThanOrEqualAndOrdered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleLessThanAndOrdered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleLessThanOrEqualAndOrdered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleEqualOrUnordered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleNotEqualOrUnordered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleGreaterThanOrUnordered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleGreaterThanOrEqualOrUnordered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleLessThanOrUnordered));
    RUN(testARMv7BranchFloatEdges(MacroAssembler::DoubleLessThanOrEqualOrUnordered));
    RUN(testARMv7BranchDoubleNonZeroAndZeroOrNaN());
    RUN(testARMv7TruncateDoubleAndFloatToInt32());
    RUN(testARMv7BranchTruncateDoubleToInt32());
    RUN(testARMv7BranchConvertDoubleToInt32(true));
    RUN(testARMv7BranchConvertDoubleToInt32(false));
    RUN(testARMv7ConvertInt32AndUint32ToFloatingPoint());
    RUN(testARMv7ConvertInt32ImmediateToDouble());
    RUN(testARMv7ConvertFloatDouble());
    RUN(testARMv7DoubleBinaryAliasing());
    RUN(testARMv7DoubleAccumulateForms());
    RUN(testARMv7DoubleUnaryAliasing());
    RUN(testARMv7FloatArithmeticIsSinglePrecision());
    RUN(testARMv7RoundTowardNearestIntDouble());
    RUN(testARMv7TruncateDoubleToInt64());
    RUN(testARMv7TruncateDoubleToUint64());
    RUN(testARMv7TransferDouble());
#endif
#if CPU(ARM_THUMB2)
    RUN(testARMv7Load32StoreOffsetBoundaries());
    RUN(testARMv7Load8Load16OffsetBoundaries());
    RUN(testARMv7LoadStoreBaseIndexScales());
    RUN(testARMv7LoadPair32OffsetBoundaries());
    RUN(testARMv7LoadPair32Aliasing());
    RUN(testARMv7CachedTempRegisterInvalidatedByMemoryOps());
    RUN(testARMv7TransferMemory());
    RUN(testARMv7SwapRegisters());
    RUN(testARMv7LoadStoreAbsoluteAddress());
#endif

    RUN(testBranchTest8());
    RUN(testBranchTest16());

#if CPU(X86_64)
    RUN(testBranchTestBit32RegReg());
    RUN(testBranchTestBit32RegImm());
    RUN(testBranchTestBit32AddrImm());
    RUN(testBranchTestBit64RegReg());
    RUN(testBranchTestBit64RegImm());
    RUN(testBranchTestBit64AddrImm());
#endif

#if CPU(X86_64) || CPU(ARM64)
    RUN(testClearBit64());
    RUN(testClearBits64WithMask());
    RUN(testClearBits64WithMaskTernary());
    RUN(testCountTrailingZeros64());
    RUN(testCountTrailingZeros64WithoutNullCheck());
    RUN(testShiftAndAdd());
    RUN(testStore64Imm64AddressPointer());
#endif

#if CPU(ARM64)
    RUN(testLoadStorePair64Int64());
    RUN(testLoadStorePair64Double());
    RUN(testMultiplySignExtend32());
    RUN(testMultiplyZeroExtend32());

    RUN(testSub32Args());
    RUN(testSub32Imm());
    RUN(testSub64Imm32());
    RUN(testSub64ArgImm32());
    RUN(testSub64Imm64());
    RUN(testSub64ArgImm64());

    RUN(testMultiplyAddSignExtend32());
    RUN(testMultiplyAddZeroExtend32());
    RUN(testMultiplySubSignExtend32());
    RUN(testMultiplySubZeroExtend32());
    RUN(testMultiplyNegSignExtend32());
    RUN(testMultiplyNegZeroExtend32());

    RUN(testExtractUnsignedBitfield32());
    RUN(testExtractUnsignedBitfield64());
    RUN(testInsertUnsignedBitfieldInZero32());
    RUN(testInsertUnsignedBitfieldInZero64());
    RUN(testInsertBitField32());
    RUN(testInsertBitField64());
    RUN(testExtractInsertBitfieldAtLowEnd32());
    RUN(testExtractInsertBitfieldAtLowEnd64());
    RUN(testClearBitField32());
    RUN(testClearBitField64());
    RUN(testClearBitsWithMask32());
    RUN(testClearBitsWithMask64());

    RUN(testOrNot32());
    RUN(testOrNot64());

    RUN(testInsertSignedBitfieldInZero32());
    RUN(testInsertSignedBitfieldInZero64());
    RUN(testExtractSignedBitfield32());
    RUN(testExtractSignedBitfield64());
    RUN(testExtractRegister32());
    RUN(testExtractRegister64());

    RUN(testAddWithLeftShift32());
    RUN(testAddWithRightShift32());
    RUN(testAddWithUnsignedRightShift32());
    RUN(testAddWithLeftShift64());
    RUN(testAddWithRightShift64());
    RUN(testAddWithUnsignedRightShift64());
    RUN(testSubWithLeftShift32());
    RUN(testSubWithRightShift32());
    RUN(testSubWithUnsignedRightShift32());
    RUN(testSubWithLeftShift64());
    RUN(testSubWithRightShift64());
    RUN(testSubWithUnsignedRightShift64());

    RUN(testXorNot32());
    RUN(testXorNot64());
    RUN(testXorNotWithLeftShift32());
    RUN(testXorNotWithRightShift32());
    RUN(testXorNotWithUnsignedRightShift32());
    RUN(testXorNotWithLeftShift64());
    RUN(testXorNotWithRightShift64());
    RUN(testXorNotWithUnsignedRightShift64());

    RUN(testStorePrePostIndex32());
    RUN(testStorePrePostIndex64());
    RUN(testLoadPrePostIndex32());
    RUN(testLoadPrePostIndex64());
    RUN(testAndLeftShift32());
    RUN(testAndRightShift32());
    RUN(testAndUnsignedRightShift32());
    RUN(testAndLeftShift64());
    RUN(testAndRightShift64());
    RUN(testAndUnsignedRightShift64());

    RUN(testXorLeftShift32());
    RUN(testXorRightShift32());
    RUN(testXorUnsignedRightShift32());
    RUN(testXorLeftShift64());
    RUN(testXorRightShift64());
    RUN(testXorUnsignedRightShift64());

    RUN(testOrLeftShift32());
    RUN(testOrRightShift32());
    RUN(testOrUnsignedRightShift32());
    RUN(testOrLeftShift64());
    RUN(testOrRightShift64());
    RUN(testOrUnsignedRightShift64());

    RUN(testZeroExtend48ToWord());
#endif

#if CPU(ARM64)
    if (isARM64_LSE()) {
        RUN(testAtomicStrongCASFill8());
        RUN(testAtomicStrongCASFill16());
    }
#endif

#if CPU(X86_64) || CPU(ARM64) || CPU(RISCV64)
    FOR_EACH_DOUBLE_CONDITION_RUN(testCompareFloat);
#endif

#if CPU(X86_64) || CPU(ARM64) || CPU(RISCV64)
    // Comparing 2 different registers.
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyDouble2);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyDouble3);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyDouble3DestSameAsThenCase);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyDouble3DestSameAsElseCase);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyFloat2);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyFloat3);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyFloat3DestSameAsThenCase);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyFloat3DestSameAsElseCase);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyDouble);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyDoubleDestSameAsThenCase);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyDoubleDestSameAsElseCase);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyFloat);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyFloatDestSameAsThenCase);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyFloatDestSameAsElseCase);

    // Comparing the same register against itself.
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyDouble2SameArg);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyDouble3SameArg);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyFloat2SameArg);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveConditionallyFloat3SameArg);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyDoubleSameArg);
    FOR_EACH_DOUBLE_CONDITION_RUN(testMoveDoubleConditionallyFloatSameArg);

    RUN(testSignExtend8To64());
    RUN(testSignExtend16To64());
#endif

    RUN(testProbeReadsArgumentRegisters());
    RUN(testProbeWritesArgumentRegisters());
    RUN(testProbePreservesGPRS());
    RUN(testProbeModifiesStackPointerToInsideProbeStateOnStack());
    RUN(testProbeModifiesStackPointerToNBytesBelowSP());
    RUN(testProbeModifiesProgramCounter());
    RUN(testProbeModifiesStackValues());

    RUN(testByteSwap());
    RUN(testMoveDoubleConditionally32());
    RUN(testMoveDoubleConditionally64());
    RUN(testLoadBaseIndex());
    RUN(testStoreImmediateAddress());
    RUN(testStoreBaseIndex());
    RUN(testStoreImmediateBaseIndex());

    RUN(testBranchIfType());
    RUN(testBranchIfNotType());
#if CPU(X86_64) || CPU(ARM64)
    RUN(testBranchConvertDoubleToInt52());
#endif

    RUN(testOrImmMem());

    RUN(testAndOrDouble());

    RUN(testGPRInfoConsistency());

    if (tasks.isEmpty())
        usage();

    Lock lock;

    Vector<Ref<Thread>> threads;
    for (unsigned i = filter ? 1 : WTF::numberOfProcessorCores(); i--;) {
        threads.append(
            Thread::create(
                "testmasm thread"_s,
                [&] () {
                    for (;;) {
                        RefPtr<SharedTask<void()>> task;
                        {
                            Locker locker { lock };
                            if (tasks.isEmpty())
                                return;
                            task = tasks.takeFirst();
                        }

                        task->run();
                    }
                }));
    }

    for (auto& thread : threads)
        thread->waitForCompletion();
    crashLock.lock();
    dataLog("Completed ", numberOfTests, " tests\n");
}

} // anonymous namespace

#else // not ENABLE(JIT)

static void run(const char*)
{
    dataLog("JIT is not enabled.\n");
}

#endif // ENABLE(JIT)

int main(int argc, char** argv)
{
    const char* filter = nullptr;
    switch (argc) {
    case 1:
        break;
    case 2:
        filter = argv[1];
        break;
    default:
        usage();
        break;
    }

    run(filter);
    return 0;
}

#if OS(WINDOWS)
extern "C" __declspec(dllexport) int WINAPI dllLauncherEntryPoint(int argc, const char* argv[])
{
    return main(argc, const_cast<char**>(argv));
}
#endif

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
