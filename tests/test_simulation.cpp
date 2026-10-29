// Vérifie les déplacements sur des cartes fabriquées à la main : distance,
// virages, changements de vitesse, arrivées et reproductibilité des tirages.
// Avec un fichier OSM en argument, mesure aussi 2 000 véhicules sur Mulhouse.

#include "simulation/Simulation.h"
#include "map/OsmLoader.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>

// Ce contrôle reste actif en Release et indique la condition exacte en échec.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __FILE__ << ':' << __LINE__ << " : " << #condition << '\n'; \
            return 1; \
        } \
    } while (false)

namespace
{
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

void ajouterRoute(OsmCarte &carte, QVector<qint64> noeuds,
                  TypeRoute type = TypeRoute::Locale, bool sensUnique = false)
{
    OsmRoute route;
    route.id = carte.routes.size() + 1;
    route.noeuds = std::move(noeuds);
    route.type = type;
    route.sensUnique = sensUnique;
    carte.routes.append(std::move(route));
}

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double longueurTrace(const Troncon &troncon)
{
    double longueur = 0.0;
    for (qsizetype i = 1; i < troncon.trace.size(); ++i) {
        longueur += distance(troncon.trace[i - 1], troncon.trace[i]);
    }
    return longueur;
}

// La projection sur chaque segment contrôle la géométrie indépendamment du
// calcul d'avancement de la simulation, y compris pour les points confondus.
bool surTrace(const QPointF &position, const Troncon &troncon)
{
    for (qsizetype i = 1; i < troncon.trace.size(); ++i) {
        const QPointF a = troncon.trace[i - 1];
        const QPointF segment = troncon.trace[i] - a;
        const double carre = QPointF::dotProduct(segment, segment);
        const double fraction = carre > 0.0
            ? std::clamp(QPointF::dotProduct(position - a, segment) / carre, 0.0, 1.0)
            : 0.0;
        if (distance(position, a + fraction * segment) < 1e-6) {
            return true;
        }
    }
    return false;
}

bool positionsSurRoutes(const Simulation &simulation, const GrapheRoutier &graphe)
{
    for (const Vehicule &vehicule : simulation.vehicules()) {
        if (vehicule.chemin.isEmpty() || vehicule.etape < 0
            || vehicule.etape >= vehicule.chemin.size()) {
            return false;
        }
        // Être sur le tronçon courant est plus strict qu'être sur une route quelconque.
        const Troncon &troncon = graphe.troncons()[vehicule.chemin[vehicule.etape]];
        if (!surTrace(vehicule.position, troncon) || vehicule.avancement < 0.0
            || vehicule.avancement > longueurTrace(troncon) + 1e-6
            || vehicule.vitesse <= 0.0 || !std::isfinite(vehicule.direction)) {
            return false;
        }
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    CHECK(Simulation::vitesseParType(TypeRoute::Autoroute) == 90.0 / 3.6);
    CHECK(Simulation::vitesseParType(TypeRoute::Principale) == 50.0 / 3.6);
    CHECK(Simulation::vitesseParType(TypeRoute::Secondaire) == 50.0 / 3.6);
    CHECK(Simulation::vitesseParType(TypeRoute::Locale) == 30.0 / 3.6);

    Simulation simulation;
    ParametresSimulation parametres;
    parametres.nombreVehicules = 128;
    parametres.rayon = 123.0;
    GrapheRoutier vide;
    CHECK(!simulation.initialiser(nullptr, parametres));
    CHECK(simulation.vehicules().isEmpty());
    CHECK(!simulation.initialiser(&vide, parametres));
    CHECK(simulation.vehicules().isEmpty());

    // Deux extrémités reliées sans virage : aucun véhicule n'arrive pendant
    // ce petit pas, donc la distance directe doit être exactement vitesse × dt.
    OsmCarte carte = carteVide();
    ajouterNoeud(carte, 1, 0, 0);
    ajouterNoeud(carte, 2, 2, 0);
    ajouterRoute(carte, {1, 2});
    GrapheRoutier droit;
    CHECK(droit.construire(carte));
    CHECK(simulation.initialiser(&droit, parametres));
    CHECK(simulation.vehicules().size() == parametres.nombreVehicules);
    const auto depart = simulation.vehicules();
    for (int i = 0; i < parametres.nombreVehicules; ++i) {
        const Vehicule &vehicule = depart[i];
        CHECK(vehicule.id == i);
        CHECK(vehicule.rayon == parametres.rayon);
        CHECK(vehicule.facteurVitesse >= 0.8 && vehicule.facteurVitesse <= 1.2);
        CHECK(vehicule.chemin.size() == 1);
        const Troncon &troncon = droit.troncons()[vehicule.chemin.front()];
        CHECK(vehicule.destination != troncon.depart);
        CHECK(vehicule.destination == troncon.arrivee);
        CHECK(vehicule.position == droit.sommets()[troncon.depart].pos);
        CHECK(vehicule.etape == 0 && vehicule.avancement == 0.0);
        CHECK(vehicule.vitesse == (30.0 / 3.6) * vehicule.facteurVitesse);
        const QPointF segment = troncon.trace.back() - troncon.trace.front();
        CHECK(vehicule.direction == std::atan2(segment.y(), segment.x()));
    }
    simulation.avancer(0.25);
    CHECK(simulation.temps() == 0.25);
    CHECK(simulation.arrivees() == 0);
    for (int i = 0; i < parametres.nombreVehicules; ++i) {
        CHECK(std::abs(distance(depart[i].position, simulation.vehicules()[i].position)
                       - depart[i].vitesse * 0.25) < 1e-6);
    }
    CHECK(positionsSurRoutes(simulation, droit));
    simulation.setRayon(456.0);
    for (const Vehicule &vehicule : simulation.vehicules()) {
        CHECK(vehicule.rayon == 456.0);
    }

    // Un seul véhicule permet de prévoir sept allers-retours partiels sans
    // dépendre de l'ordre des tirages des autres véhicules.
    parametres.nombreVehicules = 1;
    CHECK(simulation.initialiser(&droit, parametres));
    const Vehicule premier = simulation.vehicules().front();
    const Troncon &aller = droit.troncons()[premier.chemin.front()];
    const double longueur = longueurTrace(aller);
    simulation.avancer(7.25 * longueur / premier.vitesse);
    CHECK(simulation.arrivees() == 7);
    CHECK(simulation.vehicules().front().destination == aller.depart);
    const QPointF attendu = aller.trace.back()
                          + 0.25 * (aller.trace.front() - aller.trace.back());
    CHECK(distance(simulation.vehicules().front().position, attendu) < 1e-6);
    CHECK(positionsSurRoutes(simulation, droit));

    CHECK(simulation.initialiser(&droit, parametres));
    simulation.avancer(longueur / premier.vitesse);
    CHECK(simulation.arrivees() == 1);
    CHECK(simulation.vehicules().front().destination == aller.depart);
    CHECK(distance(simulation.vehicules().front().position, aller.trace.back()) < 1e-6);
    CHECK(simulation.vehicules().front().avancement == 0.0);
    CHECK(positionsSurRoutes(simulation, droit));

    // Une réinitialisation ratée doit effacer la population et les statistiques.
    CHECK(!simulation.initialiser(&vide, parametres));
    CHECK(simulation.vehicules().isEmpty());
    CHECK(simulation.temps() == 0.0 && simulation.arrivees() == 0);
    simulation.avancer(2.0);
    CHECK(simulation.temps() == 2.0);
    parametres.nombreVehicules = 0;
    CHECK(simulation.initialiser(&droit, parametres));
    CHECK(simulation.vehicules().isEmpty());

    // Une boucle à sens unique impose les virages et des vitesses différentes.
    // Les points 2 et 3 sont confondus : ce segment nul doit être sauté.
    carte = carteVide();
    ajouterNoeud(carte, 1, 0, 0);
    ajouterNoeud(carte, 2, 0, 0.2);
    ajouterNoeud(carte, 3, 0, 0.2);
    ajouterNoeud(carte, 4, 0.4, 0.2);
    ajouterNoeud(carte, 5, 0.8, 0.2);
    ajouterRoute(carte, {1, 2, 3, 4}, TypeRoute::Locale, true);
    ajouterRoute(carte, {4, 5}, TypeRoute::Autoroute, true);
    ajouterRoute(carte, {5, 1}, TypeRoute::Principale, true);
    GrapheRoutier boucle;
    CHECK(boucle.construire(carte));
    parametres.nombreVehicules = 128;
    CHECK(simulation.initialiser(&boucle, parametres));

    // Après le virage et les deux points confondus, le véhicule doit être
    // au milieu du segment horizontal, avec la direction de ce segment.
    int dansVirage = -1;
    for (int i = 0; i < simulation.vehicules().size(); ++i) {
        const Vehicule &vehicule = simulation.vehicules()[i];
        if (boucle.troncons()[vehicule.chemin.front()].type == TypeRoute::Locale) {
            dansVirage = i;
            break;
        }
    }
    CHECK(dansVirage >= 0);
    const Vehicule avantVirage = simulation.vehicules()[dansVirage];
    const auto &trace = boucle.troncons()[avantVirage.chemin.front()].trace;
    CHECK(trace.size() == 4);
    const double distanceVirage = distance(trace[0], trace[1])
                               + 0.5 * distance(trace[2], trace[3]);
    simulation.avancer(distanceVirage / avantVirage.vitesse);
    const Vehicule &apresVirage = simulation.vehicules()[dansVirage];
    CHECK(distance(apresVirage.position, (trace[2] + trace[3]) * 0.5) < 1e-6);
    CHECK(apresVirage.etape == 0);
    CHECK(std::abs(apresVirage.avancement - distanceVirage) < 1e-6);
    const QPointF directionVirage = trace[3] - trace[2];
    CHECK(apresVirage.direction == std::atan2(directionVirage.y(), directionVirage.x()));

    CHECK(simulation.initialiser(&boucle, parametres));
    int choisi = -1;
    for (int i = 0; i < simulation.vehicules().size(); ++i) {
        if (simulation.vehicules()[i].chemin.size() >= 2) {
            choisi = i;
            break;
        }
    }
    CHECK(choisi >= 0);
    const Vehicule avant = simulation.vehicules()[choisi];
    const Troncon &arc1 = boucle.troncons()[avant.chemin[0]];
    const Troncon &arc2 = boucle.troncons()[avant.chemin[1]];
    CHECK(arc1.type != arc2.type);
    const double vitesse2 = Simulation::vitesseParType(arc2.type) * avant.facteurVitesse;
    const double pas = longueurTrace(arc1) / avant.vitesse
                     + 0.25 * longueurTrace(arc2) / vitesse2;
    simulation.avancer(pas);
    const Vehicule &apres = simulation.vehicules()[choisi];
    CHECK(apres.etape == 1);
    CHECK(apres.destination == avant.destination);
    CHECK(apres.vitesse == vitesse2);
    CHECK(std::abs(apres.avancement - 0.25 * longueurTrace(arc2)) < 1e-6);
    CHECK(positionsSurRoutes(simulation, boucle));

    // Deux découpages du même temps doivent garder la même progression,
    // même après plusieurs arrivées et changements de vitesse.
    parametres.nombreVehicules = 1;
    Simulation pasFin;
    CHECK(simulation.initialiser(&boucle, parametres));
    CHECK(pasFin.initialiser(&boucle, parametres));
    simulation.avancer(300.0);
    for (int i = 0; i < 3000; ++i) {
        pasFin.avancer(0.1);
        CHECK(positionsSurRoutes(pasFin, boucle));
    }
    CHECK(simulation.arrivees() > 0);
    CHECK(simulation.arrivees() == pasFin.arrivees());
    CHECK(distance(simulation.vehicules().front().position,
                   pasFin.vehicules().front().position) < 1e-6);

    // Les simulations de même graine doivent être identiques à chaque pas,
    // pas seulement à la fin ; une autre graine doit changer les positions.
    parametres.nombreVehicules = 64;
    Simulation identique;
    Simulation differente;
    CHECK(simulation.initialiser(&boucle, parametres));
    CHECK(identique.initialiser(&boucle, parametres));
    ++parametres.graine;
    CHECK(differente.initialiser(&boucle, parametres));
    for (int pasNumero = 0; pasNumero < 1000; ++pasNumero) {
        simulation.avancer(0.04);
        identique.avancer(0.04);
        differente.avancer(0.04);
        CHECK(simulation.arrivees() == identique.arrivees());
        for (int i = 0; i < parametres.nombreVehicules; ++i) {
            CHECK(simulation.vehicules()[i].position == identique.vehicules()[i].position);
            CHECK(simulation.vehicules()[i].destination == identique.vehicules()[i].destination);
        }
        CHECK(positionsSurRoutes(simulation, boucle));
    }
    bool positionsDifferentes = false;
    for (int i = 0; i < parametres.nombreVehicules; ++i) {
        positionsDifferentes = positionsDifferentes
            || simulation.vehicules()[i].position != differente.vehicules()[i].position;
    }
    CHECK(positionsDifferentes);
    CHECK(simulation.arrivees() > 0);

    // Les entrées non finies ou négatives ne doivent pas altérer l'état.
    const double tempsAvant = simulation.temps();
    const QPointF positionAvant = simulation.vehicules().front().position;
    simulation.avancer(-1.0);
    simulation.avancer(std::numeric_limits<double>::infinity());
    simulation.avancer(std::numeric_limits<double>::quiet_NaN());
    CHECK(simulation.temps() == tempsAvant);
    CHECK(simulation.vehicules().front().position == positionAvant);

    // Seul avancer() est chronométré ; les contrôles géométriques restent hors
    // mesure. Les arrivées déclenchent bien leurs recherches A* dans la mesure.
    if (application.arguments().size() > 1) {
        QString erreur;
        const bool charge = OsmLoader::chargerFichier(application.arguments()[1], carte, &erreur);
        if (!charge) {
            std::cerr << erreur.toStdString() << '\n';
        }
        CHECK(charge);
        GrapheRoutier mulhouse;
        CHECK(mulhouse.construire(carte));
        parametres.nombreVehicules = 2000;
        parametres.graine = 42;
        CHECK(simulation.initialiser(&mulhouse, parametres));
        double totalMs = 0.0;
        double maximumMs = 0.0;
        QElapsedTimer chronometre;
        for (int i = 0; i < 750; ++i) {
            chronometre.start();
            simulation.avancer(0.04);
            const double ms = chronometre.nsecsElapsed() / 1000000.0;
            totalMs += ms;
            maximumMs = std::max(maximumMs, ms);
        }
        std::cout << "Mulhouse : " << totalMs / 750.0 << " ms en moyenne, "
                  << maximumMs << " ms au maximum, " << simulation.arrivees()
                  << " arrivees\n";
        CHECK(totalMs / 750.0 < 2.0);
        CHECK(simulation.arrivees() > 0);
        CHECK(positionsSurRoutes(simulation, mulhouse));
        for (int i = 0; i < 50; ++i) {
            simulation.avancer(2.0);
            CHECK(positionsSurRoutes(simulation, mulhouse));
        }
    }

    std::cout << "test_simulation : OK\n";
    return 0;
}
