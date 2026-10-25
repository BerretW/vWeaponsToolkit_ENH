# Weapon GLB source structure

This is the proposed authoring contract for the GLB-to-weapon workflow.
**Static inspection, part-role assignment, alignment/pivot preparation and resumable import projects are
implemented; game conversion is not.** The names below
describe authoring roles, not GTA bone names or an implemented automatic
component exporter. Keep the original authoring file for later rigging.

## Recommended hierarchy

Example for a rifle with a removable magazine:

```text
w_ar_myweapon                 single root, meaningful grip/weapon origin
|-- body                     receiver, fixed barrel, fixed grip/stock/rails
|-- bolt                     separate if it moves during firing/reloading
|-- trigger                  separate if it will be animated
|-- charging_handle          separate if animated independently
|-- magazine                 detachable/reloadable magazine
|-- scope                    optional replaceable component
|-- suppressor               optional replaceable component
|-- flashlight               optional replaceable component
`-- markers                  empty nodes, not renderable meshes
    |-- muzzle
    |-- eject
    |-- attach_magazine
    |-- attach_scope
    |-- attach_suppressor
    `-- attach_flashlight
```

Not every weapon needs every node. For a revolver, separate the cylinder
and other animated parts instead of inventing a magazine. A fixed sight
can stay in `body`; a replaceable sight belongs in its own component.

### What must be separate?

| Part | Separate source object/mesh? | Reason |
| --- | --- | --- |
| Receiver, fixed barrel, fixed stock/grip | Usually one `body` object | No independent movement or replacement |
| Bolt/slide, trigger, cylinder, charging handle | Yes, when animated independently | Each needs a defined pivot and later bone binding |
| Detachable magazine | Yes | Reload animation and/or magazine component |
| Replaceable scope, suppressor, flashlight, grip | Yes | Optional component, not permanently merged into the body |
| Screws, engravings, welded details | No, unless independently movable | Unnecessary objects/materials increase geometry/draw overhead |
| Muzzle/ejection/attachment markers | Empty nodes, not visible meshes | Positions and orientations for the future rig adapter |
| Low-detail geometry | Separate LOD group/asset later | Do not overlay all LODs as ordinary meshes in the active import scene |

**Object, glTF primitive, GTA geometry and `.ydr` are not synonyms.** One
source object with several materials becomes several glTF primitives.
Conversely, several moving objects can ultimately belong to one skinned
weapon drawable, assigned to different bones. Optional components may
become separate drawables/resources entries. Do not export one `.ydr` per
source mesh blindly.

The current importer shows **one row per node instance and primitive**.
Select **All parts** or an individual row on the left. Objects using the
same source mesh remain separate instances after their transforms are
applied. Empty marker nodes are not rendered or converted to attachments.
**Apply name suggestions** can propose roles from the object and its
ancestors, for example a generically named mesh under `trigger`. Suggestions
are applied only on request and only to unassigned objects. Ambiguous parts
such as stock/sights remain for manual review. All material primitives of
one source object receive the same role; a shared source mesh instantiated
under another node can have a different role.

## Coordinates, units and pivots

- Author at real size in **metres**. A 0.7 m rifle should measure about 0.7
  units in the exported GLB, not 70 or 700.
- GLB follows glTF: right-handed, **Y up**. This importer converts to
  right-handed **Z up** using `(x, y, z) -> (x, -z, y)`.
- For a consistent proposed source convention, point the barrel along
  **glTF -Z**; it then points along **+Y** in the imported Z-up view.
  The new alignment step can rotate this to **+X**, the muzzle direction
  in the supplied carbine reference. Select the actual source barrel axis
  in the yaw preset; the application does not detect it automatically.
  Final alignment still needs checking against the selected weapon rig.
- Place the root origin deliberately near the grip/template origin.
  The importer does **not** centre, rescale or ground the weapon silently.
  The camera centres only the view, not the imported geometry.
