# Skeleton and health bar controls

The compact native skeleton now emits 17 joints, ending each leg at the ankle.
The short ankle-to-toe segments (source joints 61 and 65) are omitted, with the
right-leg parent indices adjusted. Finger and helper chains remain excluded.

The Visual menu now offers Left, Right, Above, and Below health bar placement.
Side bars fill bottom-to-top; horizontal bars fill left-to-right. Placement uses
the actor's projected body bounds. Labels avoid the health bar. Thickness has a
separate 0.5–4 dp slider in 0.25 dp steps, with a 0.75 dp default, half the old
1.5 dp width. Both settings persist independently of line/box thickness and
apply to all actor categories with health bars enabled.

The native skeleton/wire regression passed on device. All 47 Android unit tests
passed, including four health bar geometry tests covering placement, fill
directions, health clamping, thickness, and invalid bounds. Device UI tests
verified all four position settings and a thickness change to 2.25 dp; settings
were restored to Left and 0.75 dp afterward.

The APK is installed and its reader is running. A subsequent live match feed
contained 13 verified skeletons, all with 17 joints. The live screenshot confirms
the thinner side bars and skeletons ending at the ankles. Game PID 27845 remained
running. Source binary, APK asset, and device binary SHA256 match:
`64a8b11b4eb8c92d0e37a6625ef8f0f84dc77bd4994c83cca8db7227e25e7817`.
