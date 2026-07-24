# Assets — image/icon loading plan (v0.2)

## What HunterPie does (reference)

HunterPie's resource layer (in `HunterPie.UI/`) is WPF-on-.NET.
For context, here's the breakdown so we know what to mirror and
what to deliberately skip.

| Resource kind | HunterPie mechanism                            | File count in upstream |
|---------------|------------------------------------------------|------------------------|
| Icons (UI)    | **XAML vector** `<GeometryDrawing>` with embedded Path data | 100+ inline in `Resources/Icons/*.xaml` |
| Fonts         | Embedded TTF (WorkSans family)                 | 5 .ttf in `Assets/Fonts/` |
| Textures      | JPG (noise overlays, decorative)               | 1 in `Assets/Textures/` |
| Monster art   | PNG masks (`qurio_mask.png`)                   | 1 in `Assets/Monsters/Masks/` |

How they ship:
- All .xaml + .ttf + .jpg are declared as `<Page>` or `<Resource>`
  in `HunterPie.UI.csproj`.
- MSBuild embeds them in `HunterPie.UI.dll` as .NET embedded
  resources.
- WPF accesses them via `pack://application:,,,/HunterPie.UI;component/
  Resources/Icons/Icons.Activities.MaterialRetrieval` style URIs,
  resolved by the WPF resource resolver against the merged
  `Application.Resources.MergedDictionaries`.

The icons are **vector** (Path geometry, not raster). WPF
re-rasterises at display time, so they scale cleanly to any size
and re-tint via the icon's `Brush`. This is the right call for a
game overlay (you want sharp icons at 32 px and 256 px without
shipping two PNGs).

## What we ship in v0.2 (and what we don't)

### Reuse policy (revised after licence check)

