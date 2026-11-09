/* kb-bridge: Translates USB keyboard+mouse into touch gestures for games
 * that lack native KB/M support. Runs as root alongside touch-bridge.
 *
 * Key bindings:
 *   WASD        → left joystick
 *   Shift       → sprint (joystick pushed further)
 *   Mouse move  → camera look (right-side drag)
 *   Left click  → fire (right fire btn, auto-ADS)
 *   Right click → ADS (sight button, far right)
 *   Q/E         → lean left/right (auto ADS)
 *   R           → reload
 *   F           → interact (? doors/containers)
 *   H           → pickup (item/gun prompt)
 *   C           → crouch
 *   V           → prone
 *   Space       → jump
 *   Z           → zipline
 *   G           → Gadget 1
 *   X           → Gadget 2
 *   B           → Tactical Gear
 *   1/2/3       → weapon slots
 *   4           → knife
 *   Esc         → back/cancel
 *
 * Calibrated for OnePlus 7T (2400x1080 landscape, rotation=1,
 * raw panel 1080x2400). All positions are fractional (0.0-1.0)
 * relative to the landscape screen.
 */
#define _GNU_SOURCE
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ==================== SCREEN / PANEL ==================== */
#define SCREEN_W 2400
#define SCREEN_H 1080
#define RAW_XMAX 1078
#define RAW_YMAX 2398

/* ==================== TOUCH SLOTS ==================== */
#define SLOT_MOVE       0
#define SLOT_LOOK       1
#define SLOT_FIRE       2
#define SLOT_ADS        3
#define SLOT_LEAN_L     4
#define SLOT_LEAN_R     5
#define SLOT_JUMP       6
#define SLOT_CROUCH     7
#define SLOT_PRONE      8
#define SLOT_RELOAD     9
#define SLOT_INTERACT  10
#define SLOT_PICKUP    11
#define SLOT_ZIPLINE   12
#define SLOT_GADGET    13
#define SLOT_WEAPON    14
#define SLOT_BACK      15
#define NUM_SLOTS      16

/* ==================== HUD POSITIONS (fractional) ====================
 * Mapped from firing-range screenshot on OnePlus 7T (2400x1080).
 * All values are fraction of landscape screen.                       */

/* Left joystick */
#define JOY_CX          0.133f
#define JOY_CY          0.628f
#define JOY_RADIUS      0.08f
#define SPRINT_MULT     1.6f

/* Camera look anchor (right side, clear area) */
#define LOOK_CX         0.75f
#define LOOK_CY         0.40f

/* Right fire button (large circle, auto-ADS) */
#define FIRE_R_X        0.650f
#define FIRE_R_Y        0.590f

/* Sight / ADS button (X icon when active, far right edge) */
#define ADS_X           0.943f
#define ADS_Y           0.538f

/* Lean left (figure, upper-left cluster) */
#define LEAN_L_X        0.127f
#define LEAN_L_Y        0.406f

/* Lean right (figure, next to lean left) */
#define LEAN_R_X        0.210f
#define LEAN_R_Y        0.417f

/* Reload (two bullets icon, below fire, above Gadget 2) */
#define RELOAD_X        0.650f
#define RELOAD_Y        0.700f

/* Interact - ? button (doors, containers) */
#define INTERACT_X      0.650f
#define INTERACT_Y      0.470f

/* Pickup - gun/item prompt button (above ? button) */
#define PICKUP_X        0.650f
#define PICKUP_Y        0.400f

/* Jump (right under sight button) */
#define JUMP_X          0.947f
#define JUMP_Y          0.702f

/* Crouch (lowest left in right-side cluster) */
#define CROUCH_X        0.852f
#define CROUCH_Y        0.940f

/* Prone (right of crouch, slightly above) */
#define PRONE_X         0.928f
#define PRONE_Y         0.888f

/* Zipline (right of fire button) */
#define ZIPLINE_X       0.730f
#define ZIPLINE_Y       0.590f

/* Gadget 1 */
#define GADGET1_X       0.800f
#define GADGET1_Y       0.490f

/* Gadget 2 */
#define GADGET2_X       0.650f
#define GADGET2_Y       0.780f

/* Tactical Gear */
#define TACT_X          0.860f
#define TACT_Y          0.490f

