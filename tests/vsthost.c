// tests/vsthost.c -- minimal VST2 host (run by tools/test_arm.sh): dlopen the plugin as MPC does, restore a chunk, play a note, read parameter names and text.
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic; intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t); void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t); int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2; int32_t initialDelay, realQualities, offQualities; float ioRatio; void *object, *user;
    int32_t uniqueID, version; void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t); char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char midiData[4]; char d, v, r1, r2; } VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; void *events[2]; } VstEvents;
static intptr_t master(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float f) { return op == 1 ? 2400 : 0; }
static AEffect *fx; static float L[128], R[128], *outs[2] = {L, R};
static void midi(int a, int b, int c) {
    VstMidiEvent m = {1, sizeof m, 0, 0, 0, 0, {a, b, c, 0}}; VstEvents ev = {1, 0, {&m, 0}};
    fx->dispatcher(fx, 25, 0, 0, &ev, 0);
}
static double run(int blocks) {
    double e = 0;
    for (int k = 0; k < blocks; k++) { fx->processReplacing(fx, 0, outs, 128); for (int i = 0; i < 128; i++) e += L[i] * L[i]; }
    return sqrt(e / (blocks * 128));
}
static int find(const char *name) {
    char b[256];
    for (int i = 0; i < fx->numParams; i++) { b[0] = 0; fx->dispatcher(fx, 8, i, 0, b, 0); if (!strcmp(b, name)) return i; }
    return -1;
}
static const char *disp(int i) { static char bb[8][256]; static int r; char *b = bb[r++ & 7]; b[0] = 0; fx->dispatcher(fx, 7, i, 0, b, 0); return b; }
static const char *pname(int i) { static char b[256]; b[0] = 0; fx->dispatcher(fx, 8, i, 0, b, 0); return b; }
static void settle(void) { for (int k = 0; k < 40; k++) { run(10); usleep(20000); } }
int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); return 1; }
    AEffect *(*entry)(audioMasterCallback) = (AEffect *(*)(audioMasterCallback))dlsym(h, "VSTPluginMain");
    fx = entry(master);
    printf("magic %08x, %d params, synth=%d, chunks=%d\n", fx->magic, fx->numParams, !!(fx->flags & 256), !!(fx->flags & 32));
    fx->dispatcher(fx, 0, 0, 0, 0, 0); fx->dispatcher(fx, 10, 0, 0, 0, 44100); fx->dispatcher(fx, 11, 0, 128, 0, 0);
    fx->dispatcher(fx, 12, 0, 1, 0, 0);
    settle();
    int fn = find("File");
    printf("start: File=\"%s\"\n", disp(fn));
    printf("defaults: Polyphony=%s Sample=%s Oscillator=%s\n", disp(1), disp(5), disp(6));
    midi(0x90, 60, 100); printf("default instrument note rms %.4f\n", run(100)); midi(0x80, 60, 0); run(50);
    for (int a = 2; a < argc; a++) {
        char chunk[512];
        snprintf(chunk, sizeof chunk, "sfizz-mpc 1\nfile=%s\nvolume=100\npan=64\nsample_quality=3\n", argv[a]);
        fx->dispatcher(fx, 24, 0, strlen(chunk) + 1, chunk, 0);
        settle();
        for (int w = 0; w < 600 && !strcmp(disp(fn), "Loading..."); w++) settle();
        printf("--- %s\nFile=\"%s\"  Regions=%s  Samples=%s  Memory=%s\n", argv[a], disp(fn), disp(find("Regions")),
               disp(find("Samples")), disp(find("Memory")));
        printf("Key switch: has=%s badge=\"%s\" label=\"%s\"\n", disp(find("Has key switches")), disp(find("Key switch note")),
               disp(find("Key switch")));
        for (int s = 1; s <= 8; s++) {   /* the Controls panel: shown slots, their CC badge */
            char shown[16], num[16];
            snprintf(shown, sizeof shown, "CC %d shown", s);
            snprintf(num, sizeof num, "CC %d number", s);
            if (fx->getParameter(fx, find(shown)) > 0.5f) printf("  slot %d: %s\n", s, disp(find(num)));
        }
        for (int q = 11; q <= 20; q++) printf("  param %d name=\"%s\" value=\"%s\"\n", q, pname(q), disp(q));
        midi(0x90, 60, 110); printf("note 60 rms %.4f", run(150));
        printf(", piano key C4 shown down: %s", disp(find("Key C4 down")));   /* the strip's pressed key */
        midi(0x80, 60, 0); run(100); printf(", after note-off: %s\n", disp(find("Key C4 down")));
        char *c = 0; intptr_t n = fx->dispatcher(fx, 23, 0, 0, &c, 0);
        printf("chunk %ld bytes, first line: %.*s\n", (long)n, (int)strcspn(c, "\n"), c);
    }
    fx->dispatcher(fx, 12, 0, 0, 0, 0); fx->dispatcher(fx, 1, 0, 0, 0, 0);
    printf("closed OK\n");
    return 0;
}
