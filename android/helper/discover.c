#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <sys/uio.h>
#include <sys/syscall.h>

typedef uint64_t u64;

typedef struct { u64 s, e; int x, w; char path[512]; } Map;
static Map g_maps[2048];
static int g_nmaps = 0;
static pid_t g_pid = 0;
static u64 g_base = 0, g_modlo = 0, g_modhi = 0;
static const char *g_lib = "libUE4.so";
static const char *const LIB_CANDIDATES[] = {
    "libUE4.so", "libue4.so", "libGame.so", "libUnreal.so",
    "libpubg.so", "libTencent.so", "libUE4.so"
};

static int rd(u64 a, void *b, size_t n) {
    struct iovec l = { b, n }, r = { (void *)a, n };
    return syscall(SYS_process_vm_readv, g_pid, &l, 1, &r, 1, 0) == (ssize_t)n ? 0 : -1;
}
static u64 r64(u64 a) { u64 v = 0; rd(a, &v, 8); return v; }
static int32_t r32(u64 a) { int32_t v = 0; rd(a, &v, 4); return v; }
static float rfl(u64 a) { float v = 0; rd(a, &v, 4); return v; }

static void load_maps(void) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/maps", g_pid);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof line, f) && g_nmaps < 2048) {
        u64 s, e;
        char prot[8] = {0};
        if (sscanf(line, "%llx-%llx %4s", (unsigned long long *)&s,
                   (unsigned long long *)&e, prot) != 3) continue;
        Map *m = &g_maps[g_nmaps++];
        m->s = s; m->e = e;
        m->x = strchr(prot, 'x') != NULL;
        m->w = strchr(prot, 'w') != NULL;
        char *slash = strchr(line, '/');
        if (slash) {
            strncpy(m->path, slash, sizeof m->path - 1);
            m->path[sizeof m->path - 1] = 0;
            char *nl = strchr(m->path, '\n');
            if (nl) *nl = 0;
        }
    }
    fclose(f);
}

static void detect_lib(void) {
    for (unsigned k = 0; k < sizeof LIB_CANDIDATES / sizeof LIB_CANDIDATES[0]; k++) {
        for (int i = 0; i < g_nmaps; i++) {
            if (strstr(g_maps[i].path, LIB_CANDIDATES[k])) {
                g_lib = LIB_CANDIDATES[k];
                fprintf(stderr, "# engine module: %s\n", g_lib);
                return;
            }
        }
    }
    fprintf(stderr, "# no known engine module, defaulting to %s\n", g_lib);
}

static void set_module_range(void) {
    for (int i = 0; i < g_nmaps; i++) {
        if (!strstr(g_maps[i].path, g_lib)) continue;
        if (!g_modlo || g_maps[i].s < g_modlo) g_modlo = g_maps[i].s;
        if (g_maps[i].e > g_modhi) g_modhi = g_maps[i].e;
    }
}

static int inmap(u64 a) {
    for (int i = 0; i < g_nmaps; i++) if (a >= g_maps[i].s && a < g_maps[i].e) return 1;
    return 0;
}
static int inexec(u64 a) {
    for (int i = 0; i < g_nmaps; i++)
        if (g_maps[i].x && a >= g_maps[i].s && a < g_maps[i].e) return 1;
    return 0;
}
static int is_obj(u64 p) { return inmap(p) && inexec(r64(p)); }

static pid_t find_pid(const char *pkg) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        char p[80], b[8192];
        snprintf(p, sizeof p, "/proc/%s/cmdline", e->d_name);
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, b, sizeof b - 1);
        close(fd);
        if (n <= 0) continue;
        b[n] = 0;
        for (ssize_t i = 0; i < n; i++) if (!b[i]) b[i] = ' ';
        if (strstr(b, pkg)) { closedir(d); return (pid_t)atoi(e->d_name); }
    }
    closedir(d);
    return 0;
}

static const u64 PERSIST[] = {
    0x20,0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78,
    0x80,0x88,0x90,0x98,0xa0,0xa8,0xb0,0xc0,0xd0,0xe0,0xf0,0x100,
    0x110,0x120,0x140,0x160,0x180,0x1a0,0x1c0,0x200
};
static const u64 ACTORS[] = {
    0x70,0x78,0x80,0x88,0x90,0x98,0xa0,0xa8,0xb0,0xb8,0xc0,0xc8,
    0xd0,0xd8,0xe0,0xe8,0xf0,0xf8,0x100,0x110,0x120,0x140,0x160
};

static int find_world(u64 *gw, u64 *persist, u64 *actors, u64 *adata, int *acount) {
    unsigned char *buf = malloc(1 << 16);
    if (!buf) return 0;
    int found = 0;
    for (int i = 0; i < g_nmaps && !found; i++) {
        Map *m = &g_maps[i];
        if (!m->w || m->e <= g_modlo || m->s >= g_modhi) continue;
        u64 start = m->s, end = m->e;
        if (start < g_modlo) start = g_modlo;
        if (end > g_modhi) end = g_modhi;
        for (u64 a = start; a + 0x10000 <= end && !found; a += 0x10000) {
            if (rd(a, buf, 0x10000)) continue;
            for (u64 o = 0; o + 8 <= 0x10000 && !found; o += 8) {
                u64 p;
                memcpy(&p, buf + o, 8);
                if (!is_obj(p)) continue;
                if (!is_obj(r64(p))) { /* level may not be first, keep going */ }
                for (unsigned k = 0; k < sizeof(PERSIST)/sizeof(PERSIST[0]) && !found; k++) {
                    u64 lv = r64(p + PERSIST[k]);
                    if (!is_obj(lv)) continue;
                    for (unsigned j = 0; j < sizeof(ACTORS)/sizeof(ACTORS[0]); j++) {
                        u64 d = r64(lv + ACTORS[j]);
                        if (!inmap(d)) continue;
                        int n = r32(lv + ACTORS[j] + 8);
                        if (n < 16 || n > 4096) continue;
                        u64 a0 = r64(d);
                        if (!is_obj(a0)) continue;
                        *gw = p; *persist = PERSIST[k]; *actors = ACTORS[j];
                        *adata = d; *acount = n;
                        found = 1;
                        break;
                    }
                }
            }
        }
    }
    free(buf);
    return found;
}

static int actor_index(u64 actors_data, int count, u64 ptr) {
    int lim = count < 160 ? count : 160;
    for (int i = 0; i < lim; i++) if (r64(actors_data + (u64)i*8) == ptr) return 1;
    return 0;
}

static int pov_ok(u64 a) {
    float x = rfl(a), y = rfl(a+4), z = rfl(a+8);
    float p = rfl(a+0xC), ya = rfl(a+0x10), r = rfl(a+0x14);
    float f = rfl(a+0x18);
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) return 0;
    if (fabsf(x) > 1e6 || fabsf(y) > 1e6 || fabsf(z) > 1e6) return 0;
    if (!isfinite(p) || !isfinite(ya) || !isfinite(r) || !isfinite(f)) return 0;
    if (fabsf(p) > 180 || fabsf(ya) > 360 || fabsf(r) > 180) return 0;
    if (f < 40 || f > 150) return 0;
    if (fabsf(p) + fabsf(ya) + fabsf(r) < 0.001f) return 0;
    return 1;
}

static int find_controller(u64 world, u64 adata, int acount,
                           u64 *firstpc, u64 *ackpawn, u64 *cammgr, u64 *pov) {
    for (u64 off = 0; off <= 0x500; off += 8) {
        u64 pc = r64(world + off);
        if (!actor_index(adata, acount, pc)) continue;
        u64 ack_off = 0, cm_off = 0, pv_off = 0;
        for (u64 o = 0; o <= 0x500; o += 8) {
            u64 v = r64(pc + o);
            if (!v || v == pc) continue;
            if (!ack_off && actor_index(adata, acount, v)) ack_off = o;
            if (!cm_off && is_obj(v)) {
                for (u64 q = 0; q <= 0x500; q += 4) {
                    if (pov_ok(v + q)) { cm_off = o; pv_off = q; break; }
                }
            }
            if (ack_off && cm_off) break;
        }
        if (cm_off && pv_off) {
            *firstpc = off; *ackpawn = ack_off; *cammgr = cm_off; *pov = pv_off;
            return 1;
        }
    }
    return 0;
}

static int find_transform(u64 obj, u64 *off_out) {
    for (u64 o = 0; o <= 0x300; o += 0x10) {
        float w = rfl(obj+o), x = rfl(obj+o+4), y = rfl(obj+o+8), z = rfl(obj+o+12);
        if (!isfinite(w)||!isfinite(x)||!isfinite(y)||!isfinite(z)) continue;
        float n = w*w+x*x+y*y+z*z;
        if (n < 0.7f || n > 1.3f) continue;
        float tx = rfl(obj+o+0x10), ty = rfl(obj+o+0x14), tz = rfl(obj+o+0x18);
        if (!isfinite(tx)||!isfinite(ty)||!isfinite(tz)) continue;
        if (fabsf(tx)>1e6||fabsf(ty)>1e6||fabsf(tz)>1e6) continue;
        *off_out = o;
        return 1;
    }
    return 0;
}

static int transform_ok(u64 a) {
    float w = rfl(a), x = rfl(a+4), y = rfl(a+8), z = rfl(a+12);
    if (!isfinite(w)||!isfinite(x)||!isfinite(y)||!isfinite(z)) return 0;
    float n = w*w+x*x+y*y+z*z;
    if (n < 0.7f || n > 1.3f) return 0;
    float tx = rfl(a+0x10), ty = rfl(a+0x14), tz = rfl(a+0x18);
    if (!isfinite(tx)||!isfinite(ty)||!isfinite(tz)) return 0;
    if (fabsf(tx)>1e6||fabsf(ty)>1e6||fabsf(tz)>1e6) return 0;
    return 1;
}

