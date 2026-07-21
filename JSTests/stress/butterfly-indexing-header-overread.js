//@ runDefault("--useJIT=0")
// A property-only butterfly (no indexing header) does not allocate the indexing-header
// slot, but JSObject::allocateMoreOutOfLineStorage reads oldButterfly->indexingHeader()
// on the grow path, and the compiler hoists that load ahead of the indexingType guard
// that would have skipped it. The read is one slot past the allocation; it faults when
// the butterfly is the last cell before a page bmalloc has not mapped. Adding many
// out-of-line properties reallocates the butterfly repeatedly, and on 32-bit the
// auxiliary heap reaches a fresh bmalloc chunk edge during warm-up, so the read lands on
// the unmapped next chunk. This crashes deterministically on 32-bit.
//
// On 64-bit the over-read target is almost always mapped, so it does not fault here. To
// observe it there, hold the objects so the size-class frontier marches to a chunk edge
// (do not commit that variant as a test: holding enough objects exceeds the memory-
// limited cap), and loop because the hit depends on the bmalloc layout (measured ~1/10):
//
//   for i in $(seq 30); do LD_LIBRARY_PATH=.../lib .../bin/jsc /tmp/groom.js --useJIT=0 || break; done
//   # /tmp/groom.js: keep=[]; for(r=0;r<40;r++){b=[];for(n=0;n<20000;n++){o={};
//   #   for(k=0;k<60;k++)o["p"+k]=k;b.push(o)} for(i=b.length-1;i>=0;i--){o=b[i];
//   #   for(k=60;k<80;k++)o["p"+k]=k} keep.push(b)}
for (var i = 0; i < 100000; i++) {
    var o = {};
    for (var j = 0; j < 20; j++)
        o["k" + j] = j;
}
