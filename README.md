# parsergui

Un studio « screenshot → ImGui » : une capture d'écran entre, du code C++ Dear ImGui
mesuré au pixel près sort, avec la preuve chiffrée que la reconstruction correspond.

Le projet contient un moteur portable (`pgr_core`), un studio web local, un serveur
HTTP avec file de jobs, un bridge vers les agents de code (Claude Code, Codex,
OpenCode), une application desktop Dear ImGui, une application Android et la CI
multi-plateforme.

## Ce que le moteur mesure réellement

Aucune estimation « à l'œil » : chaque valeur du rapport sort d'une mesure sur les
pixels, et la fidélité est obtenue en re-rendant l'arbre puis en le comparant à
l'image source.

| Grandeur | Méthode |
| --- | --- |
| Couleurs | quantification median-cut en CIELAB + k-means, palette triée par surface |
| Zones | composantes connexes (run-length + union-find) sur masques par couleur |
| Texte | fusion des glyphes en lignes, hauteur de capitale → taille de police |
| Bordures | recherche **vers l'extérieur** depuis le rectangle du composant, expansion jusqu'au pic de contraste |
| Rayon | ajustement du profil de coin (fit) avec inset, pas une constante |
| Dégradés | monotonie du profil de couleur sur l'axe principal |
| Ombres | uniquement si un halo plus sombre existe hors du rectangle |
| DPR | épaisseur mesurée des traits fins (1 px logique = 1, 2 ou 3 px physiques) |
| Grille | histogramme des espacements, base 4 ou 8 px, marge médiane |
| Fidélité | re-rendu logiciel anti-aliasé + SSIM / MAE / PSNR / pixels hors seuil |

Résultat sur la scène de test (`tests/test_parser.cpp`, 900×600) :

```
11 elements, 4 panels, 2 buttons, 0 inputs, 0 icons; grid 4px;
fidelity 99/100 (SSIM 0.9918, MAE 0.16, PSNR 39.1 dB, off-pixels 0.15%, fidelity 99.4/100)
rayon mesuré 12 px, bordure détectée, bouton accent classé Button
taille de police estimée 15.49 px, palette exacte
23 checks, 0 failures
```

Le code C++ généré est vérifié par le compilateur à chaque analyse :

```
g++ -std=c++17 -fsyntax-only -I third_party/imgui parsed_ui.cpp   # 0 erreur
```

## Arborescence

```
core/        moteur portable, aucune dépendance GUI
  image      chargement/écriture PNG-JPEG-WebP-BMP-TGA (stb, vendored)
  color      sRGB, CIELAB, deltaE, quantification, palette
  raster     rasterizer anti-aliasé (rect, rounded, cercle, ligne, texte)
  segment    composantes connexes run-length + union-find
  font       métriques de police et templates d'icônes vectorielles (29)
  detect     pipeline de mesure complet
  render     re-rendu + SSIM/MAE/PSNR/diff
  emit       générateur ImGui (DrawList + widgets + icônes) et rapport
  parser     orchestration, artefacts, animation entre frames
server/      serveur HTTP POSIX/Winsock, multipart, file de jobs, API, UI web
agents/      exécution de sous-processus, registre de providers, bridge d'agents
cli/         parse, serve, providers, agent, version
app/         application desktop Dear ImGui + GLFW + OpenGL3
android/     shell Android (JNI + WebView + Gradle)
skills/      contrat d'agent et skills de reconstruction
providers/   catalogue de providers (72 réels, capacité 500)
tests/       test bout en bout, 23 vérifications
tools/       générateur de fixture, inspecteur de détection
third_party/ stb_image, stb_image_write, Dear ImGui 1.91.9b
```

## Build

Prérequis : CMake ≥ 3.16, un compilateur C++17. Le GUI ajoute OpenGL et GLFW
(récupéré par `FetchContent`). Aucun codec externe : stb est vendored.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Binaires : `build/parsergui` (CLI + serveur), `build/parsergui_gui` (desktop),
`build/parsergui_tests`.

Sans GUI (serveur seul, CI légère) :

```bash
cmake -S . -B build -DPARSERGUI_BUILD_GUI=OFF
```

## CLI

```bash
parsergui parse capture.png --out ./out          # analyse + artefacts
parsergui parse t0.png t1.png t2.png --frames    # animation
parsergui serve --port 8787 --data ./parsergui-data
parsergui providers --free                       # catalogue trié
parsergui agent --job ./out --agent claude --budget 150
```

Artefacts écrits par `parse` : `source.png`, `reconstruction.png`, `diff.png`,
`parsed_ui.cpp`, `parsed_ui_main.cpp` (harnais GLFW prêt à compiler),
`report.json`, `report.md`, `layout.txt`, `agent_prompt.md`.

## Studio web

```bash
parsergui serve --port 8787
# http://localhost:8787
```

Glissez un nombre illimité de captures. Par défaut **chaque image devient un job
indépendant** ; cochez « Animations » pour traiter la sélection comme les frames
d'un même écran (transitions mesurées entre frames). L'interface montre la source,
la reconstruction, le diff, le code, le rapport de mesures, la palette et les
animations. Les jobs sont asynchrones avec progression et ETA.

