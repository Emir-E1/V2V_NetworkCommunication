#pragma once

/// @file Simulation.h
/// @brief Simulation des véhicules — étape 3 du projet.
///
/// Chaque véhicule part d'une intersection tirée au hasard, reçoit une destination
/// au hasard, calcule son plus court chemin (A*, voir RoadGraph.h) et le suit à une
/// vitesse qui dépend du type de route. Arrivé, il reçoit une nouvelle destination.
///
/// Cette classe ne connaît ni Qt Widgets ni l'affichage : elle peut tourner dans
/// un fil d'exécution séparé (exigence du cahier des charges : la simulation est
/// séparée de l'affichage pour garder l'interface fluide).
/// Elle est déterministe : même graphe + mêmes paramètres (dont la graine) =
/// exactement la même simulation, ce qui permet de comparer les algorithmes de
/// coloration sur le même scénario.

#include "roadgraph/RoadGraph.h"

#include <QPointF>
#include <QVector>

#include <random>

/// Un véhicule simulé.
struct Vehicule
{
    int id = 0;
    QPointF position;          ///< position actuelle en mètres (repère de la scène)
    double vitesse = 0.0;      ///< vitesse actuelle en m/s (type de route × facteurVitesse)
    double direction = 0.0;    ///< direction en radians, atan2(dy, dx) dans le repère de la scène
    int destination = -1;      ///< sommet visé (indice dans GrapheRoutier::sommets())
    double rayon = 300.0;      ///< portée radio en mètres (étape 4)

    // --- État interne du déplacement ---
    QVector<int> chemin;       ///< tronçons à parcourir (indices dans GrapheRoutier::troncons())
    int etape = 0;             ///< indice du tronçon courant dans `chemin`
    double avancement = 0.0;   ///< mètres déjà parcourus le long de la trace du tronçon courant
    double facteurVitesse = 1.0;  ///< conduite propre au véhicule, tiré entre 0,8 et 1,2
};

/// Réglages d'une simulation.
struct ParametresSimulation
{
    int nombreVehicules = 2000;  ///< au moins 2000 exigés par le sujet
    double rayon = 300.0;        ///< portée radio commune, en mètres
    unsigned graine = 42;        ///< graine du générateur aléatoire (reproductibilité)
};

class Simulation
{
public:
    /// Vitesse de base selon le type de route, en m/s
    /// (Autoroute 90 km/h, Principale 50 km/h, Secondaire 50 km/h, Locale 30 km/h).
    static double vitesseParType(TypeRoute type);

    /// Place les véhicules et leur donne une première destination.
    /// @param graphe graphe routier (doit rester valide tant que la simulation l'utilise).
    /// @return false si le graphe a moins de 2 sommets.
    bool initialiser(const GrapheRoutier *graphe, const ParametresSimulation &parametres);

    /// Fait avancer toute la simulation de `dt` secondes simulées.
    /// Un véhicule peut franchir plusieurs tronçons pendant un même pas
    /// (pas de temps grand quand la simulation est accélérée).
    void avancer(double dt);

    const QVector<Vehicule> &vehicules() const { return m_vehicules; }

    /// Temps simulé écoulé depuis initialiser(), en secondes.
    double temps() const { return m_temps; }

    /// Nombre total d'arrivées à destination depuis initialiser() (statistique).
    int arrivees() const { return m_arrivees; }

    /// Change la portée radio de tous les véhicules.
    void setRayon(double rayon);

private:
    const GrapheRoutier *m_graphe = nullptr;
    QVector<Vehicule> m_vehicules;
    std::mt19937 m_aleatoire;
    double m_temps = 0.0;
    int m_arrivees = 0;
};
