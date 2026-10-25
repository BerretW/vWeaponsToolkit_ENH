# vWeaponsToolkit

A tool that allows the creation of add-on weapons & add-on components with ease in GTA V/FiveM.

## Features
- Add-on weapon creation using templates from base-game weapons. (Over 20+ templates, more may be added if requested)

![Alt Text](https://gyazo.com/90820ee8b61f3a96cba911118c63dd2a.gif)

 ### Configurable values on each weapon:
  - Audio
  - Damage
  - Range
  - Ammo Type
  - LOD
  - Reload speed modifier
  - Fire rate modifier
  - Damage type
  - Headshot damage modifier

### Add-on component creation using templates from base-game weapons.
- Create any add-on component (Magazines, Grips, Scopes, Flashlights):
- Configure clip size, ammo type (Tracer/FMJ/Incendiary/Explosive)


## Quick start

1. Run `vWeaponsToolkit.exe` (keep the `templates` folder next to it).
2. **Create Add-on Weapon** tab
   - Pick the *Import Folder* with your weapon models. Found files are listed: the `<model>.ydr` is required (green), `_hi.ydr`, `.ytd` and `+hi.ytd` are matched automatically (green), component models such as `_mag1`, `_scope`, `_supp` are shown in blue.
   - Choose a *Weapon Template* (base-game weapon to copy), then fill in *Weapon Name*, *Weapon ID* and *Weapon Model* (the model file name without extension).
   - Press **Next** (needs a valid model, otherwise an error is shown).
   - A **model preview** (right side) shows the loaded `.ydr` with textures from the `.ytd`. It supports both Legacy (gen8) and Enhanced (gen9) files; drag to rotate, mouse wheel to zoom, double-click to reset. The `_hi.ydr`/`+hi.ytd` are preferred when present. The preview is software-rendered (diffuse texture only); BC7 textures other than mode 6 appear as gray.
3. **Configuration** tab - set audio, damage, range, ammo type, LOD, reload/fire rate modifiers, damage type and headshot modifier.
4. **Components** tab (optional) - pick a component template, set its name, model, LOD, clip size and ammo info, tick *Component Enabled*, then **Add Component**. Use **Remove Component** to delete the selected one.
5. **Export** tab - choose an *Export Folder* and press **Export**. A resource folder named after the weapon is created with `fxmanifest.lua`, `cl_weaponNames.lua`, `stream\` (models) and `meta\` (weapons, archetypes, animations, pedpersonality, components).
6. Copy the exported folder to your FiveM `resources` directory and add `ensure <folder name>` to `server.cfg`.

### FiveM Enhanced (GTA V Enhanced / gen9)

Legacy `.ydr/.ytd/.yft/.ydd` files do **not** load on FiveM Enhanced; they must be converted to gen9 first (Cfx **Alchemist**, **CodeWalker** Tools > Asset Converter, or **Sollumz** 2.8+ export). The manifest and `.meta` files are the same for both. The toolkit does not convert models itself:

1. Convert your model files and put the results in one folder, keeping the **same file names** as the originals.
2. On the Export tab tick *Also export FiveM Enhanced (gen9) resource* and select that folder.
3. Export creates two resources: `<WeaponName>` (legacy) and `<WeaponName>_enhanced` (identical meta, gen9 models in `stream\`). Use the one matching your server (Legacy FXServer vs. Cfx Enhanced server).

Enhanced support in FiveM is early access; keep gen9 models under 128 materials/geometries per file and test with the latest build.

The in-app **Help > How to use** shows a short version of this.

### Built-in converter: texture dictionary milestone

**Tools > Build texture dictionary from PNG...** creates a binary `.ytd`
without Blender, Sollumz, CodeWalker or another external converter:

1. Select one or more **diffuse/color PNGs**. File names become texture names
   (case-insensitive ASCII letters, digits and underscores; no extension).
   They must match the texture names referenced by the model's shaders.
2. Choose **Legacy (gen8)** or **Enhanced (gen9)**.
3. Save the `.ytd` next to the matching `.ydr`, using the weapon model's name.
   Reimport that folder to refresh the asset list and preview.

Opaque images use BC1/DXT1; images with transparency use BC3/DXT5. Dimensions
must be powers of two: **4..4096 for Legacy**, **1..4096 for Enhanced**.
Legacy mips stop when either dimension reaches four pixels, matching the
sample resources; Enhanced mips continue to 1x1. Diffuse mip filtering uses
linear-light, alpha-weighted colors. Normal maps and packed material maps
must not use this diffuse-only command.

Invalid dimensions, duplicate names/hash collisions, `script_rt_` textures
and inputs above 128 MiB of RGBA data are rejected with an error. The command
does not resize images silently. It builds the dictionary before saving and
uses a temporary file so a failed conversion does not truncate an existing
output file.

**This is not yet the GLB-to-resource workflow.** GLB import, PBR material
mapping, `.ydr` writing, skeletons, component attachment points and LOD
generation remain to be implemented. The new `.ytd` writer is checked by
native round-trip tests and an independent Python reader against both
generations of the supplied assets; in-game validation is still required.

## License

GPL-3.0 (see [LICENSE](LICENSE)). Format research references and preserved
third-party notices are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
The new resource/texture writer is an original implementation, not a port
of CodeWalker. CodeWalker's source is marked educational-use-only, not
generally GPL; Sollumz's license does not grant permission to copy CodeWalker.
Neither tool is required at runtime.

## Building

Requires Visual Studio 2026 (toolset v145, C++ desktop workload) and wxWidgets (e.g. `vcpkg install wxwidgets:x64-windows` followed by `vcpkg integrate install`). The post-build step copies `templates`, `export` and `icon.ico` next to the exe, so the output folder (e.g. `x64\Release`) is runnable as is.

### Resource writer tests

In a VS 2026 Developer PowerShell, from the repository root:

```powershell
MSBuild .\tests\ResourceWriterTests.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Test build failed" }
New-Item -ItemType Directory -Force .\tests\x64\Release\generated | Out-Null
.\tests\x64\Release\ResourceWriterTests.exe "$PWD" "$PWD\tests\x64\Release\generated"
if ($LASTEXITCODE -ne 0) { throw "Native tests failed" }
python .\tests\verify_resources.py "$PWD" "$PWD\tests\x64\Release\generated"
if ($LASTEXITCODE -ne 0) { throw "Independent verification failed" }
```

Python 3 is needed only for independent verification, not by the application.
Tests cover page sizing/version flags, raw DEFLATE, pointers, dictionary
hash ordering, gen9 SRVs, BC1/BC3 color/alpha error limits, mip chains,
invalid inputs, deterministic output, Unicode output paths and loading
new dictionaries with the supplied gen8/gen9 regular and high-LOD drawables.
Generated fixtures and test binaries stay under the ignored `tests\x64` directory.

## Planned #TODO
- Singleplayer support
- More templates from DLC weapons
- Add-on ammo creation