- Give moving objects meaningful local pivots: trigger hinge, cylinder
  axis, slide/bolt movement. Preserve these in the authoring file.
  Static inspection bakes world transforms for display, but retains the
  original source world matrices/node hierarchy separately. Identity
  transforms do not magically provide hinge/attachment pivots.
- Prefer applying rotation/scale before export where that does not
  destroy the intended pivots. Nonuniform/mirrored transforms are supported:
  normals use inverse-transpose, mirrored winding/handedness are corrected.
  Zero/near-singular scales are rejected.
- Source marker orientations and real game bone names must eventually
  come from the chosen weapon profile. A marker called `muzzle` is not
  automatically a working muzzle-flash bone.

## Geometry and materials

1. Export only the intended active scene, with triangle meshes. Remove
   cameras/lights and unused helpers where possible.
2. Export normals, UVs and tangents. Hard edges/UV seams need their proper
   split vertices. Missing normals are generated **flat**, not smoothed
   silently; missing tangents are reported, not claimed export-ready.
3. Use one sensible material per surface type, not one per small object.
   Share materials where possible. A material boundary creates another
   primitive/geometry, so unnecessary material slots matter.
4. Keep body and components separately selectable but in the **assembled
   position**, not spread around the scene. Avoid duplicate overlapping
   geometry between `body` and removable components.
5. Embed images in GLB. Current inspection supports PNG/JPEG base-color
   images, material base-color factors and normalized vertex colors.
   It does not fetch external files/URLs or data URIs.
6. Use `TEXCOORD_0`, without a texture-transform extension, and repeat
   wrapping for the current importer. Preview sampling is basic and
   nearest/repeat, not a PBR renderer.
7. Author normal and metallic/roughness maps separately from base color.
   glTF PBR channels are **not** GTA normal/specular shader channels.
   They will require an explicit shader-specific mapping/conversion.
   Do not feed normal/packed maps through the diffuse-only PNG-to-YTD tool.
8. Transparency, normal-map lighting, emission and metallic/roughness
   lighting are not rendered in this inspection preview. Alpha data is
   retained in decoded images, but no transparency sorting is performed.

### Current import limits

- GLB 2.0 only; one embedded binary buffer, file at most 64 MiB, JSON at
  most 8 MiB, parser allocations bounded to 128 MiB.
- At most 4096 nodes, 128 source materials, 128 images and 128 imported
  primitive instances in the chosen scene.
- Each primitive: 3..65,535 vertices, complete triangles, at most 3,000,000
  indices; total imported vertex/index storage at most 64 MiB.
- With missing normals, flat-normal vertex splitting must also fit the
  65,535-vertex limit. Split larger meshes or export normals.
- Images: 1..4096 pixels per dimension, at most 128 MiB decoded RGBA total.
  NPOT textures are fine for inspection; later YTD export has its own
  power-of-two requirements.
- Interleaved attributes, normalized colors, indexed/non-indexed triangles,
  scene hierarchy and ordinary mesh instances are supported.
- Sparse accessors, triangle strips/fans, Draco, required extensions, skinning,
  GPU instancing and morph targets are rejected explicitly. Animation clips
  are not played; the importer reports that it is showing static transforms.
- Numeric attributes/transforms must be finite and within +/-1,000,000.
  Invalid indices, degenerate triangles and zero-length vectors are rejected.

The default scene is used. If several scenes exist without a default,
the first scene is used with a visible warning.

## Alignment and pivot preparation

After assigning roles, press **Next: align model**. All placement fields
use the importer's **Z-up metres**, not Blender's pre-export local axes.

- **Uniform scale:** positive, 0.0001..10000. Typical corrections are 0.01
  for centimetres or 0.001 for millimetres. No implicit resizing occurs.
- **Rotation XYZ:** degrees, each -360..360. Applied in X, then Y, then Z
  order. The yaw preset replaces these rotations; it preserves anchor/offset.
