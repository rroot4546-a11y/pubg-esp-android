#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <sys/uio.h>
#include <sys/syscall.h>

typedef uint64_t u64;

typedef struct {
    u64 GWorld, PersistentLevel, FirstPC, Actors, Root, PawnPS, TeamId, Mesh,
        BoneArray, C2W, CamMgr, AckPawn, CamPOV;
} Offsets;

typedef struct {
    int head, neck, spine, pelvis, lShoulder, lElbow, lHand,
        rShoulder, rElbow, rHand, lKnee, lFoot, rKnee, rFoot;
} Bones;

typedef struct { float x,y,z; } Vec;
typedef struct { float w,x,y,z; } Quat;
typedef struct { Quat q; Vec t; } Xform;

static const char *g_lib = "libUE4.so";
static int g_sw = 1080, g_sh = 2400;
static int g_mem_fd = -1;
static pid_t g_pid = 0;

static void trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s) - 1;
    while (e >= s && isspace((unsigned char)*e)) *e-- = 0;
}

static int parse_cfg(const char *path, Offsets *o, Bones *b) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "# cannot open config: %s\n", path); return -1; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        trim(k); trim(v);
        if (!*k) continue;

        if (strncmp(k, "lib=", 4) == 0) continue;
        if (strcmp(k, "width") == 0) { g_sw = atoi(v); continue; }
        if (strcmp(k, "height") == 0) { g_sh = atoi(v); continue; }

        u64 val = (u64)strtoull(v, NULL, 0);
        if      (strcmp(k,"GWorld")==0) o->GWorld = val;
        else if (strcmp(k,"UWorld_PersistentLevel")==0) o->PersistentLevel = val;
        else if (strcmp(k,"UWorld_FirstPlayerController")==0) o->FirstPC = val;
        else if (strcmp(k,"ULevel_Actors")==0) o->Actors = val;
        else if (strcmp(k,"AActor_RootComponent")==0) o->Root = val;
        else if (strcmp(k,"Pawn_PlayerState")==0) o->PawnPS = val;
        else if (strcmp(k,"PlayerState_TeamId")==0) o->TeamId = val;
        else if (strcmp(k,"ACharacter_Mesh")==0) o->Mesh = val;
        else if (strcmp(k,"USkinnedMeshComponent_BoneArray")==0) o->BoneArray = val;
        else if (strcmp(k,"USceneComponent_ComponentToWorld")==0) o->C2W = val;
        else if (strcmp(k,"APlayerController_PlayerCameraManager")==0) o->CamMgr = val;
        else if (strcmp(k,"APlayerController_AcknowledgedPawn")==0) o->AckPawn = val;
        else if (strcmp(k,"APlayerCameraManager_CameraCachePOV")==0) o->CamPOV = val;
        else if (strcmp(k,"bones.head")==0) b->head = (int)val;
        else if (strcmp(k,"bones.neck")==0) b->neck = (int)val;
        else if (strcmp(k,"bones.spine")==0) b->spine = (int)val;
        else if (strcmp(k,"bones.pelvis")==0) b->pelvis = (int)val;
        else if (strcmp(k,"bones.lShoulder")==0) b->lShoulder = (int)val;
        else if (strcmp(k,"bones.lElbow")==0) b->lElbow = (int)val;
        else if (strcmp(k,"bones.lHand")==0) b->lHand = (int)val;
        else if (strcmp(k,"bones.rShoulder")==0) b->rShoulder = (int)val;
        else if (strcmp(k,"bones.rElbow")==0) b->rElbow = (int)val;
        else if (strcmp(k,"bones.rHand")==0) b->rHand = (int)val;
        else if (strcmp(k,"bones.lKnee")==0) b->lKnee = (int)val;
        else if (strcmp(k,"bones.lFoot")==0) b->lFoot = (int)val;
        else if (strcmp(k,"bones.rKnee")==0) b->rKnee = (int)val;
        else if (strcmp(k,"bones.rFoot")==0) b->rFoot = (int)val;
    }
    fclose(f);
    return 0;
}

static pid_t find_pid(const char *pkg) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        char path[80], buf[4096];
        snprintf(path, sizeof path, "/proc/%s/cmdline", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        for (ssize_t i = 0; i < n; i++) if (!buf[i]) buf[i] = ' ';
        if (strstr(buf, pkg)) { closedir(d); return (pid_t)atoi(e->d_name); }
    }
    closedir(d);
    return 0;
}

static u64 module_base(pid_t pid, const char *name) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, name)) continue;
        char *dash = strchr(line, '-');
        if (!dash) continue;
        *dash = 0;
        u64 base = (u64)strtoull(line, NULL, 16);
        fclose(f);
        return base;
    }
    fclose(f);
    return 0;
}

