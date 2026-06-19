# Hyprland Liquid Glass

LiquidGlass is a Hyprland plugin that renders a per-window liquid glass material. It samples the framebuffer behind each window, applies a small blur, then uses a ShojiWM-style rounded SDF shader with edge-focused radial distortion, visible color dispersion, and a simple multiplicative tint.

The effect is rendered as a window decoration under normal client surfaces and as a render-pass element under selected layer-shell surfaces. The plugin also lowers managed window alpha slightly so normal opaque apps can show the material without requiring app-specific transparency.

![LiquidGlass preview](assets/preview.png)

## Enhancements (this fork)

This is a fork of [0xdilo/hyprland-liquid-glass](https://github.com/0xdilo/hyprland-liquid-glass) with added features on top of the original plugin. All credit for the base plugin and the Shoji-style glass material goes to the original author (see the rest of this README and the License section). This section documents everything this fork adds on top of upstream:

- **Hyprland 0.55.x compatibility** — updated the render-pass element access to the 0.55.x API (`m_passElements` entries are accessed by value, not through a pointer) so the plugin builds and loads on current Hyprland. Tested on **0.55.4**. No config; it's purely a build/runtime fix. Plugins are ABI-sensitive, so still rebuild against your running Hyprland headers.
- **Per-surface distortion scaling** — independently scale the glass distortion (refraction + chromatic aberration + lensing) per window vs. per layer-shell surface, including per-namespace overrides. See below.
- **Cursor-following refraction + highlight** — a soft lens that tracks the mouse pointer across glassed surfaces. See below.

### Per-surface distortion scaling

Upstream applies the same distortion strength everywhere. This fork adds multipliers that scale only the **refraction**, **chromatic aberration**, and **lens distortion** of a surface (the blur, tint, and opacity are untouched), so you can keep windows strongly refractive while toning the effect down on a bar or notifications.

- `window_distortion_scale` multiplies distortion for all managed windows.
- `layer_distortion_scale` is the default multiplier for matched layer-shell surfaces.
- `layer_distortion_overrides` sets per-namespace multipliers that win over `layer_distortion_scale`, as a comma-separated `namespace:scale` list (case-insensitive namespace match).

```ini
plugin:liquidglass {
    window_distortion_scale    = 1.0   # windows at full strength
    layer_distortion_scale     = 1.0   # default for matched layers
    # Tone specific layer namespaces down (or up). Overrides the default above.
    layer_distortion_overrides = waybar:0.1,notifications:0.4,wofi:0.4
}
```

| Option | Default | Notes |
| --- | ---: | --- |
| `window_distortion_scale` | `1.0` | Distortion multiplier for managed windows. Clamped to `0.0`–`4.0`. `0` disables distortion (keeps blur/tint). |
| `layer_distortion_scale` | `1.0` | Default distortion multiplier for matched layer-shell surfaces. Clamped to `0.0`–`4.0`. |
| `layer_distortion_overrides` | empty | Per-namespace multipliers as `namespace:scale` pairs, comma-separated (e.g. `waybar:0.1,notifications:0.4`). A match overrides `layer_distortion_scale`; each value is clamped to `0.0`–`4.0`. |

### Cursor-following refraction + highlight

A soft "lens" that tracks the mouse pointer across every glassed surface (windows and matched layer-shell surfaces like a bar, launcher, or notifications). It layers two effects centred on the cursor:

- **Refraction** — the glass gently bends/magnifies under the pointer using a smooth dome falloff (no hard ring).
- **Highlight** — an additive tinted glow centred on the pointer.

To keep it tracking smoothly, the plugin listens for pointer motion and damages a cursor-sized region each move, so only the area around the pointer repaints (and only while the mouse is actually moving). The effect is independent of `layer_distortion_overrides`, so it shows at full strength even on surfaces whose base distortion is dialed down.

#### Config

All options live under `plugin:liquidglass:` alongside the originals.

```ini
plugin:liquidglass {
    cursor_enabled    = 1           # 0 disables the whole effect
    cursor_radius     = 220.0       # lens size, in logical pixels
    cursor_intensity  = 0.5         # highlight glow strength (0 = no glow)
    cursor_refraction = 0.6         # how hard the glass bends under the cursor (0 = highlight only)
    cursor_color      = 0xffffff66  # highlight tint, RRGGBBAA (alpha scales the glow)
}
```

| Option | Default | Notes |
| --- | ---: | --- |
| `cursor_enabled` | `1` | Master toggle for the cursor effect. `0` disables both the refraction and the highlight. |
| `cursor_radius` | `220.0` | Radius of the lens influence, in logical pixels. Scales with monitor scale automatically. |
| `cursor_intensity` | `0.5` | Strength of the additive highlight glow. `0` removes the glow and leaves only refraction. |
| `cursor_refraction` | `0.6` | Strength of the lens distortion under the cursor. `0` removes the bend and leaves only the highlight. |
| `cursor_color` | `0xffffff66` | Highlight tint as `RRGGBBAA`. The alpha byte scales the glow on top of `cursor_intensity`. |

These values are read live every frame, so changing them only needs `hyprctl reload` — no rebuild. A rebuild (`make`) is only required when changing the C++/shader source itself.

Notes:
- The cursor-to-surface coordinate mapping assumes a normal (non-rotated/non-flipped) monitor transform.
- Some starting points: cool-blue glow `cursor_color = 0x7fb8ffaa`; subtle `cursor_intensity = 0.25, cursor_refraction = 0.3`; chunky magnifier `cursor_radius = 320, cursor_refraction = 1.0`.

## Requirements

- Hyprland with plugin support
- Hyprland headers matching the running compositor
- `pkg-config`
- C++23 compiler
- `make`

Hyprland plugins are ABI-sensitive. Rebuild this plugin whenever Hyprland updates.

## Install With Hyprpm

```bash
hyprpm add https://github.com/0xdilo/hyprland-liquid-glass
hyprpm enable liquidglass
hyprpm reload
```

## Manual Build

```bash
make
hyprctl plugin load "$PWD/liquidglass.so"
```

For a persistent manual load, add the built plugin path to `hyprland.conf`:

```ini
plugin = /absolute/path/to/liquidglass.so
```

## Config

All options live under `plugin:liquidglass:` in `hyprland.conf`.

```ini
plugin:liquidglass {
    enabled = 1

    # Optional. No applications are excluded by default.
    # Use this for media players, browsers, or apps that should stay fully opaque.
    exclude_classes = mpv,helium
    layer_namespaces = quickshell

    window_opacity = 0.90
    layer_opacity = 1.0
    layer_corner_radius = 12
    glass_opacity = 0.78

    blur_strength = 0.32
    blur_iterations = 2

    refraction_strength = 1.15
    chromatic_aberration = 0.90
    lens_distortion = 1.15

    fresnel_strength = 0.46
    specular_strength = 0.38
    edge_thickness = 0.040

    # RRGGBBAA
    tint_color = 0xb8d8ff00

    brightness = 0.88
    contrast = 1.16
    saturation = 1.14
    vibrancy = 0.32
    adaptive_dim = 0.32
    adaptive_boost = 0.10
}
```

For a stronger, more visible setup, use the checked-in [evident preset](presets/evident.conf). It keeps the stable compositor path and makes the material easier to see with thicker refractive edges, stronger lensing, brighter specular highlights, and slightly more managed-window transparency.

The default values are calibrated to ShojiWM's liquid glass shader: `blur_strength = 0.32` maps to roughly a radius-2 blur, `edge_thickness = 0.040` maps to a distortion depth near `0.2`, `refraction_strength` and `lens_distortion` combine to a distortion strength near `0.15`, `chromatic_aberration = 0.90` maps to about a 3 px color-channel split, and `brightness = 0.88` maps to ShojiWM's `glass_tint = 0.9`.

## Layer Shell Surfaces

Layer-shell clients such as Quickshell bars, launchers, notifications, and panels are not normal windows, so LiquidGlass renders them through a separate layer pass. Only namespaces listed in `layer_namespaces` receive the material. The default is `quickshell`.

Fullscreen layer-shell overlays are skipped by the plugin pass. Many launchers use one transparent fullscreen layer and draw the visible panel inside it; rendering a compositor-sized LiquidGlass rectangle behind that layer causes flashes or glass in empty screen areas. Keep Hyprland layer blur enabled for those overlays and make the actual QML panel background translucent.

Keep the layer's own background transparent enough for the backing material to show. You can keep the layer rules below; LiquidGlass disables Hyprland's layer blur only for matched non-fullscreen layers that receive its sampled blur:

```ini
layerrule = match:namespace quickshell, blur on
layerrule = match:namespace quickshell, blur_popups on
layerrule = match:namespace quickshell, ignore_alpha 0.08
layerrule = match:namespace quickshell, xray 0
```

Set the panel background alpha low enough for the compositor material to show through. For example, the current Quickshell setup uses panel alpha around `0.62` for primary surfaces and `0.52` for softer surfaces. Keep `xray` off if you want the sampled material to use other windows behind the layer instead of mostly wallpaper.

### Options

| Option | Default | Notes |
| --- | ---: | --- |
| `enabled` | `1` | Enables or disables the plugin effect. |
| `exclude_classes` | empty | Comma-separated window classes to leave untouched. Matching is case-insensitive and checks current and initial class. |
| `layer_namespaces` | `quickshell` | Comma-separated layer-shell namespaces that should receive the material. Matching is case-insensitive. |
| `window_opacity` | `0.90` | Alpha applied to managed window surfaces so the glass backing can show through. Set to `1.0` to avoid forcing opacity. |
| `layer_opacity` | `1.0` | Alpha applied to matched layer-shell surfaces. Lower this only if a layer is fully opaque and you want the backing material to show through. |
| `layer_corner_radius` | `12` | Corner radius used for matched layer-shell glass rectangles, in layout pixels. |
| `glass_opacity` | `0.78` | Strength of the rendered glass backing. This value is calibrated as the Shoji-style full-strength pass. |
| `blur_strength` | `0.32` | Radius multiplier for the sampled background blur. The default maps to roughly radius 2. |
| `blur_iterations` | `2` | Number of horizontal/vertical blur passes. Higher values cost more GPU time. |
| `refraction_strength` | `1.15` | Multiplier for Shoji-style edge distortion. |
| `chromatic_aberration` | `0.90` | Multiplier for the Shoji-style 3 px color-channel split. |
| `lens_distortion` | `1.15` | Multiplier for Shoji-style radial edge lensing. |
| `fresnel_strength` | `0.46` | Compatibility option kept for older configs; the Shoji-style shader does not add a separate rim glow. |
| `specular_strength` | `0.38` | Compatibility option kept for older configs; the Shoji-style shader does not add a separate specular highlight. |
| `edge_thickness` | `0.040` | Distortion falloff multiplier. The default maps to ShojiWM's `distortion_depth = 0.2`. |
| `tint_color` | `0xb8d8ff00` | Glass tint as `RRGGBBAA`. Alpha `00` disables tint. |
| `brightness` | `0.88` | Multiplicative glass tint. The default maps to ShojiWM's `glass_tint = 0.9`. |
| `contrast` | `1.16` | Compatibility option kept for older configs; the Shoji-style shader keeps contrast unchanged. |
| `saturation` | `1.14` | Compatibility option kept for older configs; the Shoji-style shader keeps saturation unchanged. |
| `vibrancy` | `0.32` | Compatibility option kept for older configs; the Shoji-style shader does not add extra vibrancy. |
| `adaptive_dim` | `0.32` | Compatibility option kept for older configs; the Shoji-style shader does not add adaptive dimming. |
| `adaptive_boost` | `0.10` | Compatibility option kept for older configs; the Shoji-style shader does not add adaptive boosting. |

## Performance Notes

LiquidGlass renders only around windows that use the material. It samples a padded region behind each affected window, blurs that sample in an offscreen framebuffer, and composites the final glass pass back into the current render pass.

For lower GPU cost, reduce `blur_iterations`, `blur_strength`, or `glass_opacity`. For a subtler effect on opaque apps, increase `window_opacity`.

## Development

```bash
make clean
make
hyprctl plugin unload "$PWD/liquidglass.so"
hyprctl plugin load "$PWD/liquidglass.so"
```

Check Hyprland config errors after changing options:

```bash
hyprctl configerrors
```

## License

MIT