- **Source anchor:** a point in the original imported assembly that should
  map to the output offset. Right-click the model with the Model alignment
  tab active to choose a surface point, or enter XYZ explicitly.
- **Output offset:** position of that anchor in output coordinates.
  Thus output = offset + Rz*Ry*Rx*(scale*(source-anchor)).
- **Confirm alignment:** required before review, including when identity
  is the correct transform. Editing transform values clears confirmation.
- **Object pivot / test motion:** select a source object from the left,
  set a source-space pivot and a unit axis, then **Apply / confirm pivot**.
  Right-click with this tab active to choose its pivot. XYZ axis presets
  help; custom negative/diagonal unit axes can be entered numerically.

Magazine, bolt, trigger, charging handle and other-moving-part roles require
confirmed pivots. Body/fixed detail does not need an independent pivot.
Optional component attachment rigging is a later step. All primitives of
one object share their pivot; another instance of a mesh remains independent.
Changing a role invalidates that object's pivot confirmation.

**Use source object origin** is an explicit starting-point reset, not
automatic calibration. In the supplied M4A1 all node transforms are identity;
this means their source origins coincide, not that all hinges are at zero.
Do not confirm those origins blindly. Surface picking can seed a pivot,
but an actual hinge may require numeric adjustments inside the geometry.

The overlay shows output X/Y/Z in red/green/blue and the selected pivot's
axis in yellow. Point selection uses the visible frontmost triangle. Keep
alpha/preview limitations in mind; there is no alpha-aware picking.

The temporary test slider rotates up to +/-45 degrees or slides up to
+/-45 **output millimetres** along the chosen axis. Only the selected source
object moves. **Reset test** returns exactly to rest. Reset before picking;
the application rejects picking from a test pose rather than storing a
deformed point accidentally. Neither a pose nor animation keys are saved.
All previews are rebuilt from the original vertices, so repeated edits
do not accumulate transformations. Global normals rotate without scale.

Confirmations are author decisions, **not** real GTA bone bindings or proof
of a working reload/trigger animation.

## Later rig/component stage

For a first working version, prepare a **static, assembled, unskinned**
GLB with this separation. Do not merge the magazine/slide/components into
the body just to make import succeed. The future weapon-profile step needs
to map these roles to the selected template's real bones, bind transforms,
attachment points and component definitions, then generate appropriate LODs
and gen8/gen9 assets.

Today, use **Import GLB...** on the first tab or **Tools > Import GLB / open
preparation project...**. The four steps are:

1. Inspect the source.
2. Assign every source object a role or explicitly exclude it. At least one
   must be **Body / fixed detail**. Select the planned template/model name.
3. Align the model and confirm moving-part/magazine pivots.
4. Review the aligned included assembly in rest pose and save the project.

You can save unfinished assignments too. **Save project...** writes
`<name>.vwtimport.xml`, a versioned XML file referencing the GLB, its SHA-256,
source node/primitive identities, semantic roles, model name, planned
template, alignment and per-object pivots/axes/confirmation flags.
It is **not** a FiveM resource and must not go into `stream`.
Reopen it through Import GLB. Source paths are relative to the project
folder where possible (absolute across Windows drives); moving the project
and source together preserves that relationship. Changed GLB bytes, unknown
roles and inconsistent/duplicate part identities are rejected, not guessed.
The GLB itself remains untouched. Save validation failures do not truncate
an existing project.

New projects use version 2. Existing version-1 files are still accepted:
roles/template/name are preserved, alignment starts as unconfirmed identity,
and pivots start at unconfirmed source-world origins. Saving upgrades to
version 2; keep a copy if you need to open it in an older toolkit build.
Invalid scales, non-finite/out-of-range values, non-unit axes, duplicate
alignment elements and inconsistent per-object pivots are rejected.

The wizard does not alter the current weapon configuration or export files.
Static YDR serialization
exists as a separate backend, but GLB-to-game-shader/rig/resource conversion
is not yet connected. Neither inspection nor binary round-trip tests replace
an in-game FiveM test.