static int rd(u64 addr, void *buf, size_t len) {
    if (g_mem_fd >= 0) {
        return pread(g_mem_fd, buf, len, (off_t)addr) == (ssize_t)len ? 0 : -1;
    }
    struct iovec local = { buf, len }, remote = { (void *)addr, len };
    return syscall(__NR_process_vm_readv, g_pid, &local, 1, &remote, 1, 0) == (ssize_t)len ? 0 : -1;
}

static u64 r64(u64 a) { u64 v = 0; rd(a, &v, 8); return v; }
static int32_t r32(u64 a) { int32_t v = 0; rd(a, &v, 4); return v; }
static float rfl(u64 a) { float v = 0; rd(a, &v, 4); return v; }

static Vec vec(u64 a) { Vec v = { rfl(a), rfl(a+4), rfl(a+8) }; return v; }
static Xform xf(u64 a, u64 trans_off) {
    Xform x;
    x.q.w = rfl(a); x.q.x = rfl(a+4); x.q.y = rfl(a+8); x.q.z = rfl(a+12);
    x.t = vec(a + trans_off);
    return x;
}
static Vec vsub(Vec a, Vec b) { return (Vec){a.x-b.x, a.y-b.y, a.z-b.z}; }
static float vdot(Vec a, Vec b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec vcross(Vec a, Vec b) {
    return (Vec){ a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x };
}
static float vlen(Vec a) { return sqrtf(vdot(a,a)); }

static Vec qrot(Quat q, Vec v) {
    float tx = 2*(q.y*v.z - q.z*v.y),
          ty = 2*(q.z*v.x - q.x*v.z),
          tz = 2*(q.x*v.y - q.y*v.x);
    return (Vec){
        v.x + q.w*tx + (q.y*tz - q.z*ty),
        v.y + q.w*ty + (q.z*tx - q.x*tz),
        v.z + q.w*tz + (q.x*ty - q.y*tx)
    };
}
static Vec xform(Xform t, Vec p) { Vec r = qrot(t.q, p); return (Vec){r.x+t.t.x, r.y+t.t.y, r.z+t.t.z}; }

typedef struct { Vec fwd, right, up, loc; float fov; } Cam;

static Cam read_cam(u64 pov) {
    Cam c;
    c.loc = vec(pov);
    float p = rfl(pov+0xC) * 0.01745329252f,
          y = rfl(pov+0x10) * 0.01745329252f,
          rr = rfl(pov+0x14) * 0.01745329252f;
    float cp=cosf(p), sp=sinf(p), cy=cosf(y), sy=sinf(y), cr=cosf(rr), sr=sinf(rr);
    c.fwd = (Vec){ cp*cy, cp*sy, sp };
    c.right = (Vec){ sr*sp*cy - cr*sy, sr*sp*sy + cr*cy, -sr*cp };
    c.up = vcross(c.fwd, c.right);
    c.fov = rfl(pov+0x18);
    if (c.fov < 1 || c.fov > 179) c.fov = 90;
    return c;
}

static int project(Vec p, Cam *c, float *ox, float *oy, float *oz) {
    Vec d = vsub(p, c->loc);
    float z = vdot(d, c->fwd);
    if (z <= 1) return 0;
    float x = vdot(d, c->right), yy = vdot(d, c->up);
    float tan = tanf(c->fov * 0.5f * 0.01745329252f);
    if (tan <= 0) return 0;
    float nx = x/(z*tan), ny = (yy*(float)g_sh/(float)g_sw)/(z*tan);
    *ox = (nx+1)*0.5f*(float)g_sw;
    *oy = (1-ny)*0.5f*(float)g_sh;
    *oz = z;
    return 1;
}

static const int LINK_A[] = {0,1,2, 1,4,5, 1,7,8, 3,10, 3,12};
static const int LINK_B[] = {1,2,3, 4,5,6, 7,8,9, 10,11, 12,13};

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <package> <width> <height> [config]\n", argv[0]);
        return 2;
    }
    const char *pkg = argv[1];
    g_sw = atoi(argv[2]); g_sh = atoi(argv[3]);
    const char *cfg = argc > 4 ? argv[4] : "config.txt";

    Offsets o = {0}; Bones b = {0};
    if (parse_cfg(cfg, &o, &b) != 0) return 3;
    int bone_idx[14] = { b.head,b.neck,b.spine,b.pelvis,b.lShoulder,b.lElbow,b.lHand,
                         b.rShoulder,b.rElbow,b.rHand,b.lKnee,b.lFoot,b.rKnee,b.rFoot };

    fprintf(stderr, "# waiting for %s\n", pkg);
    pid_t pid = 0; u64 base = 0;
    for (int wait = 0; wait < 600 && (!pid || !base); wait++) {
        if (!pid) pid = find_pid(pkg);
        if (pid && !base) base = module_base(pid, g_lib);
        usleep(100000);
    }
    if (!pid || !base) { fprintf(stderr, "# target not found\n"); return 4; }
    g_pid = pid;
    fprintf(stderr, "# pid=%d base=0x%llx\n", pid, (unsigned long long)base);

    char mempath[64];
    snprintf(mempath, sizeof mempath, "/proc/%d/mem", pid);
    g_mem_fd = open(mempath, O_RDONLY);
    if (g_mem_fd < 0) fprintf(stderr, "# mem open failed, using process_vm_readv\n");

    u64 sw = (u64)g_sw, sh = (u64)g_sh;
    (void)sw; (void)sh;

    for (;;) {
        u64 world = r64(base + o.GWorld);
        if (!world) { usleep(50000); continue; }
        u64 level = r64(world + o.PersistentLevel);
        if (!level) { usleep(50000); continue; }
        u64 data = r64(level + o.Actors);
        if (!data) { usleep(50000); continue; }
        int num = r32(level + o.Actors + 8);
        if (num < 0 || num > 4096) num = 4096;

        u64 pc = r64(world + o.FirstPC);
        if (!pc) { usleep(50000); continue; }
        u64 camMgr = r64(pc + o.CamMgr);
        if (!camMgr) { usleep(50000); continue; }
        Cam cam = read_cam(camMgr + o.CamPOV);
        u64 local = r64(pc + o.AckPawn);

        for (int i = 0; i < num; i++) {
            u64 a = r64(data + (u64)i * 8);
            if (!a || a == local) continue;
            u64 root = r64(a + o.Root);
            if (!root) continue;
            Xform rx = xf(root + o.C2W, 0x10);
            Vec rc = rx.t;
            float dist = vlen(vsub(rc, cam.loc));
            if (dist > 500) continue;

            float px[14], py[14];
            int have[14];
            for (int k = 0; k < 14; k++) { px[k]=0; py[k]=0; have[k]=0; }

            u64 mesh = r64(a + o.Mesh);
            if (mesh) {
                u64 arr = r64(mesh + o.BoneArray);
                int nbone = r32(mesh + o.BoneArray + 8);
                if (arr && nbone > 0) {
                    Xform c2w = xf(mesh + o.C2W, 0x10);
                    for (int k = 0; k < 14; k++) {
                        int idx = bone_idx[k];
                        if (idx < 0 || idx >= nbone || idx >= 512) continue;
                        Xform bt = xf(arr + (u64)idx * 0x30, 0x10);
                        Vec w = xform(c2w, bt.t);
                        float ox,oy,oz;
                        if (project(w, &cam, &ox,&oy,&oz)) { px[k]=ox; py[k]=oy; have[k]=1; }
                    }
                }
            }

            float l=1e9,t=1e9,rr=-1e9,bb=-1e9;
            for (int k = 0; k < 14; k++) if (have[k]) {
                if (px[k]<l) l=px[k]; if (px[k]>rr) rr=px[k];
                if (py[k]<t) t=py[k]; if (py[k]>bb) bb=py[k];
            }
            if (l > 1e8) {
                float ox,oy,oz;
                if (!project(rc, &cam, &ox,&oy,&oz)) continue;
                float w = fmaxf(10, 1200/oz), h = w*1.8f;
                l=ox-w/2; t=oy-h; rr=ox+w/2; bb=oy;
            }
            l-=6; t-=6; rr+=6; bb+=6;

            u64 ps = r64(a + o.PawnPS);
            int team = ps ? r32(ps + o.TeamId) : -1;

            int nlines = 0;
            float seg[26*4];
            for (int k = 0; k < 13; k++) {
                int ia = LINK_A[k], ib = LINK_B[k];
                if (have[ia] && have[ib]) {
                    seg[nlines*4]=px[ia]; seg[nlines*4+1]=py[ia];
                    seg[nlines*4+2]=px[ib]; seg[nlines*4+3]=py[ib];
                    nlines++;
                }
            }
            float hx = have[0] ? px[0] : -1, hy = have[0] ? py[0] : -1;
            printf("T %.1f %.1f %.1f %.1f %d %d %.1f %.1f %d",
                   l,t,rr,bb, team<0?1:0, (int)dist, hx,hy,nlines);
            for (int k = 0; k < nlines*4; k++) printf(" %.1f", seg[k]);
            printf("\n");
        }
        printf("F\n");
        fflush(stdout);
        usleep(16000);
    }
}
