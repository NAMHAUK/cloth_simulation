# HISTORY.md

## 2026-05-06 - SMPL Motion Cache Playback And Browser
- Built the first SMPL motion cache playback milestone for the OpenGL simulation app.
- Added AMASS `.npz` import through the app, project-local Python conversion, binary `.cache` generation, and cache list refresh/load flow.
- Stored generated cache vertices in project Y-up coordinates and rendered animated SMPL body playback with a 3D orbit camera.
- Added a subdued `y=0` ground grid as the shared visual reference direction for a future collision floor.
- Replaced the persistent left motion browser with a compact top-left `Motions` overlay that preserves the viewer area.
- Verification recorded in `PLAN.md`: Debug builds succeeded after the major revisions, regenerated caches had valid headers and upright Y-up extents, and manual app checks confirmed import, cache loading, viewer orientation, and overlay behavior.
- Known remaining issue: `windeployqt` still reports `VCINSTALLDIR is not set` during deploy, while the Debug build still completes and produces `simulation_app.exe`.
