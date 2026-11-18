# parsergui

Un studio « screenshot → ImGui » : une capture d'écran entre, du code C++ Dear
ImGui mesuré au pixel près sort, avec la preuve chiffrée que la reconstruction
correspond.

Le dépôt contient un moteur portable (`pgr_core`), un studio web local, un
serveur HTTP avec file de jobs, un bridge vers les agents de code (Claude Code,
Codex, OpenCode), une application desktop Dear ImGui, une application Android,
une solution Visual Studio et la CI multi-plateforme.

## Ce que le moteur mesure réellement

Aucune estimation « à l'œil » : chaque valeur du rapport sort d'une mesure sur les
pixels, et la fidélité est obtenue en re-rendant l'arbre puis en le comparant à
l'image source (SSIM / MAE / PSNR / pixels hors seuil).

| Grandeur | Méthode |
| --- | --- |
| Couleurs | quantification median-cut en CIELAB + k-means, palette triée par surface |
| Zones | composantes connexes (run-length + union-find) sur masques par couleur |
| Texte | fusion des glyphes en lignes, hauteur de capitale → taille de police |
| Bordures | recherche **vers l'extérieur** depuis le rectangle du composant, expansion jusqu'au pic de contraste |
| Rayon | ajustement du profil de coin avec inset, pas une constante |
| Dégradés | monotonie du profil de couleur sur l'axe principal |
| Ombres | uniquement si un halo plus sombre existe hors du rectangle |
| DPR | épaisseur mesurée des traits fins (1 px logique = 1, 2 ou 3 px physiques) |
| Grille | histogramme des espacements, base 4/6/8 px, marge médiane |
| Rôles | passe structurelle : menus, onglets, lignes de liste, badges, toggles, sliders, champs |
| Icônes | 28 templates vectoriels, normalisation ratio + chamfer + densité 4×4 |
| Fidélité | re-rendu logiciel anti-aliasé + comparaison à la source |

### Résultats mesurés

Scène de test historique (900×600, 23 vérifications) :

```
11 elements, 4 panels, 2 buttons, 0 inputs, 0 icons; grid 4px
fidelity 99.4/100 (SSIM 0.9918, MAE 0.16, PSNR 39.1 dB, off-pixels 0.15%)
rayon 12 px, bordure détectée, palette exacte, police estimée 15.49 px
```

Scène dense (1280×800 : barre de menus + menu déroulant ouvert, toolbar, sidebar,
onglets, 3 cartes KPI, tableau de 6 lignes, formulaire, status bar) :

```
153 elements · 11 MenuItem · 3 Tab · 2 Input · 3 Toggle · 3 Button · 9 Badge
13 Icon nommées · 13 ListItem · 12 Panel
fidelity 96.0/100 · typographie 90.8/100 · grille 4 px
```

Menu contextuel (640×420, seconde mise en page indépendante) :

```
29 elements · 6 MenuItem · 6 Icon (check, edit, download, lock, trash, chevron_right)
fidelity 95.7/100
```

Overlay de jeu (1600×900, fond 3D saturé + panneau ImGui sombre) :

```
region 561x462 at 999,99 (attendu 560x460 at 1000,100) · confiance 0.90
65 elements · 4 panels · fidelity 91.4/100
```

Icônes : **84/84** templates se reconnaissent eux-mêmes à 2×, 3× et 4×.
Le code C++ généré est vérifié par le compilateur à chaque analyse :

```
g++ -std=c++17 -fsyntax-only -I third_party/imgui parsed_ui.cpp   # 0 erreur
```

## Détection automatique du GUI

Une capture n'est pas toujours l'interface : c'est souvent une scène de jeu avec
un overlay flottant. `find_gui_region` isole la fenêtre avant toute mesure :

1. masque des pixels **sombres, neutres et localement plats** (un panneau, pas une
   forêt) ;
