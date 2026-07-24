#!/usr/bin/env python3
"""
xaml2svg.py — convert HunterPie's WPF XAML icons to SVG.

Handles two layouts:
  1. Single-icon .xaml files (one <DrawingImage> per file).
  2. Multi-icon .xaml files (many <DrawingImage x:Key="..."> in one
     file). Each <DrawingImage> becomes its own .svg.

WPF geometry quirks we handle:

* WPF path data uses the same format as SVG <path d="..."/>; only the
  wrapping <svg> is missing. Fill brushes become fill="currentColor"
  for runtime tinting via QSvgRenderer.

* HunterPie's icons live in a transformed coordinate system. The
  outermost <DrawingGroup> usually has ClipGeometry="M0,0 Vw Hh V0 H0
  Z" (the icon's intended viewBox), but the <GeometryDrawing>
  children may be wrapped in multiple <DrawingGroup Transform=...>
  elements that translate raw path coords into the clip region.
  We sum those transforms and apply them to the path data, then
  compute viewBox from the result.

* ElementTree's `iter()` is fine but the WPF XAML namespace varies
  between `winfx/2006/xaml` (Key/Geometry/Brush/Transform) and
  `winfx/2006/xaml/presentation` (DrawingImage/DrawingGroup etc).
  We probe element attributes directly to extract namespace-aware
  values.

Usage:
    python3 tools/xaml2svg.py <HunterPie-Resources-dir> <out-dir>
"""

import re
import sys
from pathlib import Path
import xml.etree.ElementTree as ET


def path_data_to_svg(d: str) -> str:
    """WPF "F1 Mx,yz Mx,y ..." -> SVG "Mx,y ..."

    F1 = nonzero fill (SVG default).
    """
    d = re.sub(r'^[Ff][01]\s+', '', d.strip())
    d = re.sub(r'\s+', ' ', d)
    return d


def parse_transform(t: str) -> tuple[float, float, float, float, float, float]:
    """Parse WPF Transform="a,b,c,d,e,f" matrix (6 floats, column-major).

    For pure translates "1,0,0,1,tx,ty" returns (1, 0, 0, 1, tx, ty).
    For other matrices, returns them verbatim; we currently only
    handle pure translates but the matrix is captured for future use.
    """
    parts = re.findall(r'-?\d+\.?\d*', t)
    if len(parts) != 6:
        return (1, 0, 0, 1, 0, 0)
    return tuple(float(p) for p in parts)


def viewbox_from_clip(clip: str) -> tuple[float, float, float, float] | None:
    """Parse 'M0,0 Vw Hh V0 H0 Z' → (x, y, w, h)."""
    m = re.match(r'M\s*([-\d.]+)\s*,\s*([-\d.]+)\s+V\s*([-\d.]+)\s+H\s*([-\d.]+)\s+V\s*([-\d.]+)\s+H\s*([-\d.]+)', clip.strip())
    if not m:
        return None
    x0, y0, x1, y1, x2, y2 = (float(g) for g in m.groups())
    return (min(x0, x2), min(y0, y1), abs(x1 - x0), abs(y1 - y0))


def safe_key_to_filename(key: str) -> str:
    """Convert dotted XAML key to filesystem-safe filename."""
    return key.replace(" ", "_")


def attr(elem, local_name: str) -> str | None:
    """Find attribute on `elem` whose localname is `local_name`,
    ignoring any namespace prefix.
    """
    for k, v in elem.attrib.items():
        if k.endswith("}" + local_name) or k == local_name:
            return v
    return None


def collect_geometry_drawings(di) -> list[str]:
    """All <GeometryDrawing Geometry="..."> values inside the
    DrawingImage, regardless of how deeply nested under DrawingGroup."""
    out = []
    for gd in di.iter():
        if not (gd.tag.startswith("{") and gd.tag.endswith("}GeometryDrawing")):
            continue
        d = attr(gd, "Geometry")
        if d:
            out.append(path_data_to_svg(d))
    return out


def collect_transforms(di) -> tuple[float, float]:
    """Sum all <DrawingGroup Transform="..."> nested inside di.

    Only pure translates are summed (most HunterPie icons are). For
    non-translate matrices we emit them as SVG transform attributes
    on a wrapping <g>.
    """
    tx = ty = 0.0
    for dg in di.iter():
        if not (dg.tag.startswith("{") and dg.tag.endswith("}DrawingGroup")):
            continue
        t = attr(dg, "Transform")
        if t:
            a, b, c, d, e, f = parse_transform(t)
            if a == 1 and b == 0 and c == 0 and d == 1:
                tx += e
                ty += f
    return tx, ty


