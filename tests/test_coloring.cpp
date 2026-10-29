// Tests de la coloration : graphes de référence, erreurs de validation et stabilité.
// Les graphes aléatoires sont reproductibles ; seul le calcul de coloration est
// chronométré, sans inclure leur construction ni la vérification des résultats.

#include "coloring/Coloring.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>

// Cette vérification reste active en Release, y compris pour les performances.
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __FILE__ << ':' << __LINE__ << " : " << #condition << '\n'; \
            return 1; \
        } \
    } while (false)

namespace
{
// Toute arête est ajoutée dans les deux sens, conformément au contrat du module.
void ajouterArete(ListeAdjacence &graphe, int a, int b)
{
    graphe[a].append(b);
    graphe[b].append(a);
}

ListeAdjacence cycle(int nombre)
{
    ListeAdjacence graphe(nombre);
    for (int i = 0; i < nombre; ++i) {
        ajouterArete(graphe, i, (i + 1) % nombre);
    }
    return graphe;
}

ListeAdjacence grapheAleatoire(int nombre, int degreMoyen, unsigned int graine)
{
    std::mt19937 moteur(graine);
    std::uniform_real_distribution<double> tirage(0.0, 1.0);
    const double probabilite = static_cast<double>(degreMoyen) / (nombre - 1);
    ListeAdjacence graphe(nombre);
    // Chaque paire distincte est tirée une seule fois : ni boucle ni doublon.
    for (int i = 0; i < nombre; ++i) {
        for (int j = i + 1; j < nombre; ++j) {
            if (tirage(moteur) < probabilite) {
                ajouterArete(graphe, i, j);
            }
        }
    }
    return graphe;
}
}

