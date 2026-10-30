# Manuel utilisateur et développeur — Simulateur V2V

Projet de Master 1 Informatique et Mobilité, UE Réseaux Mobiles, Université de Haute-Alsace (automne 2026).

**Sommaire**

1. [Présentation](#1-présentation)
2. [Prérequis, outils et bibliothèques](#2-prérequis-outils-et-bibliothèques)
3. [Installation et lancement](#3-installation-et-lancement)
4. [Utilisation](#4-utilisation)
5. [Architecture du code](#5-architecture-du-code)
6. [Algorithmes](#6-algorithmes)
7. [Tests et performances](#7-tests-et-performances)
8. [Guide du développeur](#8-guide-du-développeur)
9. [Limites connues](#9-limites-connues)

---

## 1. Présentation

Les véhicules connectés communiquent directement entre eux (V2V). Quand beaucoup de véhicules sont proches,
leurs émissions radio se gênent s'ils utilisent le même canal. Le simulateur :

1. charge le réseau routier réel d'une ville (Mulhouse) depuis OpenStreetMap ;
2. en fait un graphe orienté (intersections, tronçons, sens uniques, longueurs) ;
3. fait rouler au moins 2000 véhicules, chacun vers une destination par le plus court chemin ;
4. construit régulièrement le **graphe d'interférences** : un sommet par véhicule, une arête entre deux
   véhicules plus proches que leur portée radio ;
5. attribue un canal à chaque véhicule par **coloration** de ce graphe (deux voisins = deux canaux différents)
   et compare quatre algorithmes : nombre de canaux, temps de calcul, stabilité dans le temps ;
6. montre le tout au choix sur une **carte 2D** ou dans une **vue 3D** de la ville (bâtiments, voitures, arcs
   d'interférence, portée radio du véhicule choisi).

Ce manuel décrit la **version 3D** (branche `version-3d`). La version 2D seule est sur la branche
`setup/squelette` : mêmes calculs, sans la vue 3D.

![Vue 3D](images/vue3d.jpg)

## 2. Prérequis, outils et bibliothèques

| Élément | Version utilisée | Rôle |
|---|---|---|
| C++ | C++17 | langage du projet |
| Compilateur | MinGW 13.1 (Windows) ; GCC, Clang ou MSVC conviennent | compilation |
| CMake | 3.20 ou plus (3.16 suffit pour compiler ; `ctest --test-dir` demande 3.20) | description de la compilation (`CMakeLists.txt`) |
| Ninja | 1.12 (facultatif) | compilation rapide |
| Qt 6 | 6.8.3 | Widgets (interface, `QGraphicsView` pour la carte), Charts (courbes), Xml (`QXmlStreamReader` pour le `.osm`), OpenGL et OpenGLWidgets (vue 3D : `QOpenGLWidget`, shaders) |
| OpenGL | 3.3 « core » | dessin de la vue 3D par la carte graphique (pilote de la carte graphique, rien à installer) |
| Bibliothèque standard C++ | — | conteneurs, algorithmes, générateur aléatoire `std::mt19937` |
| OpenStreetMap / Overpass API | — | données cartographiques (`data/telecharger_carte.py`) |
| Git + GitHub | — | gestion du code |
| Qt Creator ou VS Code | — | éditeurs utilisés par l'équipe |

Machine : tout PC récent. La carte 2D est dessinée par le processeur ; la vue 3D demande une carte graphique
compatible OpenGL 3.3 (tous les PC depuis 2012 environ, y compris les cartes intégrées Intel). Sur un portable à
deux cartes graphiques, le programme demande au pilote la plus puissante (NVIDIA ou AMD). Mesuré sur un portable
Windows 10 (écran à 125 %, Intel UHD + NVIDIA GTX 1650) : 2000 véhicules à 25 images/s avec les arêtes, en 2D
comme en 3D.

## 3. Installation et lancement

### 3.1 Installer Qt (Windows)

Avec l'installateur officiel de Qt : cocher **Qt 6.8 > MinGW 64-bit**, **Qt Charts**, et dans *Developer and
Designer Tools* : **MinGW 13.1**, **CMake**, **Ninja**.

Ou sans compte, en ligne de commande (Python requis), dans `C:\Qt` :

```bash
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_mingw -m qtcharts -O C:\Qt
python -m aqt install-tool windows desktop tools_mingw1310 -O C:\Qt
python -m aqt install-tool windows desktop tools_cmake -O C:\Qt
python -m aqt install-tool windows desktop tools_ninja -O C:\Qt
```

### 3.2 Compiler

Dans PowerShell, à la racine du projet :

```bash
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.8.3\mingw_64\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

La première ligne place les outils de Qt **avant** un éventuel autre compilateur (par exemple celui de
Code::Blocks), trop ancien pour Qt 6. Elle est à refaire dans chaque nouveau terminal.

Linux / macOS : installer Qt 6 (paquet de la distribution, Homebrew ou installateur Qt) puis
`cmake -S . -B build -DCMAKE_PREFIX_PATH=<dossier de Qt 6>` et `cmake --build build`.

Avec Qt Creator : *Fichier > Ouvrir un fichier ou projet* > `CMakeLists.txt`, choisir le kit Qt 6.8 MinGW.

### 3.3 Lancer

```bash
build\v2v-simulator.exe
```

- Sans argument, la carte `data/mulhouse.osm` du projet s'ouvre automatiquement, avec ses bâtiments
  (`data/mulhouse_batiments.txt`), en vue 3D.
- Avec un argument, le fichier donné est ouvert : `v2v-simulator autre_ville.osm`.
- Pour un lancement par double-clic sous Windows, copier une fois les bibliothèques de Qt à côté de l'exécutable :
  `C:\Qt\6.8.3\mingw_64\bin\windeployqt.exe --release build\v2v-simulator.exe`.

### 3.4 Changer de carte

`data/telecharger_carte.py` télécharge les routes d'une zone par l'API Overpass (Python, sans dépendance).
Modifier `ZONE = (sud, ouest, nord, est)` en degrés pour une autre ville. On peut aussi exporter une zone depuis
openstreetmap.org (*Exporter*) : le lecteur ignore tout ce qui n'est pas une route carrossable.

Les bâtiments de la vue 3D viennent de `data/telecharger_batiments.py` (même zone, même méthode). Le programme
cherche, à côté de la carte `ville.osm`, un fichier `ville_batiments.txt` ; sans ce fichier, la vue 3D montre la
ville sans bâtiments. Format (une ligne par bâtiment, coordonnées en millionièmes de degré, écarts entre points
successifs) et règles d'estimation des hauteurs : voir l'en-tête du script.

## 4. Utilisation

### 4.1 La fenêtre principale

![Fenêtre principale pendant une simulation](images/simulation.png)

| Zone | Contenu |
|---|---|
| Centre | la vue 3D (au lancement) ou la carte 2D : routes (gris, plus foncé = plus importante), véhicules (couleur de leur canal), arêtes d'interférence |
| Haut | barre d'outils : ouvrir une carte, carte 2D / vue 3D, recentrer, arêtes, comparaison ; en carte 2D seulement, une deuxième barre pour les outils qui se manient à la souris sur la carte : zone, effacer la zone, itinéraire, intersections |
| Droite | panneau de contrôle en 3 onglets : **Simulation** (réglages, état, graphe d'interférences), **Coloration** (tableau des 4 algorithmes, canaux utilisés, comparaison, export), **Carte** (carte chargée, zone, graphe routier, plus court chemin, légende des routes) ; en dessous, les gestes de souris de la vue affichée |
| Bas | barre de contrôle du temps : démarrer/pause, ralentir, vitesse (×1...), accélérer, réinitialiser, temps simulé, images par seconde |
| Barre d'état | messages, indicateurs de la coloration affichée (canaux, temps, stabilité, validité), coordonnées GPS sous la souris |

Tant que la simulation n'a pas été démarrée (au lancement, après *Appliquer et relancer* ou *Réinitialiser*), un
message au centre de la vue rappelle d'appuyer sur Espace. *Affichage > Thème sombre* (**Ctrl+T**) passe
l'interface en couleurs sombres ; les cartes 2D et 3D gardent leurs couleurs, choisies pour que les canaux restent
lisibles.

![Thème sombre, onglet Coloration](images/interface_sombre.jpg)

### 4.2 Se déplacer sur la carte

- **Molette** : zoom centré sur la souris (de « toute la ville » à 20 pixels par mètre).
- **Clic-glisser** : déplacer la carte.
- **R** ou *Recentrer* : revenir à la vue de toute la carte.
- L'échelle en bas à gauche s'adapte au zoom.

### 4.3 Choisir la zone de simulation (étape 1)

1. **Z** ou *Choisir la zone*, puis clic-glisser un rectangle sur la carte (Échap pour annuler).
2. L'extérieur est assombri ; le panneau indique la taille de la zone et le nombre de routes concernées.
3. Le graphe routier est reconstruit sur la zone et la simulation relancée sur ce graphe.
4. **Suppr** ou *Effacer la zone* : revenir à toute la carte.

### 4.4 Graphe routier et plus court chemin (étape 2)

- Le panneau *Graphe routier* indique le nombre d'intersections et de tronçons (Mulhouse : 5 817 et 12 479).
- **G** ou *Intersections* : affiche les sommets du graphe.
- **I** ou *Itinéraire* : cliquer un départ (A) puis une arrivée (B). Le plus court chemin s'affiche en violet ;
  le panneau donne sa longueur et compare A* et Dijkstra (même longueur, A* visite moins d'intersections).

### 4.5 Simulation (étape 3)

| Commande | Raccourci | Effet |
|---|---|---|
| Démarrer / Pause | Espace | lance ou suspend la simulation |
| Ralentir / Accélérer | - / + | vitesse du temps simulé de ×0,25 à ×32 |
| Réinitialiser | Ctrl+R | retour à t = 0 avec le même scénario (même graine) |

Réglages du panneau *Simulation* :

| Réglage | Défaut | Application |
|---|---|---|
| Véhicules | 2000 | bouton *Appliquer et relancer* |
| Portée R | 300 m | immédiate (même en pause) |
| Recalcul toutes les | 1,0 s simulée | immédiate |
| Graine aléatoire | 42 | bouton *Appliquer et relancer* |

Chaque véhicule a une position, une vitesse (90 km/h sur autoroute, 50 sur les routes principales et
secondaires, 30 dans les rues, multipliée par un facteur propre au véhicule entre 0,8 et 1,2), une direction, une
destination et une portée radio. À l'arrivée, il reçoit une nouvelle destination au hasard.

### 4.6 Graphe d'interférences (étape 4)

- Recalculé à intervalle régulier (réglage *Recalcul toutes les*), pas à chaque image.
- **E** ou *Arêtes d'interférence* : dessine les arêtes entre véhicules en interférence.
- Le panneau donne le nombre d'arêtes, le degré moyen et maximal, et le temps de construction.

### 4.7 Coloration et comparaison (étape 5)

- À chaque recalcul, les **quatre** algorithmes colorent le même graphe. *Couleurs affichées* (onglet
  *Coloration*) choisit celui dont les couleurs apparaissent sur la carte (et dans la barre d'état).
- Le tableau de l'onglet *Coloration* donne pour chacun, au dernier recalcul : nombre de canaux, temps de calcul
  (ms), stabilité S et validité (✓ vert = aucune arête entre deux véhicules de même canal, vérifié
  automatiquement ; ✗ rouge avec le nombre de conflits sinon). Le plus petit nombre de canaux est en gras.
  Cliquer une ligne affiche les couleurs de cet algorithme sur la carte.
- Les pastilles sous le tableau montrent les canaux utilisés (12 par ligne).
- *Comparaison* (barre d'outils, onglet *Coloration* ou menu *Simulation*) ouvre la fenêtre de comparaison :
  tableau des moyennes et maximums, courbes k(t), temps de calcul et S(t).

![Fenêtre de comparaison](images/comparaison.png)

### 4.8 Export CSV

*Exporter CSV…* (panneau, fenêtre de comparaison ou menu *Simulation*) enregistre une ligne par recalcul :

```
temps_s;vehicules;aretes;degre_moyen;degre_max;interference_ms;
glouton_k;glouton_ms;glouton_stabilite;glouton_valide;welsh_powell_k;...;recherche_locale_valide
```

Format français : `;` entre colonnes, virgule décimale (ouverture directe dans Excel ou LibreOffice).
En Python : `pandas.read_csv("mesures.csv", sep=";", decimal=",")`.

### 4.9 Raccourcis clavier

| Touche | Action | Touche | Action |
|---|---|---|---|
| Ctrl+O | ouvrir une carte | Espace | démarrer / pause |
| R | recentrer | + / - | accélérer / ralentir |
| Z | choisir la zone | Ctrl+R | réinitialiser |
| Suppr | effacer la zone | E | arêtes d'interférence |
| I | itinéraire | G | intersections |
| 2 | carte 2D | 3 | vue 3D |
| F | vue 3D : suivre le véhicule choisi | Échap | annuler le tracé de zone (2D), le choix du véhicule (3D) |
| Ctrl+T | thème clair / sombre | | |

### 4.10 Vue 3D

![Véhicule choisi dans la vue 3D](images/vue3d_voisins.jpg)

La vue 3D montre la même simulation que la carte 2D, en perspective :

- **bâtiments** d'OpenStreetMap en volume (blanc « maquette »), routes en rubans gris au sol, grille de 100 m ;
- **véhicules** en petites voitures tournées dans leur sens de marche, de la couleur de leur canal (même palette
  que la carte 2D et la légende). Vue de loin, une voiture garde une taille minimale à l'écran pour rester visible ;
- **arêtes d'interférence** (touche E) en arcs au-dessus des toits ;
- **zone de simulation** : contour bleu au sol (elle se trace sur la carte 2D, touche Z).

![Arêtes d'interférence en arcs au-dessus de la ville](images/vue3d_aretes.jpg)

| Geste | Effet |
|---|---|
| Glisser (clic gauche) | déplacer : le point du sol saisi suit la souris |
| Clic droit + glisser (ou Ctrl + clic gauche, ou molette enfoncée) | tourner autour du centre, incliner le regard |
| Molette | zoom vers le point sous la souris |
| Clic sur un véhicule | le choisir : bulle et disque de sa portée R, liens colorés vers ses voisins (les autres véhicules s'estompent), fiche en haut à gauche (numéro, canal, nombre de voisins, vitesse) |
| Double-clic sur un véhicule, ou F | la caméra le suit ; glisser reprend la main |
| Échap, ou clic dans le vide | annuler le choix |
| R | vue d'ensemble de la carte (ou de la zone) |

La boussole en haut à droite indique le nord. Quand la caméra descend dans les rues, les bâtiments situés entre
elle et le point visé sont effacés (comme une maquette coupée) pour que le véhicule suivi reste visible.

![Caméra qui suit un véhicule](images/vue3d_rue.jpg)

## 5. Architecture du code

### 5.1 Modules

Le code suit les cinq étapes du sujet ; chaque module est une petite bibliothèque CMake, testée seule.

| Module (dossier) | Bibliothèque | Classes / fonctions principales | Dépend de |
|---|---|---|---|
| Carte (`src/map`) | `v2v-carte` | `OsmLoader::charger`, `OsmCarte`, `OsmRoute`, `OsmNoeud` | Qt Core, Xml |
| Graphe routier (`src/roadgraph`) | `v2v-graphe` | `GrapheRoutier`, `Sommet`, `Troncon`, `distanceHaversine` | carte |
| Simulation (`src/simulation`) | `v2v-simulation` | `Simulation`, `Vehicule`, `ParametresSimulation` | graphe routier |
| Interférences (`src/interference`) | `v2v-interference` | `GrapheInterference`, `StatsInterference` | Qt Core |
| Coloration (`src/coloring`) | `v2v-coloration` | `Coloration::colorer`, `estValide`, `stabilite` | Qt Core |
| Bâtiments (`src/vue3d/Batiments`) | `v2v-batiments` | `Batiments::charger`, `trianguler`, `construireMaillage` | carte |
| Interface (`src/ui`, `src/map/MapView`, `src/vue3d/Vue3D`) | application | `MainWindow`, `MoteurSimulation`, `FenetreComparaison`, `Theme` (style, thème clair/sombre, icônes), `MapView`, `Vue3D` | tous, Qt Widgets, Charts, OpenGL |

Les modules de calcul n'utilisent pas Qt Widgets : ils peuvent tourner sans interface (tests, outil d'étude).

### 5.2 Fils d'exécution

Le cahier des charges demande que la simulation et la coloration soient séparées de l'affichage.

```
 Fil de l'interface (MainWindow)                 Fil de simulation (MoteurSimulation)
 ──────────────────────────────                 ─────────────────────────────────────
 boutons, réglages ──── invokeMethod ─────────► demarrer / setEnMarche / setRayon...
                       (file d'attente)          toutes les 40 ms :
                                                   Simulation::avancer(0,04 s × facteur)
                                                   chaque seconde simulée :
                                                     GrapheInterference::construire
                                                     4 × Coloration::colorer
 MapView::afficherVehicules ◄── signal ─────────  nouvelInstantane (positions, couleurs, caps,
 Vue3D::afficherVehicules                           vitesses, arêtes)
 FenetreComparaison         ◄── signal ─────────  nouvelleMesure (k, temps, stabilité...)
```

- Le moteur travaille sur **sa propre copie** du graphe routier : aucune donnée n'est partagée entre les deux fils.
- **Contre-pression** : tant que l'interface n'a pas dessiné l'image précédente, le moteur n'en envoie pas d'autre
  (sinon les images s'accumuleraient en mémoire si l'affichage prenait du retard).
- **Numéro de génération** : chaque (re)démarrage reçoit un nouveau numéro ; les messages d'une ancienne
  simulation encore en route sont reconnus et ignorés.
- Pas de temps **fixe** (0,04 s × facteur) : la simulation est reproductible quelle que soit la machine.

### 5.3 Affichage de la carte

`MapView` (une `QGraphicsView`) dessine tout en **mètres** (projection équirectangulaire autour du centre de la
carte, `OsmCarte::versMetres`). Pour rester fluide :

- toutes les routes sont **un seul objet graphique** (`CoucheRoutes`) avec un cache image : ~3 ms par image
  au lieu de ~700 ms avec un grand chemin unique ;
- tous les véhicules et arêtes sont **un seul objet** (`CoucheVehicules`) ; chaque véhicule est la copie d'une
  petite image de pastille préparée une fois par couleur : ~1 ms pour 2000 véhicules au lieu de ~20 ms ;
- seuls les éléments visibles à l'écran sont dessinés.

### 5.4 Vue 3D (OpenGL)

`Vue3D` est un `QOpenGLWidget` : le dessin est fait par la carte graphique, avec des **shaders** (petits
programmes GLSL exécutés par la carte graphique, dans `src/vue3d/Shaders3D.h`). `MainWindow` place la carte 2D
et la vue 3D dans une pile (`QStackedWidget`) et envoie les mêmes données aux deux ; seule la vue affichée se
redessine. Repère 3D : x = est, y = hauteur, z = sud, en mètres ; un point (x, y) de la carte 2D est le point
(x, 0, y) au sol.

Ordre de dessin d'une image (`Vue3D::paintGL`) :

1. **ciel et sol** : un triangle qui couvre l'écran ; pour chaque pixel, le shader calcule la direction du regard,
   dessine le dégradé du ciel ou, vers le bas, le point du sol touché et la grille de 100 m ;
2. **à plat sur le sol**, sans tampon de profondeur et dans l'ordre (petites rues, puis grandes routes, zone,
   disque de portée) : deux surfaces à la même hauteur ne « clignotent » pas l'une à travers l'autre ;
3. **objets en relief** avec le tampon de profondeur (un objet proche cache un objet lointain) : les bâtiments
   (1,3 million de triangles envoyés une fois au chargement), puis toutes les voitures en **un seul appel**
   (instanciation : la forme de voiture est envoyée une fois, puis à chaque image 8 nombres par véhicule :
   position, cap, taille minimale, couleur) ;
4. **transparents** : arcs des arêtes (un ruban de 16 morceaux par arête, épaisseur fixe en pixels calculée par
   le shader, car OpenGL ne trace pas de lignes épaisses partout), bulle de portée ;
5. **textes** (fiche du véhicule, aide, boussole) avec `QPainter` par-dessus l'image 3D.

Autres choix :

- **taille minimale à l'écran** : de loin, une rue de 6 m ou une voiture de 4,4 m ferait moins d'un pixel ; les
  shaders les élargissent pour garder la même largeur à l'écran que sur la carte 2D ;
- **brouillard** : les objets lointains prennent la couleur de l'horizon (profondeur, bord de carte invisible) ;
- **éclairage** : soleil au sud-ouest ; la normale des murs est recalculée par pixel (`dFdx`, `dFdy`), ce qui évite
  de la stocker pour chaque sommet ; pied des murs assombri pour ancrer les bâtiments au sol ;
- **choix d'un véhicule** : au clic, la vue dessine une image jamais affichée où chaque voiture a une couleur qui
  code son numéro, avec les bâtiments devant elle ; la couleur lue sous la souris (à 14 pixels près) donne la
  voiture visible, jamais une voiture cachée derrière un bâtiment ou une autre voiture. Ses voisins sont lus
  dans la liste des arêtes ;
- **maquette coupée** : près du sol, les bâtiments situés dans un couloir entre la caméra et le point visé sont
  effacés par le shader ; le point visé (le véhicule suivi) reste toujours visible ;
- **compatibilité** : OpenGL 3.3 « core » (fonctionne sous Windows, Linux et macOS) ; le format est réglé dans
  `main.cpp` avant la création de l'application, comme l'exige macOS.

## 6. Algorithmes

### 6.1 Lecture de la carte

`QXmlStreamReader` lit le fichier en un passage. Sont gardées les voies `highway` = motorway, trunk, primary,
secondary, tertiary (et leurs bretelles `_link`), unclassified, residential, living_street ; sont exclues les voies
`access=no/private` et `area=yes`. Le tag `oneway` donne le sens (`-1` : la liste des nœuds est inversée) ;
autoroutes et ronds-points sont à sens unique par défaut. Mulhouse : 5 002 routes, 25 605 nœuds, ~120 ms.

### 6.2 Graphe routier

- Un nœud devient sommet s'il est partagé par plusieurs routes ou s'il termine une route.
- Chaque route est découpée en tronçons entre sommets ; un tronçon garde tous ses points (pour le dessin et le
  déplacement) et sa longueur = somme des distances de **Haversine** entre points consécutifs.
- Double sens = deux arcs, sens unique = un arc.
- On ne garde que la **plus grande composante fortement connexe** (algorithme de Kosaraju, sans récursion) :
  depuis n'importe quelle intersection on peut atteindre toutes les autres, donc toute destination tirée au
  hasard est atteignable.
- **Dijkstra** avec tas binaire ; **A\*** ajoute la distance à vol d'oiseau jusqu'à l'arrivée (× 0,99 pour rester
  une borne inférieure malgré la projection). Mulhouse : A* ~0,10 ms par trajet contre ~0,27 ms pour Dijkstra,
  même longueur sur 500 trajets tirés au hasard.

### 6.3 Déplacement des véhicules

À chaque pas, un véhicule avance le long de la polyligne de son tronçon. En fin de tronçon, le **temps** restant
(et non la distance) est reporté sur le tronçon suivant, car la vitesse peut changer. En fin de trajet, nouvelle
destination et nouveau calcul A*. Un pas pour 2000 véhicules : ~0,25 ms.

### 6.4 Graphe d'interférences et grille spatiale

Arête entre A et B si `distance(A, B) < max(rA, rB)`. Comparer toutes les paires coûte n²/2 distances
(2 millions pour 2000 véhicules). La **grille spatiale** découpe la carte en cases de côté R (la plus grande portée) :
deux véhicules en interférence sont forcément dans la même case ou dans deux cases voisines, on ne compare donc
qu'avec les 9 cases autour. Mesuré : ~1,5 ms pour 2000 véhicules contre ~4 ms en comparant toutes les paires ;
~6 ms contre ~24 ms pour 5000.

### 6.5 Coloration

| Algorithme | Principe | Coût |
|---|---|---|
| Glouton | sommets dans l'ordre 0, 1, 2... ; chacun prend la plus petite couleur libre parmi ses voisins déjà colorés | O(n + m) |
| Welsh–Powell | idem, mais sommets triés par degré décroissant ; on remplit une couleur à la fois | O(n log n + n·k + m) |
| DSATUR (Brélaz) | on colore à chaque fois le sommet dont les voisins utilisent le plus de couleurs différentes (saturation), à égalité le plus grand degré | O((n + m) log n + n·Δ/64), Δ = degré maximal (masque de bits des couleurs voisines) |
| Recherche locale (TabuCol) | part de DSATUR (k couleurs) et cherche une coloration à k − 1 couleurs en déplaçant des sommets en conflit, avec une liste tabou qui interdit de revenir en arrière ; budget de 20 000 déplacements | borné par le budget |

- **Validité** : `Coloration::estValide` vérifie à chaque recalcul qu'aucune arête ne relie deux sommets de même
  couleur (et compte les conflits éventuels).
- **Stabilité** : `S(t) = 1 − (véhicules dont le canal a changé) / N`, entre deux recalculs du même algorithme.
- **Borne inférieure** (outil d'étude) : la taille d'une grande clique ; si k = cette taille, la coloration est optimale.

Les résultats chiffrés et leur analyse sont dans l'[étude comparative](etude.md).

### 6.6 Bâtiments en volume

- **Lecture** : le fichier compact est lu en une fois et parcouru caractère par caractère (sans découpage en
  mots, 1,1 million de nombres) ; chaque point est converti en mètres par `OsmCarte::versMetres`, la même
  projection que les routes. 73 878 bâtiments en ~45 ms.
- **Murs** : pour un contour de n points, n points au sol et n au niveau du toit, et deux triangles par côté.
- **Toit** : le contour (souvent non convexe : en L, en U...) est découpé en triangles par la méthode des
  **oreilles** (*ear clipping*) : on cherche un coin convexe dont le triangle ne contient aucun autre point du
  contour, on le retire en gardant ce triangle, et on recommence. Le signe de l'aire du contour donne son sens
  de parcours, donc quels coins sont convexes. Points alignés : retirés sans triangle. 99,9 % des toits sont
  complets (les autres contours se croisent eux-mêmes dans OSM).
- Total : 1,3 million de triangles construits en ~55 ms au chargement.

## 7. Tests et performances

```bash
ctest --test-dir build --output-on-failure
```

| Test | Vérifie |
|---|---|
| `test_osm_loader` | routes gardées/exclues, sens uniques, nœuds manquants, XML invalide, fichier absent |
| `test_road_graph` | découpage, longueurs Haversine, composante fortement connexe, zone, Dijkstra = A* |
| `test_simulation` | distance parcourue = vitesse × temps, positions toujours sur une route, arrivées, reproductibilité, grands pas |
| `test_interference` | grille = toutes les paires (positions et portées aléatoires, coordonnées négatives, bords de cases) |
| `test_coloring` | graphes connus (K5, cycles pair/impair, K3,3, étoile, Petersen, couronne), biparti aléatoire (DSATUR exact), validité, stabilité, déterminisme |
| `test_batiments` | lecture (commentaires, fins de ligne Windows, lignes invalides), découpage des toits (carré, L, U, points alignés ou répétés, 1000 polygones en étoile : surface conservée, aucun triangle retourné), volumes (nombre de sommets et de triangles, hauteurs) |

Certains tests acceptent un fichier `.osm` (ou le dossier `data`) en argument pour mesurer les performances sur la
vraie carte : `build\test_road_graph.exe data\mulhouse.osm`, `build\test_simulation.exe data\mulhouse.osm`,
`build\test_batiments.exe data`.

L'interface (2D et 3D) est vérifiée par un programme de test qui crée la fenêtre sans l'afficher et lui envoie des
clics, des glissers, des crans de molette et des touches : choix et suivi d'un véhicule, rotation, passage 2D/3D,
zone, relance à 5000 véhicules, cadrage de la carte 2D.

Performances mesurées (Mulhouse, Release) :

| Mesure | Valeur |
|---|---|
| Chargement de la carte | ~120 ms |
| Construction du graphe routier | ~16 ms |
| Un pas de simulation, 2000 véhicules | ~0,25 ms |
| Graphe d'interférences, 2000 véhicules (grille) | ~1,5 ms |
| Coloration (Glouton / Welsh–Powell / DSATUR / recherche locale) | ~0,1 / 0,25 / 2,3 / 5,7 ms |
| Une image affichée, 2000 véhicules + arêtes | ~10 ms (25 images/s) |
| 5000 véhicules | 25 images/s (22 avec les arêtes) |
| Lecture des bâtiments + construction des volumes | ~100 ms |
| Une image 3D, vue d'ensemble, 2000 véhicules + bâtiments (NVIDIA GTX 1650 / Intel UHD) | ~7 ms / ~14 ms |
| Une image 3D, 5000 véhicules + arêtes (NVIDIA GTX 1650 / Intel UHD) | ~9 ms / ~22 ms |

Les temps 3D comprennent la lecture de l'image par le programme de test (quelques ms) : même la carte intégrée
reste sous les 40 ms d'une image à 25 images/s.

## 8. Guide du développeur

### 8.1 Conventions

- C++17, noms et commentaires **en français**, documentation des fonctions en Doxygen (`///`).
- Accolade sur la même ligne pour `if`/`for`/`while`, sur sa propre ligne pour les fonctions.
- Les modules de calcul (`map/OsmLoader`, `roadgraph`, `simulation`, `interference`, `coloring`) ne dépendent pas de
  Qt Widgets.
- Toute nouvelle fonctionnalité de calcul vient avec un test dans `tests/` (macro `CHECK`, sans framework).

### 8.2 Ajouter un algorithme de coloration

1. `src/coloring/Coloring.h` : ajouter la valeur à `enum class Algo`, augmenter `NOMBRE_ALGOS`, compléter
   `TOUS_LES_ALGOS`.
2. `src/coloring/Coloring.cpp` : compléter `nom`, `nomCourt` et `colorer`.
3. `tests/test_coloring.cpp` : l'ajouter aux graphes connus.
4. Rien d'autre : le moteur, le panneau, la fenêtre de comparaison, l'export CSV et l'outil d'étude parcourent
   `TOUS_LES_ALGOS` ; seule `FenetreComparaison::couleurAlgo` attend une couleur pour la courbe.

### 8.3 Ajouter un réglage de simulation

1. Ajouter le champ à `ParametresSimulation` (`src/simulation/Simulation.h`) et l'utiliser dans `Simulation`.
2. Ajouter le contrôle dans `MainWindow::creerPanneau` et le recopier dans `MainWindow::lancerSimulation`.

### 8.4 Outil d'étude

`build\etude_coloration.exe data\mulhouse.osm > resultats.csv` rejoue, sans interface, de nombreux scénarios
(nombre de véhicules × portée × graine) et écrit une ligne CSV par recalcul ; un résumé s'affiche à la fin.
`--rapide` pour un essai court. Voir l'[étude comparative](etude.md).

## 9. Limites connues

- Pas de simulation physique de la radio (propagation, obstacles), ni de trafic microscopique (feux, priorités,
  files d'attente) : exclus par le cahier des charges. Des véhicules peuvent donc se superposer.
- La projection équirectangulaire est précise pour une ville (erreur < 0,1 %), pas pour une région entière.
- Au-delà de 100 000 arêtes visibles, l'affichage des arêtes est tronqué (l'image serait illisible) ; les calculs
  utilisent toujours le graphe complet.
- La stabilité compare les numéros de canaux tels quels : deux colorations identiques à un renommage près des
  couleurs comptent comme différentes (c'est la définition du sujet).
- Vue 3D : environ 90 % des bâtiments n'ont pas de hauteur dans OpenStreetMap ; elle est estimée (type de
  bâtiment, surface au sol). Les cours intérieures des grands bâtiments « multipolygones » sont remplies. Toits
  plats, pas d'ombres portées, pas de relief du terrain. Les bâtiments n'arrêtent pas les ondes radio (comme en 2D,
  la portée est un simple cercle de rayon R).