/* Weapon slots (bottom center bar) */
#define WEAPON1_X       0.310f
#define WEAPON1_Y       0.722f
#define WEAPON2_X       0.385f
#define WEAPON2_Y       0.722f
#define WEAPON3_X       0.430f
#define WEAPON3_Y       0.722f
/* Knife slot (above pistol slot) */
#define KNIFE_X         0.430f
#define KNIFE_Y         0.667f

/* Back / cancel */
#define BACK_X          0.025f
#define BACK_Y          0.040f

/* Mouse sensitivity: raw mouse units per screen pixel of drag */
#define MOUSE_SENS      3.0f

/* ==================== STATE ==================== */
static volatile sig_atomic_t running = 1;
static int kb_fd = -1, mouse_fd = -1, virtual_fd = -1;
static int next_id = 1;
struct Slot { int active, id, x, y; };
static struct Slot slots[NUM_SLOTS];
static int key_w, key_a, key_s, key_d, key_shift;
static float look_sx, look_sy;
static int look_active;
static long long look_last_move;
#define TAP_HOLD_MS 60
static long long tap_release_at[NUM_SLOTS];

/* ==================== HELPERS ==================== */
static long long ms_now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void stop_handler(int sig) { (void)sig; running = 0; }

static int emit(int type, int code, int value) {
    struct input_event e = {0};
    e.type = type; e.code = code; e.value = value;
    return write(virtual_fd, &e, sizeof(e)) == sizeof(e);
}
static int alloc_id(void) {
    int id = next_id++; if (next_id > 65535) next_id = 1; return id;
}
static void sync_frame(void) {
    int any = 0;
    for (int i = 0; i < NUM_SLOTS; i++) any |= slots[i].active;
    emit(EV_KEY, BTN_TOUCH, any);
    emit(EV_SYN, SYN_REPORT, 0);
}
static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void screen_to_raw(float sx, float sy, int *rx, int *ry) {
    float nx = sx / (SCREEN_W - 1), ny = sy / (SCREEN_H - 1);
    *rx = clamp_i(lroundf(ny * RAW_XMAX), 0, RAW_XMAX);
    *ry = clamp_i(lroundf((1.0f - nx) * RAW_YMAX), 0, RAW_YMAX);
}

static void slot_down(int slot, float sx, float sy) {
    int rx, ry; screen_to_raw(sx, sy, &rx, &ry);
    struct Slot *s = &slots[slot];
    if (!s->active) {
        s->id = alloc_id(); s->active = 1;
        emit(EV_ABS, ABS_MT_SLOT, slot);
        emit(EV_ABS, ABS_MT_TRACKING_ID, s->id);
        emit(EV_ABS, ABS_MT_TOUCH_MAJOR, 8);
    } else emit(EV_ABS, ABS_MT_SLOT, slot);
    s->x = rx; s->y = ry;
    emit(EV_ABS, ABS_MT_POSITION_X, rx);
    emit(EV_ABS, ABS_MT_POSITION_Y, ry);
}
static void slot_up(int slot) {
    if (!slots[slot].active) return;
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    slots[slot].active = 0;
}

/* Tap helpers: fractional coords → screen pixels */
static void hold_down(int slot, float fx, float fy) {
    slot_down(slot, fx * SCREEN_W, fy * SCREEN_H);
}
static void hold_up(int slot) { slot_up(slot); }
static void tap(int slot, float fx, float fy) {
    hold_down(slot, fx, fy);
    tap_release_at[slot] = ms_now() + TAP_HOLD_MS;
}

/* ==================== JOYSTICK (WASD) ==================== */
static void update_joystick(void) {
    int dx = (key_d ? 1 : 0) - (key_a ? 1 : 0);
    int dy = (key_s ? 1 : 0) - (key_w ? 1 : 0);
    if (dx == 0 && dy == 0) { slot_up(SLOT_MOVE); return; }
    float fdx = (float)dx, fdy = (float)dy;
    float len = sqrtf(fdx * fdx + fdy * fdy);
    if (len > 0) { fdx /= len; fdy /= len; }
    float mult = key_shift ? SPRINT_MULT : 1.0f;
    float radius = JOY_RADIUS * SCREEN_W * mult;
    if (!slots[SLOT_MOVE].active) {
        slot_down(SLOT_MOVE, JOY_CX * SCREEN_W, JOY_CY * SCREEN_H);
        sync_frame();
    }
    slot_down(SLOT_MOVE, JOY_CX * SCREEN_W + fdx * radius,
                         JOY_CY * SCREEN_H + fdy * radius);
}

