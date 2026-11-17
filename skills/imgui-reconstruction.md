# ImGui reconstruction

The generated file has two targets. Keep both working.

- `Draw(ImDrawList* dl, ImVec2 o, float time)`: pixel target. Absolute coordinates,
  no widgets, no hidden state. This is what gets compared to the screenshot.
- `Build()`: semantic target. Stock ImGui widgets placed with
  `SetCursorScreenPos`. Editable, interactive, less exact.

## Coordinates

`o` is the origin passed by the host. Every measured value is already scaled to
logical pixels. Write `ImVec2(o.x + X, o.y + Y)` and nothing else.

Use integer values where the measurement is an integer. `34.0f` is correct,
`34.37f` is a measurement you invented.

## Mapping

| Measured | ImGui |
| --- | --- |
| Panel, card | `AddRectFilled` + optional `AddRect`, radius clamped to `min(w,h)/2` |
| Vertical gradient | `AddRectFilledMultiColor`, same colour on both corners of each edge |
| Button | `Button` with `PushStyleColor(ImGuiCol_Button/Hovered/Active)` and `FrameRounding` |
| Input | `InputText` with a static buffer, `SetNextItemWidth`, then placeholder text |
| Toggle | `Checkbox`; keep the measured pill as a draw-list background if the theme is custom |
| Icon | the generated `Icon(dl, name, center, size, color, thickness)` helper |
| Label | `AddText(ImGui::GetFont(), size, pos, color, text)` |
| Divider | `AddLine` or a 1px `AddRectFilled` |
| Image | `AddImage` when a texture exists, otherwise a neutral placeholder rect |

`Build()` still draws fills and borders manually with the draw list, then places the
widget on top. This keeps the measured look while the widget stays interactive.

## Style stack discipline

ImGui style stacks are per window and must be balanced on every path. Prefer one
push/pop pair around a single call over a long unbalanced block. When you push three
colours, pop three. A stray `PushStyleVar` corrupts every window drawn afterwards.

## Typography

`AddText(font, size_px, ...)` accepts a size independent of the font atlas, which is
what makes exact typography possible. Use the measured size directly.

If a font is missing, `ImGui::GetFont()` returns the default atlas font. Never pass
nullptr to `AddText`. Never assume a glyph exists outside Latin-1 unless the emitted
`GlyphRanges` says so.

## Icons

The helper draws vector primitives on the draw list. Stroke width comes from the
measurement, usually 1 or 1.5 logical pixels. Keep the centre at the measured
element centre, and the size at the smaller side of the box.

When the match confidence is low, keep the generic square. A wrong icon is worse
than an honest placeholder.

## Pixel rules

- A 1px border is `AddRect(p0, p1, col, radius, 0, 1.0f)`. Do not use 1.5 for a 1px
  measurement.
- Anti-aliasing is on by default. Do not disable it.
- Clip children to the parent when the screenshot shows clipping
  (`PushClipRect` / `PopClipRect`).
- Shadows are approximate: one rounded rect with low alpha behind the surface,
  offset by the measured `offset_y`, spread by the measured `spread`. Never fake a
  shadow with a border.
- DPI: if `device_pixel_ratio` is 2, the host window is in logical pixels. Do not
  scale by 2 again.

## What not to write

- No `ImGui::Begin` inside `Draw`.
- No global mutable state except the static buffers `Build` needs for inputs.
- No `using namespace ImGui` in the generated file.
- No `#include "imgui_internal.h"`. The public API is enough.
- No dynamic allocation per frame in `Draw`.
