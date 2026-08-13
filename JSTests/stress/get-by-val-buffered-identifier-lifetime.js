//@ requireOptions("--useZombieMode=1", "--repatchBufferingCountdown=10")

// A by-val inline cache buffers (structure, property name) pairs while it waits for enough cases to
// generate a stub, and every later miss on the site compares against all of them. The subscripts
// here are fresh strings that nothing else refers to once the call returns, so the buffered pairs
// have to keep working across collections that sweep those strings.

function shouldBe(actual, expected) {
    if (actual !== expected)
        throw new Error("bad value: expected " + expected + " but got " + actual);
}

var prefix = "ta";
var suffix = "rgetx";

// A rope, which toPropertyKey resolves in place, so each call caches a different string cell.
function freshKey() {
    return prefix + suffix.substring(0, 4);
}

function makeSite() {
    return new Function("o", "k", "return o[k];");
}

function makeBase(shape, value) {
    var o = {};
    for (var i = 0; i <= shape; ++i)
        o["pad" + shape + "_" + i] = i;
    o.target = value;
    o[0] = value + 1000;
    return o;
}

var siteCount = 16;
var shapeCount = 6;
var sites = [];
var bases = [];
for (var s = 0; s < siteCount; ++s) {
    sites.push(makeSite());
    var group = [];
    for (var shape = 0; shape < shapeCount; ++shape)
        group.push(makeBase(shape + s * shapeCount, shape));
    bases.push(group);
}

function churn() {
    var last = "";
    for (var i = 0; i < 20000; ++i)
        last = ("a" + i) + ("b" + i);
    return last.length;
}

for (var round = 0; round < 40; ++round) {
    for (var s = 0; s < siteCount; ++s) {
        var site = sites[s];
        var group = bases[s];
        for (var i = 0; i < 24; ++i) {
            var shape = i % shapeCount;
            shouldBe(site(group[shape], freshKey()), shape);
        }
    }

    churn();
    gc();
    churn();
    edenGC();

    // Integer subscripts compare against every buffered pair on the site.
    for (var s = 0; s < siteCount; ++s) {
        var group = bases[s];
        for (var i = 0; i < shapeCount; ++i)
            shouldBe(sites[s](group[i], 0), i + 1000);
    }

    // String subscripts compare against them too, with keys that are fresh cells again.
    for (var s = 0; s < siteCount; ++s) {
        var group = bases[s];
        for (var i = 0; i < shapeCount; ++i)
            shouldBe(sites[s](group[i], freshKey()), i);
    }
}
