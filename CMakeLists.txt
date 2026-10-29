# Recette de compilation du projet, lue par CMake.
# Elle dit quels fichiers .cpp compiler et quels modules Qt utiliser.
# Compiler       : cmake -S . -B build -G Ninja   puis   cmake --build build
# Lancer les tests : ctest --test-dir build --output-on-failure

# Version minimale de CMake requise (Qt 6 en a besoin).
cmake_minimum_required(VERSION 3.16)
project(SimulateurV2V VERSION 0.1 LANGUAGES CXX)

# On impose le C++17 (décision d'équipe) pour que tout le monde compile pareil.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Modules Qt utilisés :
#   Widgets : fenêtres, boutons, et QGraphicsView pour dessiner la carte
#   Charts  : courbes de comparaison Greedy / Welsh-Powell (étape 5)
#   Xml     : QXmlStreamReader pour lire le fichier .osm (étape 1)
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Xml)

# Réglages Qt par défaut (active notamment le moc, qui gère Q_OBJECT / signaux-slots).
qt_standard_project_setup()

# ---- Cœur du projet (sans interface graphique) ----
# Bibliothèque partagée entre l'application et les tests : le code n'est compilé
# qu'une fois, et les tests vérifient exactement le même code que l'application.
add_library(v2v-core STATIC
    # Étape 1 : données de la carte et lecture du fichier .osm
    src/map/OsmData.h
    src/map/OsmLoader.h
    src/map/OsmLoader.cpp
    # Étape 2 : graphe routier orienté et plus court chemin
    src/roadgraph/RoadGraph.h
    src/roadgraph/RoadGraph.cpp
)
# PUBLIC : tout ce qui utilise v2v-core peut écrire #include "map/OsmData.h".
target_include_directories(v2v-core PUBLIC src)
target_link_libraries(v2v-core PUBLIC Qt6::Core Qt6::Xml)

# ---- Application ----
# WIN32 = pas de console noire sous Windows ; MACOSX_BUNDLE = vraie appli sous macOS.
# Chaque nouveau fichier .h/.cpp doit être ajouté ici (ou dans v2v-core), sinon il n'est pas compilé.
qt_add_executable(v2v-simulator WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    # Étape 1 : affichage de la carte (zoom, déplacement, sélection de zone)
    src/map/MapView.h
    src/map/MapView.cpp
)
target_link_libraries(v2v-simulator PRIVATE v2v-core Qt6::Widgets Qt6::Charts)

# Chemin du dossier data/ du projet, connu à la compilation : permet d'ouvrir
# data/mulhouse.osm automatiquement au lancement, sur le PC de chacun.
target_compile_definitions(v2v-simulator PRIVATE V2V_DOSSIER_DONNEES="${CMAKE_SOURCE_DIR}/data")

# ---- Tests ----
# Petit programme qui vérifie le parseur OSM sur un fichier .osm écrit en dur.
# ctest le lance et signale un échec s'il renvoie autre chose que 0.
enable_testing()
add_executable(test_osm_loader tests/test_osm_loader.cpp)
target_link_libraries(test_osm_loader PRIVATE v2v-core)
add_test(NAME test_osm_loader COMMAND test_osm_loader)

# Vérifie le graphe routier (sens uniques, longueurs, composante connexe) et que
# Dijkstra et A* trouvent bien le même plus court chemin.
add_executable(test_road_graph tests/test_road_graph.cpp)
target_link_libraries(test_road_graph PRIVATE v2v-core)
add_test(NAME test_road_graph COMMAND test_road_graph)
