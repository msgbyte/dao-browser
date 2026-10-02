# Windows application icon

`app.png` is the Windows-specific 1024px master. Keep it independent from
`branding/mac/app.icns`: the platforms share the Dao mark and white rounded
tile, but use different outer spacing. The Windows tile fills about 92% of
the canvas, versus about 84% on macOS, so it has a comparable visual size to
neighboring taskbar icons. Keep the mark-to-tile proportions and transparent
corners intact.

Regenerate `dao.ico` on Windows after editing `app.png`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/generate-windows-icon.ps1
```

The script uses Windows' built-in image APIs and includes 16, 20, 24, 32, 40,
48, 64, 96, 128, and 256px frames for taskbar, window-switcher, and Explorer
scales. Import copies only the resulting ICO into Chromium's Windows icon
resource; macOS continues to use its own ICNS. When changing the icon, bump
`kCurrentProfileIconVersion` in the corresponding Dao patch so existing
profile shortcut icons regenerate, then run `npm run rebuild`.
