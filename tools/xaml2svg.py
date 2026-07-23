#!/usr/bin/env python3
"""
xaml2svg.py — convert HunterPie's WPF XAML icons to SVG.

Handles two layouts:
  1. Single-icon .xaml files (one <DrawingImage> per file) — extracts
     the path and viewBox into one .svg.
  2. Multi-icon .xaml files (many <DrawingImage x:Key="..."> in one
     file) — splits each <DrawingImage> into its own .svg, using
     the Key as the basename.

WPF path data uses the same format as SVG <path d="..."/> — only the
wrapping <svg> element is missing. The script:
  1. Parses the WPF XAML.
  2. For each <DrawingImage x:Key="...">:
     a. Finds the outer ClipGeometry for viewBox.
     b. Collects all <GeometryDrawing> children.
     c. Writes one .svg with fill="currentColor" so the icon can be
        re-tinted at runtime via QSvgRenderer.

Usage:
    python3 tools/xaml2svg.py <HunterPie-Resources-dir> <out-dir>
"""

import re
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

WPF_NS = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
NS = {"w": WPF_NS}


def path_data_to_svg(d: str) -> str:
    """WPF "F1 Mx,yz Mx,y ..." -> SVG "Mx,y ..."

    The "F1" prefix is a fill-rule (F1 = nonzero, the SVG default).
    """
    d = re.sub(r'^[Ff][01]\s+', '', d.strip())
    d = re.sub(r'\s+', ' ', d)
    return d


def viewbox_from_clip(clip: str, fallback: str = "0 0 68 68") -> str:
    m = re.match(r'M0,0\s+V([\d.]+)\s+H([\d.]+)\s+V0\s+H0', clip.strip())
    if m:
        return f"0 0 {m.group(1)} {m.group(2)}"
    return fallback


def safe_key_to_filename(key: str) -> str:
    """Convert 'Icons.Moon.FirstQuarter' -> 'Icons.Moon.FirstQuarter' (no path
    separators, allowed by filesystem).
    HunterPie's keys are dotted but contain no slashes."""
    return key.replace(" ", "_")


def key_for(di) -> str | None:
    """Extract the x:Key attribute from a DrawingImage.

    ElementTree stores attribute names with their full Clark notation
    `{namespace}localname`. The WPF XAML namespace is
    `http://schemas.microsoft.com/winfx/2006/xaml` (no `presentation`
    suffix). We probe the element's actual attributes.
    """
    for k, v in di.attrib.items():
        if k.endswith("}Key"):
            return v
    return None


def paths_from_drawing_image(di, all_elements_by_tag: dict, parent_map: dict[int, int]) -> list[str]:
    """Recursively collect <GeometryDrawing> path data within a
    single <DrawingImage> element."""
    out = []
    for gd in all_elements_by_tag.get("GeometryDrawing", []):
        if not is_descendant(gd, di, parent_map):
            continue
        d = None
        for k, v in gd.attrib.items():
            if k.endswith("}Geometry"):
                d = v
                break
        if d is None:
            d = gd.get("Geometry", "")
        if d:
            out.append(path_data_to_svg(d))
    return out


def build_parent_map(root) -> dict[int, int]:
    """Build {id(child) -> id(parent)} for every element in the tree."""
    pm: dict[int, int] = {}
    for parent in root.iter():
        for child in parent:
            pm[id(child)] = id(parent)
    return pm


def is_descendant(elem, ancestor, parent_map: dict[int, int]) -> bool:
    """True if `elem` is in `ancestor`'s subtree.

    Requires a pre-built parent_map from `build_parent_map(root)`.
    """
    cur_id = id(elem)
    while cur_id in parent_map:
        cur_id = parent_map[cur_id]
        if cur_id == id(ancestor):
            return True
    return False


def index_elements_by_tag(root) -> dict:
    """Build {tag-suffix: [elements]} index for the whole tree."""
    by_tag: dict[str, list] = {}
    for elem in root.iter():
        if "}" in elem.tag:
            suffix = elem.tag.split("}", 1)[1]
            by_tag.setdefault(suffix, []).append(elem)
    return by_tag


def viewbox_for(di, all_drawing_groups: list, parent_map: dict[int, int]) -> str:
    """Find outermost ClipGeometry in the DrawingImage."""
    for dg in all_drawing_groups:
        if not is_descendant(dg, di, parent_map):
            continue
        for k, v in dg.attrib.items():
            if k.endswith("}ClipGeometry"):
                return viewbox_from_clip(v)
    return "0 0 68 68"


def emit_svg(viewbox: str, paths: list[str]) -> str:
    body = "\n    ".join(f'<path d="{p}"/>' for p in paths)
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{viewbox}">\n'
        f'  <g fill="currentColor">\n'
        f'    {body}\n'
        f'  </g>\n'
        f'</svg>\n'
    )


def convert_one(di, out_path: Path, all_elements_by_tag: dict,
               all_drawing_groups: list, parent_map: dict[int, int]) -> bool:
    key = key_for(di)
    if not key:
        return False
    paths = paths_from_drawing_image(di, all_elements_by_tag, parent_map)
    if not paths:
        return False
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(emit_svg(viewbox_for(di, all_drawing_groups, parent_map), paths))
    return True


def convert_file(xaml_path: Path, out_root: Path, src_root: Path) -> tuple[int, int]:
    """Returns (converted_count, skipped_count) for this file."""
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

    all_elements_by_tag = index_elements_by_tag(root)
    all_drawing_groups = all_elements_by_tag.get("DrawingGroup", [])
    parent_map = build_parent_map(root)

    rel = xaml_path.relative_to(src_root)
    rel_dir = rel.parent
    stem_prefix = xaml_path.stem

    for di in images:
        key = key_for(di)
        if not key:
            skipped += 1
            continue
        # If file has only one DrawingImage, name = file's stem
        # If multiple, name = "{stem}.{key}"
        if len(images) == 1:
            name = stem_prefix
        else:
            name = f"{stem_prefix}.{safe_key_to_filename(key)}"
        out_path = out_root / rel_dir / f"{name}.svg"
        if convert_one(di, out_path, all_elements_by_tag, all_drawing_groups, parent_map):
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