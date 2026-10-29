#pragma once

/// @file RoadGraph.h
/// @brief Graphe routier orienté — étape 2 du projet.
///
/// À partir de la carte OSM (OsmCarte), on construit le graphe sur lequel les
/// véhicules vont rouler (étape 3) :
///   - **sommets** = intersections (nœud OSM partagé par au moins deux routes)
///     et extrémités de routes ;
///   - **arcs (tronçons)** = morceau de route entre deux sommets consécutifs.
///     Une route à double sens donne deux arcs (un par sens), une route à sens
///     unique un seul ;
///   - **poids** = longueur réelle en mètres, calculée avec la formule de Haversine
///     sur les coordonnées GPS de tous les points du tronçon (pas seulement les extrémités).
///
/// Le graphe ne garde que sa plus grande composante **fortement connexe** : depuis
/// n'importe quel sommet on peut rejoindre n'importe quel autre. Sans ça, un véhicule
/// pourrait recevoir une destination impossible à atteindre (impasse en sens unique,
/// route coupée au bord de la carte...).

#include "map/OsmData.h"

#include <QPointF>
#include <QRectF>
#include <QVector>

/// Distance à vol d'oiseau entre deux points GPS, en mètres (formule de Haversine).
double distanceHaversine(double lat1, double lon1, double lat2, double lon2);

/// Un sommet du graphe : intersection ou extrémité de route.
struct Sommet
{
    qint64 idOsm = 0;  ///< id du nœud OSM d'origine
    QPointF pos;       ///< position en mètres (repère de la scène, voir OsmCarte::versMetres)
};

/// Un arc orienté : on ne peut le parcourir que de `depart` vers `arrivee`.
struct Troncon
{
    int depart = -1;            ///< indice du sommet de départ dans GrapheRoutier::sommets()
    int arrivee = -1;           ///< indice du sommet d'arrivée
    double longueur = 0.0;      ///< longueur réelle en mètres (Haversine, somme des segments)
    TypeRoute type = TypeRoute::Locale;  ///< pour les vitesses par type de route (étape 3)
    QVector<QPointF> trace;     ///< points en mètres, de depart à arrivee inclus (≥ 2 points) :
                                ///< sert à dessiner l'arc et à faire avancer un véhicule le long
};

/// Statistiques d'une recherche de plus court chemin (pour comparer Dijkstra et A*).
struct StatsChemin
{
    int sommetsVisites = 0;  ///< nombre de sommets sortis de la file de priorité
    double dureeMs = 0.0;    ///< temps de calcul en millisecondes
};

class GrapheRoutier
{
public:
    /// Algorithme de plus court chemin.
    enum class Algo
    {
        Dijkstra,  ///< explore en "cercles" autour du départ
        AStar      ///< Dijkstra guidé vers l'arrivée par la distance à vol d'oiseau (même résultat, plus rapide)
    };

    /// Construit le graphe depuis la carte.
    /// @param zone rectangle en mètres (zone de simulation choisie sur la carte) ;
    ///             si vide, toute la carte. Seuls les tronçons dont TOUS les points sont
    ///             dans la zone sont gardés.
    /// @return false si le graphe obtenu est vide (zone sans route, par exemple).
    bool construire(const OsmCarte &carte, const QRectF &zone = QRectF());

    const QVector<Sommet> &sommets() const { return m_sommets; }
    const QVector<Troncon> &troncons() const { return m_troncons; }

    /// Indices (dans troncons()) des arcs qui partent du sommet `s`.
    const QVector<int> &sortants(int s) const { return m_sortants[s]; }

    /// Sommet le plus proche d'une position en mètres (-1 si le graphe est vide).
    int sommetLePlusProche(const QPointF &pos) const;

    /// Plus court chemin (en mètres) de `depart` à `arrivee`.
    /// @return la suite des indices de tronçons à parcourir, dans l'ordre ;
    ///         vide si depart == arrivee ou si l'arrivée est inatteignable.
    QVector<int> plusCourtChemin(int depart, int arrivee, Algo algo = Algo::AStar,
                                 StatsChemin *stats = nullptr) const;

private:
    QVector<Sommet> m_sommets;
    QVector<Troncon> m_troncons;
    QVector<QVector<int>> m_sortants;  ///< liste d'adjacence : arcs sortants de chaque sommet
};
