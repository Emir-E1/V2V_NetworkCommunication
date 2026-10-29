// Coloration du graphe d'interférences : chaque couleur représente un canal radio.
// Le Glouton suit les indices ; Welsh-Powell construit une classe de couleur à
// la fois, par degré décroissant. Des marqueurs réutilisés évitent les remises
// à zéro et les allocations à chaque sommet. DSATUR (Brélaz, 1979) privilégie
// les sommets entourés du plus de couleurs distinctes. La recherche locale
// (TabuCol, Hertz et de Werra, 1987) part de DSATUR et tente de retirer des
// couleurs par déplacements tabous, en conservant toujours un résultat valide.

#include "coloring/Coloring.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
void dsatur(const ListeAdjacence &voisins, ResultatColoration &resultat)
{
    const int nombre = static_cast<int>(voisins.size());
    int degreMax = 0;
    // La saturation compte les couleurs DISTINCTES, pas les voisins colorés.
    // Un bit par couleur permet de tester sa présence sans allocation par arête ;
    // degré maximal + 1 borne le nombre de couleurs d'une coloration gloutonne.
    std::set<std::tuple<int, int, int>> priorites;
    for (int sommet = 0; sommet < nombre; ++sommet) {
        const int degre = static_cast<int>(voisins[sommet].size());
        degreMax = std::max(degreMax, degre);
        priorites.emplace(0, degre, -sommet);
    }
    const int mots = degreMax / 64 + 1;
    std::vector<std::uint64_t> presentes(static_cast<std::size_t>(nombre) * mots, 0);
    std::vector<int> saturation(nombre, 0);
    int *couleurs = resultat.couleurs.data();
    while (!priorites.empty()) {
        const auto premier = std::prev(priorites.end());
        const int sommet = -std::get<2>(*premier);
        priorites.erase(premier);
        const auto *masque = presentes.data() + static_cast<std::size_t>(sommet) * mots;
        int couleur = 0;
        while (masque[couleur / 64] & (std::uint64_t{1} << (couleur % 64))) {
            ++couleur;
        }
        couleurs[sommet] = couleur;
        resultat.nombreCouleurs = std::max(resultat.nombreCouleurs, couleur + 1);
        for (int voisin : voisins[sommet]) {
            if (couleurs[voisin] >= 0) {
                continue;
            }
            auto &mot = presentes[static_cast<std::size_t>(voisin) * mots + couleur / 64];
            const auto bit = std::uint64_t{1} << (couleur % 64);
            if (!(mot & bit)) {
                mot |= bit;
                // Départage : degré TOTAL décroissant, puis indice croissant.
                // Seule une nouvelle couleur voisine change la priorité : O(log n).
                // L'extraction réutilise le nœud de l'arbre sans le réallouer.
                auto noeud = priorites.extract({saturation[voisin],
                    static_cast<int>(voisins[voisin].size()), -voisin});
                std::get<0>(noeud.value()) = ++saturation[voisin];
                priorites.insert(std::move(noeud));
            }
        }
    }
}

