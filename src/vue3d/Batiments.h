#pragma once

/// @file Batiments.h
/// @brief Bâtiments de la ville pour la vue 3D : lecture du fichier et formes 3D.
///
/// Les bâtiments viennent d'OpenStreetMap. Le script data/telecharger_batiments.py les
/// prépare dans un fichier texte compact (data/mulhouse_batiments.txt, format décrit en
/// tête du script) : une ligne par bâtiment, avec sa hauteur et son contour au sol.
/// La vue 3D dessine chaque bâtiment comme un volume : des murs verticaux tout autour
/// du contour et un toit plat à la bonne hauteur.
///
/// Rien ici ne dépend d'OpenGL : on remplit de simples tableaux de nombres, que la vue 3D
/// envoie ensuite à la carte graphique. Tout se teste donc sans écran (tests/test_batiments.cpp).

#include "map/OsmData.h"

#include <QPointF>
#include <QString>
#include <QVector>

/// Un bâtiment : contour au sol et hauteur.
struct Batiment
{
    QVector<QPointF> contour;  ///< points du contour en mètres (repère de la scène), sans répéter le premier à la fin
    float hauteur = 0.0f;      ///< hauteur du toit, en mètres
};

/// Formes 3D de tous les bâtiments, prêtes à être envoyées à la carte graphique.
struct MaillageBatiments
{
    /// 4 nombres par sommet, à la suite : x, y, z, teinte.
    /// Repère 3D de toute la vue 3D : x = est, y = hauteur (vers le haut), z = sud, en mètres.
    /// Un point (x, y) de la scène 2D devient donc (x, 0, y) au sol : routes, véhicules
    /// et bâtiments partagent les mêmes coordonnées que la carte 2D.
    /// teinte : nombre entre 0 et 1 propre à chaque bâtiment (même valeur pour tous ses
    /// sommets) ; la vue 3D s'en sert pour varier très légèrement la couleur, afin de
    /// distinguer deux bâtiments collés l'un à l'autre.
    QVector<float> sommets;
    /// 3 indices de sommets par triangle : d'abord les murs, puis le toit, bâtiment par bâtiment.
    QVector<quint32> indices;
};

namespace Batiments
{
/// Lit un fichier de bâtiments produit par data/telecharger_batiments.py et convertit chaque
/// contour en mètres avec OsmCarte::versMetres : les bâtiments tombent ainsi exactement le
/// long des routes de la carte. Seuls latMin/latMax/lonMin/lonMax de `carte` comptent
/// (ils fixent le centre de la projection).
/// Les lignes vides ou qui commencent par '#' sont ignorées ; une ligne mal formée
/// (nombre de valeurs incohérent, moins de 3 points, hauteur nulle) est ignorée sans
/// faire échouer la lecture.
/// @return false (avec un message en français dans `erreur`) seulement si le fichier ne peut
/// pas être ouvert ; `batiments` est alors vide.
bool charger(const QString &chemin, const OsmCarte &carte, QVector<Batiment> &batiments, QString *erreur = nullptr);

/// Découpe un polygone simple (convexe ou non, parcouru dans un sens ou dans l'autre) en
/// triangles, par la méthode des « oreilles » (ear clipping).
/// Supporte les points alignés et les points répétés ; ne boucle jamais indéfiniment, même
/// sur un contour qui se croise lui-même (le résultat peut alors être imparfait).
/// @return indices des points de `contour`, 3 par triangle (au plus n - 2 triangles) ;
/// vide si le contour a moins de 3 points.
QVector<int> trianguler(const QVector<QPointF> &contour);

/// Construit les murs et le toit plat de tous les bâtiments (format : voir MaillageBatiments).
/// Pour un bâtiment de n points : 2n sommets (n au sol, puis n au niveau du toit),
/// 2 triangles par mur (n murs), puis les triangles du toit donnés par trianguler().
MaillageBatiments construireMaillage(const QVector<Batiment> &batiments);
}
