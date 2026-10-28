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
    binds_set_defaults(&kb.binds);
    assert(kb_load_layouts(&kb, NULL) == 2);
    kb_reset_input(&kb);
    for (int c = 0; c < ABS_CNT; c++) { pad.mn[c] = -32768; pad.mx[c] = 32767; }

    /* group 0 (left stick up) , right stick N / E / S / W = a b c d */
    left(0, -1); flick(0, -1); EXPECT("a");
    flick(1, 0); EXPECT("b"); flick(0, 1); EXPECT("c"); flick(-1, 0); EXPECT("d");
    left(0, 0); flick(0, -1); EXPECT("a");

    /* group 2 = i j k l */
    left(1, 0); left(0, 0); flick(0, -1); EXPECT("i");

    /* diagonal slot NE of group 7 */
    left(-0.7f, -0.7f); left(0, 0);
    flick(0.7f, -0.7f); EXPECT("'");

    /* sweep: peak decides */
    left(0, -1); left(0, 0);
    right(1, 0); right(0.92f, 0.38f); right(0.6f, 0.2f); right(0, 0); EXPECT("b");

    /* shift (LT axis) */
    kb_abs(&kb, &pad, ABS_Z, 32767); flick(0, -1); EXPECT("A");
    kb_abs(&kb, &pad, ABS_Z, -32768);

    /* symbols layer on L3 */
    kb_key(&kb, BTN_THUMBL, 1); flick(0, -1); EXPECT("1"); kb_key(&kb, BTN_THUMBL, 1);

    /* russian on R3 */
    kb_key(&kb, BTN_THUMBR, 1); flick(0, -1); EXPECT("\xd0\xb0");
    kb_abs(&kb, &pad, ABS_Z, 32767); flick(0, -1); EXPECT("\xd0\x90"); kb_abs(&kb, &pad, ABS_Z, -32768);

    /* keys: backspace press/hold/release, enter on RT edge, dpad */
    nkeys = 0;
    kb_key(&kb, BTN_EAST, 1); assert(nkeys == 1 && keys[0] == SYM_BACKSPACE);
    assert(kb_tick(&kb, now_ms() + 1000) >= 0 && nkeys == 2);
    kb_key(&kb, BTN_EAST, 0); assert(kb.held == 0);

    /* parser edge cases */
    Layout L[2]; int n = parse_layouts("[x]\nlower: _ a <bar> <us> | b\n", L, 2);
    assert(n == 1 && L[0].g[0][0].t[2][0] == 'a' && !strcmp(L[0].g[0][0].t[4], "|") && !strcmp(L[0].g[0][0].t[6], "_") && !L[0].g[0][0].t[0][0]);

    Pad unused = {0}; unused.fd = -1;
    pad_close(&unused); pad_set_grab_safe(&unused, 0); pad_open_at(&unused, "/dev/null", 0);

    /* DualShock / Xbox identity */
    assert(pad_id_wanted(0x054c, 0x09cc, "Wireless Controller"));
    assert(pad_id_wanted(0x054c, 0x0ce6, "DualSense Wireless Controller"));
    assert(pad_id_wanted(0x045e, 0x0b12, "Xbox Wireless Controller"));
    assert(pad_id_wanted(0, 0, "Xbox 360 Controller"));
    assert(!pad_id_wanted(0, 0, "USB Keyboard"));
    assert(!pad_id_wanted(0, 0, "Synaptics Touchpad"));

    /* Hybrid visibility: Smart / Manual / Always */
    assert(!vis_want(0, 1, PLASMA_VK_ALWAYS, INVOKE_AUTO, 0, 0));
    assert(!vis_want(1, 0, PLASMA_VK_ALWAYS, INVOKE_AUTO, 0, 0));
    assert(!vis_want(1, 1, PLASMA_VK_ALWAYS, INVOKE_AUTO, 0, 1));
    assert(!vis_want(1, 1, PLASMA_VK_NEVER, INVOKE_AUTO, 0, 0));
    assert( vis_want(1, 1, PLASMA_VK_NEVER, INVOKE_AUTO, 1, 0));
    assert(!vis_want(1, 1, PLASMA_VK_TOUCH, INVOKE_AUTO, 0, 0));
    assert( vis_want(1, 1, PLASMA_VK_TOUCH, INVOKE_AUTO, 1, 0));
    assert( vis_want(1, 1, PLASMA_VK_ALWAYS, INVOKE_AUTO, 0, 0));

    /* Manual mode requires user_shown */
    assert(!vis_want(1, 1, PLASMA_VK_ALWAYS, INVOKE_MANUAL, 0, 0));
    assert( vis_want(1, 1, PLASMA_VK_ALWAYS, INVOKE_MANUAL, 1, 0));

    /* Smart mode ignores unactivated field (im_active == 0) */
    assert(!vis_want(0, 1, PLASMA_VK_ALWAYS, INVOKE_SMART, 0, 0));
    assert(!vis_want(0, 1, PLASMA_VK_TOUCH,  INVOKE_SMART, 0, 0));
    assert(!vis_want(0, 1, PLASMA_VK_NEVER,  INVOKE_SMART, 0, 0));

    /* Smart mode opens automatically when input is focused, unless user explicitly hid it */
    assert( vis_want(1, 1, PLASMA_VK_ALWAYS, INVOKE_SMART, 0, 0));
    assert( vis_want(1, 1, PLASMA_VK_TOUCH,  INVOKE_SMART, 0, 0));
    assert( vis_want(1, 1, PLASMA_VK_NEVER,  INVOKE_SMART, 0, 0));
    assert(!vis_want(1, 1, PLASMA_VK_ALWAYS, INVOKE_SMART, 0, 1));

    /* Strict input armed */
    assert(!input_armed_strict(1, 0, 1));
    assert(!input_armed_strict(0, 1, 1));
    assert(!input_armed_strict(1, 1, 0));
    assert( input_armed_strict(1, 1, 1));

    /* Аккорд Select+Start: порядок нажатия не важен, одиночный тап проходит при отпускании */
    {
        Chord c; memset(&c, 0, sizeof c); c.a = BTN_SELECT; c.b = BTN_START;
        assert(chord_event(&c, BTN_SELECT, 1) == CHORD_SWALLOW);
        assert(chord_event(&c, BTN_START,  1) == CHORD_FIRE);
        assert(chord_event(&c, BTN_START,  2) == CHORD_SWALLOW);
        assert(chord_event(&c, BTN_START,  0) == CHORD_SWALLOW);
        assert(chord_event(&c, BTN_SELECT, 0) == CHORD_SWALLOW);
        assert(chord_event(&c, BTN_START,  1) == CHORD_SWALLOW);   /* Start первым — не Enter */
        assert(chord_event(&c, BTN_SELECT, 1) == CHORD_FIRE);
        assert(chord_event(&c, BTN_SELECT, 0) == CHORD_SWALLOW);
        assert(chord_event(&c, BTN_START,  0) == CHORD_SWALLOW);
        assert(chord_event(&c, BTN_SELECT, 1) == CHORD_SWALLOW);   /* одиночный тап */
        assert(chord_event(&c, BTN_SELECT, 0) == CHORD_TAP);
        assert(chord_event(&c, BTN_SOUTH,  1) == CHORD_PASS);      /* чужая кнопка */
        Chord s; memset(&s, 0, sizeof s); s.a = BTN_MODE; s.b = -1;
        assert(chord_event(&s, BTN_MODE, 1) == CHORD_FIRE);
        assert(chord_event(&s, BTN_MODE, 1) == CHORD_SWALLOW);
        assert(chord_event(&s, BTN_MODE, 0) == CHORD_SWALLOW);
        assert(chord_event(&s, BTN_MODE, 1) == CHORD_FIRE);
    }

    /* Дрожание стика в покое не должно вызывать перерисовку */
    kb_reset_input(&kb); kb.dirty = 0;
    left(0.005f, 0.0f); left(0.01f, 0.01f); left(-0.012f, 0.0f);
    assert(!kb.dirty);
    left(0.5f, 0.0f);
    assert(kb.dirty);
    kb.dirty = 0; right(0.01f, 0.0f);
    assert(!kb.dirty);
    right(1, 0); assert(kb.dirty);
    kb.dirty = 0; right(0, 0);
    assert(kb.dirty);                 /* отпускание стика всегда перерисовывается */
    kb_reset_input(&kb);

    puts("all core tests passed");
    return 0;
}