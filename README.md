# Simulateur V2V — coloration de graphes (version 3D)

Application C++ qui simule des communications véhicule à véhicule (V2V) sur le vrai réseau routier de **Mulhouse**.
Elle charge une carte OpenStreetMap, fait rouler **2000 véhicules** (ou plus) sur les routes, construit le
**graphe d'interférences** entre véhicules proches et attribue un **canal radio** à chacun par **coloration de graphe**.
Quatre algorithmes sont comparés en direct : Glouton, Welsh–Powell, DSATUR et une recherche locale (tabou).

Cette version ajoute une **vue 3D** de la ville : les 74 000 bâtiments de Mulhouse en volume, chaque véhicule en
petite voiture de la couleur de son canal, les arêtes d'interférence en arcs au-dessus des toits, et pour le
véhicule choisi à la souris sa portée radio (bulle) et ses liens vers ses voisins. La carte 2D reste disponible
(touche **2**).

> **Deux versions séparées** : la version 3D est sur la branche `version-3d` ; la version 2D seule, inchangée,
> est sur la branche `setup/squelette`. Elles partagent les mêmes calculs (simulation, interférences, coloration).

> Master 1 Informatique et Mobilité — UE Réseaux Mobiles, Université de Haute-Alsace, automne 2026.
> Encadrant : Laurent Moalic.

![Vue 3D : 2000 véhicules colorés par DSATUR dans Mulhouse](docs/images/vue3d.jpg)

## Fonctionnalités

Les cinq étapes du sujet sont réalisées :

| Étape | Ce que fait l'application |
|---|---|
| 1. Carte | Lecture d'un fichier `.osm`, affichage des routes, zoom à la molette, déplacement au clic-glisser, sélection d'une zone de simulation, échelle graphique, coordonnées GPS sous la souris |
| 2. Graphe routier | Sommets = intersections, arcs orientés = tronçons (sens uniques respectés), longueurs en mètres (Haversine), plus court chemin Dijkstra et A*, outil « Itinéraire » pour le vérifier sur la carte |
| 3. Simulation | 2000 véhicules (position, vitesse, direction, destination, portée radio), plus court chemin vers une destination aléatoire, vitesse selon le type de route ; démarrer, pause, accélérer, ralentir, réinitialiser ; simulation dans un fil séparé de l'affichage (25 images/s) |
| 4. Graphe d'interférences | Arête si distance < max(rA, rB), grille spatiale de taille R, recalcul périodique, arêtes affichables, nombre d'arêtes et degrés moyen/maximal |
| 5. Coloration et analyse | Glouton, Welsh–Powell, DSATUR, recherche locale ; vérification automatique de la validité ; nombre de canaux k(t), temps de calcul, stabilité S(t) ; fenêtre de comparaison (tableau + courbes) ; export CSV |
| Vue 3D | Ville en perspective avec ses bâtiments (OpenStreetMap), voitures 3D orientées dans leur sens de marche et colorées par canal, arêtes en arcs, caméra libre (tourner, incliner, zoomer), véhicule choisi : bulle de portée R, liens vers ses voisins, fiche (canal, nombre de voisins, vitesse), caméra qui le suit |

![Véhicule choisi : sa portée radio et ses 30 voisins, chacun sur un autre canal](docs/images/vue3d_voisins.jpg)