/* ==================== MOUSE LOOK ==================== */
static void update_look(float rel_x, float rel_y) {
    if (!look_active) {
        look_sx = LOOK_CX * SCREEN_W; look_sy = LOOK_CY * SCREEN_H;
        look_active = 1;
        slot_down(SLOT_LOOK, look_sx, look_sy); sync_frame();
    }
    look_sx += rel_x / MOUSE_SENS;
    look_sy += rel_y / MOUSE_SENS;
    float min_x = 0.52f * SCREEN_W, max_x = 0.98f * SCREEN_W;
    float min_y = 0.02f * SCREEN_H, max_y = 0.98f * SCREEN_H;
    if (look_sx < min_x || look_sx > max_x || look_sy < min_y || look_sy > max_y) {
        slot_up(SLOT_LOOK); sync_frame();
        look_sx = LOOK_CX * SCREEN_W; look_sy = LOOK_CY * SCREEN_H;
        slot_down(SLOT_LOOK, look_sx, look_sy); sync_frame();
    }
    slot_down(SLOT_LOOK, look_sx, look_sy);
    look_last_move = ms_now();
}
static void check_look_idle(void) {
    if (look_active && ms_now() - look_last_move > 100) {
        slot_up(SLOT_LOOK); sync_frame(); look_active = 0;
    }
}

/* ==================== DEVICE DISCOVERY ==================== */
static int is_keyboard(int fd) {
    unsigned long evbits[2] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) return 0;
    if (!(evbits[0] & (1UL << EV_KEY))) return 0;
    unsigned long keybits[12] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0) return 0;
    return (keybits[17/64] & (1UL << (17%64))) && (keybits[30/64] & (1UL << (30%64)));
}
static int is_mouse(int fd) {
    unsigned long evbits[2] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) return 0;
    if (!(evbits[0] & (1UL << EV_REL))) return 0;
    unsigned long relbits[1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits) < 0) return 0;
    return (relbits[0] & (1UL << REL_X)) && (relbits[0] & (1UL << REL_Y));
}
static int find_devices(void) {
    for (int n = 0; n < 20; n++) {
        char path[32]; snprintf(path, sizeof(path), "/dev/input/event%d", n);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (strstr(name, "Delta") || strstr(name, "uinput")) { close(fd); continue; }
        unsigned long props = 0;
        ioctl(fd, EVIOCGPROP(sizeof(props)), &props);
        if (props & (1UL << INPUT_PROP_DIRECT)) { close(fd); continue; }
        if (kb_fd < 0 && is_keyboard(fd)) {
            kb_fd = fd;
            fprintf(stderr, "[kb-bridge] keyboard: %s (%s)\n", path, name);
            continue;
        }
        if (mouse_fd < 0 && is_mouse(fd)) {
            mouse_fd = fd;
            fprintf(stderr, "[kb-bridge] mouse: %s (%s)\n", path, name);
            continue;
        }
        close(fd);
    }
    return kb_fd >= 0 || mouse_fd >= 0;
}

