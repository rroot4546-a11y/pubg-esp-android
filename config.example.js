'use strict';

// Reference copy of the CFG block from esp_pubg.js.
// Copy values into esp_pubg.js after running the recovery workflow in README.md.

const CFG = {
  lib: 'libUE4.so',
  refreshMs: 16,
  transformSize: 0x30,
  boneTranslation: 0x10,
  maxActors: 4096,
  maxBoneIndex: 512,
  off: {
    GWorld: 0x0,                              // module-relative pointer to UWorld
    UWorld_PersistentLevel: 0x0,              // UWorld member
    UWorld_FirstPlayerController: 0x0,        // UWorld member
    ULevel_Actors: 0x0,                       // TArray<AActor*>; num read at +8
    Pawn_PlayerState: 0x0,                    // APawn member
    PlayerState_TeamId: 0x0,                  // APlayerState member
    ACharacter_Mesh: 0x0,                     // USkeletalMeshComponent*
    USkinnedMeshComponent_BoneArray: 0x0,     // TArray<FTransform>; num read at +8
    USceneComponent_ComponentToWorld: 0x0,    // FTransform
    APlayerController_PlayerCameraManager: 0x0,
    APlayerController_AcknowledgedPawn: 0x0,
    APlayerCameraManager_CameraCachePOV: 0x0, // FMinimalViewInfo base
  },
  pov: { loc: 0x0, rot: 0xC, fov: 0x18 },
  bones: {
    head: -1, neck: -1, spine: -1, pelvis: -1,
    lShoulder: -1, lElbow: -1, lHand: -1,
    rShoulder: -1, rElbow: -1, rHand: -1,
    lKnee: -1, lFoot: -1, rKnee: -1, rFoot: -1,
  },
};

module.exports = CFG;