2. composantes connexes, puis filtres de taille, remplissage, bords droits ;
3. **preuve de widgets** : densité de contours et proportion de pixels clairs
   (texte, icônes, accents) — un trou noir sans texte est rejeté ;
4. fusion des fragments séparés par un filet de 1 px (pied de fenêtre, onglet) ;
5. score 0..1, seuil 0.42, sinon la pleine image est analysée comme avant.

```bash
parsergui parse shot.png --out ./out              # auto par défaut
parsergui parse shot.png --crop 1000,100,560,460  # région explicite
parsergui parse ui.png --no-auto-gui              # image déjà recadrée
```

Serveur : option `auto_gui` (activée par défaut, case « GUI auto » dans le studio)
et `crop: [x,y,w,h]`. Le rapport expose `region` et `auto_gui` en JSON, et
« GUI region … (auto-detected) » en Markdown. Les coordonnées des éléments sont
celles de la fenêtre détectée, donc directement utilisables dans une ImGui.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

| Suite | Vérifications | Contenu |
| --- | --- | --- |
| `parser_roundtrip` | 23 | encodage PNG, mesures, palette, JSON, émission, artefacts |
| `icon_recognition` | 6 | 84 auto-correspondances + scène loupe nommée |
| `complex_ui` | 17 | dashboard + menu déroulant, menu contextuel, rôles, fidélité |
| `overlay_detection` | 19 | overlay sur fond de jeu, réplique 1600×900, crop explicite, faux positifs |

Total : **65 vérifications**, 0 échec. La suite overlay vérifie aussi toute image
PNG déposée dans `assets/demo/overlay/`.

## Arborescence

```
core/        moteur portable, aucune dépendance GUI
  image      PNG/JPEG/WebP/BMP/TGA (stb, vendored)
  color      sRGB, CIELAB, deltaE, quantification, palette
  raster     rasterizer anti-aliasé (rect, rounded, cercle, ligne, texte)
  segment    composantes connexes run-length + union-find
  font       métriques de police, 28 templates d'icônes, matching
  detect     pipeline de mesure + passe de rôles structurelle
  render     re-rendu + SSIM/MAE/PSNR/diff
  emit       générateur ImGui (DrawList + widgets + icônes) et rapports
  parser     orchestration, artefacts, animation entre frames
server/      serveur HTTP POSIX/Winsock, multipart, file de jobs, API, UI web
agents/      sous-processus, registre de providers, bridge d'agents
cli/         parse, serve, providers, agent, version
app/         application desktop Dear ImGui + GLFW + OpenGL3
android/     shell Android (JNI + WebView + Gradle)
skills/      contrat d'agent et skills de reconstruction
providers/   catalogue de providers (72 réels, capacité 500)
tests/       trois suites, 46 vérifications
vcxproj/     projets Visual Studio 2022
ParserGUI.sln
prebuilt/    binaires Linux et Windows déjà compilés
assets/demo/ captures, reconstructions, diffs, rapports et code générés
```

## Build (CMake)

Prérequis : CMake ≥ 3.16, compilateur C++17. Le GUI ajoute OpenGL et GLFW
(`FetchContent`). Aucun codec externe : stb est vendored.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Sans GUI (serveur seul) : `-DPARSERGUI_BUILD_GUI=OFF`.

## Solution Visual Studio

`ParserGUI.sln` (Visual Studio 2022, x64) contient six projets :

| Projet | Sortie |
| --- | --- |
| `pgr_core` | bibliothèque statique du moteur |
| `parsergui` | CLI + serveur HTTP (`bin\x64\Release\parsergui.exe`) |
| `parsergui_gui` | application desktop ImGui (GLFW via vcpkg) |
| `parsergui_tests` | suite parser |
| `parsergui_icon_tests` | suite icônes |
| `parsergui_ui_tests` | suites de scènes denses |