/* ==================== UINPUT SETUP ==================== */
static int setup_virtual(void) {
    virtual_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (virtual_fd < 0) return 0;
    ioctl(virtual_fd, UI_SET_EVBIT, EV_SYN);
    ioctl(virtual_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(virtual_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(virtual_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    ioctl(virtual_fd, UI_SET_KEYBIT, BTN_TOUCH);
    int axes[] = {ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X,
                  ABS_MT_POSITION_Y, ABS_MT_TOUCH_MAJOR};
    for (unsigned i = 0; i < sizeof(axes)/sizeof(*axes); i++) ioctl(virtual_fd, UI_SET_ABSBIT, axes[i]);
    struct uinput_user_dev dev = {0};
    snprintf(dev.name, sizeof(dev.name), "Delta KB Bridge");
    dev.id.bustype = BUS_VIRTUAL; dev.id.vendor = 0x18d1;
    dev.id.product = 0xd17b; dev.id.version = 1;
    dev.absmax[ABS_MT_SLOT] = 9;
    dev.absmax[ABS_MT_TRACKING_ID] = 65535;
    dev.absmax[ABS_MT_POSITION_X] = RAW_XMAX;
    dev.absmax[ABS_MT_POSITION_Y] = RAW_YMAX;
    dev.absmax[ABS_MT_TOUCH_MAJOR] = 255;
    if (write(virtual_fd, &dev, sizeof(dev)) != sizeof(dev)) return 0;
    return ioctl(virtual_fd, UI_DEV_CREATE) >= 0;
}

/* ==================== CLEANUP ==================== */
static void cleanup(void) {
    if (virtual_fd >= 0) {
        for (int i = 0; i < NUM_SLOTS; i++) slot_up(i);
        sync_frame();
        ioctl(virtual_fd, UI_DEV_DESTROY); close(virtual_fd); virtual_fd = -1;
    }
    if (kb_fd >= 0) { ioctl(kb_fd, EVIOCGRAB, 0); close(kb_fd); kb_fd = -1; }
    if (mouse_fd >= 0) { ioctl(mouse_fd, EVIOCGRAB, 0); close(mouse_fd); mouse_fd = -1; }
}

/* ==================== KEYBOARD HANDLER ==================== */
static int log_count = 0;
static void log_action(const char *name, float fx, float fy, int down) {
    if (log_count < 30) {
        int rx, ry; screen_to_raw(fx * SCREEN_W, fy * SCREEN_H, &rx, &ry);
        fprintf(stderr, "[kb-bridge] %s %s → screen(%.0f,%.0f) raw(%d,%d)\n",
                name, down ? "DOWN" : "UP", fx * SCREEN_W, fy * SCREEN_H, rx, ry);
        log_count++;
    }
}
static void handle_key(int code, int value) {
    int pressed = value > 0;
    int fresh = value == 1;
    switch (code) {
    /* Movement */
    case KEY_W: key_w = pressed; update_joystick(); break;
    case KEY_A: key_a = pressed; update_joystick(); break;
    case KEY_S: key_s = pressed; update_joystick(); break;
    case KEY_D: key_d = pressed; update_joystick(); break;
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
        key_shift = pressed;
        if (key_w || key_a || key_s || key_d) update_joystick();
        break;

    /* Lean (hold) */
    case KEY_Q:
        if (pressed) hold_down(SLOT_LEAN_L, LEAN_L_X, LEAN_L_Y);
        else hold_up(SLOT_LEAN_L);
        break;
    case KEY_E:
        if (pressed) hold_down(SLOT_LEAN_R, LEAN_R_X, LEAN_R_Y);
        else hold_up(SLOT_LEAN_R);
        break;

    /* Crouch / Prone (toggle taps) */
    case KEY_C: if (fresh) tap(SLOT_CROUCH, CROUCH_X, CROUCH_Y); break;
    case KEY_V: if (fresh) tap(SLOT_PRONE, PRONE_X, PRONE_Y); break;

    /* Jump (hold) */
    case KEY_SPACE:
        log_action("JUMP", JUMP_X, JUMP_Y, pressed);
        if (pressed) hold_down(SLOT_JUMP, JUMP_X, JUMP_Y);
        else hold_up(SLOT_JUMP);
        break;

    /* Reload (tap) */
    case KEY_R:
        if (fresh) { log_action("RELOAD", RELOAD_X, RELOAD_Y, 1); tap(SLOT_RELOAD, RELOAD_X, RELOAD_Y); }
        break;

    /* Interact (tap) */
    case KEY_F: if (fresh) tap(SLOT_INTERACT, INTERACT_X, INTERACT_Y); break;

    /* Pickup (tap) */
    case KEY_H: if (fresh) tap(SLOT_PICKUP, PICKUP_X, PICKUP_Y); break;

    /* Zipline (tap) */
    case KEY_Z: if (fresh) tap(SLOT_ZIPLINE, ZIPLINE_X, ZIPLINE_Y); break;

    /* Gadget 1 (tap) */
    case KEY_G: if (fresh) tap(SLOT_GADGET, GADGET1_X, GADGET1_Y); break;

    /* Gadget 2 (tap) */
    case KEY_X: if (fresh) tap(SLOT_GADGET, GADGET2_X, GADGET2_Y); break;

    /* Tactical Gear (tap) */
    case KEY_B: if (fresh) tap(SLOT_GADGET, TACT_X, TACT_Y); break;

    /* Weapon slots */
    case KEY_1: if (fresh) tap(SLOT_WEAPON, WEAPON1_X, WEAPON1_Y); break;
    case KEY_2: if (fresh) tap(SLOT_WEAPON, WEAPON2_X, WEAPON2_Y); break;
    case KEY_3: if (fresh) tap(SLOT_WEAPON, WEAPON3_X, WEAPON3_Y); break;
    case KEY_4: if (fresh) tap(SLOT_WEAPON, KNIFE_X, KNIFE_Y); break;

    /* Back / cancel */
    case KEY_ESC: if (fresh) tap(SLOT_BACK, BACK_X, BACK_Y); break;
    }
}

/* ==================== MOUSE HANDLER ==================== */
static void handle_mouse_key(int code, int value) {
    int pressed = value > 0;
    switch (code) {
    case BTN_LEFT:
        log_action("FIRE", FIRE_R_X, FIRE_R_Y, pressed);
        if (pressed) hold_down(SLOT_FIRE, FIRE_R_X, FIRE_R_Y);
        else hold_up(SLOT_FIRE);
        break;
    case BTN_RIGHT:
        log_action("ADS", ADS_X, ADS_Y, pressed);
        if (pressed) hold_down(SLOT_ADS, ADS_X, ADS_Y);
        else hold_up(SLOT_ADS);
        break;
    }
}

/* ==================== MAIN ==================== */
int main(void) {
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGINT, stop_handler); signal(SIGTERM, stop_handler);
    signal(SIGHUP, stop_handler); signal(SIGPIPE, stop_handler);
    atexit(cleanup);

    fprintf(stderr, "[kb-bridge] scanning for keyboard and mouse...\n");
    if (!find_devices()) { fprintf(stderr, "[kb-bridge] no keyboard or mouse found\n"); return 1; }
    if (!setup_virtual()) { fprintf(stderr, "[kb-bridge] failed to create virtual touchscreen\n"); return 1; }

    int grab_kb = kb_fd >= 0 ? (ioctl(kb_fd, EVIOCGRAB, 1) == 0) : -1;
    int grab_mouse = mouse_fd >= 0 ? (ioctl(mouse_fd, EVIOCGRAB, 1) == 0) : -1;
    fprintf(stderr, "[kb-bridge] grab_kb=%d grab_mouse=%d\n", grab_kb, grab_mouse);

    fprintf(stderr, "[kb-bridge] ready\n");
    float mouse_dx = 0, mouse_dy = 0;

    while (running) {
        struct pollfd fds[2]; int nfds = 0;
        if (kb_fd >= 0) { fds[nfds].fd = kb_fd; fds[nfds].events = POLLIN; nfds++; }
        if (mouse_fd >= 0) { fds[nfds].fd = mouse_fd; fds[nfds].events = POLLIN; nfds++; }
        int ret = poll(fds, nfds, 16);
        if (ret < 0 && errno != EINTR) break;

        int need_sync = 0;
        /* Release deferred taps */
        long long now = ms_now();
        for (int i = 0; i < NUM_SLOTS; i++) {
            if (tap_release_at[i] && now >= tap_release_at[i]) {
                slot_up(i); tap_release_at[i] = 0; need_sync = 1;
            }
        }

        for (int fi = 0; fi < nfds; fi++) {
            if (fds[fi].revents & (POLLERR | POLLHUP)) {
                fprintf(stderr, "[kb-bridge] device disconnected (fd %d)\n", fds[fi].fd);
                if (fds[fi].fd == kb_fd) { close(kb_fd); kb_fd = -1; }
                if (fds[fi].fd == mouse_fd) { close(mouse_fd); mouse_fd = -1; }
                if (kb_fd < 0 && mouse_fd < 0) { running = 0; break; }
                continue;
            }
            if (!(fds[fi].revents & POLLIN)) continue;
            struct input_event es[64];
            ssize_t n = read(fds[fi].fd, es, sizeof(es));
            if (n <= 0) continue;
            int count = n / (int)sizeof(struct input_event);
            for (int i = 0; i < count; i++) {
                struct input_event *e = &es[i];
                if (e->type == EV_KEY) {
                    if (fds[fi].fd == kb_fd) handle_key(e->code, e->value);
                    else handle_mouse_key(e->code, e->value);
                    need_sync = 1;
                } else if (e->type == EV_REL) {
                    if (e->code == REL_X) mouse_dx += e->value;
                    else if (e->code == REL_Y) mouse_dy += e->value;
                } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
                    if (mouse_dx != 0 || mouse_dy != 0) {
                        update_look(mouse_dx, mouse_dy);
                        mouse_dx = 0; mouse_dy = 0;
                        need_sync = 1;
                    }
                }
            }
        }
        if (need_sync) sync_frame();
        check_look_idle();
    }
    return 0;
}