| Source                              | Licence                          | Verdict                          |
|-------------------------------------|----------------------------------|----------------------------------|
| HunterPie code (C# / XAML)          | Apache-2.0                       | We are already a derivative work (offsets/structs are copy-pasted with attribution). Copy the licence text and we're done. |
| HunterPie icon **Path data**        | Apache-2.0 + author-stated "free to use" | **Reuse, convert XAML → SVG, attribute** in our NOTICE. |
| WorkSans font (TTF, in `Assets/Fonts/`) | SIL OFL 1.1                  | **Reuse directly** in `assets/fonts/`. |
| `Assets/Textures/alligator_noise_512x512.jpg` | HunterPie/Apache-2.0       | **Reuse** if we ship a noise texture; otherwise skip. |
| `Assets/Monsters/Masks/qurio_mask.png` | HunterPie (community-drawn but tied to a Capcom character) | **Skip** — Capcom copyright on the character. |
| MHW in-game assets (Capcom)         | MHW EULA (forbidden)             | **Skip** entirely. |

### What this changes in v0.2

1. **Icons** — instead of hand-drawing SVG placeholders, we copy
   HunterPie's `Resources/Icons/*.xaml` path data, run a
   one-off `xaml2svg.py` script (we'll provide it), and produce
   `assets/icons/*.svg` files. Attribution: "Icons adapted from
   HunterPie (Apache-2.0)" in `NOTICE`.
2. **Fonts** — copy `WorkSans-{Regular,Medium,SemiBold}.ttf` (or all
   5 weights) into `assets/fonts/`. SIL OFL requires the licence
   text and copyright notice in our `NOTICE`.
3. **Noise texture** — copy `Assets/Textures/alligator_noise_512x512.jpg`
   if we want the same background grain HunterPie uses.

### What we still don't ship

- Game-internal assets (`em\*_st108_*` etc. extracted from the
  binary at runtime). We **never** read these. The `em\*` strings
  in memory are used as data, not as image sources.
- Per-monster icons. HunterPie's `Assets/Monsters/` is empty of
  per-monster PNGs; the per-monster visual in HunterPie is also
  generated from the runtime data, not from static assets. We do
  the same: a generic monster silhouette + `id`-derived colour.

### Reuse pipeline: `tools/xaml2svg.py`

`HunterPie.UI/Resources/Icons/*.xaml` files contain `DrawingImage`
elements with `<GeometryDrawing Geometry="F1 ...z M0,0z M..."/>`
and `<EllipseGeometry>` / `<RectangleGeometry>`. The "F1 M..." path
data is **literally the same format as SVG's `<path d="M..."/>`** —
the only conversion is wrapping it in an SVG `<svg viewBox="0 0 W H">`
element.

The script does:
- Parse the XAML's `ClipGeometry` to get viewBox dimensions
- Extract every `<GeometryDrawing>` (paths, ellipses, rects)
- Emit an SVG file with the right viewBox + `<path>` children
- Replace Brush="#FFFFFFFF" with `currentColor` so the SVG can be
  re-tinted at runtime via `QSvgRenderer`

```python
# tools/xaml2svg.py (v0.2 starter, will live under src/tools/)
import re, sys
from pathlib import Path
import xml.etree.ElementTree as ET

WPF_NS = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"

def path_data_to_svg(d):
    # WPF "F1 Mx,yz Mx,y ..." → SVG "Mx,y ..." (the F1 is a
    # "fill rule" prefix, F1 = nonzero, default for SVG path).
    d = re.sub(r'^F1\s+', '', d.strip())
    d = d.replace(' M', ' M').strip()
    return d

def convert(xaml_path, out_path):
    tree = ET.parse(xaml_path)
    root = tree.getroot()
    # viewBox: from outermost DrawingGroup.ClipGeometry="M0,0 Vw Hh V0 H0 Z"
    # where Vw and Hh are the bounds. Simpler: from the file's DrawingImage
    # if no clip; fall back to 0 0 68 68.
    viewbox = "0 0 68 68"  # most HunterPie icons are 67.733 x 67.733
    for dg in root.iter(f"{{{WPF_NS}}}DrawingGroup"):
        clip = dg.get("ClipGeometry", "")
        m = re.match(r'M0,0\s+V([\d.]+)\s+H([\d.]+)\s+V0\s+H0', clip)
        if m:
            viewbox = f"0 0 {m.group(1)} {m.group(2)}"
            break
    paths = []
    for gd in root.iter(f"{{{WPF_NS}}}GeometryDrawing"):
        d = gd.get("Geometry", "")
        if d:
            paths.append(f'<path d="{path_data_to_svg(d)}"/>')
    out = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="{viewbox}">
  <g fill="currentColor">
    {chr(10).join("    " + p for p in paths)}
  </g>
</svg>
'''
    Path(out_path).write_text(out)

for src in Path("HunterPie.UI/Resources/Icons").rglob("*.xaml"):
    out = Path("assets/icons") / (src.stem + ".svg")
    convert(src, out)
```

After running this once, we have a 100+ icon library in
`assets/icons/`. v0.2 picks the 9 we need and bundles them via
`assets/icons.qrc`.

## What goes in `assets/`

```
assets/
├── fonts/
│   ├── OFL.txt                  # SIL OFL 1.1 (required)
│   └── WorkSans-{Regular,Medium,SemiBold,Light,ExtraLight}.ttf
├── icons/
│   ├── Weapons/                 # 14 weapon types × 12 ranks = 168 files
│   │   ├── Bow/Bow_Rank_01.svg … Bow_Rank_12.svg
│   │   ├── Great_Sword/Great_Sword_Rank_01.svg … Rank_12.svg
│   │   └── …
│   ├── crowns/                  # 3 files (crown_mini/large/king)
│   ├── Decorations/             # 76 files
│   ├── Hunter/                  # 7 files (Arms, Charm, Chest, …)
│   ├── Mantles/                 # 17 files
│   ├── Tools/                   # 3 files
│   ├── Traps/                   # 2 files
│   └── LICENSE                  # MIT, copied from upstream
├── charts/
│   └── alligator_noise_512x512.jpg
├── icons.qrc                    # Qt resource manifest
└── NOTICE                       # attribution summary
```

Total: **278 SVG icons + 5 fonts + 1 texture + 1 LICENSE + 1 NOTICE**.

## Attribution template (`assets/NOTICE`)

```
MHW Linux Overlay
Copyright 2024 a27exe

This product includes software developed by the HunterPie contributors
(https://github.com/HunterPie/HunterPie), licensed under the Apache
License, Version 2.0. Icon Path data was adapted from HunterPie's
XAML resources (Resources/Icons/*.xaml) per Apache-2.0 terms.

This product includes WorkSans (https://github.com/weiweihuanghuang/
Work-Sans), licensed under the SIL Open Font License, Version 1.1.
A copy of the OFL text is included as assets/fonts/OFL.txt.

This product includes software developed by the MHW community at
large; see README.md for the full list of contributors and source
repositories.
```

## v0.2 checklist (revised)

- [ ] Copy WorkSans TTFs into `assets/fonts/`
- [ ] Copy `OFL.txt` into `assets/fonts/`
- [ ] Write `tools/xaml2svg.py` and run it on HunterPie icons
- [ ] Pick 9-10 icons from generated set, place in `assets/icons/`
- [ ] Copy `alligator_noise_512x512.jpg` (optional)
- [ ] Write `assets/icons.qrc` bundling
- [ ] Write `assets/NOTICE`
- [ ] Implement `src/core/icon.h` + `icon.cpp` (QSvgRenderer wrapper)
- [ ] Implement `src/ui/dps_chart.h` + `dps_chart.cpp` (paint + grid + data series)
- [ ] Update `CMakeLists.txt` to link `assets/icons.qrc`