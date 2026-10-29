// Moteur de simulation exécuté dans son propre fil (voir MoteurSimulation.h).

#include "ui/MoteurSimulation.h"

#include <QElapsedTimer>
#include <QTimer>

namespace
{
// Durée d'un pas, en secondes de temps réel : 40 ms = 25 images par seconde,
// la fluidité visée par le cahier des charges.
constexpr int PAS_MS = 40;
constexpr double PAS_S = PAS_MS / 1000.0;
}

MoteurSimulation::MoteurSimulation(QObject *parent)
    : QObject(parent)
    , m_minuteur(new QTimer(this))  // enfant du moteur : il le suit dans son fil d'exécution
{
    // Les signaux traversent les fils d'exécution : Qt doit savoir copier ces types.
    qRegisterMetaType<Instantane>();
    qRegisterMetaType<Mesure>();

    m_minuteur->setInterval(PAS_MS);
    m_minuteur->setTimerType(Qt::PreciseTimer);
    connect(m_minuteur, &QTimer::timeout, this, &MoteurSimulation::pas);
}

void MoteurSimulation::demarrer(const GrapheRoutier &graphe, const ParametresSimulation &parametres,
                                double intervalleRecalcul, int generation)
{
    m_minuteur->stop();  // une nouvelle simulation commence en pause
    m_generation = generation;
    if (&graphe != &m_graphe)
        m_graphe = graphe;
    m_parametres = parametres;
    m_intervalleRecalcul = intervalleRecalcul;
    m_couleursGlouton.clear();
    m_couleursWelshPowell.clear();
    m_aretes.clear();
    m_aretesAEnvoyer = false;

    m_pret = m_simulation.initialiser(&m_graphe, m_parametres);
    if (!m_pret) {
        envoyerInstantane(true, true);  // vide : l'interface efface les véhicules
        return;
    }
    // Premier recalcul tout de suite : les véhicules apparaissent déjà colorés.
    recalculer();
    m_prochainRecalcul = m_simulation.temps() + m_intervalleRecalcul;
    envoyerInstantane(true, true);
}

void MoteurSimulation::reinitialiser(int generation)
{
    // Même graphe, mêmes paramètres, même graine : exactement le même scénario.
    demarrer(m_graphe, m_parametres, m_intervalleRecalcul, generation);
}

void MoteurSimulation::setEnMarche(bool enMarche)
{
    if (enMarche && m_pret) {
        m_minuteur->start();
    } else {
        m_minuteur->stop();
        // À la mise en pause, on envoie l'état exact (dont des arêtes recalculées qui
        // attendaient peut-être encore que l'interface ait fini de dessiner).
        if (m_pret)
            envoyerInstantane(false, true);
    }
}

void MoteurSimulation::setFacteurTemps(double facteur)
{
    m_facteurTemps = facteur;
}

void MoteurSimulation::setRayon(double rayon)
{
    m_parametres.rayon = rayon;
    if (!m_pret)
        return;
    m_simulation.setRayon(rayon);
    // Effet visible tout de suite, même en pause : nouveau graphe, nouvelles couleurs.
    recalculer();
    m_prochainRecalcul = m_simulation.temps() + m_intervalleRecalcul;
    envoyerInstantane(true, true);
}

void MoteurSimulation::setIntervalleRecalcul(double secondes)
{
    m_intervalleRecalcul = secondes;
    m_prochainRecalcul = m_simulation.temps() + secondes;
}

void MoteurSimulation::setAlgoAffiche(Coloration::Algo algo)
{
    m_algoAffiche = algo;
    envoyerInstantane(false, true);  // les couleurs changent tout de suite sur la carte
}

void MoteurSimulation::pas()
{
    if (!m_pret)
        return;

    QElapsedTimer chrono;
    chrono.start();
    // Pas de temps fixe (et non le temps réellement écoulé) : la simulation reste
    // reproductible, quelle que soit la vitesse de la machine.
    m_simulation.avancer(PAS_S * m_facteurTemps);
    m_dureePasMs = chrono.nsecsElapsed() / 1e6;

    // Recalcul périodique (pas à chaque image, comme le demande le sujet).
    bool recalcule = false;
    if (m_simulation.temps() >= m_prochainRecalcul) {
        recalculer();
        m_prochainRecalcul = m_simulation.temps() + m_intervalleRecalcul;
        recalcule = true;
    }
    envoyerInstantane(recalcule, false);
}

