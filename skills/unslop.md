# unslop

Rules for everything an agent writes here: code, comments, reports, commits, UI
copy, and the final message. Derived from the public unslop skill (MIT, Lauren Tan /
Cursor plugins), rewritten for this project. Read the originals if you want the full
rationale.

## Words that get cut

delve, leverage, harness, foster, unpack, navigate (as a metaphor), underscore,
bolster, empower, seamless, robust, scalable, cutting-edge, best-in-class, elevate,
unlock, supercharge, revolutionize, holistic, synergy, world-class, next-generation,
blazing fast, powerful, crucial, pivotal, transformative, game-changer, tapestry,
testament, nuanced, multifaceted.

Also cut: "it is worth noting", "in summary", "at the end of the day", "moving
forward", "arguably", "essentially", "importantly", "of course", "indeed".

If a sentence works without the word, the word was decoration.

## Shapes that get cut

- "Not just X, but Y." Say Y.
- Three-item lists used as rhythm rather than content.
- A trailing `-ing` clause that adds no fact: "..., ensuring quality."
- "Experts believe", "studies show", "many teams". Name the source or drop it.
- A synonym for the subject every time it appears. Pick one noun and keep it.
- Openers that restate the request. Start with the answer.
- Closers that summarise what the reader just read.

## Code

- No `utils.py`, `helpers.ts`, `common.cpp` dumping grounds. A function lives next
  to its caller or in a module named for what it does.
- No abstraction with one implementation. Inline it.
- No wrapper that only forwards arguments.
- No catch-all `try/catch` that logs and continues. Handle, or let it fail.
- No comments that restate the line. Comments explain why, or a measurement, or a
  constraint.
- No test that only asserts a mock was called. Assert output.
- No flag whose only value is the one you use today.

## UI copy

- Labels are nouns: "Captures", "File d'attente", "Providers".
- Buttons are verbs: "Analyser", "Lancer l'agent", "Tester".
- Errors say what failed and what to do: "curl absent du PATH. Installez curl puis
  relancez." Not "Une erreur est survenue."
- No exclamation marks. No emoji as icons. No "Oops".

## Generated code comments

Keep them factual and tied to a measurement or a decision:

```
// #12 Button 132x36px conf 74% border 1
// radius 8 from the fill profile; the outer stroke is 1px
// TODO(text): glyphs not recognised, 23 runs
```

Do not write `// This function draws the UI` above a function named `Draw`.

## Commits

Subject line, imperative, under 72 characters, no period. Body only when the reason
is not obvious. No "various improvements", no "update files".

## Self-check

Before sending, read your own output once and delete:

- any sentence that could appear unchanged in another project's README,
- any adjective that is not measured,
- any paragraph that restates the one above it.
