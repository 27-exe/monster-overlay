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

**We do**: SVG icons, since Qt 6 has first-class SVG support via
`QSvgRenderer` and Qt Resource System (`<file>` in .qrc).

**We don't**: ship actual icon files. Reasons:

1. **HunterPie's icons are licensed separately** from the code
   (Apache-2.0 for code, but assets carry their own terms). Copying
   them into our binary is a license problem.
2. **MHW's own art is copyrighted** by Capcom. We can't pull icons out
   of the game binary. HunterPie doesn't either — its `qurio_mask`
   is community-drawn.
3. **Drawing them by hand is cheap** for the small set v0.2 needs:
   a handful of generic status icons (enrage, mantle, ailment).
   Custom art per monster is out of scope.

This means the asset story is:

- **v0.2 ships a default skin** with hand-drawn SVG placeholders
  (e.g. `<circle>` and `<rect>` glyphs for "enrage", "stamina", etc.).
- **Users can override** by dropping SVGs into `assets/icons/` and
  adding `<file alias="...">` entries to a v0.2-shipped
  `assets/icons.qrc`.
- The runtime path is fixed (`:/icons/...`); only the bundle
  contents change.

## SVG / QRC pipeline (v0.2)

### `assets/icons/` structure

```
assets/
├── icons.qrc                  # v0.2: bundle manifest
├── icons/
│   ├── enrage.svg             # 🔥-style flame icon
│   ├── stamina.svg            # ⚡-style bolt
│   ├── mantle.svg             # generic mantle shape
│   ├── ailment_sleep.svg      # Zzz
│   ├── ailment_paralysis.svg  # lightning
│   ├── ailment_stun.svg       # stars
│   ├── ailment_drool.svg      # droplet
│   ├── part_flinch.svg        # impact mark
│   ├── part_breakable.svg     # crack
│   └── part_severable.svg     # scissors
└── charts/
    └── dps_grid.svg           # v0.2: DPS chart background grid
```

### `assets/icons.qrc`

```xml
<!DOCTYPE RCC>
<RCC version="1.0">
    <qresource prefix="/icons">
        <file alias="enrage.svg">icons/enrage.svg</file>
        <file alias="stamina.svg">icons/stamina.svg</file>
        <file alias="mantle.svg">icons/mantle.svg</file>
        <file alias="ailment_sleep.svg">icons/ailment_sleep.svg</file>
        <file alias="ailment_paralysis.svg">icons/ailment_paralysis.svg</file>
        <file alias="ailment_stun.svg">icons/ailment_stun.svg</file>
        <file alias="ailment_drool.svg">icons/ailment_drool.svg</file>
        <file alias="part_flinch.svg">icons/part_flinch.svg</file>
        <file alias="part_breakable.svg">icons/part_breakable.svg</file>
        <file alias="part_severable.svg">icons/part_severable.svg</file>
    </qresource>
    <qresource prefix="/charts">
        <file alias="dps_grid.svg">charts/dps_grid.svg</file>
    </qresource>
</RCC>
```

### C++ loader (`src/core/icon.h`)

```cpp
class Icon {
public:
    explicit Icon(const QString &qrcPath);   // e.g. ":/icons/enrage.svg"
    QPixmap render(const QSize &size, const QColor &tint = Qt::white) const;
    int naturalWidth() const;
    int naturalHeight() const;
private:
    QSvgRenderer renderer_;
    QByteArray svgBytes_;
    QSize naturalSize_;
};
```

`render()` produces a `QPixmap` of the requested size, optionally
tinted. This lets the overlay resize icons per-panel without
maintaining multiple SVGs.

### Caching

`Icon` instances are cheap to construct (one QSvgRenderer, holds
SVG bytes). They're stored in a `QHash<QString, Icon>` per-panel,
keyed by alias. Don't share icons across panels — each panel
re-tints independently.

## v0.2 usage examples

```cpp
// In a player-status panel
auto staminaIcon = Icon(QStringLiteral(":/icons/stamina.svg"));
auto tinted = staminaIcon.render(QSize(24, 24), QColor("#ffd479"));
staminaLabel_->setPixmap(tinted);
staminaLabel_->setText(mh::tr("ui.player_stamina_format")
                         .arg(stamina).arg(maxStamina));
```

```cpp
// In a monster panel — per-part icons
auto icon = part.isSeverable ? Icon(":/icons/part_severable.svg")
                             : part.isBreakable
                                ? Icon(":/icons/part_breakable.svg")
                                : Icon(":/icons/part_flinch.svg");
partNameLabel_->setPixmap(icon.render(QSize(16, 16)));
```

## Chart asset (DPS, v0.2)

`assets/charts/dps_grid.svg` is the **background grid** for the
DPS sparkline. The data series is drawn on top via `QPainter`
(this is the only place we use raster drawing — SVG only for the
frame).

Layout:
- The SVG provides the gridlines (5 horizontal lines at 0%, 25%,
  50%, 75%, 100% of the panel height, plus tick marks for time).
- The panel reads `DAMAGE_ADDRESS + DAMAGE_OFFSETS[i*0x2A0]` for
  each of the 4 party members, samples the 4 cumulative values at
  1 Hz, computes per-second deltas, and draws a line per member.
- Member colours come from `i18n/zh-CN.json`'s `ui.member_color_*`
  keys (so the user can theme).

## Why no auto-update from game assets

HunterPie doesn't extract assets from the running game either. Its
flow is: developer ships assets, app reads them from the embedded
resource. We follow the same model. The **game's** assets are off
limits, both for license and for engineering reasons (we'd need to
parse `.pak` files inside `/proc/<pid>/mem` to find them, which is
way out of scope for v0.2).

## Asset checklist for v0.2

| File                                  | Source                             | Status  |
|---------------------------------------|------------------------------------|---------|
| `assets/icons.qrc`                    | Generated                          | TODO    |
| `assets/icons/enrage.svg`             | Hand-drawn / placeholder           | TODO    |
| `assets/icons/stamina.svg`            | Hand-drawn / placeholder           | TODO    |
| `assets/icons/mantle.svg`             | Hand-drawn / placeholder           | TODO    |
| `assets/icons/ailment_*.svg` (4)      | Hand-drawn / placeholder           | TODO    |
| `assets/icons/part_*.svg` (3)         | Hand-drawn / placeholder           | TODO    |
| `assets/charts/dps_grid.svg`          | Hand-drawn / placeholder           | TODO    |
| `src/core/icon.h` + `icon.cpp`        | New                                | TODO    |
| `src/ui/dps_chart.{h,cpp}`            | New                                | TODO    |
| `CMakeLists.txt` (link `assets/icons.qrc`) | Update                       | TODO    |

The hand-drawn placeholders can be **really minimal** — a 24×24
circle or a 16×16 square with a label. The point is the pipeline;
the visuals are placeholders.