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

Legacy `.ydr/.ytd/.yft/.ydd` files do **not** load on FiveM Enhanced; they must be converted to gen9 first (Cfx **Alchemist**, **CodeWalker** Tools > Asset Converter, or **Sollumz** 2.8+ export). The manifest and `.meta` files are the same for both. This existing-asset Export tab expects preconverted gen9 files; the separate GLB wizard below can generate both formats natively:

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

This standalone PNG command is diffuse-only. The GLB workflow below has its
own normal/data-map conversion path. The `.ytd` writer is checked by
native round-trip tests and an independent Python reader against both
generations of the supplied assets; in-game validation is still required.

### Native drawable and weapon conversion

The native `buildYdr` API in [cYdrWriter.h](cYdrWriter.h) now creates binary
`.ydr` files for **Legacy (gen8)** and **Enhanced (gen9)** without an external
converter. The GLB wizard and `vwt-convert.exe` use the same backends.

- Supports multiple indexed triangle meshes and materials, positions, normals,
  vertex colors, UVs and tangent handedness.
- Writes vertex/index declarations, shader parameters, external texture
  references, bounds and a conservative bounding sphere for each generation.
- Accepts 1..128 meshes/materials and 3..65,535 vertices per mesh. Larger
  meshes must be split by the caller; indices are validated before conversion
  to 16 bits. Invalid vectors, triangles, material references and oversized
  inputs are rejected explicitly.
- Supports static `normal_spec_decal` and the fixture-verified opaque
  `weapon_normal_spec_detail_palette` binary shader profiles. This is not
  general support for every GTA shader.
- Supports a supplied gen8/gen9 reference skeleton, its real names/tags/
  hierarchy/runtime tables, inverse bind matrices and rigid one-bone-per-
  object weights. It does not invent animation clips or missing bones.
- High LOD only: no collision/automatic LOD generation or embedded textures.

Native tests generate models for both generations and load them back through
the preview reader. An independent Python reader checks vertex attributes,
indices, material mappings, bounds, declarations and shader buffer/resource
layouts, including the 65,535-vertex boundary. Gen9 shader metadata is also
compared with the supplied model fixture.

**Generated resources have not yet been validated in GTA/FiveM.** In particular
the separate magazine's reload/detachment behavior, game animations, normal
map decoding and palette/tint behavior need a real game test. Specialized
suppressor/flashlight helper rigs remain unsupported and are rejected explicitly.

### GLB import and model preparation

The wizard shows the full five-step route at the top, a short **what/why**
instruction for the current step and live counts of unfinished assignments,
pivots or bone bindings. **Show next unfinished object** selects and scrolls
to the next unresolved item (or focuses the missing alignment/reference
action). **Explain this step...** provides detailed instructions without
leaving the wizard. Save project stores editable work; Export creates game
files. Neither action silently substitutes for the other.

Use **Import GLB...** on the first tab or **Tools > Import GLB / open
preparation project...**:

1. **Inspect** the assembled model. Select a source object on the left;
   drag to rotate, scroll to zoom. All its material primitives are previewed
   together; the selected material name is shown in the preview status.
2. Press **Next: assign parts**. Choose the planned weapon template and
   output model name, then assign roles using the dropdown: body/fixed
   detail, magazine, bolt, trigger, charging handle, other moving part,
   optional component, or explicit exclusion.
   **Apply name suggestions** uses source node/parent names, but only after
   you request it and never overwrites manually assigned objects.
   Review every suggestion; unknown objects stay unassigned.
3. Press **Next: align model**. Adjust positive uniform scale, rotation XYZ,
   source anchor and output offset. Use **Apply placement** to preview edits
   and **Confirm alignment** once correct (even when identity is correct).
   The yaw preset turns a manually selected source barrel axis towards +X,
   matching the supplied carbine reference, not an automatically detected rig.
   In **Object pivot / test motion**, select an included object on the left,
   set its source-space pivot and unit hinge/slide axis, then confirm it.
   Right-click a visible surface in the preview to pick the source anchor
   or selected pivot, depending on the active tab. Numeric edits allow
   points inside/off the surface. Rotate/slide testing is temporary; reset
   it before picking. Magazine/bolt/trigger/charging-handle/other-moving-part
   objects require pivot confirmation before review.