int main()
{
    using Coloration::Algo;
    const Algo algorithmes[] = {Algo::Glouton, Algo::WelshPowell};
    CHECK(Coloration::nom(Algo::Glouton) == QStringLiteral("Glouton"));
    CHECK(Coloration::nom(Algo::WelshPowell) == QStringLiteral("Welsh-Powell"));

    ListeAdjacence complet(5);
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            ajouterArete(complet, i, j);
        }
    }
    ListeAdjacence biparti(6);
    for (int i = 0; i < 3; ++i) {
        for (int j = 3; j < 6; ++j) {
            ajouterArete(biparti, i, j);
        }
    }
    ListeAdjacence etoile(7);
    for (int i = 1; i < 7; ++i) {
        ajouterArete(etoile, 0, i);
    }
    ListeAdjacence petersen(10);
    for (int i = 0; i < 5; ++i) {
        ajouterArete(petersen, i, (i + 1) % 5);
        ajouterArete(petersen, i, i + 5);
        ajouterArete(petersen, i + 5, (i + 2) % 5 + 5);
    }
    ListeAdjacence couronne(8);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (i != j) {
                ajouterArete(couronne, 2 * i, 2 * j + 1);
            }
        }
    }
    // Chemin 0--2--3--1 : l'ordre naturel réserve 0 aux deux extrémités,
    // puis impose 1 et 2 au centre. Traiter le centre d'abord suffit à deux canaux.
    ListeAdjacence chemin(4);
    ajouterArete(chemin, 0, 2);
    ajouterArete(chemin, 2, 3);
    ajouterArete(chemin, 3, 1);

    struct Cas
    {
        const char *nom;
        ListeAdjacence graphe;
        int glouton;
        int welshPowell;
    };
    const Cas cas[] = {
        {"vide", {}, 0, 0},
        {"sommet isole", ListeAdjacence(1), 1, 1},
        {"sommets isoles", ListeAdjacence(6), 1, 1},
        {"K5", complet, 5, 5},
        {"C6", cycle(6), 2, 2},
        {"C7", cycle(7), 3, 3},
        {"K3,3", biparti, 2, 2},
        {"K1,6", etoile, 2, 2},
        {"Petersen", petersen, -1, -1},
        {"couronne", couronne, 4, 4},
        {"chemin favorable a Welsh-Powell", chemin, 3, 2}
    };
    for (const Cas &test : cas) {
        for (Algo algo : algorithmes) {
            const ResultatColoration resultat = Coloration::colorer(test.graphe, algo);
            int conflits = -1;
            CHECK(Coloration::estValide(test.graphe, resultat.couleurs, &conflits));
            CHECK(conflits == 0);
            CHECK(resultat.couleurs.size() == test.graphe.size());
            CHECK(std::isfinite(resultat.dureeMs) && resultat.dureeMs >= 0.0);
            const int maximum = resultat.couleurs.isEmpty() ? -1
                : *std::max_element(resultat.couleurs.cbegin(), resultat.couleurs.cend());
            CHECK(resultat.nombreCouleurs == maximum + 1);
            const int attendu = algo == Algo::Glouton ? test.glouton : test.welshPowell;
            if (attendu >= 0) {
                CHECK(resultat.nombreCouleurs == attendu);
            } else {
                CHECK(resultat.nombreCouleurs >= 3 && resultat.nombreCouleurs <= 4);
            }
            // Deux appels doivent conserver les mêmes choix, notamment à degré égal.
            CHECK(Coloration::colorer(test.graphe, algo).couleurs == resultat.couleurs);
        }
        std::cout << test.nom << " : OK\n";
    }
    // Ces affectations précises vérifient la plus petite couleur et le départage
    // par indice des deux sommets centraux, qui ont le même degré.
    CHECK((Coloration::colorer(chemin, Algo::Glouton).couleurs == QVector<int>{0, 0, 1, 2}));
    CHECK((Coloration::colorer(chemin, Algo::WelshPowell).couleurs == QVector<int>{1, 0, 0, 1}));
    CHECK(Coloration::colorer(chemin, Algo::WelshPowell).nombreCouleurs
          < Coloration::colorer(chemin, Algo::Glouton).nombreCouleurs);

    // K5 possède dix arêtes : on vérifie le comptage complet, puis un seul
    // conflit, même si chacune de ces arêtes figure deux fois dans l'adjacence.
    int conflits = -1;
    CHECK(!Coloration::estValide(complet, {0, 0, 0, 0, 0}, &conflits));
    CHECK(conflits == 10);
    CHECK(!Coloration::estValide(complet, {0, 0, 1, 2, 3}, &conflits));
    CHECK(conflits == 1);
    CHECK(!Coloration::estValide(complet, {0, 0, 1, 2, 3}));
    CHECK(Coloration::estValide(complet, {0, 1, 2, 3, 4}, &conflits));
    CHECK(conflits == 0);
    CHECK(!Coloration::estValide(complet, {-1, 0, 1, 2, 3}, &conflits));
    CHECK(conflits == 0);
    CHECK(!Coloration::estValide(ListeAdjacence(1), {-1}));
    conflits = -1;
    CHECK(!Coloration::estValide(complet, {0, 1}, &conflits));
    CHECK(conflits == 0);
    CHECK(!Coloration::estValide(complet, {0, 1, 2, 3, 4, 5}));
    CHECK(!Coloration::estValide({}, {0}));
    CHECK(Coloration::estValide({}, {}, &conflits));
    CHECK(conflits == 0);
    std::cout << "validation : OK\n";

    CHECK(Coloration::stabilite({0, 1, 2, 3}, {0, 1, 2, 3}) == 1.0);
    CHECK(Coloration::stabilite({0, 1, 2, 3}, {0, 1, 0, 3}) == 0.75);
    CHECK(Coloration::stabilite({0, 1}, {1, 0}) == 0.0);
    CHECK(Coloration::stabilite({}, {}) == 1.0);
    // Tailles différentes : seuls les changements de la partie commune comptent,
    // avec la plus grande taille au dénominateur, dans les deux sens.
    CHECK(Coloration::stabilite({0, 1}, {0, 2, 3, 4}) == 0.75);
    CHECK(Coloration::stabilite({0, 2, 3, 4}, {0, 1}) == 0.75);
    CHECK(Coloration::stabilite({0}, {0, 1}) == 1.0);
    CHECK(Coloration::stabilite({}, {0, 1}) == 1.0);
    CHECK(Coloration::stabilite({0, 1}, {}) == 1.0);
    std::cout << "stabilite : OK\n";

    constexpr int nombre = 2000;
    constexpr int repetitions = 20;
    std::cout << std::fixed << std::setprecision(4);
    for (int degreVise : {10, 30}) {
        double totalMs[2] = {};
        double totalCouleurs[2] = {};
        double totalDegre = 0.0;
        for (int essai = 0; essai < repetitions; ++essai) {
            const ListeAdjacence graphe = grapheAleatoire(
                nombre, degreVise, 1729u + static_cast<unsigned int>(essai));
            int degreMax = 0;
            for (const QVector<int> &voisins : graphe) {
                degreMax = std::max(degreMax, static_cast<int>(voisins.size()));
                totalDegre += static_cast<double>(voisins.size()) / nombre;
            }
            for (int i = 0; i < 2; ++i) {
                const ResultatColoration resultat = Coloration::colorer(graphe, algorithmes[i]);
                CHECK(Coloration::estValide(graphe, resultat.couleurs));
                CHECK(resultat.nombreCouleurs <= degreMax + 1);
                CHECK(resultat.nombreCouleurs
                      == *std::max_element(resultat.couleurs.cbegin(), resultat.couleurs.cend()) + 1);
                CHECK(std::isfinite(resultat.dureeMs) && resultat.dureeMs >= 0.0);
                totalMs[i] += resultat.dureeMs;
                totalCouleurs[i] += resultat.nombreCouleurs;
            }
        }
        CHECK(std::abs(totalDegre / repetitions - degreVise) < 1.0);
        for (int i = 0; i < 2; ++i) {
            std::cout << Coloration::nom(algorithmes[i]).toStdString()
                      << " : N=" << nombre << ", degre moyen=" << totalDegre / repetitions
                      << ", k moyen=" << totalCouleurs[i] / repetitions
                      << ", ms moyens=" << totalMs[i] / repetitions
                      << " (" << repetitions << " essais)\n";
            CHECK(totalMs[i] / repetitions < 5.0);
        }
        std::cout << "graphes aleatoires de degre " << degreVise << " : OK\n";
    }

    std::cout << "test_coloring : OK\n";
    return 0;
}
