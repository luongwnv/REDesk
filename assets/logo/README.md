# REDesk logo & icon

Brand mark for REDesk, styled after the reference wordmark: a solid square tile
holding white letters, with the rest of the name in black beside it.

- **`redesk-logo.svg`** — horizontal **wordmark**: `RED` (white) inside a sharp red
  square tile + `esk` (black) → reads "REDesk". Use in the app header, README,
  website, docs.
- **`redesk-icon.svg`** — square **app icon** (1:1): solid red field with `RED`
  in white, legible at small sizes (Dock / taskbar / favicon).

PNG exports are rendered from the SVGs with `rsvg-convert`:

- `redesk-logo-{256,512,1024}.png` — wordmark
- `redesk-icon-{32,64,128,256,512,1024}.png` — square icon

## Colors

| Token | Hex | Use |
|---|---|---|
| Red (accent) | `#EF443B` | the tile / icon field (AnyDesk-style accent, matches `ui/qml/Theme.qml`) |
| White | `#FFFFFF` | letters inside the tile |
| Near-black | `#111111` | the `esk` letters |

## Regenerate PNGs

```sh
cd assets/logo
for w in 256 512 1024; do rsvg-convert -w $w redesk-logo.svg -o redesk-logo-$w.png; done
for s in 32 64 128 256 512 1024; do rsvg-convert -w $s -h $s redesk-icon.svg -o redesk-icon-$s.png; done
```

> Text is rendered with the system Helvetica/Arial. To make the SVG fully
> font-independent (identical on any machine), convert the `<text>` to outlines
> (e.g. `inkscape --export-text-to-path`) — left as a follow-up.

## Wiring into the app icon (later)

To replace the default macOS/Windows app icon, pack the PNGs into `.icns`/`.ico`
and point the bundle at them (see `cmake/PackageDeploy.cmake` / `ui/CMakeLists.txt`).
Not wired yet — the marks here are the source of truth.
