// Coloration du graphe d'interférences : chaque couleur représente un canal radio.
// Le Glouton suit les indices ; Welsh-Powell construit une classe de couleur à
// la fois, par degré décroissant. Des marqueurs réutilisés évitent les remises
// à zéro et les allocations à chaque sommet.

#include "coloring/Coloring.h"

#include <QElapsedTimer>

#include <algorithm>
#include <numeric>

QString Coloration::nom(Algo algo)
{
    return algo == Algo::Glouton ? QStringLiteral("Glouton")
                                : QStringLiteral("Welsh-Powell");
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
    } else {
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
