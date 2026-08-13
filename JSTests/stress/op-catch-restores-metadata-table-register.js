// A strict-mode indexed store whose Proxy set trap returns false throws out of the by-val inline
// cache. Nothing between the throw and the handler copies the handler frame's callee saves into the
// entry frame buffer, so op_catch restores a metadata table belonging to another CodeBlock and has to
// rematerialize metadataTableRegister. A named store (proxy.x) takes a different path and does not
// reach this, so keep the subscript numeric.

globalThis.testLoopCount ??= 1e4;

(function() {
    "use strict";
    const proxy = new Proxy({}, { set: function() { return false; } });
    for (let i = 0; i < testLoopCount; ++i) {
        let threw = false;
        try {
            proxy[42] = 40;
        } catch (e) {
            threw = e instanceof TypeError;
        }
        if (!threw)
            throw new Error("strict indexed store through a rejecting proxy set trap should throw a TypeError");
    }
})();