void rechercheLocale(const ListeAdjacence &voisins, ResultatColoration &resultat)
{
    const int nombre = static_cast<int>(voisins.size());
    std::mt19937 moteur(1979u);
    std::uniform_int_distribution<int> delai(0, 9);
    int deplacements = 0;
    while (resultat.nombreCouleurs > 1
           && deplacements < Coloration::ITERATIONS_RECHERCHE_LOCALE) {
        const int k = resultat.nombreCouleurs - 1;
        QVector<int> tentative = resultat.couleurs;
        int *couleurs = tentative.data();
        // gamma[v*k+c] compte les voisins de v portant c. Le tableau plat
        // rend l'évaluation d'un déplacement constante et favorise le cache.
        const auto taille = static_cast<std::size_t>(nombre) * k;
        std::vector<int> gamma(taille, 0);
        std::vector<int> tabou(taille, 0);
        for (int sommet = 0; sommet < nombre; ++sommet) {
            int *ligne = gamma.data() + static_cast<std::size_t>(sommet) * k;
            for (int voisin : voisins[sommet]) {
                if (couleurs[voisin] < k) {
                    ++ligne[couleurs[voisin]];
                }
            }
        }
        // On supprime la dernière couleur : chaque sommet concerné prend celle
        // créant le moins de conflits, en tenant compte des réaffectations précédentes.
        for (int sommet = 0; sommet < nombre; ++sommet) {
            if (couleurs[sommet] != k) {
                continue;
            }
            const int *ligne = gamma.data() + static_cast<std::size_t>(sommet) * k;
            const int couleur = static_cast<int>(std::min_element(ligne, ligne + k) - ligne);
            couleurs[sommet] = couleur;
            for (int voisin : voisins[sommet]) {
                ++gamma[static_cast<std::size_t>(voisin) * k + couleur];
            }
        }

        // Cette liste ne contient que les sommets en conflit. Une suppression
        // par échange avec le dernier évite de reparcourir les n sommets à chaque coup.
        std::vector<int> actifs;
        actifs.reserve(nombre);
        std::vector<int> position(nombre, -1);
        const auto actualiser = [&](int sommet) {
            const bool conflit = gamma[static_cast<std::size_t>(sommet) * k + couleurs[sommet]] > 0;
            if (conflit && position[sommet] < 0) {
                position[sommet] = static_cast<int>(actifs.size());
                actifs.push_back(sommet);
            } else if (!conflit && position[sommet] >= 0) {
                const int place = position[sommet];
                actifs[place] = actifs.back();
                position[actifs.back()] = place;
                actifs.pop_back();
                position[sommet] = -1;
            }
        };
        int conflits = 0;
        for (int sommet = 0; sommet < nombre; ++sommet) {
            conflits += gamma[static_cast<std::size_t>(sommet) * k + couleurs[sommet]];
            actualiser(sommet);
        }
        conflits /= 2;
        int meilleurConflits = conflits;
        while (conflits > 0 && deplacements < Coloration::ITERATIONS_RECHERCHE_LOCALE) {
            int sommetChoisi = -1;
            int couleurChoisie = -1;
            int meilleurDelta = std::numeric_limits<int>::max();
            // Le départ aléatoire départage les coups équivalents sans toujours
            // favoriser les mêmes sommets. La graine fixe rend le parcours reproductible.
            const int depart = static_cast<int>(moteur() % actifs.size());
            const int nombreActifs = static_cast<int>(actifs.size());
            for (int i = 0; i < nombreActifs; ++i) {
                const int sommet = actifs[(depart + i) % nombreActifs];
                const auto debut = static_cast<std::size_t>(sommet) * k;
                const int *ligne = gamma.data() + debut;
                for (int couleur = 0; couleur < k; ++couleur) {
                    if (couleur == couleurs[sommet]) {
                        continue;
                    }
                    const int delta = ligne[couleur] - ligne[couleurs[sommet]];
                    // La liste taboue interdit un retour immédiat vers l'ancienne
                    // couleur. L'aspiration l'autorise si le meilleur score de
                    // cette tentative est battu : on ne bloque pas un progrès réel.
                    if (delta < meilleurDelta
                        && (tabou[debut + couleur] <= deplacements
                            || conflits + delta < meilleurConflits)) {
                        sommetChoisi = sommet;
                        couleurChoisie = couleur;
                        meilleurDelta = delta;
                    }
                }
            }
            if (sommetChoisi < 0) {
                break;
            }
            const int ancienne = couleurs[sommetChoisi];
            couleurs[sommetChoisi] = couleurChoisie;
            conflits += meilleurDelta;
            ++deplacements;
            tabou[static_cast<std::size_t>(sommetChoisi) * k + ancienne]
                = deplacements + delai(moteur) + static_cast<int>(0.6 * conflits);
            // Un déplacement ne change gamma que chez les voisins : O(degré).
            for (int voisin : voisins[sommetChoisi]) {
                int *ligne = gamma.data() + static_cast<std::size_t>(voisin) * k;
                --ligne[ancienne];
                ++ligne[couleurChoisie];
                actualiser(voisin);
            }
            actualiser(sommetChoisi);
            meilleurConflits = std::min(meilleurConflits, conflits);
        }
        if (conflits != 0) {
            break;
        }
        // Seule une tentative sans conflit remplace le meilleur résultat valide.
        resultat.couleurs = std::move(tentative);
        resultat.nombreCouleurs = k;
    }
}
}

QString Coloration::nom(Algo algo)
{
    switch (algo) {
    case Algo::Glouton: return QStringLiteral("Glouton");
    case Algo::WelshPowell: return QStringLiteral("Welsh-Powell");
    case Algo::Dsatur: return QStringLiteral("DSATUR");
    case Algo::RechercheLocale: return QStringLiteral("Recherche locale");
    }
    return {};
}

QString Coloration::nomCourt(Algo algo)
{
    switch (algo) {
    case Algo::Glouton: return QStringLiteral("glouton");
    case Algo::WelshPowell: return QStringLiteral("welsh_powell");
    case Algo::Dsatur: return QStringLiteral("dsatur");
    case Algo::RechercheLocale: return QStringLiteral("recherche_locale");
    }
    return {};
}

