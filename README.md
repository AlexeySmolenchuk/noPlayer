# noPlayer

`noPlayer` is a desktop image viewer for high-end image inspection. It is built around OpenImageIO, OpenEXR, ImGui, OpenGL, and OCIO, and is aimed at looking at rendered images, AOVs, MIPs, channels, and waveform data quickly.

## What the app can do

- Open images supported by OpenImageIO.
- Open a file from the command line or by drag and drop.
- View subimages / AOVs and switch between them.
- View MIP levels and switch between them.
- Zoom, pan, fit, and inspect images interactively.
- Solo channels as `RGB`, `R`, `G`, `B`, `H`, `S`, `V`, `Y`, or `A`.
- Adjust gain and offset, or auto-fit the current visible range.
- Toggle OCIO display/view transforms.
- Toggle NaN highlighting.
- Inspect a single pixel under the mouse.
- Drag an inspect region and see the average values over that region.
- Open a split waveform view.
- Resize the waveform split left and right with a draggable divider.
- Show waveform paint modes:
  - `Luminance - YUV`
  - `RGB`
  - `R`
  - `G`
  - `B`
- Scope only the selected inspect region in the waveform.
- Link image hover and waveform hover both ways when inspect is enabled:
  - hover the waveform to find the matching place in the image
  - hover the image to find the matching place in the waveform

## Build

Run commands from the repository root.

### Linux

```bash
cmake --preset linux-release
cmake --build --preset linux-release
cmake --install build/linux-release --prefix /your/install/path
```

### Windows

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
cmake --install build/windows-release --prefix C:/path/to/install
```

## Run

Run with an image path:

### Linux

```bash
./build/linux-release/src/noPlayer /path/to/image.exr
```

### Windows

```powershell
.\build\windows-release\src\Release\noPlayer.exe C:\path\to\image.exr
```

You can also:

- launch without an image and drag a file into the window
- reload the current image with `F5`

## How to use

Typical workflow:

1. Open an image.
2. Use the AOV list on the left side of the image pane to switch planes.
3. Use mouse wheel to zoom and middle mouse to pan.
4. Press `F` to fit the image to the current image pane.
5. Press `I` to enable inspect mode.
6. Hover the image to inspect pixel values.
7. Drag with left mouse in inspect mode to create a region and inspect average values.
8. Press `W` to open the split waveform.
9. Drag the split divider to resize waveform and image panes.
10. Change waveform paint mode from the dropdown at the top of the waveform pane.
11. Press `O` to open the OCIO display/view picker if you want to change display transform.

## Mouse controls

These controls apply to the image view.

| Control | What it does |
|---|---|
| `Mouse wheel` | Zoom in and out around the mouse cursor |
| `RMB drag` | Temporary zoom drag around the click pivot |
| `MMB drag` | Pan image view |
| `Ctrl + LMB drag` | Pan image view |
| `LMB drag` in inspect mode | Create or update an inspect region |
| `LMB click` in inspect mode with an existing region | Clear the current inspect region |
| Drag waveform divider | Resize waveform and image panes |
| Drag file into window | Open a new image |

## Keyboard shortcuts

| Shortcut | What it does |
|---|---|
| `F1` | Show shortcut help while held |
| `Esc` | Exit |
| `F11` | Toggle fullscreen |
| `F5` | Reload current image |
| `H` | Hide UI |
| `F` | Toggle fit / 100% view |
| `[` | Previous AOV / plane |
| `]` | Next AOV / plane |
| `PgUp` or `Numpad 9` | Previous MIP |
| `PgDn` or `Numpad 3` | Next MIP |
| `` ` `` | Clear channel solo and return to normal RGB view |
| `1` | Solo `R` |
| `2` | Solo `G` |
| `3` | Solo `B` |
| `4` | Solo `H` |
| `5` | Solo `S` |
| `6` | Solo `V` |
| `7` | Solo `Y` |
| `8` | Solo alpha on the current plane, or jump to another alpha plane |
| `0` | Reset gain and offset |
| `-` | Decrease gain by one stop |
| `=` | Increase gain by one stop |
| `R` | Auto-fit gain and offset to the current visible range |
| `I` | Toggle inspect mode |
| `O` | Toggle OCIO display/view picker |
| `W` | Toggle split waveform view |
| `Numpad +` | Zoom in |
| `Numpad -` | Zoom out |

Notes:

- `H`, `S`, `V`, and `Y` solo modes only make sense on RGB planes.
- `8` prefers the current plane when it already has an alpha channel.
- If the current plane has no alpha, `8` switches to another alpha-bearing plane and prefers a single-channel `A` plane when available.
- `R` auto-fit uses the currently active solo mode when possible.

## Inspect mode

Press `I` to toggle inspect mode.

What inspect mode gives you:

- pixel coordinates
- per-channel values under the mouse
- raw `H`, `S`, `V`, `Y` readout for RGB planes
- average values over a dragged region

When an inspect region is active:

- the waveform scopes only the selected pixels
- the selected region is stretched across the full waveform width
- image-to-waveform hover markers only appear while the mouse is inside the region

## Waveform view

Press `W` to open the split waveform.

Waveform behavior:

- the left pane shows the waveform
- the right pane keeps the image view
- the divider between them can be dragged left or right
- all image overlays and controls stay attached to the image pane
- the waveform uses the visible panel resolution for speed, while still accumulating all source pixels into the visible bins
- the waveform background grid follows the displayed logarithmic value scale

Waveform paint modes:

- `Luminance - YUV`: shows Y only using BT.709 luma coefficients
- `RGB`: shows red, green, and blue waveforms together
- `R`: red only
- `G`: green only
- `B`: blue only

Waveform/image linking:

- with inspect enabled, hover the waveform to draw a crosshair over the matching image location
- with inspect enabled, hover the image to draw a marker in the waveform
- in `RGB` waveform mode, the linked marker uses per-channel colors

## OCIO setup

`noPlayer` reads the OCIO config from the `OCIO` environment variable.

### Linux / macOS

```bash
export OCIO=/absolute/path/to/config.ocio
```

### Windows (PowerShell)

```powershell
$env:OCIO = "C:\path\to\config.ocio"
```

If `OCIO` is not set, or the config cannot be loaded, `noPlayer` falls back to a built-in OCIO config.

## OCIO examples

### Colour-Science legacy OCIO configs (archived)

- Repository: https://github.com/colour-science/OpenColorIO-Configs
- Example `.ocio` file (ACES 1.2): https://raw.githubusercontent.com/colour-science/OpenColorIO-Configs/master/aces_1.2/config.ocio
- Releases: https://github.com/colour-science/OpenColorIO-Configs/releases

### Current ACES OCIO configs (recommended)

- Project: https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES
- Releases: https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES/releases
- Example `.ocio` download paths:
  - https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES/releases/download/v4.0.0/cg-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio
  - https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES/releases/download/v4.0.0/studio-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio
