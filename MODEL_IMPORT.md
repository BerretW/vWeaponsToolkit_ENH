# Weapon GLB source structure

This is the proposed authoring contract for the GLB-to-weapon workflow.
**Only static GLB inspection is currently implemented.** The names below
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
No role is inferred from a name yet.

## Coordinates, units and pivots

- Author at real size in **metres**. A 0.7 m rifle should measure about 0.7
  units in the exported GLB, not 70 or 700.
- GLB follows glTF: right-handed, **Y up**. This importer converts to
  right-handed **Z up** using `(x, y, z) -> (x, -z, y)`.
- For a consistent proposed source convention, point the barrel along
  **glTF -Z**; it then points along **+Y** in the imported Z-up view.
  Final game alignment must be checked against the selected weapon
  template's rig; there is no automatic grip/alignment calibration yet.
- Place the root origin deliberately near the grip/template origin.
  The importer does **not** centre, rescale or ground the weapon silently.
  The camera centres only the view, not the imported geometry.
- Give moving objects meaningful local pivots: trigger hinge, cylinder
  axis, slide/bolt movement. Preserve these in the authoring file.
  Static inspection bakes world transforms; it is not a rigged model.
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

## Later rig/component stage

For a first working version, prepare a **static, assembled, unskinned**
GLB with this separation. Do not merge the magazine/slide/components into
the body just to make import succeed. The future weapon-profile step needs
to map these roles to the selected template's real bones, bind transforms,
attachment points and component definitions, then generate appropriate LODs
and gen8/gen9 assets.

Today, use **Import GLB...** on the first tab or **Tools > Import GLB for
inspection...**. This opens a separate inspection window and does not alter
the current weapon configuration or export files. Static YDR serialization
exists as a separate backend, but GLB-to-game-shader/rig/resource conversion
is not yet connected. Neither inspection nor binary round-trip tests replace
an in-game FiveM test.
