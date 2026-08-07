// A proxy inline cache emits a JS call, and values the enclosing JIT holds in callee save registers
// have to survive it. Each function below keeps more live integers than ARMv7 has allocatable GPRs,
// so some of them land in the registers that hold metadataTable and jitData there.

function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error(`bad value: ${actual}, expected ${expected}`);
}

const handler = {
    get(target, property) {
        return target[property];
    },
    set(target, property, value) {
        target[property] = value;
        return true;
    },
};

const loadProxy = new Proxy({ field: 42 }, handler);
const indexedProxy = new Proxy({ 0: 7 }, handler);
const storeProxy = new Proxy({ field: 0 }, handler);

function load(proxy, x) {
    const v0 = (x + 1) | 0;
    const v1 = (x + 2) | 0;
    const v2 = (x + 3) | 0;
    const v3 = (x + 4) | 0;
    const v4 = (x + 5) | 0;
    const v5 = (x + 6) | 0;
    const v6 = (x + 7) | 0;
    const v7 = (x + 8) | 0;
    const v8 = (x + 9) | 0;
    const v9 = (x + 10) | 0;
    const v10 = (x + 11) | 0;
    const v11 = (x + 12) | 0;
    const got = proxy.field;
    return (v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + got) | 0;
}
noInline(load);

function loadByVal(proxy, x, index) {
    const v0 = (x + 1) | 0;
    const v1 = (x + 2) | 0;
    const v2 = (x + 3) | 0;
    const v3 = (x + 4) | 0;
    const v4 = (x + 5) | 0;
    const v5 = (x + 6) | 0;
    const v6 = (x + 7) | 0;
    const v7 = (x + 8) | 0;
    const v8 = (x + 9) | 0;
    const v9 = (x + 10) | 0;
    const v10 = (x + 11) | 0;
    const v11 = (x + 12) | 0;
    const got = proxy[index];
    return (v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + got) | 0;
}
noInline(loadByVal);

function store(proxy, x) {
    const v0 = (x + 1) | 0;
    const v1 = (x + 2) | 0;
    const v2 = (x + 3) | 0;
    const v3 = (x + 4) | 0;
    const v4 = (x + 5) | 0;
    const v5 = (x + 6) | 0;
    const v6 = (x + 7) | 0;
    const v7 = (x + 8) | 0;
    const v8 = (x + 9) | 0;
    const v9 = (x + 10) | 0;
    const v10 = (x + 11) | 0;
    const v11 = (x + 12) | 0;
    proxy.field = x;
    return (v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11) | 0;
}
noInline(store);

// The first call runs before any inline cache exists, so its result is the oracle for every tier.
const expectedLoad = load(loadProxy, 1);
const expectedLoadByVal = loadByVal(indexedProxy, 1, 0);
const expectedStore = store(storeProxy, 1);

for (let i = 0; i < 5e4; ++i) {
    shouldBe(load(loadProxy, 1), expectedLoad);
    shouldBe(loadByVal(indexedProxy, 1, 0), expectedLoadByVal);
    shouldBe(store(storeProxy, 1), expectedStore);
    shouldBe(storeProxy.field, 1);
}