Le projet GUI utilise le manifeste `vcpkg.json` (`glfw3`). Une fois vcpkg
intégré (`vcpkg integrate install`, ou `VCPKG_ROOT` défini), Visual Studio
restaure GLFW au premier build. Les projets CLI et tests n'ont besoin d'aucune
dépendance externe.

En ligne de commande :

```powershell
.\build_windows.ps1            # configure, build, ctest, dist\
```

## Binaires déjà compilés

`prebuilt/linux-x64/` — compilés **et testés** ici :

```
parsergui  parsergui_tests  parsergui_icon_tests  parsergui_ui_tests
```

`prebuilt/windows-x64/` — cross-compilés avec LLVM-MinGW (PE32+, statiques) :

```
parsergui.exe  parsergui_tests.exe  parsergui_icon_tests.exe  parsergui_ui_tests.exe
```

La compilation croisée a d'ailleurs révélé un vrai défaut de portabilité
(`std::getenv` absent sous MinGW), corrigé depuis. Ces exécutables Windows n'ont
pas pu être lancés ici (pas de Windows ni de Wine) : la CI les reconstruit avec
MSVC et exécute les tests.

## CLI

```bash
prebuilt/linux-x64/parsergui parse capture.png --out ./out
parsergui parse overlay.png --auto-gui --out ./out
parsergui parse capture.png --crop 1000,100,560,460 --out ./out
parsergui parse t0.png t1.png t2.png --frames
parsergui serve --port 8787 --data ./parsergui-data
parsergui providers --free
parsergui agent --job ./out --agent claude --budget 150
```

Artefacts : `source.png`, `reconstruction.png`, `diff.png`, `parsed_ui.cpp`,
`parsed_ui_main.cpp` (harnais GLFW), `report.json`, `report.md`, `layout.txt`,
`agent_prompt.md`.

## Studio web

```bash
prebuilt/linux-x64/parsergui serve --port 8787
# http://localhost:8787
```

Glissez un nombre illimité de captures. Par défaut **chaque image devient un job
indépendant** ; cochez « Animations » pour traiter la sélection comme les frames
d'un même écran. L'interface montre source, reconstruction, diff, code, rapport,
palette et animations, avec progression et ETA.

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

`providers/providers.json` contient 72 providers réels, triés par gratuité puis
priorité puis clé disponible ; 17 fonctionnent sans clé. Capacité 500 ;
`providers/user.json` ajoute les vôtres. Token Harbor est inclus
(`https://tokenharbor.ai/v1`, modèles `:free`) avec `TOKENHARBOR_API_KEY`.
Les clés ne sont jamais écrites sur disque : session en mémoire ou environnement.

Le bridge crée de vraies sessions : `claude -p`, `codex exec`, `opencode run`
(sous-processus avec timeout et budget d'éditions), sinon boucle provider
OpenAI/Anthropic avec outils `read_file` / `write_file`. Le mode `plan` écrit
`agent_prompt.md` sans rien modifier — vérifié de bout en bout ici.

## Application desktop

`parsergui_gui` : dépôt de fichiers, options, analyse, aperçu
source/reconstruction/diff, code ImGui copiable, rapport tabulaire, layout.
`Ctrl+Entrée` analyse, `Ctrl+O` ajoute un chemin. Thème sombre dense, 1440×900,
aucune dépendance à un serveur.

## Android

`android/` construit un APK (arm64-v8a + x86_64) : WebView + JNI vers `pgr_core`.
La capture choisie est copiée dans le cache, analysée sur l'appareil, puis l'UI
affiche mesures, reconstruction, diff et code.

```bash
cd android && gradle assembleDebug
```

## CI

`.github/workflows/build.yml` :

| Job | Runner | Cible |
| --- | --- | --- |
| desktop-linux-x64 | ubuntu-24.04 | CLI, serveur, GUI (X11), tests |
| desktop-windows-x64 | windows-2022 | Visual Studio 17, `-A x64` |
| desktop-windows-x86 | windows-2022 | Visual Studio 17, `-A Win32` |
| desktop-macos-arm64 | macos-15 | Apple Silicon |
| desktop-macos-x64 | macos-15-intel | Intel |
| android-arm64-x86_64 | ubuntu-24.04 | Gradle 8.7 + NDK 26 + CMake 3.22.1 |

Chaque job desktop compile GUI + tests, exécute `ctest`, installe `dist/`, fait
un smoke test réel (`parsergui version`, puis une analyse auto-croppée) et
publie l'artefact. Le job Android installe explicitement `cmake;3.22.1` (sans lui
la configuration native échoue), assemble l'APK, vérifie la présence des `.so`
arm64/x86_64 puis publie l'APK.