ResultatColoration Coloration::colorer(const ListeAdjacence &voisins, Algo algo)
{
    QElapsedTimer chronometre;
    chronometre.start();
    const int nombre = static_cast<int>(voisins.size());
    ResultatColoration resultat;
    resultat.couleurs.fill(-1, nombre);
    QVector<int> interdits(nombre, -1);

    if (algo == Algo::Glouton) {
        // interdits[c] porte l'indice du sommet courant si un voisin utilise c.
        // Changer d'indice rend les anciennes marques caduques sans les effacer.
        // La recherche de couleur examine au plus degré + 1 cases : O(n + m).
        for (int sommet = 0; sommet < nombre; ++sommet) {
            for (int voisin : voisins[sommet]) {
                const int couleur = resultat.couleurs[voisin];
                if (couleur >= 0) {
                    interdits[couleur] = sommet;
                }
            }
            int couleur = 0;
            while (interdits[couleur] == sommet) {
                ++couleur;
            }
            resultat.couleurs[sommet] = couleur;
            resultat.nombreCouleurs = std::max(resultat.nombreCouleurs, couleur + 1);
        }
    } else if (algo == Algo::WelshPowell) {
        QVector<int> ordre(nombre);
        std::iota(ordre.begin(), ordre.end(), 0);
        // Les sommets de fort degré ont plus de voisins à éviter. Les traiter
        // d'abord leur laisse davantage de choix et évite souvent d'ouvrir des
        // canaux supplémentaires à la fin ; ce n'est pas une garantie d'optimalité.
        std::sort(ordre.begin(), ordre.end(), [&](int a, int b) {
            if (voisins[a].size() != voisins[b].size()) {
                return voisins[a].size() > voisins[b].size();
            }
            return a < b;
        });

        // Ici interdits[v] est la couleur qu'un voisin vient de recevoir.
        // Une classe est un ensemble de sommets sans arête entre eux : après
        // chaque affectation, on exclut ses voisins de la classe courante.
        // Chaque liste de voisins n'est parcourue qu'une fois au total ; seuls
        // les passages sur l'ordre sont répétés, soit O(n log n + n*k + m).
        int restants = nombre;
        while (restants > 0) {
            const int couleur = resultat.nombreCouleurs;
            for (int sommet : ordre) {
                if (resultat.couleurs[sommet] >= 0 || interdits[sommet] == couleur) {
                    continue;
                }
                resultat.couleurs[sommet] = couleur;
                --restants;
                for (int voisin : voisins[sommet]) {
                    interdits[voisin] = couleur;
                }
            }
            ++resultat.nombreCouleurs;
        }
    } else {
        dsatur(voisins, resultat);
        if (algo == Algo::RechercheLocale) {
            rechercheLocale(voisins, resultat);
        }
    }

    resultat.dureeMs = chronometre.nsecsElapsed() / 1000000.0;
    return resultat;
}

bool Coloration::estValide(const ListeAdjacence &voisins, const QVector<int> &couleurs,
                          int *conflits)
{
    if (conflits) {
        *conflits = 0;
    }
    if (voisins.size() != couleurs.size()) {
        return false;
    }

    bool valide = true;
    int nombreConflits = 0;
    for (int sommet = 0; sommet < voisins.size(); ++sommet) {
        if (couleurs[sommet] < 0) {
            valide = false;
        }
        // On poursuit après un conflit pour tous les compter. Le graphe étant
        // non orienté, le test d'indice évite de compter aussi l'arête inverse.
        for (int voisin : voisins[sommet]) {
            if (couleurs[sommet] == couleurs[voisin]) {
                valide = false;
                if (sommet < voisin) {
                    ++nombreConflits;
                }
            }
        }
    }
    if (conflits) {
        *conflits = nombreConflits;
    }
    return valide;
}

double Coloration::stabilite(const QVector<int> &avant, const QVector<int> &apres)
{
    const qsizetype nombre = std::max(avant.size(), apres.size());
    if (nombre == 0) {
        return 1.0;
    }
    qsizetype changements = 0;
    // Le contrat compare seulement les véhicules présents dans les deux
    // vecteurs : une entrée absente n'est pas un changement de couleur.
    for (qsizetype i = 0; i < std::min(avant.size(), apres.size()); ++i) {
        if (avant[i] != apres[i]) {
            ++changements;
        }
    }
    return 1.0 - static_cast<double>(changements) / static_cast<double>(nombre);
}
