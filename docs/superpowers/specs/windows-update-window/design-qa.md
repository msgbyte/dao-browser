# Update window preview verification

The reference was the selected second concept, `exec-a4f929e7-f000-4bd0-90e2-c3d8270d2994.png` in the session's generated images. Edge rendered the local preview in an 800 × 620 CSS pixel viewport at 1×. The installation window occupied 368 × 268 pixels. The comparison normalized the window in the 1470 × 1070 reference to the same size. The state selector and theme control sit outside the product window.

The initial side-by-side comparison showed the original Dao logo was slightly smaller and the heading slightly higher than in the concept. The second capture, taken after enlarging the logo and ring and increasing the gap above the heading, aligned the emblem and both lines of copy with the reference. The screenshot used the system Chinese font. The native text rasterization and the sharper edges of the original brand asset account for minor differences. The installation copy, window outline, light surface, blue halo and minimize control matched the selected composition. The dark rendering retained legible copy and a light mark.

DOM interaction checks exercised both locales, all six preview states, minimization and restoration, error dismissal, retry paths, log details, theme selection, playback and the final handoff. The Windows integration test can capture the corresponding native installation frame with its `-CapturePath` option. See [the implementation plan](../../plans/2026-11-28-windows-update-window.md) for the native verification commands.

final result: passed
