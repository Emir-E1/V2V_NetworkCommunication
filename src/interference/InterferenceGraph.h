#pragma once

/// @file InterferenceGraph.h
/// @brief Graphe d'interférences dynamique — étape 4 du projet.
///
/// Sommets = véhicules. Arête entre A et B si distance(A, B) < max(rayonA, rayonB) :
/// les deux véhicules se gênent s'ils émettent sur le même canal radio.
///
/// Comparer toutes les paires coûte n² / 2 calculs (2 millions pour 2000 véhicules).
/// On utilise donc une **grille spatiale** : la carte est découpée en cases de la
/// taille du plus grand rayon ; deux véhicules ne peuvent être voisins que s'ils sont
/// dans la même case ou dans deux cases adjacentes. On ne compare donc que les
/// véhicules des 9 cases autour de chacun.
///
/// Le graphe est reconstruit périodiquement (pas à chaque image), par le fil de simulation.

#include <QPair>
#include <QPointF>
#include <QVector>

/// Statistiques affichées dans l'interface (exigence du cahier : arêtes, degré moyen et maximal).
struct StatsInterference
{
    int sommets = 0;
    int aretes = 0;
    double degreMoyen = 0.0;
    int degreMax = 0;
    double dureeMs = 0.0;  ///< temps de construction
};

class GrapheInterference
{
public:
    /// Construit le graphe avec la grille spatiale.
    /// `positions` et `rayons` ont la même taille (un élément par véhicule) ; rayons > 0.
    void construire(const QVector<QPointF> &positions, const QVector<double> &rayons);

    /// Même résultat en comparant toutes les paires (O(n²)) : sert de référence aux tests
    /// et à mesurer le gain de la grille.
    void construireNaif(const QVector<QPointF> &positions, const QVector<double> &rayons);

    /// Liste d'adjacence : voisins()[i] = véhicules en interférence avec i, triés par indice croissant.
    const QVector<QVector<int>> &voisins() const { return m_voisins; }

    /// Toutes les arêtes (i, j) avec i < j, pour les dessiner.
    QVector<QPair<int, int>> aretes() const;

    StatsInterference stats() const { return m_stats; }

private:
    QVector<QVector<int>> m_voisins;
    StatsInterference m_stats;
};
