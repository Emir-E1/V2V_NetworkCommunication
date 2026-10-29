#pragma once

/// @file Coloring.h
/// @brief Coloration du graphe d'interférences — étape 5 du projet.
///
/// Couleur = canal radio. Deux véhicules reliés par une arête (en interférence)
/// doivent recevoir des couleurs différentes. Le but est d'utiliser le moins de
/// couleurs (canaux) possible, rapidement, car le calcul est refait régulièrement.
/// Trouver le minimum exact est NP-difficile : on utilise des heuristiques.
///
/// Algorithmes (cahier des charges : 2 obligatoires, DSATUR et recherche locale ensuite) :
///   - Glouton : on prend les sommets dans l'ordre naturel (0, 1, 2...) et on donne
///     à chacun la plus petite couleur non utilisée par ses voisins déjà colorés ;
///   - Welsh–Powell : même principe, mais les sommets sont triés par degré
///     décroissant (les plus "contraints" d'abord), ce qui réduit souvent le nombre de couleurs.

#include <QString>
#include <QVector>

/// voisins[i] = sommets adjacents à i (graphe non orienté : si j est dans voisins[i],
/// alors i est dans voisins[j]).
using ListeAdjacence = QVector<QVector<int>>;

/// Résultat d'une coloration.
struct ResultatColoration
{
    QVector<int> couleurs;   ///< couleurs[i] = couleur du sommet i, de 0 à nombreCouleurs - 1
    int nombreCouleurs = 0;  ///< k : nombre de couleurs (canaux) utilisées
    double dureeMs = 0.0;    ///< temps de calcul en millisecondes
};

namespace Coloration
{
/// Algorithmes disponibles. (DSATUR et recherche locale s'ajouteront ici.)
enum class Algo
{
    Glouton,
    WelshPowell
};

/// Nom affiché dans l'interface et dans l'export CSV ("Glouton", "Welsh-Powell").
QString nom(Algo algo);

/// Colore le graphe avec l'algorithme demandé. Graphe vide -> 0 couleur.
ResultatColoration colorer(const ListeAdjacence &voisins, Algo algo);

/// Vérifie qu'aucune arête ne relie deux sommets de même couleur, et que chaque
/// sommet a une couleur >= 0. Si `conflits` n'est pas nul, il reçoit le nombre
/// d'arêtes en conflit (chaque arête comptée une fois).
bool estValide(const ListeAdjacence &voisins, const QVector<int> &couleurs, int *conflits = nullptr);

/// Stabilité entre deux recolorations : S = 1 - (nombre de véhicules dont la couleur
/// a changé) / N. Les deux vecteurs décrivent les mêmes véhicules dans le même ordre.
/// Renvoie 1.0 si les vecteurs sont vides ; s'ils n'ont pas la même taille, compare
/// la partie commune et divise par la plus grande taille.
double stabilite(const QVector<int> &avant, const QVector<int> &apres);
}
