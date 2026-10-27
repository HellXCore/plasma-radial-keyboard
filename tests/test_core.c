#define _GNU_SOURCE
#include <locale.h>
#include <assert.h>
#include "kb_core.h"

static char typed[256]; static uint32_t keys[32]; static int nkeys;
static void t_text(void *u, const char *s) { (void)u; strcat(typed, s); }
static void t_key(void *u, uint32_t s) { (void)u; keys[nkeys++] = s; }

static Pad pad; static Kb kb;
static int16_t AMAX = 32767;
static void stick(int xc, int yc, float x, float y) {
    kb_abs(&kb, &pad, xc, (int)(x * AMAX)); kb_abs(&kb, &pad, yc, (int)(y * AMAX));
}
static void left(float x, float y)  { stick(ABS_X, ABS_Y, x, y); }
static void right(float x, float y) { stick(ABS_RX, ABS_RY, x, y); }
static void flick(float x, float y) { right(x, y); right(0, 0); }
#define EXPECT(s) do { if (strcmp(typed, s)) { fprintf(stderr, "FAIL line %d: got '%s' want '%s'\n", __LINE__, typed, s); return 1; } typed[0] = 0; } while (0)

int main(void) {
    setlocale(LC_CTYPE, "C.UTF-8");
    memset(&kb, 0, sizeof kb); kb.text = t_text; kb.key = t_key;
    assert(kb_load_layouts(&kb, NULL) == 2);
    kb_reset_input(&kb);
    for (int c = 0; c < ABS_CNT; c++) { pad.mn[c] = -32768; pad.mx[c] = 32767; }

    /* group 0 (left stick up) , right stick N / E / S / W = a b c d */
    left(0, -1); flick(0, -1); EXPECT("a");
    flick(1, 0); EXPECT("b"); flick(0, 1); EXPECT("c"); flick(-1, 0); EXPECT("d");
    /* left stick released: group stays sticky */
    left(0, 0); flick(0, -1); EXPECT("a");
    /* left stick E -> group 2 = i j k l */
    left(1, 0); left(0, 0); flick(0, -1); EXPECT("i");
    /* diagonal slot NE of group 7 (left stick NW) = ' ... (token 5 -> NE is token index 4) */
    left(-0.7f, -0.7f); left(0, 0);
    flick(0.7f, -0.7f); EXPECT("'");
    /* sweep: right stick pushes to E, slides along the rim to SE, returns => peak decides */
    left(0, -1); left(0, 0);
    right(1, 0); right(0.92f, 0.38f); right(0.6f, 0.2f); right(0, 0); EXPECT("b");
    /* shift (LT axis) */
    kb_abs(&kb, &pad, ABS_Z, 32767); flick(0, -1); EXPECT("A");
    kb_abs(&kb, &pad, ABS_Z, -32768);
    /* symbols layer on L3 */
    kb_key(&kb, BTN_THUMBL, 1); flick(0, -1); EXPECT("1"); kb_key(&kb, BTN_THUMBL, 1);
    /* russian on R3: group 0 N = а, shift -> А, ё in group 1 NE? (token 1 -> slot 2 = E) */
    kb_key(&kb, BTN_THUMBR, 1); flick(0, -1); EXPECT("\xd0\xb0");
    kb_abs(&kb, &pad, ABS_Z, 32767); flick(0, -1); EXPECT("\xd0\x90"); kb_abs(&kb, &pad, ABS_Z, -32768);
    left(0.7f, -0.7f); left(0, 0);                      /* group 1 */
    flick(1, 0); EXPECT("\xd1\x91");                     /* ё */
    /* keys: backspace press/hold/release, enter on RT edge only once, dpad */
    nkeys = 0;
    kb_key(&kb, BTN_EAST, 1); assert(nkeys == 1 && keys[0] == SYM_BACKSPACE);
    assert(kb_tick(&kb, now_ms() + 1000) >= 0 && nkeys == 2);
    kb_key(&kb, BTN_EAST, 0); assert(kb.held == 0);
    kb_abs(&kb, &pad, ABS_RZ, 32767); kb_key(&kb, BTN_TR2, 1); assert(nkeys == 3 && keys[2] == SYM_RETURN);
    kb_abs(&kb, &pad, ABS_HAT0X, 1); assert(keys[nkeys - 1] == SYM_RIGHT);
    kb_abs(&kb, &pad, ABS_HAT0X, 0); assert(kb.held == 0);
    /* parser edge cases */
    Layout L[2]; int n = parse_layouts("[x]\nlower: _ a <bar> <us> | b\n", L, 2);
    assert(n == 1 && L[0].g[0][0].t[2][0] == 'a' && !strcmp(L[0].g[0][0].t[4], "|") && !strcmp(L[0].g[0][0].t[6], "_") && !L[0].g[0][0].t[0][0]);

    Pad unused = {0}; unused.fd = -1;
    pad_close(&unused); pad_set_grab(&unused, 0); pad_open_at(&unused, "/dev/null", 0);

    /* DualShock / Xbox identity */
    assert(pad_id_wanted(0x054c, 0x09cc, "Wireless Controller"));
    assert(pad_id_wanted(0x054c, 0x0ce6, "DualSense Wireless Controller"));
    assert(pad_id_wanted(0x045e, 0x0b12, "Xbox Wireless Controller"));
    assert(pad_id_wanted(0, 0, "Xbox 360 Controller"));
    assert(!pad_id_wanted(0, 0, "USB Keyboard"));
    assert(!pad_id_wanted(0, 0, "Synaptics Touchpad"));

    /* Hybrid visibility: Plasma Never still allows a manual chord. */
    assert(!vis_want(0, 1, PLASMA_VK_ALWAYS, 0, 0, 0));          /* no IM context */
    assert(!vis_want(1, 0, PLASMA_VK_ALWAYS, 0, 0, 0));          /* no pad */
    assert(!vis_want(1, 1, PLASMA_VK_ALWAYS, 0, 0, 1));          /* user hid */
    assert(!vis_want(1, 1, PLASMA_VK_NEVER, 0, 0, 0));           /* Never, no chord */
    assert( vis_want(1, 1, PLASMA_VK_NEVER, 0, 1, 0));           /* Never + chord */
    assert(!vis_want(1, 1, PLASMA_VK_TOUCH, 0, 0, 0));           /* Touch: теперь только по хоткею */
    assert( vis_want(1, 1, PLASMA_VK_TOUCH, 0, 1, 0));           /* Touch + chord */
    assert( vis_want(1, 1, PLASMA_VK_ALWAYS, 0, 0, 0));          /* Always: авто-показ */
    assert(!vis_want(1, 1, PLASMA_VK_ALWAYS, 1, 0, 0));          /* manual, not toggled */
    assert( vis_want(1, 1, PLASMA_VK_ALWAYS, 1, 1, 0));
    assert(!input_armed(1, 0));                                  /* buffer attached, not on screen */
    assert(!input_armed(0, 1));
    assert( input_armed(1, 1));

    puts("all core tests passed");
    return 0;
}