void MoteurSimulation::recalculer()
{
    const QVector<Vehicule> &vehicules = m_simulation.vehicules();
    QVector<QPointF> positions;
    QVector<double> rayons;
    positions.reserve(vehicules.size());
    rayons.reserve(vehicules.size());
    for (const Vehicule &v : vehicules) {
        positions.append(v.position);
        rayons.append(v.rayon);
    }

    // Étape 4 : graphe d'interférences (grille spatiale).
    m_interference.construire(positions, rayons);
    const ListeAdjacence &voisins = m_interference.voisins();

    Mesure mesure;
    mesure.generation = m_generation;
    mesure.temps = m_simulation.temps();
    mesure.vehicules = vehicules.size();
    mesure.interference = m_interference.stats();

    // Étape 5 : chaque algorithme colore le MÊME graphe ; on vérifie la validité
    // et on mesure la stabilité par rapport à son propre résultat précédent.
    const auto colorer = [&](Coloration::Algo algo, QVector<int> &precedentes, MesureAlgo &resultat) {
        const ResultatColoration coloration = Coloration::colorer(voisins, algo);
        resultat.couleurs = coloration.nombreCouleurs;
        resultat.dureeMs = coloration.dureeMs;
        resultat.valide = Coloration::estValide(voisins, coloration.couleurs, &resultat.conflits);
        // Premier recalcul : rien à comparer, on considère l'allocation stable.
        resultat.stabilite = precedentes.isEmpty() ? 1.0 : Coloration::stabilite(precedentes, coloration.couleurs);
        precedentes = coloration.couleurs;
    };
    colorer(Coloration::Algo::Glouton, m_couleursGlouton, mesure.glouton);
    colorer(Coloration::Algo::WelshPowell, m_couleursWelshPowell, mesure.welshPowell);

    m_aretes = m_interference.aretes();
    emit nouvelleMesure(mesure);
}

void MoteurSimulation::envoyerInstantane(bool nouvellesAretes, bool forcer)
{
    // Des arêtes recalculées doivent arriver à l'interface tôt ou tard : on le note.
    m_aretesAEnvoyer = m_aretesAEnvoyer || nouvellesAretes;

    // L'interface n'a pas fini de dessiner l'image précédente : on n'en envoie pas
    // d'autre (sinon, si l'affichage prend du retard, les images s'accumulent en
    // mémoire). Les arêtes en attente partiront avec la prochaine image envoyée.
    // Exception : `forcer`, après une action de l'utilisateur (réglage, pause...).
    if (m_attenteAffichage && !forcer)
        return;
    nouvellesAretes = m_aretesAEnvoyer;
    m_aretesAEnvoyer = false;

    Instantane instantane;
    instantane.generation = m_generation;
    const QVector<Vehicule> &vehicules = m_simulation.vehicules();
    instantane.positions.reserve(vehicules.size());
    double sommeVitesses = 0.0;
    for (const Vehicule &v : vehicules) {
        instantane.positions.append(v.position);
        sommeVitesses += v.vitesse;
    }
    instantane.couleurs = (m_algoAffiche == Coloration::Algo::Glouton) ? m_couleursGlouton : m_couleursWelshPowell;
    instantane.nouvellesAretes = nouvellesAretes;
    if (nouvellesAretes)
        instantane.aretes = m_aretes;
    instantane.temps = m_simulation.temps();
    instantane.arrivees = m_simulation.arrivees();
    instantane.vitesseMoyenne = vehicules.isEmpty() ? 0.0 : sommeVitesses / vehicules.size();
    instantane.dureePasMs = m_dureePasMs;

    m_attenteAffichage = true;
    emit nouvelInstantane(instantane);
}
