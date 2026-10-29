# Recette de compilation du projet, lue par CMake.
# Elle dit quels fichiers .cpp compiler et quels modules Qt utiliser.
# Compiler         : cmake -S . -B build -G Ninja   puis   cmake --build build
# Lancer les tests : ctest --test-dir build --output-on-failure

# Version minimale de CMake requise (Qt 6 en a besoin).
cmake_minimum_required(VERSION 3.16)
project(SimulateurV2V VERSION 0.1 LANGUAGES CXX)

# On impose le C++17 (décision d'équipe) pour que tout le monde compile pareil.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Modules Qt utilisés :
#   Widgets : fenêtres, boutons, et QGraphicsView pour dessiner la carte
#   Charts  : courbes de comparaison Glouton / Welsh-Powell (étape 5)
#   Xml     : QXmlStreamReader pour lire le fichier .osm (étape 1)
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Xml)

# Réglages Qt par défaut (active notamment le moc, qui gère Q_OBJECT / signaux-slots).
qt_standard_project_setup()

# ---- Modules du projet (sans interface graphique) ----
# Une petite bibliothèque par module, comme demandé par le cahier des charges
# (carte, graphe routier, simulation, graphe d'interférences, coloration).
# Intérêt : chaque test ne compile que le module qu'il vérifie, et un module en
# cours d'écriture ne casse pas la compilation des autres.

# Étape 1 : données de la carte et lecture du fichier .osm
add_library(v2v-carte STATIC src/map/OsmData.h src/map/OsmLoader.h src/map/OsmLoader.cpp)
# PUBLIC : tout ce qui utilise ce module peut écrire #include "map/OsmData.h".
target_include_directories(v2v-carte PUBLIC src)
target_link_libraries(v2v-carte PUBLIC Qt6::Core Qt6::Gui Qt6::Xml)

# Étape 2 : graphe routier orienté et plus court chemin
add_library(v2v-graphe STATIC src/roadgraph/RoadGraph.h src/roadgraph/RoadGraph.cpp)
target_link_libraries(v2v-graphe PUBLIC v2v-carte)

# Étape 3 : déplacement des véhicules sur le graphe routier
add_library(v2v-simulation STATIC src/simulation/Simulation.h src/simulation/Simulation.cpp)
target_link_libraries(v2v-simulation PUBLIC v2v-graphe)

# Étape 4 : graphe d'interférences (grille spatiale)
add_library(v2v-interference STATIC src/interference/InterferenceGraph.h src/interference/InterferenceGraph.cpp)
target_include_directories(v2v-interference PUBLIC src)
target_link_libraries(v2v-interference PUBLIC Qt6::Core)

# Étape 5 : algorithmes de coloration
add_library(v2v-coloration STATIC src/coloring/Coloring.h src/coloring/Coloring.cpp)
target_include_directories(v2v-coloration PUBLIC src)
target_link_libraries(v2v-coloration PUBLIC Qt6::Core)

# ---- Application ----
# WIN32 = pas de console noire sous Windows ; MACOSX_BUNDLE = vraie appli sous macOS.
# Chaque nouveau fichier .h/.cpp doit être ajouté ici (ou dans un module), sinon il n'est pas compilé.
qt_add_executable(v2v-simulator WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    # Fil d'exécution de la simulation (simulation + interférences + coloration)
    src/ui/MoteurSimulation.h
    src/ui/MoteurSimulation.cpp
    # Fenêtre de comparaison des algorithmes (tableau + courbes)
    src/ui/FenetreComparaison.h
    src/ui/FenetreComparaison.cpp
    # Affichage de la carte (zoom, déplacement, zone, itinéraire, véhicules, arêtes)
    src/map/MapView.h
    src/map/MapView.cpp
)
target_link_libraries(v2v-simulator PRIVATE
    v2v-carte v2v-graphe v2v-simulation v2v-interference v2v-coloration
    Qt6::Widgets Qt6::Charts)

# Chemin du dossier data/ du projet, connu à la compilation : permet d'ouvrir
# data/mulhouse.osm automatiquement au lancement, sur le PC de chacun.
target_compile_definitions(v2v-simulator PRIVATE V2V_DOSSIER_DONNEES="${CMAKE_SOURCE_DIR}/data")

# ---- Tests ----
# Un petit programme par module ; ctest les lance tous et signale un échec
# dès que l'un d'eux renvoie autre chose que 0.
enable_testing()

# Lecteur OSM sur un fichier .osm écrit en dur.
add_executable(test_osm_loader tests/test_osm_loader.cpp)
target_link_libraries(test_osm_loader PRIVATE v2v-carte)
add_test(NAME test_osm_loader COMMAND test_osm_loader)

# Graphe routier (sens uniques, longueurs, composante connexe) ; Dijkstra = A*.
add_executable(test_road_graph tests/test_road_graph.cpp)
target_link_libraries(test_road_graph PRIVATE v2v-graphe)
add_test(NAME test_road_graph COMMAND test_road_graph)

# Simulation : déplacement le long des tronçons, arrivées, reproductibilité.
add_executable(test_simulation tests/test_simulation.cpp)
target_link_libraries(test_simulation PRIVATE v2v-simulation)
add_test(NAME test_simulation COMMAND test_simulation)

# Graphe d'interférences : la grille donne exactement les mêmes arêtes que la comparaison de toutes les paires.
add_executable(test_interference tests/test_interference.cpp)
target_link_libraries(test_interference PRIVATE v2v-interference)
add_test(NAME test_interference COMMAND test_interference)

# Coloration sur des graphes connus (complet, cycles, biparti...) + validité automatique.
add_executable(test_coloring tests/test_coloring.cpp)
target_link_libraries(test_coloring PRIVATE v2v-coloration)
add_test(NAME test_coloring COMMAND test_coloring)

# ---- Étude comparative (hors application) ----
# Programme en ligne de commande qui rejoue de nombreux scénarios (nombre de véhicules,
# portée, graines) sans interface et écrit les résultats en CSV : sert à l'étude chiffrée
# demandée par le cahier des charges (voir docs/etude.md).
#   build/etude_coloration data/mulhouse.osm > docs/resultats/etude.csv
add_executable(etude_coloration outils/etude_coloration.cpp)
target_link_libraries(etude_coloration PRIVATE v2v-simulation v2v-interference v2v-coloration)