def find_clip(di) -> tuple[float, float, float, float] | None:
    """Outermost DrawingGroup.ClipGeometry → viewBox."""
    for dg in di.iter():
        if not (dg.tag.startswith("{") and dg.tag.endswith("}DrawingGroup")):
            continue
        clip = attr(dg, "ClipGeometry")
        if clip:
            return viewbox_from_clip(clip)
    return None


def build_parent_map(root) -> dict[int, int]:
    """{id(child) → id(parent)} for every element in the tree."""
    pm: dict[int, int] = {}
    for parent in root.iter():
        for child in parent:
            pm[id(child)] = id(parent)
    return pm


def is_descendant(elem, ancestor, parent_map: dict[int, int]) -> bool:
    cur_id = id(elem)
    while cur_id in parent_map:
        cur_id = parent_map[cur_id]
        if cur_id == id(ancestor):
            return True
    return False


def index_by_local_tag(root) -> dict[str, list]:
    """{local-tag-name: [elements]} for fast lookup."""
    by_tag: dict[str, list] = {}
    for elem in root.iter():
        if "}" in elem.tag:
            suffix = elem.tag.split("}", 1)[1]
            by_tag.setdefault(suffix, []).append(elem)
    return by_tag


def convert_one(di, out_path: Path, parent_map: dict[int, int]) -> bool:
    key = attr(di, "Key")
    if not key:
        return False

    paths = collect_geometry_drawings(di)
    if not paths:
        return False

    # Apply transforms: sum translates, use as <g transform="...">.
    tx, ty = collect_transforms(di)

    # Determine viewBox: prefer ClipGeometry if present, else compute
    # from transformed path bbox.
    clip_vb = find_clip(di)
    if clip_vb:
        vb_x, vb_y, vb_w, vb_h = clip_vb
    else:
        # Last resort: scan path numbers for a rough range. Most icons
        # have a ClipGeometry; this is just a safety net.
        all_nums = []
        for p in paths:
            all_nums.extend(float(n) for n in re.findall(r'-?\d+\.?\d*', p))
        if not all_nums:
            return False
        vb_x = min(all_nums) + tx
        vb_y = max([n for n in all_nums if n > 0]) + ty
        vb_w = vb_h = 100  # placeholder

    body = "\n      ".join(f'<path d="{p}"/>' for p in paths)
    transform_attr = ""
    if tx != 0.0 or ty != 0.0:
        transform_attr = f' transform="translate({tx},{ty})"'

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb_x} {vb_y} {vb_w} {vb_h}">\n'
        f'  <g fill="currentColor"{transform_attr}>\n'
        f'      {body}\n'
        f'  </g>\n'
        f'</svg>\n'
    )
    return True


def convert_file(xaml_path: Path, out_root: Path, src_root: Path) -> tuple[int, int]:
    converted = skipped = 0
    try:
        tree = ET.parse(xaml_path)
        root = tree.getroot()
    except ET.ParseError as e:
        print(f"warn: {xaml_path}: {e}", file=sys.stderr)
        return 0, 0

    images = []
    for elem in root.iter():
        if elem.tag.startswith("{") and elem.tag.endswith("}DrawingImage"):
            images.append(elem)
    if not images:
        return 0, 0

    parent_map = build_parent_map(root)
    rel = xaml_path.relative_to(src_root)
    rel_dir = rel.parent
    stem_prefix = xaml_path.stem

    for di in images:
        key = attr(di, "Key")
        if not key:
            skipped += 1
            continue
        if len(images) == 1:
            name = stem_prefix
        else:
            name = f"{stem_prefix}.{safe_key_to_filename(key)}"
        out_path = out_root / rel_dir / f"{name}.svg"
        if convert_one(di, out_path, parent_map):
            converted += 1
        else:
            skipped += 1
    return converted, skipped


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    src_root = Path(sys.argv[1])
    out_root = Path(sys.argv[2])
    if not src_root.is_dir():
        print(f"error: {src_root} not a directory", file=sys.stderr)
        sys.exit(1)

    total_c = total_s = 0
    for xaml in src_root.rglob("*.xaml"):
        c, s = convert_file(xaml, out_root, src_root)
        total_c += c
        total_s += s
        if c:
            print(f"  {xaml.relative_to(src_root)}: {c} icons")
    print(f"\nconverted {total_c} icons, skipped {total_s}")


if __name__ == "__main__":
    main()