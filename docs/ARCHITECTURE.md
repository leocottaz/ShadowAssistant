# Architecture

Shadow Assistant = un petit hôte natif **C++20** (`src/`) + une interface **HTML/CSS/JS** (`ui/`). Deux plateformes, **la même interface et le même protocole** :

| | Windows | Linux |
| --- | --- | --- |
| Hôte | Win32 (`src/app`, `src/web`, `src/ai`, `src/features`) | GTK3 (`src/linux/`) |
| Vue web | WebView2 | WebKitGTK (`webkit2gtk-4.1`) |
| Fenêtre | acrylique (DWM) | transparente, fond dessiné par la page (`html.linux`) |
| Build | `CMakeLists.txt` (MinGW) | `cmake/linux.cmake` (sélectionné quand `NOT WIN32`) |

- Le natif gère tout ce qu'une page web ne peut pas faire : la fenêtre et ses animations, la souris sur le bord de l'écran, le raccourci global, l'icône de notification, l'heure des rappels, la clé API, HTTPS et le lancement de Claude Code.
- La page gère l'affichage et les données de l'utilisateur (rappels, notes, réglages, conversation), stockées dans son `localStorage`.

Ils se parlent par messages (voir [Protocole](#protocole)). Le reste de ce document décrit le côté **Windows** ; la section [Linux](#linux) donne ce qui change.

```
 ┌──────────── ui/ (WebView2) ─────────────┐        ┌──────────────── src/ (C++) ─────────────────┐
 │ main.js ── route les messages natifs    │ "cmd\n │ web/bridge.cpp ── route les commandes       │
 │ dock/ · features/ · core/settings.js    │  arg"  │ app/dock.cpp      fenêtre, états, souris    │
 │ chat/chat.js ─┬─ chat/api.js  (clé)     │ ─────► │ ai/anthropic_http.cpp  HTTPS (Clé API)      │
 │               └─ chat/pro.js  (Pro)     │ ◄───── │ ai/claude_code.cpp     claude -p (Pro)      │
 │ core/bridge.js: send(), ask(), http     │  JSON  │ features/reminders.cpp, media.cpp, pc.cpp   │
 └─────────────────────────────────────────┘        └─────────────────────────────────────────────┘
```

## Organisation

Les deux côtés sont rangés de la même façon : un dossier par domaine.

| Domaine | Natif `src/` | Interface `ui/js/` |
| --- | --- | --- |
| Socle partagé | `common/` | `core/` (pont, état, réglages, utilitaires) |
| Fenêtre du dock | `app/` (fenêtre, icône, démarrage) | `dock/` (états visuels, onglets, mini-dock) |
| Lien page ↔ natif | `web/` (WebView2, routeur) | `core/bridge.js` |
| Claude et modèles | `ai/` | `chat/` |
| Fonctionnalités | `features/` (ce qui a besoin de Windows) | `features/` (un fichier par onglet ou outil) |

Le CSS reste à plat dans `ui/css/` : `base.css` (thème, verre, contrôles), puis un fichier par zone, du même nom que le module JS.

Les `#include` partent de `src/` : `#include "app/dock.h"`.

## Natif (`src/`)

Chaque module expose une petite interface dans son `.h` (un `namespace` : `dock::`, `webview::`, `bridge::`…) ; l'état reste privé dans le `.cpp`.

| Module | Rôle |
| --- | --- |
| `main.cpp` | Instance unique, initialisation, boucle de messages. |
| `app/dock.*` | Fenêtre acrylique, états (`hidden`, `progress`, `peek`, `mini`, `music`, `bar`, `panel`), animations, détection de la souris, écran courant (rangé, le dock suit l'écran de la souris), repli auto, raccourci Ctrl+Alt+Espace, menu de l'icône, application des réglages. |
| `app/tray.*` | Icône de notification (générée à l'exécution) et bulles Windows. |
| `app/autostart.*` | Clé `Run` pour le lancement au démarrage. |
| `web/webview_host.*` | Création de WebView2 (`--in-process-gpu` si l'économie de mémoire est activée, réglage dans `HKCU\Software\ShadowAssistant\LowMemory`), fond transparent, liens externes ouverts dans le navigateur, réception des messages, veille quand le dock est caché (`SetVisible(false)` : cible mémoire basse + `TrySuspend`), recréation si le processus meurt. |
| `web/bridge.*` | `PostToPage()` (depuis n'importe quel thread) et le routage des commandes de la page vers les modules. |
| `ai/anthropic_http.*` | HTTPS en flux (lignes complètes) : mode Clé API vers `api.anthropic.com`, mode Gratuit vers Groq ou Gemini (hôte fixe, préfixe de chemin imposé, clé ajoutée en `Bearer`). |
| `ai/claude_code.*` | Mode Abonnement Pro : lance `claude -p` sans outils, sans `ANTHROPIC_API_KEY`, dans un dossier dédié ; relaie le stream-json. |
| `ai/api_key.*` | Clés chiffrées par DPAPI : Anthropic, et Groq / Gemini pour le mode Gratuit. Jamais envoyées à la page (seuls les 4 derniers caractères). |
| `features/reminders.*` | Liste synchronisée depuis la page ; vérifie les échéances chaque seconde et déclenche notification + apparition du dock. Tient aussi la fin du minuteur (`SetTimerEnd`). |
| `features/media.*` | Lecteur : thread qui interroge les contrôles multimédias de Windows (GlobalSystemMediaTransportControls) toutes les 400 ms, choisit la session (Deezer, ou toutes les sources), lit titre/pochette/position et exécute lecture/pause/suivant/précédent/position. Interfaces WinRT déclarées à la main (voir l'avertissement en tête de fichier). |
| `features/pc.*` | Actions sur le PC : recherche de fichiers (index Windows Search via OLE DB, dossier de l'utilisateur hors AppData, mots nettoyés), ouverture d'un résultat (seulement un chemin déjà renvoyé ; exécutables et scripts → dossier sélectionné), routines (thread : ouvrir, volume Core Audio, musique, attendre), fenêtre « Ouvrir ». |
| `common/util.h` · `util.cpp` · `util_common.cpp` | JSON, découpage, base64 (`util_common.cpp`, commun) ; UTF-8/UTF-16 et dossiers Windows (`util.cpp`). |

Les `.h` partagés (`dock.h`, `bridge.h`, `tray.h`, `pc.h`, `util.h`) gardent ce qui touche à Windows derrière `#ifdef _WIN32`.

Les threads réseau et processus ne touchent jamais la fenêtre ni WebView2 : ils passent par `bridge::PostToPage`, qui poste un message à la fenêtre (thread UI).

## Interface (`ui/`)

Modules ES (`<script type="module" src="js/main.js">`), sans dépendance externe ni étape de build. Chaque module de fonctionnalité exporte `setupX()` (branche les évènements, appelé une fois par `main.js`) et `renderX()` (redessine).

| Fichier | Rôle |
| --- | --- |
| `js/main.js` | Démarrage et routage des messages du natif (`onNative`). |
| `js/core/bridge.js` | `send(cmd, …args)`, `ask(cmd, args)` (promesse résolue par `{t:"reply"}`), `http` (requêtes en cours par id), messages d'erreur API. |
| `js/core/state.js` | État partagé `S` (état du dock, onglet, connexion). |
| `js/core/settings.js` | Réglages persistés, apparence, envoi au natif. `onSettingChange(fn)` pour réagir sans dépendance circulaire. |
| `js/core/format.js`, `js/core/util.js` | Dates, icônes, stockage (`store`, clés préfixées `shadow.`), toast… |
| `js/dock/dock.js` | États visuels, onglets (`NAV_TABS` = boutons de la barre ; Claude n'en a pas : orbe, champ « Demander », Ctrl+Alt+Espace), horloge, barre, raccourcis clavier. |
| `js/dock/mini.js` | Mini-dock (dock rangé) : cloche + nombre de rappels sonnés (`setMiniAlerts`, appelé par `tick`), ligne de paroles (`setMiniLyric`) ; calcule la largeur et l'envoie au natif (`mini`). |
| `js/features/reminders.js` | Rappels : saisie en français, liste, alertes. |
| `js/features/notes.js` | Notes. |
| `js/features/todo.js` | To-do : tâches à cocher, étoile (en tête de liste), modification par double-clic, section « Faites ». |
| `js/features/timer.js` | Minuteur : temps limité (compte à rebours) ou illimité (chronomètre), calculé par horodatages ; texte de la barre (`timerLabel`). |
| `js/features/music.js` | Mini-lecteur de la barre et onglet Musique : position interpolée, barre de progression, couleur de la pochette, paroles LRCLIB (synchronisées au format LRC, ou texte simple), ligne en cours pour le mini-dock (`updateMini`). |
| `js/features/files.js` | Recherche de fichiers pour Claude : résultats rangés par recherche (cartes cliquables), ouverture par numéro dans la dernière recherche, contexte du mode Pro. |
| `js/features/routines.js` | Routines : données, éditeur des Réglages, lancement (`routine` vers le natif), recherche par nom pour Claude. |
| `js/chat/connection.js` | Section « Connexion » des Réglages : statut de Claude Code, clé API, clés gratuites. |
| `js/chat/chat.js` | Conversation : état (`convo`, `turn`), envoi, évènements de streaming, rendu. |
| `js/chat/api.js` | Transport mode Clé API (API Messages + outils). |
| `js/chat/free.js` | Transport mode Gratuit (Gemini, Groq ; API compatibles OpenAI) : conversion de la conversation (format Claude ↔ OpenAI), outils, choix automatique du modèle dans la liste du service. Sert aussi de secours (`tryFallback` dans `chat.js`) quand Claude échoue. |
| `js/chat/pro.js` | Transport mode Pro (Claude Code) + exécution des lignes `[[rappel|…]]`, `[[note|…]]`, `[[tache|…]]` (`[[tache!|…]]` = importante), `[[fait|…]]`, `[[chercher|mots|ext|du|au]]`, `[[ouvrir|n]]`, `[[routine|Nom]]` (`actionToTool`). |
| `js/chat/prompts.js` | Prompt système (recalculé à chaque message, jamais gardé dans la conversation), bloc `[Contexte]` (date, heure, morceau en cours, routines) ; en mode Pro, listes des rappels, des tâches et de la dernière recherche de fichiers. |
| `js/chat/tools.js` | Outils (`create_reminder`, `list_reminders`, `create_note`, `create_todo`, `list_todos`, `complete_todo`, `search_files`, `open_file`, `run_routine`, `timer`) et leur exécution (`execTool`, asynchrone). |
| `js/chat/markdown.js` | Markdown minimal et sûr. |
| `css/*.css` | `base` (thème, verre, contrôles), `dock`, puis un fichier par onglet. |

## Protocole

**Page → natif** : `send('commande', arg1, arg2…)` envoie la chaîne `"commande\narg1\narg2…"`.

| Commande | Arguments | Effet |
| --- | --- | --- |
| `ready` | — | La page est chargée : le natif répond `init` puis `state`. |
| `config` | lignes `clé=valeur` | `mode`, `revealDelay`, `hideDelay`, `width`, `panelH`, `theme`, `sound`, `autostart`, `miniLyrics` (garde la page éveillée, dock caché, pendant une lecture). |
| `open` / `bar` / `hide` | — | Ouvre le panneau / replie sur la barre / masque. |
| `pin` | `0`/`1` | Garder affiché. |
| `hold` | ms | Empêche le repli automatique pendant ms. |
| `reveal` | ms | Fait descendre la barre pendant ms (sauf devant une appli plein écran). |
| `revealmusic` | ms, largeur (DIP) | Nouveau morceau : fait descendre seulement la carte musique (état `music`, 280 à 380 × 76 selon le titre) pendant ms. |
| `mini` | largeur (DIP) | Mini-dock (rappels sonnés, paroles) : quand le dock est rangé, la fenêtre devient une pastille de cette largeur. `0` = rien à montrer. |
| `media` | `toggle`, `next`, `prev`, `seek\n<ms>`, `sources\n<deezer\|all>`, `refresh` | Pilote le lecteur / choisit la source / redemande l'état complet. |
| `lowmem` | `0`/`1` | Économie de mémoire (`--in-process-gpu`) ; si elle change, la vue est recréée. |
| `reminders` | lignes `id\tdueMs\ttexte` | Rappels à venir. |
| `timer` | `finMs` (0 = aucun) | Fin du compte à rebours du minuteur. |
| `notify` | titre, texte | Notification Windows. |
| `key` / `delkey` | clé / — | Enregistre / supprime la clé API. |
| `http` | id, méthode, chemin, betas, corps | Requête vers l'API (mode Clé API). |
| `fhttp` | id, service (`gemini`/`groq`), méthode, chemin, corps | Requête vers le modèle gratuit (mêmes réponses `chunk` / `done` / `error`). |
| `freekey` / `delfreekey` | service, clé / service | Enregistre / supprime la clé gratuite → `freekey`. |
| `cc` | `id␟modèle␟effort␟session␟système␟message` (séparateur `\x1f`) | Message via Claude Code (mode Pro). |
| `ccstatus` / `cclogin` | — | Cherche Claude Code / ouvre une console de connexion. |
| `cancel` | id | Annule une requête `http` ou `cc`. |
| `openurl` | URL http(s) | Ouvre le navigateur. |
| `search` | id, mots, extension, du, au | Recherche de fichiers → `reply` avec `results` (`path`, `name`, `modified`, `size`). |
| `openfound` | chemin | Ouvre un fichier déjà renvoyé par `search` (sinon rien). |
| `routine` | lignes `type\tvaleur` (`open`, `volume`, `music`, `wait`) | Exécute une routine dans un thread natif. |
| `volume` | 0 à 100 | Volume de la sortie audio par défaut. |
| `pickfile` | id | Fenêtre « Ouvrir » → `reply` avec `path` (vide si annulé). |
| `quit` | — | Quitte. |

**Natif → page** : objets JSON avec un champ `t`.

| `t` | Champs | Sens |
| --- | --- | --- |
| `init` | `hasKey`, `keyHint`, `autostart`, `lowMemory`, `freeKeys` | Réponse à `ready`. |
| `freekey` | `service`, `ok`, `freeKeys` | Résultat de `freekey` / `delfreekey`. |
| `state` | `s`, `delay`, `pinned` | Nouvel état du dock. |
| `key` | `ok`, `hasKey`, `keyHint` | Résultat de `key`/`delkey`. |
| `chunk` | `id`, `data` | Lignes de flux (SSE en mode API, stream-json en mode Pro). |
| `done` | `id`, `cancelled`, (`exit`, `stderr` en mode Pro) | Fin d'une requête. |
| `error` | `id`, `status`, `body` | Échec (`-1` pas de clé, `-3` Claude Code introuvable, `0` réseau, sinon HTTP). |
| `ccstatus` | `found`, `path`, `version` | Résultat de `ccstatus`. |
| `fire` | `id` | Un rappel sonne. |
| `timerdone` | — | Le compte à rebours du minuteur est fini. |
| `media` | `active`, `app`, `title`, `artist`, `album`, `status`, `pos`, `dur`, `at`, `canNext`, `canPrev`, `canSeek`, (`art` : data URL, seulement au changement de morceau) | Musique suivie. `pos` est la position (ms) à l'instant `at` (ms epoch). |
| `hotkey`, `show` | (`tab`) | Ouverture par raccourci ou menu de l'icône. |
| `reply` | `id`, `ok`, (`error`, `results`, `path`) | Réponse à une commande `ask()` (`search`, `pickfile`). |

## Données

| Quoi | Où |
| --- | --- |
| Rappels, to-do, notes, routines, réglages, conversation, dernières recherches de fichiers (clés `shadow.*`) | `localStorage` de WebView2 : `%LOCALAPPDATA%\ShadowAssistant\WebView2` |
| Clé API (DPAPI) | `%APPDATA%\ShadowAssistant\apikey.bin` |
| Clés gratuites (DPAPI) | `%APPDATA%\ShadowAssistant\geminikey.bin`, `groqkey.bin` |
| Dossier de travail de Claude Code | `%LOCALAPPDATA%\ShadowAssistant\claude-chat` |
| Lancement au démarrage | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\ShadowAssistant` |
| Économie de mémoire | `HKCU\Software\ShadowAssistant\LowMemory` (DWORD, 1 par défaut) |

## Linux

Cible séparée : `cmake/linux.cmake` (GTK3, WebKitGTK, libcurl, X11). Les modules ont **la même interface** (`.h`) que sous Windows, avec une implémentation dans `src/linux/`. Sont communs : `common/util_common.cpp` et `features/reminders.cpp`.

| Fichier `src/linux/` | Rôle (équivalent Windows) |
| --- | --- |
| `main_linux.cpp` | Fenêtre GTK, vue WebKitGTK, schéma `shadow://app/` (voir ci-dessous), instance unique par D-Bus (`GtkApplication`), `host::` (`Quit`, `OpenUrl`, `EvalJson`, `ChooseFile`). (`main.cpp`, `webview_host.cpp`) |
| `dock_linux.cpp` | Mêmes états, animations et repli que `app/dock.cpp`. Souris lue au pointeur global X11, Ctrl+Alt+Espace par `XGrabKey`, `reminders::Check()` chaque seconde. |
| `bridge_linux.cpp` | Routeur des commandes de la page ; `PostToPage` remet le JSON sur la boucle GTK (`g_idle_add`). (`web/bridge.cpp`) |
| `tray_linux.cpp` | Icône **StatusNotifierItem** en D-Bus (clic gauche = panneau, clic droit = menu GTK) et notifications `org.freedesktop.Notifications`. (`app/tray.cpp`) |
| `anthropic_http_linux.cpp` | HTTPS en flux avec libcurl, mêmes règles (hôtes fixes pour le mode Gratuit). (`ai/anthropic_http.cpp`) |
| `claude_code_linux.cpp` | `claude -p` par `fork`/`exec`, groupe de processus tué à l'annulation. (`ai/claude_code.cpp`) |
| `api_key_linux.cpp` | Clés dans `~/.config/ShadowAssistant`, fichiers `0600` (pas de trousseau pour l'instant). (`ai/api_key.cpp`) |
| `media_linux.cpp` | Lecteur via **MPRIS2** (D-Bus) : mêmes messages `media` ; pochettes converties en data URL. (`features/media.cpp`) |
| `pc_linux.cpp` | Recherche = parcours de `$HOME` (noms seulement, sans dossiers cachés ni `node_modules`) ; `xdg-open` ; volume `wpctl`/`pactl`. (`features/pc.cpp`) |
| `autostart_linux.cpp` | `~/.config/autostart/shadow-assistant.desktop`. (`app/autostart.cpp`) |
| `util_linux.cpp` | Dossiers XDG, dossier de l'exécutable. (`common/util.cpp`) |
| `host.h` | Petite interface entre `bridge_linux`/`pc_linux`/`tray_linux` et la fenêtre GTK. |

**Page** : `ui/js/core/bridge.js` détecte `window.webkit.messageHandlers.shadow` ; le natif répond en appelant `window.__fromNative(json)`. Il ajoute la classe `linux` à `<html>` (fond opaque, voir `base.css`).

**Interface servie en `shadow://app/…`**, pas en `file://` : WebKit refuse les modules JavaScript chargés depuis `file://`. `ServeUi` ne sert que `ui/` (chemins normalisés, pas de sortie du dossier). Équivalent de `https://shadow.local/` avec WebView2.

**Fenêtre** : un `GtkLayout` (taille minimale nulle) contient la vue, qui garde la taille du panneau complet ; la fenêtre la découpe, comme sous Windows.

Limites connues (voir aussi `Pièges` dans les notes) :
- Backend GDK forcé sur **X11/XWayland** (`gdk_set_allowed_backends("x11,wayland")`) : Wayland ne permet ni de lire la souris au bord de l'écran, ni de placer la fenêtre, ni de capter un raccourci global.
- Pas d'acrylique, pas de détection du plein écran (`ForegroundIsFullscreen` renvoie toujours faux), pas de mode « économie de mémoire » (`lowmem` ignoré).
- GNOME n'affiche l'icône qu'avec l'extension AppIndicator.
- Commandes **non portées** : aucune ; toutes celles du tableau Protocole sont routées par `bridge_linux.cpp`, sauf `lowmem` (ignorée).

### Données sous Linux

| Quoi | Où |
| --- | --- |
| `localStorage` de la page | `~/.local/share/ShadowAssistant/localstorage` (cache WebKit : `~/.cache/ShadowAssistant`) |
| Clés (Anthropic, Groq, Gemini) | `~/.config/ShadowAssistant/apikey`, `groqkey`, `geminikey` (0600) |
| Dossier de travail de Claude Code | `~/.local/share/ShadowAssistant/claude-chat` |
| Lancement à l'ouverture de session | `~/.config/autostart/shadow-assistant.desktop` |

## Compiler et publier

| Plateforme | Commande |
| --- | --- |
| Windows | `cmake --preset release && cmake --build --preset release` |
| Linux | `cmake --preset linux-release && cmake --build --preset linux-release` (paquets : `libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev libx11-dev`) |
| Archive portable | `cpack -G ZIP` (Windows) ou `cpack -G TGZ` (Linux), dans le dossier de build ; `-DSHADOW_VERSION=v0.1.0` fixe le nom |
| Installateur Windows | `cmake --install <build> --prefix stage`, puis `ISCC /DAppVersion=0.1.0 /DStageDir=stage /DOutDir=dist installer\windows\shadow-assistant.iss` (Inno Setup 6) |
| Paquet `.deb` | configurer avec `-DSHADOW_DEB_LAYOUT=ON`, puis `cpack -G DEB` |

**Installateur Windows** (`installer/windows/shadow-assistant.iss`) : installation par utilisateur (`PrivilegesRequired=lowest`, `{autopf}` = `%LOCALAPPDATA%\Programs`), raccourci du menu Démarrer, cases « Bureau » et « Lancer à l'ouverture de session » (décochées). Cette dernière écrit la même clé `Run\ShadowAssistant` que le réglage de l'application (`autostart.cpp`). `AppId` est fixe : c'est lui qui relie une mise à jour à l'installation précédente, ne jamais le changer. Avant d'installer ou de désinstaller, `StopInstalledDock` arrête *seulement* le `ShadowAssistant.exe` du dossier d'installation (comparaison du chemin exact), jamais une autre copie. La désinstallation ne retire la clé `Run` que si elle pointe vers le dossier installé, et conserve les données de l'utilisateur.

**Paquet `.deb`** (`cmake/linux.cmake`, option `SHADOW_DEB_LAYOUT`) : `/opt/shadow-assistant` (exécutable + `ui/`), lien `/usr/bin/shadow-assistant`, `shadow-assistant.desktop` et l'icône SVG (`installer/linux/`). L'exécutable retrouve `ui/` par `/proc/self/exe`, donc le lien symbolique n'est pas un problème. Dépendances : `dpkg-shlibdeps` quand il existe (Debian, Ubuntu), sinon liste écrite à la main. L'architecture est fixée explicitement (sans `dpkg`, CPack choisirait `i386`). Une version qui ne commence pas par un chiffre devient `0.0.0~<nom>`.

GitHub Actions (`.github/workflows/`) : `ci.yml` compile Windows (MSYS2/MinGW) et Linux (Ubuntu 24.04) à chaque push et pull request. `release.yml` se déclenche sur un tag `v*` : il construit l'installateur et le ZIP Windows, le `.deb` et le `tar.gz` Linux, et les attache à une GitHub Release.

```
git tag v0.1.0 && git push origin v0.1.0
```

## Recettes

- **Ajouter une commande native** : une fonction dans le module concerné (`.h` + `.cpp`), une branche dans `bridge::HandleMessage` (`src/web/bridge.cpp` **et** `src/linux/bridge_linux.cpp`), une ligne dans le tableau ci-dessus ; côté page, `send('ma-commande', …)`.
- **Ajouter un message vers la page** : `bridge::PostToPage("{\"t\":\"…\"}")` (utiliser `util::Json` pour les chaînes) et un `case` dans `onNative` (`ui/js/main.js`).
- **Ajouter un module natif** : `src/<domaine>/nom.h` + `nom.cpp`, un `namespace nom`, et une ligne dans `add_executable` (`CMakeLists.txt`). Pour Linux : `src/linux/nom_linux.cpp` avec la même interface, et une ligne dans `cmake/linux.cmake`.
- **Ajouter une fonctionnalité à l'interface** : `ui/js/features/nom.js` qui exporte `setupNom()` / `renderNom()`, appelés dans `init()` (`ui/js/main.js`) ; son style dans `ui/css/nom.css`, lié dans `index.html`. Pour un nouvel onglet : sa vue dans `index.html` et son entrée dans `NAV_TABS` (`ui/js/dock/dock.js`).
- **Ajouter un réglage** : valeur par défaut dans `DEFAULTS` (`ui/js/core/settings.js`) et un contrôle avec `data-setting="nom"` dans `index.html` (segmenté, curseur ou interrupteur sont gérés automatiquement). S'il concerne le natif : l'ajouter à `NATIVE_KEYS` et `sendConfig()`, puis le lire dans `dock::ApplyConfig`.
- **Ajouter un outil pour Claude** : définition dans `TOOLS` et exécution dans `execTool` (`ui/js/chat/tools.js`), libellé dans `toolHTML` (`ui/js/chat/chat.js`). Pour le mode Pro, documenter la ligne d'action dans `PRO_ACTIONS` (`prompts.js`) et la lire dans `commitCcAnswer` (`pro.js`).
- **Changer la taille de la barre** : `kBarH`/`kInset` dans `src/app/dock.cpp` **et** `src/linux/dock_linux.cpp`, **et** `--bar-h`/`--inset` dans `ui/css/base.css`.

## Déboguer

- **Interface seule** : servir `ui/` avec n'importe quel serveur local et ouvrir `index.html`. Sans l'hôte natif, la page passe en mode aperçu (panneau ouvert, API désactivée).
- **Dans l'appli** : lancer avec `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222`, puis ouvrir `edge://inspect` ; ou appuyer sur F12 dans le dock.
- **Lecteur dans l'aperçu** : `window.__preview.onNative({t:'media', active:true, title:'…', status:'playing', pos:0, dur:200000, at:Date.now(), …})` simule un morceau, `window.__preview.previewLyrics([{t:0, text:'…'}])` des paroles factices.
- **Linux, interface seule** : comme sous Windows (serveur local, mode aperçu). **Linux, dans l'appli** : lancer `ShadowAssistant` depuis le dossier de build ; sous WSL, WSLg fournit l'affichage (X11), mais ignore les positions de fenêtre demandées et donne le focus à l'ouverture. Pour suivre la fenêtre sans outil graphique, un petit programme Xlib qui interroge `XQueryTree` suffit.
- **Plantage en Release seulement** : compiler en `RelWithDebInfo` et lancer sous le gdb de CLion (`bin\gdb\win\x64\bin\gdb.exe`) pour avoir la pile d'appels.
- **Mode Pro sans consommer de quota** : un faux `claude.exe` placé plus tôt dans le `PATH` (ni `%USERPROFILE%\.local\bin` ni winget ne doivent contenir le vrai) reçoit les mêmes arguments.
