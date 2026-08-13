// A stub records one spill state shared by all of its access cases, so every case that calls out
// to JS has to spill the same registers. This inline cache sees both a Proxy and a JS getter.
const target = { p: 1 };
const proxy = new Proxy(target, { get(t, k) { return t[k]; } });
const withGetter = {};
Object.defineProperty(withGetter, "p", { get: function () { return 2; } });

function f(o) { return o.p; }
noInline(f);

globalThis.testLoopCount ??= 1e4;
for (let i = 0; i < testLoopCount; ++i) {
    if (f(proxy) !== 1)
        throw new Error("proxy load returned the wrong value");
    if (f(withGetter) !== 2)
        throw new Error("getter load returned the wrong value");
}
