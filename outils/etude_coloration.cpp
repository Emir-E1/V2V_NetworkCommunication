// Étude comparative des canaux radio : mêmes scénarios pour chaque coloration,
// mesures détaillées en CSV et moyennes destinées au rapport sur stderr.

#include "map/OsmLoader.h"
#include "roadgraph/RoadGraph.h"
#include "simulation/Simulation.h"
#include "interference/InterferenceGraph.h"
#include "coloring/Coloring.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocale>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <utility>

namespace
{
// Une clique impose autant de canaux que de sommets. Cette heuristique donne
// une borne inférieure, suffisante pour certifier k optimal quand k l'atteint.
int borneClique(const ListeAdjacence &voisins)
{
    int meilleur = 0;
    QVector<int> clique;
    for (int s = 0; s < voisins.size(); ++s) {
        QVector<int> candidats = voisins[s];
        std::sort(candidats.begin(), candidats.end(), [&](int a, int b) {
            if (voisins[a].size() != voisins[b].size())
                return voisins[a].size() > voisins[b].size();
            return a < b;
        });
        clique.clear();
        clique.append(s);
        for (int candidat : candidats) {
            const auto &adjacents = voisins[candidat];
            if (std::all_of(clique.cbegin(), clique.cend(), [&](int membre) {
                    return std::binary_search(adjacents.cbegin(), adjacents.cend(), membre);
                }))
                clique.append(candidat);
        }
        meilleur = std::max(meilleur, static_cast<int>(clique.size()));
    }
    return meilleur;
}

struct Bilan
{
    int echantillons = 0;
    int transitions = 0;
    int optimaux = 0;
    int valides = 0;
    double sommeK = 0.0;
    double sommeMs = 0.0;
    double maxMs = 0.0;
    double sommeStabilite = 0.0;

    void ajouter(const ResultatColoration &resultat, double stabilite, bool valide,
                 bool optimal, bool premier)
    {
        ++echantillons;
        sommeK += resultat.nombreCouleurs;
        sommeMs += resultat.dureeMs;
        maxMs = std::max(maxMs, resultat.dureeMs);
        valides += valide;
        optimaux += optimal;
        // Le premier résultat vaut 1 par convention, sans transition à mesurer.
        if (!premier) {
            ++transitions;
            sommeStabilite += stabilite;
        }
    }
};

using BilansAlgos = std::array<Bilan, Coloration::NOMBRE_ALGOS>;

struct Scenario
{
    int vehicules;
    int portee;
    BilansAlgos bilans{};
};

// Les largeurs fixes permettent de comparer directement qualité et coût.
void afficherBilan(QTextStream &sortie, const BilansAlgos &bilans)
{
    sortie << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8\n")
                  .arg(QStringLiteral("Algorithme"), -18)
                  .arg(QStringLiteral("Mesures"), 8)
                  .arg(QStringLiteral("k moyen"), 9)
                  .arg(QStringLiteral("ms moyen"), 10)
                  .arg(QStringLiteral("ms max"), 10)
                  .arg(QStringLiteral("Stabilité"), 10)
                  .arg(QStringLiteral("% optimal"), 10)
                  .arg(QStringLiteral("% valide"), 10);
    for (Coloration::Algo algo : Coloration::TOUS_LES_ALGOS) {
        const Bilan &b = bilans[static_cast<int>(algo)];
        sortie << QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8\n")
                      .arg(Coloration::nom(algo), -18)
                      .arg(b.echantillons, 8)
                      .arg(b.sommeK / b.echantillons, 9, 'f', 3)
                      .arg(b.sommeMs / b.echantillons, 10, 'f', 3)
                      .arg(b.maxMs, 10, 'f', 3)
                      .arg(b.transitions ? b.sommeStabilite / b.transitions : 1.0, 10, 'f', 6)
                      .arg(100.0 * b.optimaux / b.echantillons, 10, 'f', 2)
                      .arg(100.0 * b.valides / b.echantillons, 10, 'f', 2);
    }
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream csv(stdout);
    QTextStream journal(stderr);
    // Le format C garde le point décimal même sur un poste configuré en français.
    csv.setLocale(QLocale::c());
    journal.setLocale(QLocale::c());
    csv.setRealNumberNotation(QTextStream::FixedNotation);
    csv.setRealNumberPrecision(6);
    journal.setRealNumberNotation(QTextStream::FixedNotation);
    journal.setRealNumberPrecision(3);

    const QStringList arguments = application.arguments();
    const bool rapide = arguments.size() == 3 && arguments[2] == QStringLiteral("--rapide");
    if ((arguments.size() != 2 && !rapide) || arguments[1].startsWith(QLatin1String("--"))) {
        journal << "Usage : etude_coloration <carte.osm> [--rapide]\n";
        return 1;
    }

    QElapsedTimer total;
    total.start();
    OsmCarte carte;
    QString erreur;
    if (!OsmLoader::chargerFichier(arguments[1], carte, &erreur)) {
        journal << "Impossible de charger la carte : " << erreur << '\n';
        return 1;
    }
    GrapheRoutier routes;
    if (!routes.construire(carte)) {
        journal << "Impossible de construire le graphe routier sur toute la carte.\n";
        return 1;
    }

    const QVector<int> populations = rapide ? QVector<int>{2000} : QVector<int>{1000, 2000, 3000};
    const QVector<int> portees = rapide ? QVector<int>{300} : QVector<int>{200, 300, 500};
    const int nombreGraines = rapide ? 2 : 5;
    const int duree = rapide ? 20 : 60;
    const int nombreRuns = static_cast<int>(populations.size() * portees.size()) * nombreGraines;
    int run = 0;
    bool toutValide = true;
    QVector<Scenario> scenarios;
    BilansAlgos ensemble{};

    csv << "vehicules;portee;graine;temps_s;aretes;degre_moyen;degre_max;interference_ms;borne_clique";
    for (Coloration::Algo algo : Coloration::TOUS_LES_ALGOS) {
        const QString prefixe = Coloration::nomCourt(algo);
        for (const char *suffixe : {"_k", "_ms", "_stabilite", "_valide", "_optimal"})
            csv << ';' << prefixe << suffixe;
    }
    csv << '\n';

    for (int n : populations) {
        for (int rayon : portees) {
            Scenario scenario{n, rayon, {}};
            for (int graine = 1; graine <= nombreGraines; ++graine) {
                journal << '[' << ++run << '/' << nombreRuns << "] N=" << n
                        << ", R=" << rayon << " m, graine=" << graine
                        << ", durée simulée=" << duree << " s\n";
                journal.flush();
                ParametresSimulation parametres;
                parametres.nombreVehicules = n;
                parametres.rayon = rayon;
                parametres.graine = static_cast<unsigned>(graine);
                Simulation simulation;
                if (!simulation.initialiser(&routes, parametres)) {
                    journal << "Échec de l'initialisation des véhicules.\n";
                    return 1;
                }

                // L'historique est propre à chaque algorithme et à chaque graine.
                std::array<QVector<int>, Coloration::NOMBRE_ALGOS> precedentes;
                QVector<QPointF> positions(n);
                QVector<double> rayons(n);
                GrapheInterference interference;
                for (int seconde = 0; seconde <= duree; ++seconde) {
                    // Un compteur entier évite la dérive des seuils en virgule flottante.
                    if (seconde > 0)
                        for (int pas = 0; pas < 25; ++pas)
                            simulation.avancer(0.04);

                    const auto &vehicules = simulation.vehicules();
                    for (int i = 0; i < n; ++i) {
                        positions[i] = vehicules[i].position;
                        rayons[i] = vehicules[i].rayon;
                    }
                    interference.construire(positions, rayons);
                    const auto &voisins = interference.voisins();
                    const StatsInterference stats = interference.stats();
                    const int borne = borneClique(voisins);
                    csv << n << ';' << rayon << ';' << graine << ';' << seconde << ';'
                        << stats.aretes << ';' << stats.degreMoyen << ';' << stats.degreMax
                        << ';' << stats.dureeMs << ';' << borne;

                    for (Coloration::Algo algo : Coloration::TOUS_LES_ALGOS) {
                        const int i = static_cast<int>(algo);
                        ResultatColoration resultat = Coloration::colorer(voisins, algo);
                        const bool valide = Coloration::estValide(voisins, resultat.couleurs);
                        const bool optimal = resultat.nombreCouleurs == borne;
                        const double stabilite = seconde == 0 ? 1.0
                            : Coloration::stabilite(precedentes[i], resultat.couleurs);
                        toutValide = toutValide && valide;
                        csv << ';' << resultat.nombreCouleurs << ';' << resultat.dureeMs
                            << ';' << stabilite << ';' << int(valide) << ';' << int(optimal);
                        scenario.bilans[i].ajouter(resultat, stabilite, valide, optimal, seconde == 0);
                        ensemble[i].ajouter(resultat, stabilite, valide, optimal, seconde == 0);
                        precedentes[i] = std::move(resultat.couleurs);
                    }
                    csv << '\n';
                }
                csv.flush();
                if (csv.status() != QTextStream::Ok) {
                    journal << "Échec de l'écriture du CSV.\n";
                    return 1;
                }
            }
            scenarios.append(scenario);
        }
    }

    journal << "\nBilan par densité et portée (moyennes sur toutes les graines et mesures)\n"
            << "Stabilité : t=0 exclu pour chaque graine ; optimal : k = borne de clique.\n";
    for (const Scenario &scenario : scenarios) {
        journal << "\nN=" << scenario.vehicules << ", R=" << scenario.portee << " m\n";
        afficherBilan(journal, scenario.bilans);
    }
    journal << "\nBilan global par algorithme\n";
    afficherBilan(journal, ensemble);
    journal << "\nDurée totale (chargement, simulation, mesures et CSV) : "
            << total.nsecsElapsed() / 1e9 << " s\n";
    return toutValide ? 0 : 1;
}
