/*
 * ps5-dualsense-overlay - DualSense input probe (ptrace RPC)
 * Copyright (C) 2026 erickdavestech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* pad_rpc — paso 1 (v3) del overlay: leer el DualSense desde DENTRO de un proceso que ya posee
 * el mando, vía ptrace-RPC (técnica de Ghostpad / elfldr). La v1-v2 confirmaron que un payload
 * suelto NO puede abrir el mando (scePadOpen 0x809b0081): el mando pertenece al proceso en primer
 * plano. Aquí nos enganchamos con ptrace a ese proceso y llamamos scePadGetHandle/scePadReadState
 * en SU contexto, copiando los 120 bytes de vuelta con pt_copyout.
 *
 * Enumera procesos, prueba primero el juego en primer plano y luego SceShellCore, lee ~READ_SECS
 * segundos a ~READ_HZ y registra cada cambio en klog (3232) y en /data/pad_rpc.log. SIEMPRE se
 * desengancha (pt_detach). Mientras está enganchado el proceso objetivo queda congelado unos
 * segundos (el mando se lee igual porque su estado es de hardware); al soltar, sigue normal.
 *
 * OJO: esto es una PRUEBA. Un fallo en la llamada remota puede cerrar el proceso objetivo (el juego
 * o el shell se reinician solos; no hay riesgo para la consola). Copyright (C) 2026. GPL-3.0-or-later.
 */
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ps5/klog.h>
#include <ps5/kernel.h>

#include "pt/pt.h"

long pt_call(pid_t pid, intptr_t addr, ...);

#define READ_SECS      5
#define READ_HZ        15
#define LOG_PATH       "/data/pad_rpc.log"
#define RAW_DUMP       0x60
#define PORT_STANDARD  0
#define PAD_INTERCEPTED 0x80000000u
#define NOTIFY_USER_SYSTEM 0xFE

#define NID_scePadGetHandle                "u1GRHp+oWoY"
#define NID_scePadOpen                     "xk0AcarP3V4"
#define NID_scePadReadState                "YndgXqQVV7c"
#define NID_scePadGetControllerInformation "gjP9/KQzoUk"
#define NID_GetForegroundUser              "eNb53LQJmIM"

int sceNotificationSend(int user_id, bool is_logged, const char *payload);

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    uint32_t app_type;
    char title_id[10];
    char unknown2[0x3c];
} app_info_t;
int sceKernelGetAppInfo(pid_t pid, app_info_t *info);

static FILE *g_log;

static void say(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    klog_printf("[pad_rpc] %s\n", line);
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
    sceNotificationSend(NOTIFY_USER_SYSTEM, true, json);
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2, touches;
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

static bool moved(int a, int b, int th) { return abs(a - b) > th; }

static bool changed(const pad_view_t *a, const pad_view_t *b) {
    return a->buttons != b->buttons || a->touches != b->touches || a->connected != b->connected ||
           moved(a->lx, b->lx, 24) || moved(a->ly, b->ly, 24) || moved(a->rx, b->rx, 24) ||
           moved(a->ry, b->ry, 24) || moved(a->l2, b->l2, 32) || moved(a->r2, b->r2, 32);
}

static void describe(const char *tag, double t, const pad_view_t *v) {
    say("  %-7s t=%5.2fs buttons=0x%08x%s L=(%3u,%3u) R=(%3u,%3u) L2=%3u R2=%3u touch=%u(%u,%u) conn=%d",
        tag, t, v->buttons, (v->buttons & PAD_INTERCEPTED) ? " INTERCEPTED" : "",
        v->lx, v->ly, v->rx, v->ry, v->l2, v->r2, v->touches, v->tx, v->ty, v->connected);
}

static uint32_t module_handle(pid_t pid, const char *base) {
    const char *suffixes[] = {".sprx", ".prx", "", NULL};
    char name[64];
    for (int i = 0; suffixes[i]; i++) {
        uint32_t h = 0;
        snprintf(name, sizeof name, "%s%s", base, suffixes[i]);
        if (kernel_dynlib_handle(pid, name, &h) == 0 && h)
            return h;
    }
    return 0;
}

static uint32_t probe_process(pid_t pid, const char *name) {
    uint32_t h_pad = module_handle(pid, "libScePad");
    if (!h_pad)
        return 0;

    uint32_t h_usr = module_handle(pid, "libSceUserService");
    intptr_t get_handle = kernel_dynlib_resolve(pid, h_pad, NID_scePadGetHandle);
    intptr_t pad_open   = kernel_dynlib_resolve(pid, h_pad, NID_scePadOpen);
    intptr_t read_state = kernel_dynlib_resolve(pid, h_pad, NID_scePadReadState);
    intptr_t get_fg     = h_usr ? kernel_dynlib_resolve(pid, h_usr, NID_GetForegroundUser) : 0;
    say("== objetivo %s (pid %d): libScePad handle=0x%x ==", name, pid, h_pad);
    say("  GetHandle=%p Open=%p ReadState=%p FgUser=%p",
        (void *)get_handle, (void *)pad_open, (void *)read_state, (void *)get_fg);
    if (!read_state || (!get_handle && !pad_open)) {
        say("  funciones del mando no resueltas -> salto");
        return 0;
    }

    if (pt_attach(pid)) {
        say("  pt_attach fallo (errno=%d) -> salto", errno);
        return 0;
    }

    uint32_t seen = 0;
    intptr_t scratch = 0;
    do {
        scratch = pt_mmap(pid, 0, 0x4000, PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (scratch == -1 || !scratch) {
            say("  pt_mmap fallo -> salto");
            scratch = 0;
            break;
        }

        int user = -1;
        if (get_fg) {
            pt_setint(pid, scratch, -1);
            long rc = pt_call(pid, get_fg, scratch);
            user = pt_getint(pid, scratch);
            say("  GetForegroundUser(interno) rc=0x%lx user=0x%08x", rc, user);
        }

        int handle = -1;
        if (get_handle) {
            handle = (int)pt_call(pid, get_handle, (uint64_t)(unsigned)user, PORT_STANDARD, 0);
            say("  scePadGetHandle(interno) user=0x%08x -> 0x%08x", user, handle);
        }
        if (handle < 0 && pad_open) {
            handle = (int)pt_call(pid, pad_open, (uint64_t)(unsigned)user, PORT_STANDARD, 0, 0);
            say("  scePadOpen(interno) user=0x%08x -> 0x%08x", user, handle);
        }
        if (handle < 0) {
            say("  sin handle en este proceso -> salto");
            break;
        }
        say("  handle valido = 0x%08x, leyendo %d s...", handle, READ_SECS);

        pad_view_t last = {0}, cur;
        uint8_t raw[0x80];
        int events = 0, reads_ok = 0;
        double t0 = now_s();
        bool first = true;
        for (double t = 0; t < READ_SECS; t = now_s() - t0) {
            long rc = pt_call(pid, read_state, (uint64_t)(unsigned)handle, (uint64_t)scratch);
            if (rc < 0) {
                say("  scePadReadState(interno) rc=0x%lx", rc);
                break;
            }
            if (pt_copyout(pid, scratch, raw, sizeof raw)) {
                say("  pt_copyout fallo");
                break;
            }
            reads_ok++;
            decode(raw, &cur);
            if (first || changed(&cur, &last)) {
                events++;
                seen |= cur.buttons;
                describe(first ? "primera" : "cambio", t, &cur);
                if (first || cur.buttons != last.buttons) {
                    char hex[RAW_DUMP * 2 + 1];
                    for (int i = 0; i < RAW_DUMP; i++) snprintf(hex + i * 2, 3, "%02x", raw[i]);
                    say("    raw %s", hex);
                }
                last = cur;
                first = false;
            }
            usleep(1000000 / READ_HZ);
        }
        say("  resumen %s: lecturas=%d eventos=%d bits=0x%08x%s",
            name, reads_ok, events, seen & ~PAD_INTERCEPTED,
            (seen & PAD_INTERCEPTED) ? " (INTERCEPTED: entrada no es de este proceso)" : "");
    } while (0);

    if (scratch)
        pt_munmap(pid, scratch, 0x4000);
    pt_detach(pid, 0);
    say("  desenganchado de %s", name);
    return seen;
}

int main(void) {
    g_log = fopen(LOG_PATH, "w");
    say("=== pad_rpc v3: lectura del mando via ptrace ===");
    notify("pad_rpc: pulsa botones ahora", "prueba de lectura via ptrace");

    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t sz = 0;
    if (sysctl(mib, 4, NULL, &sz, NULL, 0)) {
        say("sysctl size fallo");
        if (g_log) fclose(g_log);
        return 1;
    }
    void *buf = malloc(sz);
    if (!buf || sysctl(mib, 4, buf, &sz, NULL, 0)) {
        say("sysctl fallo");
        free(buf);
        if (g_log) fclose(g_log);
        return 1;
    }

    struct { pid_t pid; char name[32]; } cand[128];
    int ncand = 0;
    for (void *p = buf; p < buf + sz && ncand < 128;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        p += ki->ki_structsize;
        app_info_t ai;
        if (sceKernelGetAppInfo(ki->ki_pid, &ai)) memset(&ai, 0, sizeof ai);
        if (strstr(ki->ki_comm, "eboot.bin") || ai.app_id)
            say("  pid=%-6u app_id=%04x title=%-10s %s", ki->ki_pid, ai.app_id,
                ai.title_id[0] ? ai.title_id : "-", ki->ki_comm);
        cand[ncand].pid = ki->ki_pid;
        snprintf(cand[ncand].name, sizeof cand[ncand].name, "%s", ki->ki_comm);
        ncand++;
    }
    free(buf);
    say("--- barriendo %d procesos (solo se engancha a los que tengan libScePad) ---", ncand);

    uint32_t any = 0;
    int found = 0;
    for (int i = 0; i < ncand; i++) {
        uint32_t r = probe_process(cand[i].pid, cand[i].name);
        if (r) { found++; any |= r; }
    }
    if (!found)
        say("ningun proceso tiene libScePad (normal en el menu: solo los juegos la usan)");

    char sub[96];
    snprintf(sub, sizeof sub, "bits totales=0x%08x", any & ~PAD_INTERCEPTED);
    say("=== fin: %s ===", sub);
    notify("pad_rpc: terminado", sub);
    if (g_log) fclose(g_log);
    return 0;
}
