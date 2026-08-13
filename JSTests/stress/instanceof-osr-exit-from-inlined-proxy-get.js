//@ runDefault("--forceOSRExitToLLInt=true", "--thresholdForJITAfterWarmUp=10", "--thresholdForOptimizeAfterWarmUp=20")

// The DFG inlines a proxy's get trap for the Symbol.hasInstance and prototype reads that instanceof
// performs. An OSR exit out of that inlined call returns into the LLInt at op_instanceof's return
// location, which has to finish the operation rather than fall through to the next opcode with the
// destination register still holding whatever was there before.

function assert(b) {
    if (!b)
        throw new Error("Bad assertion");
}

function test(f) {
    for (let i = 0; i < 1000; i++)
        f();
}

// A pass-through proxy, so this instanceof is genuinely true and the site caches a hit first.
test(function() {
    let proxy = new Proxy(function () { }, { });
    assert(new proxy instanceof proxy);
});

// The trap hands back a fresh object every time "prototype" is read, so the instance is never an
// instanceof the proxy.
test(function() {
    let handler = {
        get: function(target, prop) {
            if (prop === "prototype")
                return { };
            return target[prop];
        }
    };
    let proxy = new Proxy(function () { }, handler);
    assert(!(new proxy instanceof proxy));
});
