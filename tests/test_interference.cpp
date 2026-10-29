// Tests du graphe d'interférences : cas géométriques connus, comparaison avec
// toutes les paires et mesure sur une zone de 8 km par 8 km.
// Les graines fixes rendent les échecs reproductibles sur toute la chaîne.

#include "interference/InterferenceGraph.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>

// Cette vérification reste active en Release, contrairement à assert.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __FILE__ << ':' << __LINE__ << " : " << #condition << '\n'; \
            return 1; \
        } \
    } while (false)

namespace
{
bool memesStatistiques(const StatsInterference &a, const StatsInterference &b)
{
    return a.sommets == b.sommets && a.aretes == b.aretes
        && a.degreMoyen == b.degreMoyen && a.degreMax == b.degreMax;
}

// Vérifier les invariants indépendamment de l'autre méthode : deux erreurs
// identiques dans la grille et la référence ne doivent pas se masquer.
int verifier(const GrapheInterference &graphe)
{
    const auto &voisins = graphe.voisins();
    QVector<QPair<int, int>> aretes;
    qsizetype sommeDegres = 0;
    int degreMax = 0;
    for (int i = 0; i < voisins.size(); ++i) {
        int precedent = -1;
        for (int j : voisins[i]) {
            CHECK(j >= 0 && j < voisins.size());
            CHECK(j != i);
            CHECK(j > precedent);
            CHECK(std::binary_search(voisins[j].cbegin(), voisins[j].cend(), i));
            precedent = j;
            if (i < j) {
                aretes.append({i, j});
            }
        }
        sommeDegres += voisins[i].size();
        degreMax = std::max(degreMax, static_cast<int>(voisins[i].size()));
    }

    const StatsInterference stats = graphe.stats();
    CHECK(graphe.aretes() == aretes);
    CHECK(stats.sommets == voisins.size());
    CHECK(2 * stats.aretes == sommeDegres);
    CHECK(stats.aretes == aretes.size());
    CHECK(stats.degreMax == degreMax);
    CHECK(stats.degreMoyen == (voisins.isEmpty() ? 0.0 : 2.0 * aretes.size() / voisins.size()));
    CHECK(std::isfinite(stats.dureeMs) && stats.dureeMs >= 0.0);
    if (voisins.isEmpty()) {
        CHECK(stats.dureeMs == 0.0);
    }
    return 0;
}

QVector<QPointF> positionsAleatoires(int nombre, std::mt19937 &generateur)
{
    std::uniform_real_distribution<double> coordonnee(-4000.0, 4000.0);
    QVector<QPointF> positions;
    positions.reserve(nombre);
    for (int i = 0; i < nombre; ++i) {
        const double x = coordonnee(generateur);
        const double y = coordonnee(generateur);
        positions.append({x, y});
    }
    return positions;
}
}