### API

| Méthode | Route | Rôle |
| --- | --- | --- |
| `GET` | `/api/health` | version, uptime, jobs, providers, skills, CLI détectés |
| `POST` | `/api/jobs` | multipart `files` + `options` (`max_colors`, `icons`, `sequence`) |
| `GET` | `/api/jobs` | file de jobs |
| `GET` | `/api/jobs/{id}` | état, rapport, artefact, session agent |
| `GET` | `/api/jobs/{id}/artifact/{name}` | source, reconstruction, diff, code, main, report.json, report.md, layout.txt |
| `GET` | `/api/providers` | catalogue trié, `?free=1`, `?test=<id>` |
| `POST` | `/api/agents/session` | `{job_id, agent, budget}` → session |
| `GET` | `/api/agents/session/{id}` | statut, ETA, journal, éditions |

## Providers et agents

`providers/providers.json` contient 72 providers réels (OpenAI, Anthropic, Google,
Mistral, Groq, Cerebras, OpenRouter, Token Harbor, etc.), triés par gratuité puis
priorité puis clé disponible. 17 sont utilisables sans clé. La capacité est fixée
à 500 ; `providers/user.json` ajoute les vôtres sans toucher au catalogue.

Token Harbor est inclus (`https://tokenharbor.ai/v1`, modèles `:free`) avec
`TOKENHARBOR_API_KEY`. Les clés ne sont jamais écrites sur disque : elles vivent
en mémoire de session, via l'UI ou l'environnement.

Le bridge crée de vraies sessions :

- CLI détectés : `claude -p`, `codex exec`, `opencode run` (sous-processus avec timeout,
  sortie capturée, budget d'éditions) ;
- sinon boucle provider compatible OpenAI/Anthropic avec outils `read_file` /
  `write_file`, budget d'éditions et journal ;
- mode `plan` : construit le prompt complet (mesures + skills + contrat), écrit
  `agent_prompt.md`, ne modifie aucun fichier — utile pour vérifier le bridge sans clé.

```bash
curl -X POST localhost:8787/api/agents/session \
  -H 'Content-Type: application/json' \
  -d '{"job_id":"j_...","agent":"plan","budget":150}'
```

## Application desktop

`parsergui_gui` : dépôt de fichiers, options, analyse, aperçu source/reconstruction/
diff, code ImGui copiable, rapport tabulaire, layout. Raccourcis : `Ctrl+Entrée`
analyse, `Ctrl+O` ajoute un chemin. La fenêtre s'ouvre en 1440×900, thème sombre
dense, sans dépendance à un serveur.

## Android

`android/` construit un APK (arm64-v8a + x86_64) : WebView + JNI vers `pgr_core`.
La capture choisie est copiée dans le cache, analysée sur l'appareil, et l'UI
affiche mesures, reconstruction, diff et code.

```bash
cd android && gradle assembleDebug
# app/build/outputs/apk/debug/app-debug.apk
```

## CI

`.github/workflows/build.yml` :

| Job | Runner | Cible |
| --- | --- | --- |
| desktop-linux-x64 | ubuntu-22.04 | CLI, serveur, GUI, tests |
| desktop-windows-x64 | windows-2022 | `-A x64` |
| desktop-windows-x86 | windows-2022 | `-A Win32` |
| desktop-macos-arm64 | macos-15 | Apple Silicon |
| desktop-macos-x64 | macos-15-intel | Intel |
| android-arm64-x86_64 | ubuntu-22.04 | `gradle assembleDebug` + NDK 26 |

Chaque job desktop exécute `ctest`, un smoke test du binaire et publie `dist/`
en artefact.

## Skills

`skills/` contient le contrat d'agent et les skills transmis aux modèles :
`agent-contract.md`, `imgui-reconstruction.md`, `pixel-forensics.md`,
`frontend-design.md`, `unslop.md`, plus `manifest.json`. Ils décrivent les règles
de mesure, le mapping vers ImGui, l'anti-slop (pas de gradients décoratifs, pas
d'emojis, pas de copy creuse) et la procédure de vérification.

## Limites honnêtes

- **Pas d'OCR.** Les textes non reconnus restent des placeholders `// TODO(text)`
  mesurés (position, taille, couleur) ; l'agent vision peut les remplir.
- Les captures très bruitées (photo d'écran, JPEG agressif) dégradent la
  segmentation ; le rapport expose alors les avertissements et la fidélité baisse.
- L'application desktop a été compilée en vérification de syntaxe mais **n'a pas
  été lancée ici** (pas de serveur X) ; la CI la construit sur les cinq cibles.
- L'APK Android **n'a pas été construit localement** (pas de SDK/NDK) ; la CI
  l'assemble et vérifie la présence des `.so` arm64/x86_64.
- Les sessions agents réelles exigent un binaire CLI ou une clé provider ; seul
  le mode `plan` est vérifiable sans credentials.

## Licence

Code du projet : MIT. `third_party/stb` : MIT / domaine public.
Dear ImGui : MIT. GLFW : zlib.