![Arêtes du graphe d'interférences en arcs au-dessus de la ville (touche E)](docs/images/vue3d_aretes.jpg)

![Caméra qui suit un véhicule dans la rue](docs/images/vue3d_rue.jpg)

La même simulation en carte 2D (touche **2**) et la fenêtre de comparaison des algorithmes :

![Carte 2D : 2000 véhicules colorés par DSATUR, arêtes d'interférence affichées](docs/images/simulation.png)

![Fenêtre de comparaison des algorithmes](docs/images/comparaison.png)

Thème sombre (**Ctrl+T**), onglet *Coloration* du panneau : le tableau compare les 4 algorithmes au dernier recalcul.

![Thème sombre et tableau des résultats de coloration](docs/images/interface_sombre.jpg)

## Prérequis

- Un compilateur **C++17** : MinGW 13 (fourni avec Qt sous Windows), GCC, Clang ou MSVC
- **CMake 3.20** ou plus récent (celui fourni avec Qt convient)
- **Qt 6** (testé avec Qt 6.8.3) avec les modules **Widgets**, **Charts**, **Xml**, **OpenGL** et
  **OpenGLWidgets** (ces deux derniers sont fournis avec Qt, rien à cocher en plus)
- Pour la vue 3D : une carte graphique compatible **OpenGL 3.3** (tous les PC depuis 2012 environ ; testé sur
  Intel UHD et NVIDIA GTX 1650)

Installer Qt sous Windows sans compte, en ligne de commande (dossier `C:\Qt`) :

```bash
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_mingw -m qtcharts -O C:\Qt
python -m aqt install-tool windows desktop tools_mingw1310 -O C:\Qt
python -m aqt install-tool windows desktop tools_cmake -O C:\Qt
python -m aqt install-tool windows desktop tools_ninja -O C:\Qt
```

(On peut aussi utiliser l'installateur officiel de Qt et cocher Qt 6.8, MinGW et le module Qt Charts.)

## Compilation

Sous Windows (PowerShell), après avoir mis les outils de Qt dans le PATH de la session :

```bash
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.8.3\mingw_64\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Sous Linux / macOS : `cmake -S . -B build -DCMAKE_PREFIX_PATH=<dossier de Qt 6>` puis `cmake --build build`.

## Lancement

```bash
build\v2v-simulator.exe
```

La carte de Mulhouse (`data/mulhouse.osm`) et ses bâtiments (`data/mulhouse_batiments.txt`) s'ouvrent
automatiquement, en vue 3D. Une autre carte : `v2v-simulator <fichier.osm>` ou **Fichier > Ouvrir une carte…**.

Pour lancer l'exécutable par double-clic sous Windows, copier une fois les bibliothèques de Qt à côté :

```bash
C:\Qt\6.8.3\mingw_64\bin\windeployqt.exe --release build\v2v-simulator.exe
```

## Utilisation rapide

1. **Espace** : démarrer / mettre en pause. **+** / **-** : accélérer / ralentir (×0,25 à ×32).
2. **3** : vue 3D ; **2** : carte 2D.
3. Vue 3D : glisser = déplacer ; clic droit + glisser = tourner et incliner ; molette = zoom ;
   clic sur un véhicule = sa portée et ses voisins ; double-clic (ou **F**) = le suivre ; **Échap** = annuler.
4. **Z** puis clic-glisser : choisir une zone de simulation (sur la carte 2D). **Suppr** : revenir à toute la carte.
5. Panneau de droite, en 3 onglets : **Simulation** (nombre de véhicules, portée R, intervalle de recalcul, graine),
   **Coloration** (tableau des 4 algorithmes ; cliquer une ligne affiche ses couleurs), **Carte** (zone, graphe, itinéraire).
6. **E** : afficher les arêtes d'interférence. **G** : afficher les intersections. **I** : tester un plus court chemin.
7. **Comparaison** : tableau et courbes des 4 algorithmes ; **Exporter CSV…** pour les mesures.
8. **Ctrl+T** (menu Affichage) : thème sombre.

Le détail est dans le [manuel utilisateur et développeur](docs/manuel.md).

## Tests

```bash
ctest --test-dir build --output-on-failure
```

| Test | Ce qu'il vérifie |
|---|---|
| `test_osm_loader` | lecture du `.osm` : routes gardées ou exclues, sens uniques, nœuds manquants, erreurs |
| `test_road_graph` | découpage en tronçons, longueurs, composante fortement connexe, zone ; Dijkstra = A* sur 500 trajets |
| `test_simulation` | déplacement le long des tronçons, arrivées, grands pas de temps, reproductibilité |
| `test_interference` | la grille spatiale donne exactement les mêmes arêtes que la comparaison de toutes les paires |
| `test_coloring` | les 4 algorithmes sur des graphes connus (complet, cycles, biparti, Petersen...) et validité |
| `test_batiments` | lecture du fichier de bâtiments, découpage des toits en triangles (surface conservée), volumes des bâtiments |

## Résultats

Sur Mulhouse, 2000 véhicules, portée 300 m, 5 graines × 60 s simulées
(extrait de l'[étude comparative](docs/etude.md) : 9 configurations, 2 745 recalculs) :

| Algorithme | Canaux k | Prouvé optimal | Temps de calcul | Stabilité S |
|---|---:|---:|---:|---:|
| Glouton | 25,1 | 10 % | 0,09 ms | **0,77** |
| Welsh–Powell | 23,6 | 63 % | 0,25 ms | 0,37 |
| DSATUR | 23,3 | 86 % | 2,3 ms | 0,31 |
| Recherche locale | **23,2** | **91 %** | 5,7 ms | 0,31 |

100 % des colorations sont valides. « Prouvé optimal » : k est égal à une borne inférieure (taille d'une clique).
DSATUR et la recherche locale utilisent le moins de canaux ; le Glouton est deux fois plus stable : il faut choisir
entre économiser des canaux et éviter les changements de canal.

## Structure du projet

```
src/
  map/           lecture OSM (OsmLoader), données de la carte (OsmData), affichage (MapView)
  roadgraph/     graphe routier orienté, Dijkstra et A* (RoadGraph)
  simulation/    déplacement des véhicules (Simulation)
  interference/  graphe d'interférences avec grille spatiale (InterferenceGraph)
  coloring/      Glouton, Welsh-Powell, DSATUR, recherche locale (Coloring)
  ui/            fenêtre principale, moteur de simulation (fil séparé), fenêtre de comparaison
  vue3d/         vue 3D OpenGL (Vue3D, Shaders3D), bâtiments en volume (Batiments)
tests/           un test automatique par module (ctest)
outils/          etude_coloration : étude comparative en ligne de commande
data/            mulhouse.osm, mulhouse_batiments.txt et les scripts qui les téléchargent
docs/            manuel, étude comparative, captures d'écran
```

## Données

`data/mulhouse.osm` contient les routes carrossables de Mulhouse et de sa proche banlieue (environ 8 km × 8 km),
extraites d'OpenStreetMap avec l'API Overpass par `data/telecharger_carte.py`.
`data/mulhouse_batiments.txt` contient les 73 878 bâtiments de la même zone (contour et hauteur), extraits par
`data/telecharger_batiments.py` dans un format texte compact (4,5 Mo au lieu d'environ 70 Mo en XML OSM).
Quand OSM ne donne pas la hauteur d'un bâtiment (cas de la plupart), elle est estimée d'après son type et sa surface.
Données © les contributeurs d'OpenStreetMap, disponibles sous licence ODbL.

## Équipe

Emir Faycal Benabdallah, Moundir Houazar, Samy Mohamed Daoudi, Salah Eddine Djemaoui.

## Références

- D. J. A. Welsh, M. B. Powell, *An upper bound for the chromatic number of a graph and its application to timetabling problems*, The Computer Journal, 1967.
- D. Brélaz, *New methods to color the vertices of a graph*, Communications of the ACM, 1979.
- A. Hertz, D. de Werra, *Using tabu search techniques for graph coloring*, Computing, 1987.
- OpenStreetMap : https://www.openstreetmap.org — Overpass API : https://overpass-api.de
- Documentation Qt : https://doc.qt.io
