'use strict';

// PUBG Mobile ESP — UE4 / arm64 / rooted (Frida, in-process overlay)
// Every version-specific value lives in CFG. Fill it once per game build.

const CFG = {
  lib: 'libUE4.so',
  refreshMs: 16,
  transformSize: 0x30,
  boneTranslation: 0x10,
  maxActors: 4096,
  maxBoneIndex: 512,
  // FILL: module-relative offsets for your exact game version.
  off: {
    GWorld: 0x0,
    UWorld_PersistentLevel: 0x0,
    UWorld_FirstPlayerController: 0x0,
    ULevel_Actors: 0x0,
    Pawn_PlayerState: 0x0,
    PlayerState_TeamId: 0x0,
    ACharacter_Mesh: 0x0,
    USkinnedMeshComponent_BoneArray: 0x0,
    USceneComponent_ComponentToWorld: 0x0,
    APlayerController_PlayerCameraManager: 0x0,
    APlayerController_AcknowledgedPawn: 0x0,
    APlayerCameraManager_CameraCachePOV: 0x0,
  },
  // FMinimalViewInfo layout — stable across UE4 builds.
  pov: { loc: 0x0, rot: 0xC, fov: 0x18 },
  // FILL: bone indices from your USkeleton dump.
  bones: {
    head: -1, neck: -1, spine: -1, pelvis: -1,
    lShoulder: -1, lElbow: -1, lHand: -1,
    rShoulder: -1, rElbow: -1, rHand: -1,
    lKnee: -1, lFoot: -1, rKnee: -1, rFoot: -1,
  },
};

const BONE_LINKS = [
  ['head','neck'], ['neck','spine'], ['spine','pelvis'],
  ['neck','lShoulder'], ['lShoulder','lElbow'], ['lElbow','lHand'],
  ['neck','rShoulder'], ['rShoulder','rElbow'], ['rElbow','rHand'],
  ['pelvis','lKnee'], ['lKnee','lFoot'],
  ['pelvis','rKnee'], ['rKnee','rFoot'],
];

