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

## Building

Requires Visual Studio 2026 (toolset v145, C++ desktop workload) and wxWidgets (e.g. `vcpkg install wxwidgets:x64-windows` followed by `vcpkg integrate install`). The post-build step copies `templates`, `export` and `icon.ico` next to the exe, so the output folder (e.g. `x64\Release`) is runnable as is.

## Planned #TODO
- Singleplayer support
- More templates from DLC weapons
- Add-on ammo creation