static int find_root(u64 adata, int acount, u64 *root_off, u64 *c2w_off) {
    int n = acount < 24 ? acount : 24;
    u64 samples[24];
    for (int i = 0; i < n; i++) samples[i] = r64(adata + (u64)i*8);
    int best_r = -1, best_c = -1, best_score = 0;
    /* UObject header slots (Class/Name/Outer) are excluded: their values
       also point into executable memory and produce false positives. */
    for (u64 ro = 0x28; ro <= 0x500; ro += 8) {
        u64 roots[24];
        int valid = 0;
        for (int i = 0; i < n; i++) {
            roots[i] = r64(samples[i] + ro);
            if (is_obj(roots[i])) valid++;
        }
        if (valid < 2) continue;
        for (u64 co = 0; co <= 0x300; co += 0x10) {
            int score = 0;
            for (int i = 0; i < n; i++)
                if (is_obj(roots[i]) && transform_ok(roots[i] + co)) score++;
            if (score > best_score) { best_score = score; best_r = (int)ro; best_c = (int)co; }
        }
    }
    if (best_score < 2 || best_r < 0 || best_c < 0) return 0;
    *root_off = (u64)best_r;
    *c2w_off = (u64)best_c;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <package> <config_out>\n", argv[0]);
        return 2;
    }
    const char *pkg = argv[1], *out = argv[2];
    fprintf(stderr, "# searching for %s\n", pkg);
    g_pid = find_pid(pkg);
    if (!g_pid) { fprintf(stderr, "# target not running\n"); return 4; }
    load_maps();
    detect_lib();
    set_module_range();
    /* base = start of first executable mapping of the lib */
    for (int i = 0; i < g_nmaps; i++) {
        if (g_maps[i].x && g_maps[i].s >= g_modlo && g_maps[i].s < g_modhi) {
            g_base = g_maps[i].s;
            break;
        }
    }
    if (!g_base) g_base = g_modlo;
    fprintf(stderr, "# pid=%d base=0x%llx\n", g_pid, (unsigned long long)g_base);

    u64 gw, persist, actors, adata;
    int acount;
    if (!find_world(&gw, &persist, &actors, &adata, &acount)) {
        fprintf(stderr, "# GWorld discovery failed\n");
        return 5;
    }
    fprintf(stderr, "# world=0x%llx actors=%d\n", (unsigned long long)(gw-g_base), acount);

    u64 firstpc = 0, ackpawn = 0, cammgr = 0, pov = 0;
    find_controller(gw, adata, acount, &firstpc, &ackpawn, &cammgr, &pov);

    u64 root = 0, c2w = 0;
    find_root(adata, acount, &root, &c2w);

    FILE *f = fopen(out, "w");
    if (!f) { fprintf(stderr, "# cannot write %s\n", out); return 6; }
    fprintf(f, "# auto-discovered offsets — verify in game\n");
    fprintf(f, "lib=%s\n", g_lib);
    fprintf(f, "GWorld=0x%llx\n", (unsigned long long)(gw-g_base));
    fprintf(f, "UWorld_PersistentLevel=0x%llx\n", (unsigned long long)persist);
    fprintf(f, "ULevel_Actors=0x%llx\n", (unsigned long long)actors);
    fprintf(f, "AActor_RootComponent=0x%llx\n", (unsigned long long)root);
    fprintf(f, "USceneComponent_ComponentToWorld=0x%llx\n", (unsigned long long)c2w);
    fprintf(f, "UWorld_FirstPlayerController=0x%llx\n", (unsigned long long)firstpc);
    fprintf(f, "APlayerController_AcknowledgedPawn=0x%llx\n", (unsigned long long)ackpawn);
    fprintf(f, "APlayerController_PlayerCameraManager=0x%llx\n", (unsigned long long)cammgr);
    fprintf(f, "APlayerCameraManager_CameraCachePOV=0x%llx\n", (unsigned long long)pov);
    fprintf(f, "Pawn_PlayerState=0xffffffffffffffff\n");
    fprintf(f, "PlayerState_TeamId=0x0\n");
    fprintf(f, "ACharacter_Mesh=0xffffffffffffffff\n");
    fprintf(f, "USkinnedMeshComponent_BoneArray=0x0\n");
    fprintf(f, "bones.head=-1\nbones.neck=-1\nbones.spine=-1\nbones.pelvis=-1\n");
    fprintf(f, "bones.lShoulder=-1\nbones.lElbow=-1\nbones.lHand=-1\n");
    fprintf(f, "bones.rShoulder=-1\nbones.rElbow=-1\nbones.rHand=-1\n");
    fprintf(f, "bones.lKnee=-1\nbones.lFoot=-1\nbones.rKnee=-1\nbones.rFoot=-1\n");
    fclose(f);
    fprintf(stderr, "# wrote %s\n", out);
    return 0;
}
