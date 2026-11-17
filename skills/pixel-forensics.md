# Pixel forensics

Every number in `report.json` was measured on the pixels. Use it, and know when it
lies.

## Pipeline

1. Decode to RGBA. Composite transparency on white.
2. Quantise to at most 24 colours with median cut in CIELAB, then two k-means passes.
3. Label every pixel with its nearest palette entry, cached per RGB value.
4. Connected components per label, eight-way, with run-length union-find.
5. Merge glyph-sized components that share a baseline into text runs. A lone small
   component is an icon candidate, not a letter.
6. Rectangles come from surface components. The fill colour is the component colour,
   not a sample of the interior, because interiors contain children.
7. Borders are searched outward: a uniform band immediately outside the fill that
   differs from both the fill and the parent surface grows the rectangle.
8. Radius is fitted per corner against the circle profile, then the median of the
   corners wins. Disagreeing corners fall back to the smallest.
9. Gradient: row medians must move monotonically and the top and bottom stops must
   differ by more than deltaE 3.5.
10. Shadow: the halo must be darker than the surrounding surface. A lighter halo is
    a border or a raised card, not a shadow.
11. Device pixel ratio: every measured stroke is even, at least one is 2px, the image
    is at least 1200px wide. Then the capture is treated as 2x.
12. Fidelity: the tree is re-rendered with a software rasteriser and compared to the
    source with SSIM and MAE. Glyph pixels are carried over from the source so the
    score measures geometry and colour, not OCR.

## Reading the report

- `confidence` per element is structural, not a probability. 55 means "this came
  from a solid component with no special evidence". Text confidence comes from glyph
  count and family separation.
- `fidelity` above 95 means the geometry is essentially exact. 70 to 95 means some
  regions were not modelled. Below 70 means large areas are missing.
- `typography` is the mean confidence of the font estimates, weighted by text area.
  It is not a rendered comparison.
- `device_pixel_ratio` is an inference. If the screenshot is a 2x capture scaled
  down, the estimate can be wrong. The emitter divides by it; verify one border
  width by hand before trusting the whole file.

## Known failure modes

- Photographs and charts become hundreds of components. The report will be noisy.
- Glassmorphism and blur break colour quantisation: fills become several bands.
- A surface and its parent with near-identical colours can merge.
- Same-colour siblings that touch cannot be separated without edges. The detector
  keeps them as one element.
- Text over images produces unstable glyph runs.
- Gradients with more than two stops are reduced to two. Say so in a comment.
- Anti-aliased 1px borders on dark themes are close to noise. The tolerance is
  adaptive, but a border can be missed.

## When to override a measurement

Only with evidence in the image. Examples:

- A button measured at 39px height inside a 40px row: keep 40 and comment the row.
- A radius measured at 11 next to a 12px design system: keep 12 only if at least two
  corners agree on 12.
- A colour measured at `#1A1D24` and reused by six panels: promote it to a palette
  constant. Do not invent a seventh shade.

## Honesty

If the screenshot shows a feature the tree does not model, draw it with a `TODO`
comment. Do not claim fidelity the diff does not support. The diff image is the
ground truth and the user can look at it.
