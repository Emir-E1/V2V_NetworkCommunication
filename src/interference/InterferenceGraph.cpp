// Graphe d'interférences dynamique : les sommets sont les véhicules et les
// arêtes représentent les portées radio qui se recouvrent selon la règle du maximum.
// Une grille limite les recherches aux neuf cellules voisines ; la version
// naïve compare toutes les paires pour vérifier le résultat et mesurer le gain.

#include "interference/InterferenceGraph.h"

#include <QElapsedTimer>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace
{
// Comparer les carrés évite une racine par paire. Le test de positivité empêche
// qu'un rayon négatif, une fois élevé au carré, crée une portée inexistante.
bool enInterference(const QPointF &a, const QPointF &b, double rayonA, double rayonB)
{
    const double rayon = std::max(rayonA, rayonB);
    const QPointF ecart = a - b;
    return rayon > 0.0 && QPointF::dotProduct(ecart, ecart) < rayon * rayon;
}

// Les deux méthodes publient les mêmes voisins triés et les mêmes statistiques.
// Chaque arête figure dans deux listes : la somme des degrés vaut donc 2E.
void finaliser(QVector<QVector<int>> &voisins, StatsInterference &stats,
               const QElapsedTimer &chronometre)
{
    qsizetype sommeDegres = 0;
    stats.sommets = static_cast<int>(voisins.size());
    for (QVector<int> &liste : voisins) {
        std::sort(liste.begin(), liste.end());
        sommeDegres += liste.size();
        stats.degreMax = std::max(stats.degreMax, static_cast<int>(liste.size()));
    }
    stats.aretes = static_cast<int>(sommeDegres / 2);
    stats.degreMoyen = stats.sommets == 0 ? 0.0 : 2.0 * stats.aretes / stats.sommets;
    stats.dureeMs = chronometre.nsecsElapsed() / 1000000.0;
}
}

void GrapheInterference::construire(const QVector<QPointF> &positions,
                                   const QVector<double> &rayons)
{
    QElapsedTimer chronometre;
    chronometre.start();
    const int nombre = static_cast<int>(std::min(positions.size(), rayons.size()));
    m_voisins = QVector<QVector<int>>(nombre);
    m_stats = {};
    if (nombre == 0) {
        return;  // Une entrée vide efface aussi le graphe de l'étape précédente.
    }

    // Seuls les véhicules retenus comptent dans la taille des cellules.
    // Si personne n'émet, les sommets existent mais aucune arête n'est possible.
    double cellule = 0.0;
    for (int i = 0; i < nombre; ++i) {
        cellule = std::max(cellule, rayons[i]);
    }
    if (cellule <= 0.0) {
        finaliser(m_voisins, m_stats, chronometre);
        return;
    }

    // Pour une carte dispersée, QHash évite le tri des indices et le parcours
    // des cases vides d'un tri par comptage : construction linéaire en moyenne,
    // mémoire limitée aux cases occupées. On réserve pour éviter les réallocations.
    using Cellule = QPair<qint64, qint64>;
    QHash<Cellule, QVector<int>> grille;
    grille.reserve(nombre);
    QVector<Cellule> cellules;
    cellules.reserve(nombre);
    for (int i = 0; i < nombre; ++i) {
        // floor, contrairement à une troncature, place correctement les positions
        // négatives : -0,1 appartient à la case -1 et non à la case 0.
        const Cellule cle{static_cast<qint64>(std::floor(positions[i].x() / cellule)),
                          static_cast<qint64>(std::floor(positions[i].y() / cellule))};
        cellules.append(cle);
        grille[cle].append(i);
    }

    for (int i = 0; i < nombre; ++i) {
        const Cellule cle = cellules[i];
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                // Une portée ne dépasse jamais la taille d'une cellule : au-delà
                // de ces neuf cases, la distance ne peut pas satisfaire le seuil.
                const auto caseVoisine = grille.constFind({cle.first + dx, cle.second + dy});
                if (caseVoisine == grille.cend()) {
                    continue;
                }
                for (int j : caseVoisine.value()) {
                    // j > i exclut les boucles et calcule chaque paire une seule
                    // fois ; l'ajout dans les deux sens garantit la symétrie.
                    if (j > i && enInterference(positions[i], positions[j], rayons[i], rayons[j])) {
                        m_voisins[i].append(j);
                        m_voisins[j].append(i);
                    }
                }
            }
        }
    }
    finaliser(m_voisins, m_stats, chronometre);
}

void GrapheInterference::construireNaif(const QVector<QPointF> &positions,
                                       const QVector<double> &rayons)
{
    QElapsedTimer chronometre;
    chronometre.start();
    const int nombre = static_cast<int>(std::min(positions.size(), rayons.size()));
    m_voisins = QVector<QVector<int>>(nombre);
    m_stats = {};
    if (nombre == 0) {
        return;
    }

    // Même règle radio, sans filtre spatial : cette référence coûte N(N-1)/2
    // comparaisons, mais permet de détecter une cellule oubliée dans la grille.
    for (int i = 0; i < nombre; ++i) {
        for (int j = i + 1; j < nombre; ++j) {
            if (enInterference(positions[i], positions[j], rayons[i], rayons[j])) {
                m_voisins[i].append(j);
                m_voisins[j].append(i);
            }
        }
    }
    finaliser(m_voisins, m_stats, chronometre);
}

QVector<QPair<int, int>> GrapheInterference::aretes() const
{
    QVector<QPair<int, int>> resultat;
    resultat.reserve(m_stats.aretes);
    // La liste symétrique stocke chaque liaison deux fois ; i < j n'en garde
    // qu'une pour le dessin, dans un ordre stable entre deux reconstructions.
    for (int i = 0; i < m_voisins.size(); ++i) {
        for (int j : m_voisins[i]) {
            if (i < j) {
                resultat.append({i, j});
            }
        }
    }
    return resultat;
}
