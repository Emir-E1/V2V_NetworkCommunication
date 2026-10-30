#pragma once

/// @file MoteurSimulation.h
/// @brief Fil d'exécution (thread) qui fait tourner la simulation — étapes 3 à 5.
///
/// Le cahier des charges exige que la simulation et la coloration soient séparées
/// de l'affichage, pour que l'interface reste fluide avec 2000 véhicules.
/// MoteurSimulation vit donc dans un QThread à part. Environ 25 fois par seconde :
///   1. il fait avancer les véhicules (étape 3) ;
///   2. à intervalle régulier (par défaut chaque seconde simulée), il reconstruit le
///      graphe d'interférences (étape 4) et le colore avec CHAQUE algorithme (Glouton,
///      Welsh–Powell, DSATUR, recherche locale), sur le même graphe, pour une
///      comparaison équitable (étape 5) ;
///   3. il envoie à l'interface un Instantane (positions, couleurs, arêtes) et, après
///      chaque recalcul, une Mesure (nombre de couleurs, temps, stabilité...).
///
/// L'interface ne l'appelle jamais directement : elle passe par
/// QMetaObject::invokeMethod(..., Qt::QueuedConnection), pour que chaque commande
/// s'exécute dans le fil du moteur (aucun accès simultané aux données).

#include "coloring/Coloring.h"
#include "interference/InterferenceGraph.h"
#include "roadgraph/RoadGraph.h"
#include "simulation/Simulation.h"

#include <QObject>
#include <QPair>
#include <QPointF>
#include <QVector>

#include <array>
#include <atomic>

class QTimer;

/// Image de la simulation envoyée à l'interface pour être dessinée.
struct Instantane
{
    int generation = 0;                  ///< numéro de la simulation qui l'a produit (voir demarrer())
    QVector<QPointF> positions;          ///< position de chaque véhicule (mètres)
    QVector<int> couleurs;               ///< canal de chaque véhicule (algorithme affiché), -1 = pas encore coloré
    QVector<float> caps;                 ///< direction de chaque véhicule (radians, atan2(dy, dx)) : vue 3D
    QVector<float> vitesses;             ///< vitesse de chaque véhicule (m/s) : fiche de la vue 3D
    QVector<QPair<int, int>> aretes;     ///< arêtes d'interférence (seulement si nouvellesAretes)
    bool nouvellesAretes = false;        ///< true juste après un recalcul du graphe
    double temps = 0.0;                  ///< temps simulé en secondes
    int arrivees = 0;                    ///< nombre de véhicules arrivés à destination
    double vitesseMoyenne = 0.0;         ///< m/s, moyenne sur tous les véhicules
    double dureePasMs = 0.0;             ///< temps de calcul du dernier pas de déplacement
};

/// Résultat d'un algorithme lors d'un recalcul.
struct MesureAlgo
{
    int couleurs = 0;        ///< k(t)
    double dureeMs = 0.0;    ///< temps de calcul
    double stabilite = 1.0;  ///< S(t), par rapport au recalcul précédent du même algorithme
    bool valide = true;      ///< aucune arête entre deux sommets de même couleur
    int conflits = 0;
};

/// Une ligne de mesures, produite à chaque recalcul (courbes, tableau, export CSV).
struct Mesure
{
    int generation = 0;           ///< numéro de la simulation qui l'a produite
    double temps = 0.0;           ///< temps simulé (s)
    int vehicules = 0;
    StatsInterference interference;
    /// Résultat de chaque algorithme, indicé par Coloration::Algo
    /// (algos[int(Coloration::Algo::Dsatur)]...).
    std::array<MesureAlgo, Coloration::NOMBRE_ALGOS> algos;

    const MesureAlgo &algo(Coloration::Algo a) const { return algos[static_cast<int>(a)]; }
};

Q_DECLARE_METATYPE(Instantane)
Q_DECLARE_METATYPE(Mesure)

class MoteurSimulation : public QObject
{
    Q_OBJECT

public:
    explicit MoteurSimulation(QObject *parent = nullptr);

    /// À appeler depuis l'interface quand elle a fini de dessiner le dernier
    /// Instantane : tant que ce n'est pas fait, le moteur n'en envoie pas d'autre
    /// (sinon, si l'affichage est lent, les images s'accumuleraient en mémoire).
    void instantaneAffiche() { m_attenteAffichage = false; }

    // ---- Commandes (à appeler via QMetaObject::invokeMethod, en file d'attente) ----

    /// (Re)lance une simulation sur ce graphe, en pause. Le graphe est copié.
    /// `generation` : numéro choisi par l'interface, recopié dans chaque Instantane et
    /// chaque Mesure. Des messages de l'ancienne simulation peuvent encore être en route
    /// au moment du redémarrage : l'interface les reconnaît à leur ancien numéro et les ignore.
    void demarrer(const GrapheRoutier &graphe, const ParametresSimulation &parametres, double intervalleRecalcul,
                  int generation);
    /// Repart de zéro avec les mêmes paramètres (même graine : même scénario), sous un nouveau numéro.
    void reinitialiser(int generation);
    void setEnMarche(bool enMarche);
    /// Multiplicateur du temps : 1 = temps réel, 2 = deux fois plus vite...
    void setFacteurTemps(double facteur);
    void setRayon(double rayon);
    /// Temps simulé (s) entre deux recalculs du graphe d'interférences et de la coloration.
    void setIntervalleRecalcul(double secondes);
    /// Algorithme dont les couleurs sont affichées sur la carte.
    void setAlgoAffiche(Coloration::Algo algo);

signals:
    void nouvelInstantane(const Instantane &instantane);
    void nouvelleMesure(const Mesure &mesure);

private:
    void pas();          ///< un pas de temps (appelé par le minuteur)
    void recalculer();   ///< graphe d'interférences + colorations
    /// Envoie l'état courant à l'interface. `forcer` : même si elle n'a pas fini
    /// de dessiner le précédent (utile quand un réglage vient de changer).
    void envoyerInstantane(bool nouvellesAretes, bool forcer);

    QTimer *m_minuteur;
    GrapheRoutier m_graphe;  ///< copie propre au moteur : l'interface peut reconstruire le sien sans risque
    Simulation m_simulation;
    ParametresSimulation m_parametres;
    GrapheInterference m_interference;
    bool m_pret = false;     ///< une simulation a été initialisée
    int m_generation = 0;    ///< numéro de la simulation en cours

    double m_facteurTemps = 1.0;
    double m_intervalleRecalcul = 1.0;
    double m_prochainRecalcul = 0.0;
    Coloration::Algo m_algoAffiche = Coloration::Algo::WelshPowell;

    /// Dernière coloration de chaque algorithme (indicée par Coloration::Algo) :
    /// sert à calculer la stabilité au recalcul suivant et à colorer la carte.
    std::array<QVector<int>, Coloration::NOMBRE_ALGOS> m_couleurs;
    QVector<QPair<int, int>> m_aretes;
    bool m_aretesAEnvoyer = false;  ///< arêtes recalculées pas encore transmises à l'interface
    double m_dureePasMs = 0.0;

    std::atomic<bool> m_attenteAffichage{false};
};
