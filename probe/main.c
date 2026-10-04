/*
 * ps5-dualsense-overlay - DualSense input probe (standalone payload)
 * Copyright (C) 2026 erickdavestech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* pad_probe — paso 1 del overlay: ¿puede un payload leer el DualSense con un juego en primer plano?
 *
 * v2: la v1 falló al ABRIR el mando (scePadOpen 0x809b0081) por usar el usuario de primer plano.
 * Según ps5-native-gamepad-input-research hay que usar sceUserServiceGetInitialUser (el usuario
 * firmado), no el de primer plano. Esta versión prueba varias combinaciones de usuario/método en
 * una sola pasada, se queda con el primer handle válido y entonces lee el mando ~100 veces/segundo
 * durante PROBE_SECONDS, registrando cada cambio en klog (3232) y en /data/pad_probe.log.
 *
 * No toca el juego: abre su propio handle y lo cierra al salir. Copyright (C) 2026. GPL-3.0-or-later.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ps5/klog.h>

#define PROBE_SECONDS  120
#define POLL_US        10000
#define HEARTBEAT_S    5
#define LOG_PATH       "/data/pad_probe.log"
#define RAW_DUMP       0x60

#define NOTIFY_USER_SYSTEM 0xFE
#define PAD_INTERCEPTED    0x80000000u
#define PORT_STANDARD      0

int sceUserServiceInitialize(void *params);
int sceUserServiceGetInitialUser(int *user_id);
int sceUserServiceGetForegroundUser(int *user_id);
int scePadInit(void);
int scePadOpen(int user_id, int type, int index, const void *param);
int scePadGetHandle(int user_id, int type, int index);
int scePadReadState(int handle, void *data);
int scePadGetControllerInformation(int handle, void *info);
int scePadClose(int handle);
int scePadSetProcessPrivilege(int privilege);
int sceNotificationSend(int user_id, bool is_logged, const char *payload);

static FILE *g_log;

static void say(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    klog_printf("[pad_probe] %s\n", line);
    if (g_log) {
        fprintf(g_log, "%s\n", line);
        fflush(g_log);
    }
}

static void notify(const char *msg, const char *sub) {
    char json[1024];
    snprintf(json, sizeof json,
             "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\",\"channelType\":\"Downloads\","
             "\"useCaseId\":\"IDC\",\"toastOverwriteType\":\"No\",\"isImmediate\":true,\"priority\":100,"
             "\"viewData\":{\"message\":{\"body\":\"%s\"},\"subMessage\":{\"body\":\"%s\"}},"
             "\"platformViews\":{\"previewDisabled\":{\"viewData\":{\"icon\":{\"type\":\"Predefined\","
             "\"parameters\":{\"icon\":\"download\"}},\"message\":{\"body\":\"%s\"}}}}},"
             "\"createdDateTime\":\"2026-10-02T00:00:00.000Z\",\"localNotificationId\":\"%d\"}",
             msg, sub, msg, (int)(time(NULL) & 0x7fffffff));
    int rc = sceNotificationSend(NOTIFY_USER_SYSTEM, true, json);
    if (rc < 0)
        say("notificacion fallo rc=0x%08x", rc);
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2;
    uint8_t touches;
    uint16_t tx, ty;
    int32_t connected;
} pad_view_t;

static void decode(const uint8_t *raw, pad_view_t *v) {
    memcpy(&v->buttons, raw + 0x00, 4);
    v->lx = raw[0x04]; v->ly = raw[0x05]; v->rx = raw[0x06]; v->ry = raw[0x07];
    v->l2 = raw[0x08]; v->r2 = raw[0x09];
    v->touches = raw[0x34];
    memcpy(&v->tx, raw + 0x3c, 2);
    memcpy(&v->ty, raw + 0x3e, 2);
    memcpy(&v->connected, raw + 0x4c, 4);
}

static bool moved(int a, int b, int threshold) {
    return abs(a - b) > threshold;
}

static bool changed(const pad_view_t *a, const pad_view_t *b) {
    return a->buttons != b->buttons || a->touches != b->touches || a->connected != b->connected ||
           moved(a->lx, b->lx, 24) || moved(a->ly, b->ly, 24) || moved(a->rx, b->rx, 24) ||
           moved(a->ry, b->ry, 24) || moved(a->l2, b->l2, 32) || moved(a->r2, b->r2, 32);
}

static void describe(const char *tag, double t, int rc, const pad_view_t *v) {
    say("%s t=%6.2fs rc=0x%08x buttons=0x%08x%s L=(%3u,%3u) R=(%3u,%3u) L2=%3u R2=%3u touch=%u(%u,%u) conn=%d",
        tag, t, rc, v->buttons, (v->buttons & PAD_INTERCEPTED) ? " INTERCEPTED" : "",
        v->lx, v->ly, v->rx, v->ry, v->l2, v->r2, v->touches, v->tx, v->ty, v->connected);
}

static void dump(const uint8_t *raw) {
    char hex[RAW_DUMP * 2 + 1];
    for (int i = 0; i < RAW_DUMP; i++)
        snprintf(hex + i * 2, 3, "%02x", raw[i]);
    say("  raw %s", hex);
}

static int try_open(const char *label, int user, bool via_get_handle) {
    if (user < 0) {
        say("  intento %-22s omitido (sin usuario)", label);
        return -1;
    }
    int h = via_get_handle ? scePadGetHandle(user, PORT_STANDARD, 0)
                           : scePadOpen(user, PORT_STANDARD, 0, NULL);
    say("  intento %-22s user=0x%08x -> 0x%08x%s", label, user, h, h >= 0 ? "  OK" : "");
    return h;
}

static int open_pad(void) {
    int initial = -1, foreground = -1;
    int rc = sceUserServiceGetInitialUser(&initial);
    say("GetInitialUser rc=0x%08x user=0x%08x", rc, initial);
    rc = sceUserServiceGetForegroundUser(&foreground);
    say("GetForegroundUser rc=0x%08x user=0x%08x", rc, foreground);

    say("scePadInit rc=0x%08x", scePadInit());
    say("scePadSetProcessPrivilege(1) rc=0x%08x", scePadSetProcessPrivilege(1));

    int h;
    if ((h = try_open("Open(initial)", initial, false)) >= 0) return h;
    if ((h = try_open("GetHandle(initial)", initial, true)) >= 0) return h;
    if (foreground != initial) {
        if ((h = try_open("Open(foreground)", foreground, false)) >= 0) return h;
        if ((h = try_open("GetHandle(foreground)", foreground, true)) >= 0) return h;
    }
    return -1;
}

int main(void) {
    g_log = fopen(LOG_PATH, "w");
    say("=== pad_probe v2: inicio, %d s leyendo el mando ===", PROBE_SECONDS);
    notify("pad_probe: pulsa botones 2 min", "prueba de lectura del mando");

    say("sceUserServiceInitialize rc=0x%08x", sceUserServiceInitialize(NULL));

    int handle = open_pad();
    if (handle < 0) {
        say("fin: ningun metodo abrio el mando (ver intentos arriba)");
        notify("pad_probe: no pude abrir el mando", "mira /data/pad_probe.log");
        if (g_log) fclose(g_log);
        return 1;
    }
    say("handle valido = 0x%08x", handle);

    static uint8_t info[64] __attribute__((aligned(16)));
    if (scePadGetControllerInformation(handle, info) == 0) {
        uint16_t rx, ry;
        memcpy(&rx, info + 0x04, 2);
        memcpy(&ry, info + 0x06, 2);
        say("ControllerInformation: touchpad %ux%u, deviceClass=%d conn=%d",
            rx, ry, *(int32_t *)(info + 0x14), *(int32_t *)(info + 0x10));
    }

    static uint8_t raw[1024] __attribute__((aligned(16)));
    pad_view_t last = {0}, cur;
    uint32_t seen = 0;
    int reads_ok = 0, reads_err = 0, events = 0, last_err = 0;
    double t0 = now_s(), next_beat = 0;
    bool first = true;

    for (double t = 0; t < PROBE_SECONDS; t = now_s() - t0) {
        memset(raw, 0, sizeof raw);
        int rc = scePadReadState(handle, raw);
        if (rc < 0) {
            if (rc != last_err || reads_err % 500 == 0)
                say("scePadReadState error rc=0x%08x (t=%.2fs, errores=%d)", rc, t, reads_err + 1);
            last_err = rc;
            reads_err++;
            usleep(POLL_US);
            continue;
        }
        reads_ok++;
        decode(raw, &cur);
        if (first || changed(&cur, &last)) {
            events++;
            seen |= cur.buttons;
            describe(first ? "primera" : "cambio", t, rc, &cur);
            if (first || cur.buttons != last.buttons)
                dump(raw);
            last = cur;
            first = false;
        }
        if (t >= next_beat) {
            describe("latido", t, rc, &cur);
            next_beat = t + HEARTBEAT_S;
        }
        usleep(POLL_US);
    }

    scePadClose(handle);
    say("=== fin: lecturas ok=%d err=%d, eventos=%d, bits vistos=0x%08x ===",
        reads_ok, reads_err, events, seen & ~PAD_INTERCEPTED);

    char sub[128];
    snprintf(sub, sizeof sub, "ok=%d eventos=%d bits=0x%08x", reads_ok, events, seen & ~PAD_INTERCEPTED);
    notify("pad_probe: terminado", sub);
    if (g_log) fclose(g_log);
    return 0;
}