4. Press **Next: bind bones**. Load a reference **weapon `.ydr`** matching
   the chosen template. No XML/Blender/converter is needed at runtime.
   For the supplied fixture, choose `test\gen8\w_ar_carbinerifle_m31.ydr`
   or its gen9 counterpart and the CARBINERIFLE template.
   Assign each included object to a bone from the dropdown. Suggestions
   fill only unbound objects and only known matching bone names. Bolt,
   charging-handle and other moving-part mappings require an explicit choice.
   Test rotation affects the selected bone **and its descendants**, unlike
   the individual-object pivot test. The displayed pivot-distance helps
   spot alignment differences. **Move bone origin to confirmed pivot**
   adjusts that bone's rest origin while preserving child world positions
   and the authored mesh rest pose; review this explicitly.
   With **All parts** selected, the bone dropdown can inspect auxiliary
   muzzle/ejection/attachment bones too. Arm **Pick reference bone origin**
   and right-click a surface to move its origin; child origins stay fixed.
   The CLI `bone-origin` command also allows points off the surface.
5. Press **Next: review**. Inspect the rest assembly, counts and sizes.
   Set a unique resource name and weapon ID, then **Export Legacy + Enhanced
   resources...**. A new `<resource>_converted` folder contains `<resource>`
   and `<resource>_enhanced`, each with YDR/YTD, manifest, Lua names and meta.
   Magazine, opaque scope and grip roles are exported as separate models,
   own dictionaries, custom component metadata and default attachments.
   The base model does not contain a duplicate of the component geometry.
   Export never overwrites an existing destination. Use only one generation
   on a server. **Back** preserves preparation data.
6. **Save project...** stores a draft or completed preparation as
   `<name>.vwtimport.xml`. Keep this filename suffix. Reopen it through the
   same Import GLB button. The project references the original GLB and
   checks its SHA-256; it does not embed or modify the model. Keep the source
   GLB with the project. A re-exported/changed GLB needs a new import.
   New saves use project version 3, embedding the reference skeleton snapshot
   and bone bindings, so the original reference YDR is not required to reopen.
   Changing a part role clears that object's bone mapping. Changing the
   weapon template requires reloading a reference for the new template.
   Version-1 projects retain their roles
   and load with identity alignment and unconfirmed source-origin pivots;
   no calibration is invented. Version-2 projects keep alignment/pivots.
   Both old versions start without a reference skeleton or bone mappings.

Your existing weapon configuration and `.ydr/.ytd` export are not changed.
Closing a changed project asks whether to save it.

The native importer reads embedded GLB 2.0 triangle geometry, scene/node
transforms, normals, UVs, tangents, vertex colors, base-color factors and
PNG/JPEG textures. It preserves metres/origin and converts Y-up to Z-up.
Mirrored/nonuniform transforms are handled explicitly. Missing normals
generate flat faces; missing tangents and preview limitations are reported.
Unsupported inputs fail with a diagnostic rather than dropping meshes.

Semantic roles and actual reference-bone mappings are separate. The supplied
skeleton is declared to belong to the selected template by the author; the
tool cannot establish animation compatibility from the filename alone.
Placement is non-destructive: output = offset + Rz*Ry*Rx*(scale*(source-anchor)).
Original GLB vertices are never overwritten, and test poses are never saved.
Confirmations record your authoring decisions, not in-game verification.
The preview is base-color/basic lighting only, not PBR/alpha rendering.
For responsiveness, imported GLB preview textures are capped at512 pixels
on their longest side; export reads the original embedded images independently.
Camera events are coalesced on a16ms timer. During rotation/zoom/resize the
software renderer uses at most360 pixels on the longest side, then restores
full viewport resolution after120ms idle. Geometry and picking are not
decimated; pending camera updates are flushed before a point is picked.
Linear-light vertex-color modulation uses lookup tables (tested within one
RGB byte of the direct calculation) rather than per-pixel power functions.
See [MODEL_IMPORT.md](MODEL_IMPORT.md) for limits and the proposed authoring
hierarchy: fixed body together, moving parts and removable components
separate, meaningful origins/pivots and future attachment markers.

### Material policy and current export limits

GLB export accepts **OPAQUE metallic-roughness** materials, embedded PNG/JPEG,
UV0 without texture transforms and repeat wrapping. It validates original
glTF metadata, not just the reduced inspection fields. MASK/BLEND,
double-sided/unlit/unsupported PBR extensions and separate AO maps fail
explicitly. Shared occlusion in the metallic-roughness texture's red channel
is baked into linear diffuse using occlusion strength.

- Base color factors are baked in linear light, then encoded as sRGB.
- Normal maps use their glTF scale and are renormalized; no green inversion
  is applied. Normal mips renormalize; data/spec mips stay linear.
- Metallic/roughness is an **artistic approximation**, not PBR equivalence:
  grayscale spec = `(0.04 + 0.96 * metallic) * (1 - 0.75 * roughness)`.
- Neutral detail normal and white palette textures with
  `detailSettings=(0,0,1,1)` are explicit profile choices. Their in-game
  visual neutrality is not proven by binary structural tests.
- Export uses power-of-two textures, default maximum **1024**, minimum4.
  CLI can request 4..4096. Oversized textures are explicitly box-filtered
  by color/normal/data semantic; no arbitrary image distortion is performed.
- Supplied tangents are retained and rotated with alignment; missing
  tangents are generated per triangle, preserving UV handedness. Meshes
  above 65,535 generated vertices are split at triangle boundaries.
  Collapsed UVs have no unique normal-map orientation: a deterministic
  geometric tangent basis is generated with an explicit per-part diagnostic.
- Magazine/Scope/Grip roles become separate component models, using their
  actual reference WAP bone and a real component-local root anchor.
  All pieces of one role must share the same attachment bone; one assembled
  variant per role is supported. Default components are enabled in metadata.
- Suppressor/Flashlight export is blocked until compatible component-specific
  muzzle/bulb rigs are supplied through a supported workflow. The weapon's
  skeleton alone cannot prove those helpers exist. Do not relabel them as
  Body to bypass this. Transparent scopes also remain outside OPAQUE support.

`conversion-report.txt` records the policy and limitations next to the
generated resources. A completed conversion is not a claim of an in-game test.
Each resource includes `vwt_validate.lua`: on client resource start it
nonintrusively requests the weapon/component assets and logs structured
`[vwt-validation]` results. It does not equip weapons or change the player.
Check a captured client log with
`python tests\verify_fivem_log.py <log> <resource>`.
Missing/failing records are errors, not assumed success. Even a passing
runtime asset check does not validate animation alignment or appearance.

### Autonomous / command-line operation

Release builds also produce **`vwt-convert.exe`**, using the same native
import/project/rig/material/resource code without dialogs. Run it from any
working directory. It uses `templates` beside the executable. All mutations
are explicit commands, JSON goes to stdout, errors to stderr; exit codes
are **0 success, 1 error, 2 incomplete validation**.

```powershell
.\x64\Release\vwt-convert.exe --help
.\x64\Release\vwt-convert.exe inspect-glb .\test\m4a1.glb
.\x64\Release\vwt-convert.exe create .\test\m4a1.glb C:\work\m4.vwtimport.xml WEAPON_CARBINERIFLE w_ar_m4a1
.\x64\Release\vwt-convert.exe roles C:\work\m4.vwtimport.xml --suggest
.\x64\Release\vwt-convert.exe inspect-project C:\work\m4.vwtimport.xml
```

Use source **node indices** from inspection, not list row numbers:
`roles project NODE role-id`, `bind project NODE bone-index`.
`align project SCALE RX RY RZ ORIGIN_X ORIGIN_Y ORIGIN_Z OFFSET_X OFFSET_Y OFFSET_Z --confirm`
and `pivot project NODE PX PY PZ AX AY AZ --confirm` set placement explicitly.
`reference project reference.ydr`, `bind project --suggest`,
`fit-bone project NODE`, `bone-origin project BONE_INDEX OUTPUT_X OUTPUT_Y OUTPUT_Z`,
and `validate project assignments|placement|rig`
complete the programmable preparation. Existing assignments are not overwritten
by suggestions. No command silently confirms a guessed pivot or alignment.

```powershell
.\x64\Release\vwt-convert.exe validate C:\work\m4.vwtimport.xml rig
if ($LASTEXITCODE -ne 0) { throw "Preparation incomplete" }
.\x64\Release\vwt-convert.exe export C:\work\m4.vwtimport.xml C:\work\new_output m4a1 WEAPON_CUSTOM_M4A1 1024
```

