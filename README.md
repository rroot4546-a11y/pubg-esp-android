# PUBG Mobile ESP — rooted arm64 (Frida)

In-process ESP for PUBG Mobile on a rooted Android arm64 device (e.g. Samsung Galaxy A55).
Reads UE4 actor state from game memory and draws a transparent overlay inside the game
process: enemy boxes, skeleton, head marker, distance, team colour.

## Features

- Box ESP from projected skeleton bounds (falls back to root-location box when bones are unavailable)
- Skeleton lines between configured bone indices
- Head circle
- Distance in metres
- Local pawn excluded; team ID from `PlayerState` drives red (enemy) / green (mate)
- No external overlay app — the View is injected into the game's own window

## Requirements

- Rooted Android **arm64** device (A55 / Snapdragon 6 Gen 1 confirmed target)
- `frida-server` matching the installed Frida version on the host
- The exact game version's UE4 offsets (see below)
- ADB or a root shell on the device

## Files

| Path | Purpose |
|---|---|
| `esp_pubg.js` | Main Frida script — offset config + memory reader + overlay |
| `tools/addr_to_offset.js` | Turn an absolute address from a memory scanner into a module-relative offset |
| `tools/module_info.js` | Log loaded UE4 module base/size and mapped ranges |
| `scripts/run.sh` | Push/start frida-server and attach the script |
| `config.example.js` | Documented copy of the CFG block |

## Quick start

```bash
# host: install Frida tooling
pip install frida-tools

# device: push and start frida-server (arm64)
adb push frida-server-android-arm64 /data/local/tmp/frida-server
adb shell 'chmod 755 /data/local/tmp/frida-server'
adb shell 'su -c /data/local/tmp/frida-server -D &'

# attach on spawn
frida -U -f com.tencent.ig -l esp_pubg.js
```

Package names by region:

| Variant | Package |
|---|---|
| Global | `com.tencent.ig` |
| China HD | `com.tencent.tmgp.pubgmhd` |
| China | `com.tencent.tmgp.pubgm` |

If `CFG.lib` does not match, run `tools/module_info.js` first and copy the real module name.

## Standalone APK (no Frida needed)

`android/` is a complete Android 34 arm64 APK. It bundles a static root helper (`process_vm_readv`
against the game process) and an in-app transparent overlay. No Frida, no repackaging of the game.

Build on a host with the local SDK layout (`/opt/sdk`, `/opt/aapt2-shim`, NDK r25c):

```bash
./android/build.sh          # -> build/OLL-ESP.apk (debug-signed, arm64)
```

Install and run:

```bash
adb install -r build/OLL-ESP.apk
adb shell am start -n com.oll.esp/.MainActivity
```

On device:

1. Open the game first and leave it in the lobby.
2. Tap **1. Grant overlay permission** and enable "Display over other apps".
3. Type the target package and tap **Auto-find offsets**. The bundled `espdiscover` binary
   scans the live process for `GWorld`, the level actor array, root component / transform,
   player controller, camera manager and POV, then writes `config.txt`. The status box prints
   the offset it found and the exit code — `exit=0` means it wrote a config.
4. Tap **2. Start ESP**, then switch to the game.

### Diagnostics (v1.2+)

- **Test overlay (draw fixed box)** — draws a guaranteed red box. If that does not appear,
  overlay permission / service is the problem, not memory reading.
- **Show log** — prints `/storage/emulated/0/Android/data/com.oll.esp/files/oll_esp.log`.
  Grab it with `adb pull /sdcard/Android/data/com.oll.esp/files/oll_esp.log` too.
- During ESP operation the log records scanner output, the first decoded target, and a frame
  summary every 100 frames (`frames=N withTargets=M last=K`). `withTargets=0` means the
  offset chain is still wrong; `last>0` with no visible box means the overlay is the issue.

You can still edit `/data/data/com.oll.esp/files/config.txt` by hand; it is created once and
is never overwritten on later launches (only the auto-find button rewrites it). Bone indices
stay at `-1` after auto-discovery, so the ESP draws root-location boxes without a skeleton.

The helper is executed through `su`, so the device must be rooted and root access granted to
the app. Overlay permission is required on Android 8+. The app keeps a foreground service alive
while the ESP runs; **3. Stop ESP** kills the helper and removes the overlay.

## Offset recovery (one time per game update)

All 12 offsets in `CFG.off` are **module-relative** — add them to the base reported by
`tools/module_info.js`. Nothing is hardcoded to an absolute address.

1. Pull `libUE4.so` from the APK (`unzip -p base.apk lib/arm64-v8a/libUE4.so > libUE4.so`)
   or copy it from `/data/app/.../lib/arm64/`.
2. Open it in Ghidra / IDA and locate the globals:
   - `GWorld` — pointer to `UWorld`; commonly referenced near engine init strings.
   - `GNames` — only needed if you add FName name resolution.
   - Use a public UE4 offset finder for your exact build as a cross-check.
3. Struct member offsets (from the object base, not the module base):
   - `UWorld::PersistentLevel`
   - `UWorld::FirstPlayerController`
   - `ULevel::ActorArray` — `TArray<AActor*>` (data `+0`, num `+8`; the script reads num at `+8`)
   - `APawn::PlayerState`
   - `APlayerState::TeamId`
   - `ACharacter::Mesh` (`USkeletalMeshComponent*`)
   - `USkinnedMeshComponent::BoneArray` — `TArray<FTransform>` (num at `+8`)
   - `USceneComponent::ComponentToWorld` — `FTransform`
   - `APlayerController::PlayerCameraManager`
   - `APlayerController::AcknowledgedPawn`
   - `APlayerCameraManager::CameraCachePOV` — point this at the `FMinimalViewInfo` inside the cache
4. Convert any absolute address you find with `tools/addr_to_offset.js`.
5. Fill `CFG.bones` with indices from a `USkeleton` dump (head, neck, spine, pelvis,
   shoulders, elbows, hands, knees, feet).

`FTransform` assumptions in the reader: size `0x30`, quaternion at `+0x0`, translation at
`+0x10`. Override `CFG.transformSize` / `CFG.boneTranslation` if your UE4 build differs.

## Detection

Running Frida inside the game process is detectable by PUBG's anti-cheat. If the client
kicks you, move the read loop to an external root reader (`/proc/<pid>/mem`) and keep the
same overlay projection, or use a private server build. The offset block is unchanged by
that switch.

## License

MIT
