# Recette de compilation du projet, lue par CMake.
# Elle dit quels fichiers .cpp compiler et quels modules Qt utiliser.
# Compiler : cmake -S . -B build -G Ninja   puis   cmake --build build

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

# L'exécutable. WIN32 = pas de console noire sous Windows ; MACOSX_BUNDLE = vraie appli sous macOS.
# Chaque nouveau fichier .h/.cpp doit être ajouté à cette liste, sinon il n'est pas compilé.
qt_add_executable(v2v-simulator WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

# Permet d'écrire #include "ui/MainWindow.h" depuis n'importe quel dossier de src/.
target_include_directories(v2v-simulator PRIVATE src)
target_link_libraries(v2v-simulator PRIVATE Qt6::Widgets Qt6::Charts Qt6::Xml)