int main()
{
    GrapheInterference grille;
    GrapheInterference naif;
    CHECK(verifier(grille) == 0);

    struct Cas
    {
        QVector<QPointF> positions;
        QVector<double> rayons;
        QVector<QVector<int>> voisins;
    };
    const Cas cas[] = {
        // Le seuil est strict, y compris sur une diagonale de longueur 300.
        {{{0, 0}, {299, 0}}, {300, 300}, {{1}, {0}}},
        {{{0, 0}, {300, 0}}, {300, 300}, {{}, {}}},
        {{{0, 0}, {180, 240}}, {300, 300}, {{}, {}}},
        {{{0, 0}, {std::nextafter(300.0, 0.0), 0}}, {300, 300}, {{1}, {0}}},
        {{{0, 0}, {std::nextafter(300.0, 400.0), 0}}, {300, 300}, {{}, {}}},
        // Le plus grand rayon décide de l'arête, quel que soit l'ordre des sommets.
        {{{0, 0}, {350, 0}}, {100, 400}, {{1}, {0}}},
        {{{0, 0}, {350, 0}}, {400, 100}, {{1}, {0}}},
        // Les frontières négatives et le passage par zéro doivent garder les voisins.
        {{{-300.1, -300.1}, {-299.9, -299.9}}, {300, 300}, {{1}, {0}}},
        {{{-0.1, -0.1}, {0.1, 0.1}}, {300, 300}, {{1}, {0}}},
        {{{-300, -300}, {-600, -300}}, {300, 300}, {{}, {}}},
        {{{0, 0}, {200, 0}, {400, 0}}, {300, 300, 300}, {{1}, {0, 2}, {1}}},
        // Les positions confondues sont reliées sans boucle ni arête en double.
        {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}, {300, 300, 300, 300},
         {{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}}},
        // Sans portée propre, un véhicule peut quand même être atteint par un autre.
        {{{0, 0}, {299, 0}, {-299, 0}}, {0, 300, -5}, {{1}, {0}, {}}},
        {{{0, 0}, {99, 0}}, {100, -10}, {{1}, {0}}},
        {{{0, 0}, {0, 0}}, {0, -10}, {{}, {}}},
        {{{0, 0}, {1, 0}}, {-100, -300}, {{}, {}}},
        // Les éléments sans position ou sans rayon correspondant sont ignorés.
        {{{0, 0}, {200, 0}, {400, 0}}, {300, 300}, {{1}, {0}}},
        {{{0, 0}, {299, 0}}, {300, 300, 5000}, {{1}, {0}}},
        {{{0, 0}, {299, 0}}, {300}, {{}}},
        // Réutiliser les objets vérifie aussi l'effacement du graphe précédent.
        {{}, {}, {}},
        {{}, {300}, {}},
        {{{0, 0}}, {}, {}}
    };
    for (const Cas &exemple : cas) {
        grille.construire(exemple.positions, exemple.rayons);
        naif.construireNaif(exemple.positions, exemple.rayons);
        CHECK(grille.voisins() == exemple.voisins);
        CHECK(naif.voisins() == exemple.voisins);
        CHECK(memesStatistiques(grille.stats(), naif.stats()));
        CHECK(verifier(grille) == 0);
        CHECK(verifier(naif) == 0);
    }

    // Des rayons uniformes puis variables exercent la grille avec plusieurs tailles
    // de cellule. Les petits N vérifient les cas vides et isolés parmi les reconstructions.
    for (unsigned int graine : {1u, 42u, 2026u, 0xC0FFEEu}) {
        std::mt19937 generateur(graine);
        std::uniform_real_distribution<double> rayon(50.0, 500.0);
        for (int nombre : {0, 1, 50, 500, 2000}) {
            const auto positions = positionsAleatoires(nombre, generateur);
            for (bool variables : {false, true}) {
                QVector<double> rayons(nombre, 300.0);
                if (variables) {
                    for (double &portee : rayons) {
                        portee = rayon(generateur);
                    }
                }
                grille.construire(positions, rayons);
                naif.construireNaif(positions, rayons);
                CHECK(grille.voisins() == naif.voisins());
                CHECK(memesStatistiques(grille.stats(), naif.stats()));
                CHECK(verifier(grille) == 0);
                CHECK(verifier(naif) == 0);
            }
        }
    }

    // Chronométrer l'appel complet inclut les allocations et leur libération.
    // Un premier passage prépare les caches ; la moyenne de 50 réduit le bruit.
    std::cout << std::fixed << std::setprecision(3);
    for (int nombre : {2000, 5000}) {
        std::mt19937 generateur(12345u);
        const auto positions = positionsAleatoires(nombre, generateur);
        const QVector<double> rayons(nombre, 300.0);
        grille.construire(positions, rayons);
        naif.construireNaif(positions, rayons);
        constexpr int repetitions = 50;
        QElapsedTimer chronometre;
        chronometre.start();
        for (int repetition = 0; repetition < repetitions; ++repetition) {
            grille.construire(positions, rayons);
        }
        const double moyenneGrille = chronometre.nsecsElapsed() / 1000000.0 / repetitions;
        chronometre.restart();
        for (int repetition = 0; repetition < repetitions; ++repetition) {
            naif.construireNaif(positions, rayons);
        }
        const double moyenneNaif = chronometre.nsecsElapsed() / 1000000.0 / repetitions;
        std::cout << nombre << " vehicules : grille " << moyenneGrille << " ms, naif "
                  << moyenneNaif << " ms (moyennes sur " << repetitions << " passages)\n";
        CHECK(grille.voisins() == naif.voisins());
        CHECK(memesStatistiques(grille.stats(), naif.stats()));
        CHECK(verifier(grille) == 0);
        CHECK(verifier(naif) == 0);
        if (nombre == 2000) {
            CHECK(moyenneGrille < 3.0);
        }
    }

    std::cout << "test_interference : OK\n";
    return 0;
}