Le dernier run est **vert sur les 6 jobs** et publie six artefacts :
`parsergui-linux-x64`, `-windows-x64`, `-windows-x86`, `-macos-arm64`,
`-macos-x64` et `parsergui-android` (APK 4,5 Mo, `libpgr_core.so` arm64 +
x86_64 vérifié).

Défauts réels trouvés en réparant la CI :

- `install(TARGETS parsergui ...)` s'exécutait sous Android alors que la cible
  n'existe pas → configuration CMake en échec ;
- `setup-android@v3` cassé par la migration Node 20 → 24 → SDK de l'image
  Ubuntu utilisé directement, avec `cmake;3.22.1` installé explicitement ;
- MSVC n'a **pas** `socklen_t` (Winsock attend un `int*`) ;
- MSVC tronque tout littéral de plus de 16380 octets (C2026) → page du studio
  découpée en cinq littéraux adjacents ;
- `<GL/gl.h>` du SDK Windows exige `<windows.h>` avant lui, et
  `GL_CLAMP_TO_EDGE` (GL 1.2) manque du header 1.1 ;
- `luma_at` renvoie 0..1 alors que les seuils de la détection GUI étaient
  calibrés sur 0..255.

Pour rendre ces diagnostics lisibles sans session authentifiée, le build écrit
son journal et republie les premières erreurs du compilateur en annotations du
run.

## Skills

`skills/` contient le contrat d'agent et les skills transmis aux modèles :
`agent-contract.md`, `imgui-reconstruction.md`, `pixel-forensics.md`,
`frontend-design.md`, `unslop.md`, plus `manifest.json`.

## Limites honnêtes

- **Pas d'OCR.** Les textes non reconnus restent des placeholders `// TODO(text)`
  mesurés (position, taille, couleur) ; l'agent vision peut les remplir.
- La détection d'overlay suppose une fenêtre **sombre, neutre et rectangulaire**
  (ImGui classique). Un thème clair sur fond clair, ou un panneau très incurvé,
  demande un `--crop` explicite.
- Les deux captures de jeu fournies dans la conversation n'étaient pas
  accessibles comme fichiers dans l'espace de travail : la détection est validée
  sur une réplique 1600×900 générée, et la suite vérifiera automatiquement vos
  PNG dès qu'ils sont dans `assets/demo/overlay/`.
- Les captures bruitées (photo d'écran, JPEG agressif) dégradent la segmentation ;
  le rapport expose alors les avertissements et la fidélité baisse.
- L'application desktop a été validée par compilation (headers GLFW réels, CI)
  mais **n'a pas été lancée ici** (pas de serveur X).
- L'APK Android **n'a pas été construit localement** (pas de SDK/NDK) ; la CI
  l'assemble et vérifie les `.so` arm64/x86_64.
- Les `.exe` Windows sont cross-compilés, **pas exécutés ici** (pas de Wine).
- Les sessions agents réelles exigent une authentification CLI ou une clé
  provider ; seul le mode `plan` est vérifiable sans credentials.

## Licence

Code du projet : MIT. `third_party/stb` : MIT / domaine public.
Dear ImGui : MIT. GLFW : zlib.
