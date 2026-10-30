#pragma once

/// @file Theme.h
/// @brief Apparence de l'application : style, couleurs (thème clair ou sombre) et icônes.
///
/// Qt dessine les boutons, onglets et tableaux avec un « style ». On utilise Fusion, le style
/// moderne fourni avec Qt : il est identique sous Windows, macOS et Linux, donc l'application
/// a la même allure chez tous les membres de l'équipe. On lui donne une palette de couleurs
/// (claire ou sombre) et une courte feuille de style (coins arrondis, espacements, onglets).
///
/// Les icônes sont dessinées par le programme (QPainter) au lieu d'être lues dans des
/// fichiers image : rien à fournir avec l'exécutable, elles restent nettes à toute taille
/// d'écran, et elles prennent la couleur du texte du thème (claires en thème sombre).
///
/// Le thème ne touche qu'à l'interface (barres, panneau, fenêtre de comparaison) : la carte 2D
/// et la vue 3D gardent leurs couleurs, choisies pour que les canaux radio restent lisibles.

#include <QColor>
#include <QIcon>

namespace Theme
{
/// Applique à toute l'application le style Fusion, la palette claire (sombre = false) ou
/// sombre, et la feuille de style. Peut être rappelée à tout moment pour changer de thème.
void appliquer(bool sombre);
bool estSombre();

/// Couleur d'accent du thème actuel, pour mettre un texte en valeur (par exemple le plus
/// petit nombre de canaux dans le tableau des résultats) : lisible sur le fond du thème.
QColor accent();

/// Icônes de l'interface.
enum class Icone
{
    Ouvrir,
    Carte2D,
    Vue3D,
    Recentrer,
    Zone,
    EffacerZone,
    Itineraire,
    Intersections,
    Aretes,
    Comparaison,
    Lecture,
    Pause,
    Ralentir,
    Accelerer,
    Reinitialiser
};

/// Icône dessinée dans la couleur du texte du thème actuel : à redemander après un
/// changement de thème (voir MainWindow::majIcones).
QIcon icone(Icone type);
}