function rp(a) { try { return Memory.readPointer(a); } catch (e) { return ptr(0); } }
function ri(a) { try { return Memory.readInt(a); } catch (e) { return 0; } }
function rf(a) { try { return Memory.readFloat(a); } catch (e) { return 0; } }
function vec(a) { return { x: rf(a), y: rf(a.add(4)), z: rf(a.add(8)) }; }
function rot3(a) { return { p: rf(a), y: rf(a.add(4)), r: rf(a.add(8)) }; }
function sub(a, b) { return { x: a.x-b.x, y: a.y-b.y, z: a.z-b.z }; }
function dot(a, b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
function cross(a, b) { return { x: a.y*b.z-a.z*b.y, y: a.z*b.x-a.x*b.z, z: a.x*b.y-a.y*b.x }; }
function len(a) { return Math.sqrt(dot(a, a)); }

function qrot(q, v) {
  const tx = 2*(q.y*v.z - q.z*v.y), ty = 2*(q.z*v.x - q.x*v.z), tz = 2*(q.x*v.y - q.y*v.x);
  return {
    x: v.x + q.w*tx + (q.y*tz - q.z*ty),
    y: v.y + q.w*ty + (q.z*tx - q.x*tz),
    z: v.z + q.w*tz + (q.x*ty - q.y*tx),
  };
}
function xform(t, p) { const r = qrot(t.q, p); return { x: r.x+t.t.x, y: r.y+t.t.y, z: r.z+t.t.z }; }
function readXform(a) {
  return { q: { x: rf(a), y: rf(a.add(4)), z: rf(a.add(8)), w: rf(a.add(12)) },
           t: vec(a.add(CFG.boneTranslation)) };
}

function camVectors(r) {
  const P = r.p*Math.PI/180, Y = r.y*Math.PI/180, R = r.r*Math.PI/180;
  const cp = Math.cos(P), sp = Math.sin(P), cy = Math.cos(Y), sy = Math.sin(Y),
        cr = Math.cos(R), sr = Math.sin(R);
  const fwd = { x: cp*cy, y: cp*sy, z: sp };
  const right = { x: sr*sp*cy - cr*sy, y: sr*sp*sy + cr*cy, z: -sr*cp };
  return { fwd: fwd, right: right, up: cross(fwd, right) };
}

let SW = 1080, SH = 2400;
function project(p, cam) {
  const d = sub(p, cam.loc);
  const z = dot(d, cam.fwd);
  if (z <= 1) return null;
  const x = dot(d, cam.right), y = dot(d, cam.up);
  const tan = Math.tan(cam.fov*0.5*Math.PI/180);
  if (tan <= 0) return null;
  const nx = x/(z*tan), ny = (y*SH/SW)/(z*tan);
  return { x: (nx+1)*0.5*SW, y: (1-ny)*0.5*SH, z: z };
}

function readBones(mesh, c2w, cam) {
  const arr = rp(mesh.add(CFG.off.USkinnedMeshComponent_BoneArray));
  if (arr.isNull()) return { pts: {}, lines: [] };
  const num = ri(mesh.add(CFG.off.USkinnedMeshComponent_BoneArray + 8));
  const pts = {};
  for (const k in CFG.bones) {
    const idx = CFG.bones[k];
    if (idx < 0 || idx >= num || idx >= CFG.maxBoneIndex) continue;
    const tr = readXform(arr.add(idx*CFG.transformSize));
    const s = project(xform(c2w, tr.t), cam);
    if (s) pts[k] = s;
  }
  const lines = [];
  for (const link of BONE_LINKS) {
    const a = pts[link[0]], b = pts[link[1]];
    if (a && b) lines.push([a.x, a.y, b.x, b.y]);
  }
  return { pts: pts, lines: lines };
}

function readWorld() {
  const m = Process.findModuleByName(CFG.lib);
  if (!m) return [];
  const world = rp(m.base.add(CFG.off.GWorld));
  if (world.isNull()) return [];
  const level = rp(world.add(CFG.off.UWorld_PersistentLevel));
  if (level.isNull()) return [];
  const data = rp(level.add(CFG.off.ULevel_Actors));
  if (data.isNull()) return [];
  const num = Math.min(ri(level.add(CFG.off.ULevel_Actors + 8)), CFG.maxActors);

  const pc = rp(world.add(CFG.off.UWorld_FirstPlayerController));
  if (pc.isNull()) return [];
  const camMgr = rp(pc.add(CFG.off.APlayerController_PlayerCameraManager));
  if (camMgr.isNull()) return [];
  const pov = camMgr.add(CFG.off.APlayerCameraManager_CameraCachePOV);
  const cam = { loc: vec(pov.add(CFG.pov.loc)), fov: rf(pov.add(CFG.pov.fov)) };
  const cv = camVectors(rot3(pov.add(CFG.pov.rot)));
  cam.fwd = cv.fwd; cam.right = cv.right; cam.up = cv.up;
  if (cam.fov < 1 || cam.fov > 179) cam.fov = 90;

  const local = rp(pc.add(CFG.off.APlayerController_AcknowledgedPawn));
  const out = [];
  for (let i = 0; i < num; i++) {
    const a = rp(data.add(i*Process.pointerSize));
    if (a.isNull() || a.equals(local)) continue;
    const root = rp(a.add(CFG.off.AActor_RootComponent));
    if (root.isNull()) continue;
    const rc = xform(readXform(root.add(CFG.off.USceneComponent_ComponentToWorld)), {x:0,y:0,z:0});
    const dist = len(sub(rc, cam.loc));
    if (dist > 500) continue;

    const mesh = rp(a.add(CFG.off.ACharacter_Mesh));
    let box = null, lines = [], head = null;
    if (!mesh.isNull()) {
      const c2w = readXform(mesh.add(CFG.off.USceneComponent_ComponentToWorld));
      const br = readBones(mesh, c2w, cam);
      lines = br.lines;
      head = br.pts.head || null;
      const all = [];
      for (const k in br.pts) all.push(br.pts[k]);
      if (all.length) {
        let l = 1e9, t = 1e9, rr = -1e9, bb = -1e9;
        for (const p of all) { l=Math.min(l,p.x); t=Math.min(t,p.y); rr=Math.max(rr,p.x); bb=Math.max(bb,p.y); }
        box = { l: l-6, t: t-6, r: rr+6, b: bb+6 };
      }
    }
    if (!box) {
      const s = project(rc, cam);
      if (!s) continue;
      const w = Math.max(10, 1200/s.z), h = w*1.8;
      box = { l: s.x-w/2, t: s.y-h, r: s.x+w/2, b: s.y };
    }
    const ps = rp(a.add(CFG.off.Pawn_PlayerState));
    let team = -1;
    if (!ps.isNull()) team = ri(ps.add(CFG.off.PlayerState_TeamId));
    out.push({ box: box, lines: lines, head: head, dist: Math.round(dist), enemy: team < 0 });
  }
  return out;
}

let TARGETS = [];
let attached = false;
let paintEnemy, paintMate, paintLine, paintHead, paintText;

function installOverlay() {
  const View = Java.use('android.view.View');
  const Paint = Java.use('android.graphics.Paint');
  const Style = Java.use('android.graphics.Paint$Style');
  const Color = Java.use('android.graphics.Color');
  const LP = Java.use('android.view.WindowManager$LayoutParams');
  const PF = Java.use('android.graphics.PixelFormat');

  paintEnemy = Paint.$new(); paintEnemy.setStyle(Style.STROKE.value);
  paintEnemy.setColor(Color.RED()); paintEnemy.setStrokeWidth(2);
  paintMate = Paint.$new(); paintMate.setStyle(Style.STROKE.value);
  paintMate.setColor(Color.GREEN()); paintMate.setStrokeWidth(2);
  paintLine = Paint.$new(); paintLine.setStyle(Style.STROKE.value);
  paintLine.setColor(Color.argb(180,255,255,0)); paintLine.setStrokeWidth(1.5);
  paintHead = Paint.$new(); paintHead.setStyle(Style.FILL.value); paintHead.setColor(Color.RED());
  paintText = Paint.$new(); paintText.setColor(Color.WHITE()); paintText.setTextSize(26);

  const EspView = Java.registerClass({
    name: 'oll.esp.EspView',
    superClass: View,
    methods: {
      $init: { argumentTypes: ['android.content.Context'],
        implementation: function (ctx) { this.$super.$init(ctx); } },
      onDraw: { argumentTypes: ['android.graphics.Canvas'], returnType: 'void',
        implementation: function (canvas) {
          SW = this.getWidth(); SH = this.getHeight();
          for (const t of TARGETS) {
            const b = t.box; if (!b) continue;
            canvas.drawRect(b.l, b.t, b.r, b.b, t.enemy ? paintEnemy : paintMate);
            for (const s of t.lines) canvas.drawLine(s[0], s[1], s[2], s[3], paintLine);
            if (t.head) canvas.drawCircle(t.head.x, t.head.y, 7, paintHead);
            canvas.drawText((t.enemy ? 'ENEMY ' : 'MATE ') + t.dist + 'm', b.l, b.t-10, paintText);
          }
        } }
    }
  });

  function attach(activity) {
    if (attached) return;
    try {
      const view = EspView.$new(activity);
      const lp = LP.$new(LP.MATCH_PARENT.value, LP.MATCH_PARENT.value,
        LP.TYPE_APPLICATION_PANEL.value,
        LP.FLAG_NOT_FOCUSABLE.value | LP.FLAG_NOT_TOUCHABLE.value |
        LP.FLAG_LAYOUT_IN_SCREEN.value | LP.FLAG_FULLSCREEN.value,
        PF.TRANSPARENT.value);
      lp.token = activity.getWindow().getDecorView().getWindowToken();
      activity.getWindowManager().addView(view, lp);
      attached = true;
      console.log('ESP overlay attached');
    } catch (e) { console.log('attach failed: ' + e); }
  }

  let act = null;
  Java.choose('android.app.Activity', { onMatch: function (i) { if (!act) act = i; }, onComplete: function () {} });
  if (act) attach(act);
  const Act = Java.use('android.app.Activity');
  Act.onResume.implementation = function () { act = this; attach(this); this.onResume(); };
}

setInterval(function () { try { TARGETS = readWorld(); } catch (e) {} }, CFG.refreshMs);
Java.perform(installOverlay);
console.log('PUBG ESP loaded — fill CFG offsets before use');
