# Frontend design

Two surfaces are in scope: the reconstruction (must match the screenshot) and the
studio UI (must be usable). The reconstruction defers to measurement. The studio
follows the rules below.

## Grid and spacing

- Base unit 4. Use 8 for layout rhythm, 4 for icon and text gaps, 12 and 16 for
  section padding. Never 7, 9 or 13.
- Page padding 18 to 24. Card padding 14 to 16. Row height 34 to 40.
- One spacing scale per screen. If a panel uses 14, its siblings use 14.

## Type

- UI body 13px, line-height 1.5. Dense tables 11 to 12px with monospace numerals.
- Section labels 10 to 11px, uppercase, letter-spacing 0.06em, muted colour.
- One display size per screen, at most 1.6x the body size. No 48px headline inside
  an application shell.
- Numerals in tables use a monospace stack so columns align.

## Colour

- One neutral ramp: `#0d1015`, `#12161d`, `#161b24`, `#242b36`, `#8b94a6`, `#e7eaf1`.
- One accent. One success, one warning, one error. Nothing else.
- Body text contrast at least 7:1 on its surface. Muted text at least 4.5:1.
- Borders are 1px and one step above the surface. Do not outline everything.
- No purple-to-blue gradient as decoration. Gradients are allowed only when they
  reproduce a measured fill.

## Components

- Buttons: 6px radius, 6x11 padding, one primary per view. Secondary buttons are
  outlined, not filled.
- Inputs: 6px radius, 7x9 padding, visible focus ring in the accent colour, 2px
  offset.
- Cards: 8px radius, 1px border, no drop shadow at rest. Shadow only for overlays.
- Empty states: one sentence, one action. No illustration.
- Progress: a 2px bar is enough. Do not animate a spinner next to a progress bar.

## Motion

- 120 to 180ms for state changes, 200 to 260ms for layout. Ease-out entering,
  ease-in leaving.
- Animate opacity and transform only. Do not animate width, height or colour unless
  the change is the point.
- Respect `prefers-reduced-motion` in the studio, and do not animate the
  reconstruction preview at all.

## Content

- French UI in the studio, English in code, comments and identifiers.
- Labels are nouns. Buttons are verbs. No exclamation marks, no "Oops", no emoji as
  iconography.
- Numbers carry units: `320 ms`, `99.2/100`, `24 px`. A bare `99.2` is ambiguous.

## Anti-patterns

- Hero sections, badge rows, feature grids, testimonial cards: none of these belong
  in a tool.
- Glassmorphism, neon glow, gradient text, 24px rounded everything.
- Icon-only buttons without a tooltip.
- Two competing primary buttons in the same panel.
- Placeholder copy that sounds like marketing.