The final directory must not exist; its parent must exist. Output is staged
and atomically published only after both generations and metadata are built.
The source GLB is never modified. Scripted fixtures use synthetic placement
for structural tests and do **not** replace your authored alignment/pivots.

### Completion and remaining acceptance checks

| Area | Verified offline | Remaining |
| --- | --- | --- |
| GLB import, roles, pivots and project migration | Native tests and actual wizard events | Author-specific calibration |
| Real reference skeleton, rigid bindings and native gen8/gen9 YDR/YTD | Native round trip plus independent binary/XML checks | GTA/FiveM animation and visual acceptance |
| Base weapon + magazine/scope/grip resource metadata | Production CLI end-to-end export tests | Real component/reload behavior |
| Autonomous operation | CLI inspection/configuration/validation/export and failure tests | A running FiveM client/server test environment |
| Specialized components and rendering | Explicit rejection, no invented helper bones | Suppressor/flashlight helper-rig workflow, transparent/PBR variants |

Only high LOD is generated; automatic LODs/collision and arbitrary shader
profiles are not part of this supported conversion profile. Existing malformed
templates (for example MINISMG animation XML) fail explicitly instead of being
silently accepted.

## License

GPL-3.0 (see [LICENSE](LICENSE)). Format research references and preserved
third-party notices are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
The new resource/texture/drawable writers are original implementations, not a port
of CodeWalker. CodeWalker's source is marked educational-use-only, not
generally GPL; Sollumz's license does not grant permission to copy CodeWalker.
Neither tool is required at runtime. GLB parsing uses the vendored,
MIT-licensed cgltf 1.15; it adds no external runtime dependency.

## Building

Requires Visual Studio 2026 (toolset v145, C++ desktop workload) and wxWidgets (e.g. `vcpkg install wxwidgets:x64-windows` followed by `vcpkg integrate install`). The post-build step copies `templates`, `export`, `icon.ico`, licenses and user documentation next to the exe, so the output folder (e.g. `x64\Release`) is runnable as is.
Release x64 also builds the console converter project automatically.

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
MSBuild .\vWeaponsToolkit.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Release/CLI build failed" }
python .\tests\verify_converter_cli.py "$PWD" "$PWD\tests\x64\Release\generated"
if ($LASTEXITCODE -ne 0) { throw "CLI pipeline failed" }
```

Python 3 is needed only for independent verification, not by the application.
Tests cover page sizing/version flags, raw DEFLATE, pointers, dictionary
hash ordering, gen9 SRVs, BC1/BC3 color/alpha error limits, mip chains,
invalid inputs, deterministic output, Unicode output paths and loading
new dictionaries with the supplied gen8/gen9 regular and high-LOD drawables.
Drawable tests additionally cover single/multiple meshes and materials,
vertex/index round trips, bounds, legacy parameter blocks, gen9 constant
buffer copies and resource slots, and invalid geometry/material inputs.
GLB tests cover hierarchy/instances, inverse-transpose normals, mirrored
winding/tangents, UV/color/material data, embedded PNG alpha/JPEG, per-part
selection, flat-normal generation, Unicode paths and malformed/unsupported
inputs. Project tests cover drafts/completed mappings, relative/Unicode paths,
source SHA-256, malformed XML/roles/identity, failed-save preservation,
grouping an object's material primitives, excluded assembly geometry and
the actual modal wizard's Next/role selection/right-click picking/alignment/
pivot confirmation/motion/Review/Back event handlers. Placement tests check
rotation order, inverse coordinate conversion, physical slide distances,
normals, excluded/unchanged objects, rest-pose reset and version-1 migration.
When `test\m4a1.glb` is present, it is tested read-only, including hierarchy
role suggestions and project round trips; this optional user fixture is not
required on a clean checkout. Own-dialog screenshots are written to the
generated test directory. The generated `static_import.glb` can also be opened in the GUI.
Preview tests measure the real M4A1 at640x400 and interactive360x225 (50ms
interactive render budget), verify color accuracy and a real modal-loop burst
of40 mouse events becoming one interactive frame plus one full-quality settled
frame. Timings are machine-dependent; these are not GPU/PBR benchmarks.
Generated fixtures and test binaries stay under the ignored `tests\x64` directory.

## Planned #TODO
- Singleplayer support
- More templates from DLC weapons
- Add-on ammo creation
