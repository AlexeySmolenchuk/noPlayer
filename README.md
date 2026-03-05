# noPlayer

## How to use

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

## OCIO Setup

`noPlayer` reads the OCIO config from the `OCIO` environment variable.

### Linux / macOS

```bash
export OCIO=/absolute/path/to/config.ocio
```

### Windows (PowerShell)

```powershell
$env:OCIO = "C:\path\to\config.ocio"
```

If `OCIO` is not set (or the config is invalid), `noPlayer` falls back to a built-in OCIO config.

## .ocio Examples (Web Paths)

### Colour-Science legacy OCIO configs (archived)

- Repository: https://github.com/colour-science/OpenColorIO-Configs
- Example `.ocio` file (ACES 1.2): https://raw.githubusercontent.com/colour-science/OpenColorIO-Configs/master/aces_1.2/config.ocio
- Releases (includes downloadable config archives): https://github.com/colour-science/OpenColorIO-Configs/releases

### Current ACES OCIO configs (recommended)

- Project: https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES
- Releases: https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES/releases
- Example `.ocio` download paths:
  - https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES/releases/download/v4.0.0/cg-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio
  - https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES/releases/download/v4.0.0/studio-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio
