// Vérifie les règles du graphe sur de petites cartes lisibles, puis compare
// Dijkstra et A* sur Mulhouse si un chemin de fichier est donné en argument.
#include "roadgraph/RoadGraph.h"
#include "map/OsmLoader.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <utility>

// Ce contrôle reste actif en Release, contrairement à assert.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __FILE__ << ':' << __LINE__ << " : " << #condition << '\n'; \
            return 1; \
        } \
    } while (false)

namespace
{
// Les coordonnées restent près de Mulhouse pour respecter l'usage de la
// projection. Les décalages en millièmes de degré rendent les cartes lisibles.
OsmCarte carteVide()
{
    OsmCarte carte;
    carte.latMin = carte.latMax = 47.75;
    carte.lonMin = carte.lonMax = 7.33;
    return carte;
}

void ajouterNoeud(OsmCarte &carte, qint64 id, double x, double y)
{
    carte.noeuds.insert(id, {id, 47.75 + y * 0.001, 7.33 + x * 0.001});
}

void ajouterRoute(OsmCarte &carte, QVector<qint64> noeuds, bool sensUnique = false,
                  TypeRoute type = TypeRoute::Locale)
{
    OsmRoute route;
    route.id = carte.routes.size() + 1;
    route.noeuds = std::move(noeuds);
    route.sensUnique = sensUnique;
    route.type = type;
    carte.routes.append(std::move(route));
}

int indiceSommet(const GrapheRoutier &graphe, qint64 id)
{
    for (int i = 0; i < graphe.sommets().size(); ++i) {
        if (graphe.sommets()[i].idOsm == id) {
            return i;
        }
    }
    return -1;
}

double longueurChemin(const GrapheRoutier &graphe, const QVector<int> &chemin)
{
    double longueur = 0.0;
    for (int indice : chemin) {
        longueur += graphe.troncons()[indice].longueur;
    }
    return longueur;
}

// Vérifie aussi les indices après suppression d'une composante et renumérotation.
bool grapheCoherent(const GrapheRoutier &graphe)
{
    QVector<int> references(graphe.troncons().size(), 0);
    for (int i = 0; i < graphe.sommets().size(); ++i) {
        if (graphe.sortants(i).isEmpty()) {
            return false;
        }
        for (int indice : graphe.sortants(i)) {
            if (indice < 0 || indice >= graphe.troncons().size()) {
                return false;
            }
            const Troncon &arc = graphe.troncons()[indice];
            if (arc.depart != i || arc.arrivee < 0 || arc.arrivee >= graphe.sommets().size()
                || arc.depart == arc.arrivee || arc.longueur <= 0.0 || arc.trace.size() < 2) {
                return false;
            }
            if (arc.trace.front() != graphe.sommets()[arc.depart].pos
                || arc.trace.back() != graphe.sommets()[arc.arrivee].pos) {
                return false;
            }
            ++references[indice];
        }
    }
    return std::all_of(references.cbegin(), references.cend(), [](int n) { return n == 1; });
}

bool cheminCoherent(const GrapheRoutier &graphe, const QVector<int> &chemin,
                    int depart, int arrivee)
{
    for (int indice : chemin) {
        if (indice < 0 || indice >= graphe.troncons().size()
            || graphe.troncons()[indice].depart != depart) {
            return false;
        }
        depart = graphe.troncons()[indice].arrivee;
    }
    return depart == arrivee;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    using Algo = GrapheRoutier::Algo;
    CHECK(std::abs(distanceHaversine(48.8566, 2.3522, 45.7640, 4.8357) - 392000.0) < 1000.0);
    CHECK(distanceHaversine(47.75, 7.33, 47.75, 7.33) == 0.0);
    CHECK(std::isfinite(distanceHaversine(0.0, 0.0, 0.0, 180.0)));

    GrapheRoutier graphe;
    CHECK(graphe.sommetLePlusProche({0.0, 0.0}) == -1);
    StatsChemin stats{99, -1.0};
    CHECK(graphe.plusCourtChemin(0, 1, Algo::AStar, &stats).isEmpty());
    CHECK(stats.sommetsVisites == 0 && stats.dureeMs >= 0.0);

    // Un point de courbure ne crée pas de sommet ; il compte bien dans la
    // longueur et apparaît dans les deux traces, en sens opposés.
    OsmCarte carte = carteVide();
    ajouterNoeud(carte, 1, 0, 0);
    ajouterNoeud(carte, 2, 1, 1);
    ajouterNoeud(carte, 3, 2, 0);
    ajouterRoute(carte, {1, 2, 3}, false, TypeRoute::Secondaire);
    CHECK(graphe.construire(carte));
    CHECK(graphe.sommets().size() == 2);
    CHECK(graphe.troncons().size() == 2);
    CHECK(indiceSommet(graphe, 2) == -1);
    CHECK(grapheCoherent(graphe));
    const OsmNoeud n1 = carte.noeuds.value(1);
    const OsmNoeud n2 = carte.noeuds.value(2);
    const OsmNoeud n3 = carte.noeuds.value(3);
    const double longueurCourbe = distanceHaversine(n1.lat, n1.lon, n2.lat, n2.lon)
                               + distanceHaversine(n2.lat, n2.lon, n3.lat, n3.lon);
    QVector<QPointF> traceAttendue = {carte.versMetres(n1.lat, n1.lon),
                                     carte.versMetres(n2.lat, n2.lon),
                                     carte.versMetres(n3.lat, n3.lon)};
    CHECK(graphe.troncons()[0].trace == traceAttendue);
    std::reverse(traceAttendue.begin(), traceAttendue.end());
    CHECK(graphe.troncons()[1].trace == traceAttendue);
    for (const Troncon &arc : graphe.troncons()) {
        CHECK(std::abs(arc.longueur - longueurCourbe) < 1e-6);
        CHECK(arc.type == TypeRoute::Secondaire);
    }
    CHECK(graphe.sommets()[0].pos == carte.versMetres(n1.lat, n1.lon));
    CHECK(graphe.sommetLePlusProche(graphe.sommets()[1].pos + QPointF(0.1, 0.1)) == 1);

    // Le retour par 3 -> 1 maintient les sens uniques dans la composante.
    // La branche vers 4 et le petit îlot 5 <-> 6 doivent disparaître.
    carte = carteVide();
    for (int i = 1; i <= 6; ++i) {
        ajouterNoeud(carte, i, i, i % 2);
    }
    ajouterRoute(carte, {5, 6});  // avant la composante gardée : indices à remapper
    ajouterRoute(carte, {1, 2});
    ajouterRoute(carte, {2, 3}, true, TypeRoute::Principale);
    ajouterRoute(carte, {3, 1}, true);
    ajouterRoute(carte, {3, 4}, true);
    CHECK(graphe.construire(carte));
    CHECK(graphe.sommets().size() == 3);
    CHECK(graphe.troncons().size() == 4);
    CHECK(grapheCoherent(graphe));
    CHECK(indiceSommet(graphe, 4) == -1);
    CHECK(indiceSommet(graphe, 5) == -1);
    CHECK(indiceSommet(graphe, 6) == -1);
    int doubles = 0;
    int uniques = 0;
    for (const Troncon &arc : graphe.troncons()) {
        const qint64 de = graphe.sommets()[arc.depart].idOsm;
        const qint64 vers = graphe.sommets()[arc.arrivee].idOsm;
        if ((de == 1 && vers == 2) || (de == 2 && vers == 1)) {
            ++doubles;
        } else {
            CHECK((de == 2 && vers == 3) || (de == 3 && vers == 1));
            if (de == 2) {
                CHECK(arc.type == TypeRoute::Principale);
            }
            ++uniques;
        }
    }
    CHECK(doubles == 2 && uniques == 2);

    // Le nœud central partagé coupe chacune des deux routes en deux morceaux.
    carte = carteVide();
    ajouterNoeud(carte, 1, -1, 0);
    ajouterNoeud(carte, 2, 0, 0);
    ajouterNoeud(carte, 3, 1, 0);
    ajouterNoeud(carte, 4, 0, -1);
    ajouterNoeud(carte, 5, 0, 1);
    ajouterRoute(carte, {1, 2, 3});
    ajouterRoute(carte, {4, 2, 5});
    CHECK(graphe.construire(carte));
    CHECK(graphe.sommets().size() == 5);
    CHECK(graphe.troncons().size() == 8);
    CHECK(graphe.sortants(indiceSommet(graphe, 2)).size() == 4);
    CHECK(grapheCoherent(graphe));

    // Un retour au même nœud crée un sommet même sans autre route. Le morceau
    // 2 -> 3 -> 2 est ensuite écarté, comme les arcs de longueur nulle.
    carte = carteVide();
    ajouterNoeud(carte, 1, 0, 0);
    ajouterNoeud(carte, 2, 1, 0);
    ajouterNoeud(carte, 3, 1, 1);
    ajouterNoeud(carte, 4, 2, 0);
    ajouterNoeud(carte, 5, 2, 0);
    ajouterRoute(carte, {1, 2, 3, 2, 4});
    ajouterRoute(carte, {4, 5});
    CHECK(graphe.construire(carte));
    CHECK(graphe.sommets().size() == 3);
    CHECK(graphe.troncons().size() == 4);
    CHECK(indiceSommet(graphe, 2) >= 0);
    CHECK(indiceSommet(graphe, 3) == -1);
    CHECK(indiceSommet(graphe, 5) == -1);
    CHECK(grapheCoherent(graphe));

    // Les extrémités de la route secondaire sont dans la zone, mais son point
    // de courbure sort : seuls les arcs centraux de la première route restent.
    carte = carteVide();
    ajouterNoeud(carte, 1, 0, 0);
    ajouterNoeud(carte, 2, 1, 0);
    ajouterNoeud(carte, 3, 2, 0);
    ajouterNoeud(carte, 4, 3, 0);
    ajouterNoeud(carte, 5, 1.5, 1);
    ajouterRoute(carte, {1, 2, 3, 4});
    ajouterRoute(carte, {2, 5, 3});
    const QPointF p2 = carte.versMetres(carte.noeuds.value(2).lat, carte.noeuds.value(2).lon);
    const QPointF p3 = carte.versMetres(carte.noeuds.value(3).lat, carte.noeuds.value(3).lon);
    const QRectF zone(p2 - QPointF(1, 10), p3 + QPointF(1, 10));
    CHECK(graphe.construire(carte, zone));
    CHECK(graphe.sommets().size() == 2);
    CHECK(graphe.troncons().size() == 2);
    CHECK(indiceSommet(graphe, 2) >= 0 && indiceSommet(graphe, 3) >= 0);
    for (const Troncon &arc : graphe.troncons()) {
        CHECK(arc.trace.size() == 2);
        for (const QPointF &point : arc.trace) {
            CHECK(zone.contains(point));
        }
    }
    CHECK(grapheCoherent(graphe));

    // La route directe fait un grand crochet. Le plus court chemin doit
    // préférer les deux petites routes malgré la destination déjà mise en file.
    carte = carteVide();
    ajouterNoeud(carte, 1, 0, 0);
    ajouterNoeud(carte, 2, 1, 0.1);
    ajouterNoeud(carte, 3, 2, 0);
    ajouterNoeud(carte, 4, 1, 4);
    ajouterRoute(carte, {1, 4, 3});
    ajouterRoute(carte, {1, 2});
    ajouterRoute(carte, {2, 3});
    CHECK(graphe.construire(carte));
    CHECK(graphe.sommets().size() == 3);
    CHECK(graphe.troncons().size() == 6);
    const int depart = indiceSommet(graphe, 1);
    const int arrivee = indiceSommet(graphe, 3);
    const auto dijkstra = graphe.plusCourtChemin(depart, arrivee, Algo::Dijkstra, &stats);
    CHECK(dijkstra.size() == 2);
    CHECK(cheminCoherent(graphe, dijkstra, depart, arrivee));
    CHECK(graphe.sommets()[graphe.troncons()[dijkstra[0]].arrivee].idOsm == 2);
    CHECK(stats.sommetsVisites == 3 && stats.dureeMs >= 0.0);
    const auto astar = graphe.plusCourtChemin(depart, arrivee, Algo::AStar, &stats);
    CHECK(astar == dijkstra);
    CHECK(std::abs(longueurChemin(graphe, dijkstra) - longueurChemin(graphe, astar)) < 1e-6);
    CHECK(stats.sommetsVisites == 3 && stats.dureeMs >= 0.0);
    CHECK(graphe.plusCourtChemin(arrivee, depart).size() == 2);
    for (Algo algo : {Algo::Dijkstra, Algo::AStar}) {
        for (const auto &paire : {std::pair<int, int>{-1, arrivee}, {depart, -1},
                                  {3, arrivee}, {depart, 3}, {depart, depart}}) {
            stats = {99, -1.0};
            CHECK(graphe.plusCourtChemin(paire.first, paire.second, algo, &stats).isEmpty());
            CHECK(stats.sommetsVisites == 0 && stats.dureeMs >= 0.0);
        }
    }

    // Une destination valide ne peut être inatteignable après le filtre SCC.
    // Une chaîne à sens unique donne en revanche un graphe vide, sans résidus.
    carte.routes.clear();
    ajouterRoute(carte, {1, 2, 3}, true);
    CHECK(!graphe.construire(carte));
    CHECK(graphe.sommets().isEmpty() && graphe.troncons().isEmpty());
    CHECK(graphe.sommetLePlusProche({0, 0}) == -1);
    CHECK(graphe.plusCourtChemin(0, 1).isEmpty());
    CHECK(!graphe.construire(carteVide()));
    CHECK(graphe.sommets().isEmpty() && graphe.troncons().isEmpty());
    ajouterRoute(carte, {3, 1});
    CHECK(graphe.construire(carte));
    CHECK(!graphe.construire(carte, QRectF(10000, 10000, 10, 10)));
    CHECK(graphe.sommets().isEmpty() && graphe.troncons().isEmpty());

    // Une longue chaîne bidirectionnelle exerce les piles explicites : une
    // implémentation récursive risquerait ici de déborder la pile du programme.
    carte = carteVide();
    constexpr int tailleChaine = 20000;
    for (int i = 0; i < tailleChaine; ++i) {
        ajouterNoeud(carte, i + 1, i * 0.0001, 0);
        if (i > 0) {
            ajouterRoute(carte, {i, i + 1});
        }
    }
    CHECK(graphe.construire(carte));
    CHECK(graphe.sommets().size() == tailleChaine);
    CHECK(graphe.troncons().size() == 2 * (tailleChaine - 1));
    CHECK(grapheCoherent(graphe));
    CHECK(graphe.plusCourtChemin(0, tailleChaine - 1).size() == tailleChaine - 1);

    if (application.arguments().size() > 1) {
        QString erreur;
        const bool charge = OsmLoader::chargerFichier(application.arguments()[1], carte, &erreur);
        if (!charge) {
            std::cerr << erreur.toStdString() << '\n';
        }
        CHECK(charge);
        QElapsedTimer chronometre;
        chronometre.start();
        const bool construit = graphe.construire(carte);
        const double constructionMs = chronometre.nsecsElapsed() / 1000000.0;
        CHECK(construit);
        CHECK(grapheCoherent(graphe));
        std::cout << "Mulhouse : " << graphe.sommets().size() << " sommets, "
                  << graphe.troncons().size() << " arcs, construction : " << constructionMs << " ms\n";

        std::mt19937 aleatoire(42);
        std::uniform_int_distribution<int> choisir(0, static_cast<int>(graphe.sommets().size()) - 1);
        double tempsDijkstra = 0.0, tempsAstar = 0.0;
        qint64 visitesDijkstra = 0, visitesAstar = 0;
        constexpr int essais = 500;
        for (int i = 0; i < essais; ++i) {
            const int de = choisir(aleatoire);
            const int vers = choisir(aleatoire);
            StatsChemin statsDijkstra, statsAstar;
            const auto cheminDijkstra = graphe.plusCourtChemin(de, vers, Algo::Dijkstra, &statsDijkstra);
            const auto cheminAstar = graphe.plusCourtChemin(de, vers, Algo::AStar, &statsAstar);
            CHECK(cheminCoherent(graphe, cheminDijkstra, de, vers));
            CHECK(cheminCoherent(graphe, cheminAstar, de, vers));
            CHECK(std::abs(longueurChemin(graphe, cheminDijkstra)
                           - longueurChemin(graphe, cheminAstar)) < 1e-6);
            CHECK(statsDijkstra.sommetsVisites <= graphe.sommets().size());
            CHECK(statsAstar.sommetsVisites <= graphe.sommets().size());
            tempsDijkstra += statsDijkstra.dureeMs;
            tempsAstar += statsAstar.dureeMs;
            visitesDijkstra += statsDijkstra.sommetsVisites;
            visitesAstar += statsAstar.sommetsVisites;
        }
        std::cout << "Dijkstra : " << tempsDijkstra / essais << " ms, "
                  << static_cast<double>(visitesDijkstra) / essais << " sommets visites en moyenne\n"
                  << "A* : " << tempsAstar / essais << " ms, "
                  << static_cast<double>(visitesAstar) / essais << " sommets visites en moyenne\n";
    }

    std::cout << "test_road_graph : OK\n";
    return 0;
}
