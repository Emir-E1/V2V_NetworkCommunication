// Bâtiments de la vue 3D (voir Batiments.h) : trois étapes, toutes faites une seule fois
// au chargement de la carte, côté processeur (les tableaux obtenus ne dépendent pas d'OpenGL) :
//   1. charger()            : lit le fichier compact de data/telecharger_batiments.py et
//                             convertit chaque contour en mètres (même projection que les routes) ;
//   2. trianguler()         : découpe le contour du toit en triangles, seule forme que la carte
//                             graphique sait remplir (méthode des « oreilles », expliquée plus bas) ;
//   3. construireMaillage() : assemble murs et toits de tous les bâtiments en deux grands tableaux,
//                             envoyés d'un coup à la carte graphique par Vue3D.

#include "vue3d/Batiments.h"

#include <QFile>

#include <cstring>
#include <limits>
#include <utility>

namespace
{
bool espace(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

// Le curseur reste dans sa ligne, même si un entier est absent ou déborde.
// Aucun jeton temporaire : le gros fichier est parcouru directement en mémoire.
bool lireEntier(const char *&p, const char *fin, int &valeur)
{
    while (p != fin && espace(*p)) {
        ++p;
    }
    bool negatif = false;
    if (p != fin && (*p == '-' || *p == '+')) {
        negatif = *p++ == '-';
    }
    if (p == fin || *p < '0' || *p > '9') {
        return false;
    }
    const quint32 limite = quint32(std::numeric_limits<int>::max()) + negatif;
    quint32 nombre = 0;
    while (p != fin && *p >= '0' && *p <= '9') {
        const quint32 chiffre = quint32(*p++ - '0');
        if (nombre > (limite - chiffre) / 10) {
            return false;
        }
        nombre = nombre * 10 + chiffre;
    }
    if (p != fin && !espace(*p)) {
        return false;
    }
    valeur = int(negatif ? -qint64(nombre) : qint64(nombre));
    return true;
}

// Sens du virage a -> b -> c (produit vectoriel) : positif d'un côté, négatif de l'autre,
// nul si les trois points sont alignés. Sommé sur tout le contour, il donne deux fois
// l'aire orientée, dont le signe dit dans quel sens le contour est parcouru.
double tourner(const QPointF &a, const QPointF &b, const QPointF &c)
{
    return (b.x() - a.x()) * (c.y() - a.y())
         - (b.y() - a.y()) * (c.x() - a.x());
}
}

bool Batiments::charger(const QString &chemin, const OsmCarte &carte,
                        QVector<Batiment> &batiments, QString *erreur)
{
    batiments.clear();
    if (erreur) {
        erreur->clear();
    }
    QFile fichier(chemin);
    if (!fichier.open(QIODevice::ReadOnly)) {
        if (erreur) {
            *erreur = QStringLiteral("Impossible d'ouvrir « %1 » : %2")
                          .arg(chemin, fichier.errorString());
        }
        return false;
    }
    const QByteArray texte = fichier.readAll();
    batiments.reserve(texte.count('\n') + 1);
    const char *p = texte.constData();
    const char *fin = p + texte.size();
    while (p != fin) {
        const char *saut = static_cast<const char *>(std::memchr(p, '\n', fin - p));
        const char *finLigne = saut ? saut : fin;
        const char *curseur = p;
        p = saut ? saut + 1 : fin;
        while (curseur != finLigne && espace(*curseur)) {
            ++curseur;
        }
        if (curseur == finLigne || *curseur == '#') {
            continue;
        }
        int hauteur, n;
        if (!lireEntier(curseur, finLigne, hauteur)
            || !lireEntier(curseur, finLigne, n) || hauteur <= 0 || n < 3
            || n > (finLigne - curseur) / 2) {
            continue;
        }

        // Le premier couple est absolu ; les suivants s'ajoutent au précédent.
        // Les sommes utilisent 64 bits pour éviter un débordement sur une ligne invalide.
        Batiment batiment;
        batiment.hauteur = hauteur / 10.0f;
        batiment.contour.reserve(n);
        qint64 latitude = 0, longitude = 0;
        for (int i = 0; i < n; ++i) {
            int lat, lon;
            if (!lireEntier(curseur, finLigne, lat)
                || !lireEntier(curseur, finLigne, lon)) {
                break;
            }
            latitude += lat;
            longitude += lon;
            batiment.contour.append(carte.versMetres(latitude / 1e6, longitude / 1e6));
        }
        while (curseur != finLigne && espace(*curseur)) {
            ++curseur;
        }
        if (batiment.contour.size() == n && curseur == finLigne) {
            batiments.append(std::move(batiment));
        }
    }
    return true;
}

// Méthode des oreilles : une « oreille » est un coin convexe du contour dont le triangle
// (coin précédent, coin, coin suivant) ne contient aucun autre point du contour. On peut
// alors couper ce triangle et retirer le coin : il reste un contour plus petit, toujours
// simple. On recommence jusqu'à ce qu'il ne reste qu'un triangle. Tout polygone simple a au
// moins deux oreilles, donc la méthode aboutit toujours (n - 2 triangles pour n points).
QVector<int> Batiments::trianguler(const QVector<QPointF> &contour)
{
    const int n = int(contour.size());
    if (n < 3) {
        return {};
    }
    QVector<int> triangles;
    triangles.reserve(3 * (n - 2));
    QVector<int> restants;
    restants.reserve(n);
    double aire = 0.0;
    for (int i = 0; i < n; ++i) {
        restants.append(i);
        // Le signe de l'aire orientée indique le sens du parcours. On prend
        // le premier point comme origine pour limiter les erreurs d'arrondi.
        aire += tourner(contour[0], contour[i], contour[(i + 1) % n]);
    }
    const double sens = aire < 0.0 ? -1.0 : 1.0;
    int courant = 0, essais = 0, contactDiffere = -1;
    while (restants.size() > 2) {
        const int taille = int(restants.size());
        const int avant = restants[(courant + taille - 1) % taille];
        const int milieu = restants[courant];
        const int apres = restants[(courant + 1) % taille];
        const QPointF &a = contour[avant];
        const QPointF &b = contour[milieu];
        const QPointF &c = contour[apres];
        const double virage = sens * tourner(a, b, c);
        bool oreille = virage > 0.0;
        bool contact = false;
        // Une oreille est un coin convexe dont le triangle ne contient aucun
        // autre sommet. Son retrait laisse le reste du contour à trianguler.
        if (oreille) {
            for (int indice : restants) {
                if (indice == avant || indice == milieu || indice == apres) {
                    continue;
                }
                const QPointF &point = contour[indice];
                // Les inégalités strictes excluent les arêtes et les doublons
                // des coins : ils ne doivent pas empêcher le retrait du coin.
                const double ab = sens * tourner(a, b, point);
                const double bc = sens * tourner(b, c, point);
                const double ca = sens * tourner(c, a, point);
                if (ab > 0.0 && bc > 0.0 && ca > 0.0) {
                    oreille = false;
                    break;
                }
                if (ab > 0.0 && bc > 0.0 && ca == 0.0) {
                    contact = true;
                }
            }
        }
        // Une diagonale passant par un sommet reste admissible, mais peut
        // pincer le contour restant. On préfère une autre oreille si possible,
        // puis on reprend celle-ci après un tour : le contact ne bloque jamais.
        if (oreille && contact && contactDiffere < 0) {
            contactDiffere = courant;
        }
        ++essais;
        if (virage == 0.0 || (oreille && !contact) || essais == taille) {
            if (virage != 0.0 && !(oreille && !contact) && contactDiffere >= 0) {
                courant = contactDiffere;
            }
            const int p = restants[(courant + taille - 1) % taille];
            const int q = restants[courant];
            const int r = restants[(courant + 1) % taille];
            // Un point aligné (ou répété) ne couvre aucune surface : on le
            // retire sans triangle. Après un tour sans oreille, on force une
            // coupe pour terminer même si le contour se croise lui-même.
            if (tourner(contour[p], contour[q], contour[r]) != 0.0) {
                triangles.append(p);
                triangles.append(q);
                triangles.append(r);
            }
            restants.removeAt(courant);
            courant %= int(restants.size());
            essais = 0;
            contactDiffere = -1;
        } else {
            courant = (courant + 1) % taille;
        }
    }
    return triangles;
}

MaillageBatiments Batiments::construireMaillage(const QVector<Batiment> &batiments)
{
    qsizetype points = 0, indicesMax = 0;
    for (const Batiment &batiment : batiments) {
        const qsizetype n = batiment.contour.size();
        points += n;
        indicesMax += 6 * n + (n >= 3 ? 3 * (n - 2) : 0);
    }
    MaillageBatiments maillage;
    // Le toit peut perdre des points alignés : sa borne supérieure suffit
    // pour garantir une seule allocation par tableau, sans le calculer deux fois.
    maillage.sommets.reserve(8 * points);
    maillage.indices.reserve(indicesMax);
    quint32 base = 0;
    for (qsizetype i = 0; i < batiments.size(); ++i) {
        const Batiment &batiment = batiments[i];
        const quint32 n = quint32(batiment.contour.size());
        const float teinte = float((quint32(i) * 2654435761u) & 65535u) / 65535.0f;
        for (float hauteur : {0.0f, batiment.hauteur}) {
            for (const QPointF &point : batiment.contour) {
                maillage.sommets.append(float(point.x()));
                maillage.sommets.append(hauteur);
                maillage.sommets.append(float(point.y()));
                maillage.sommets.append(teinte);
            }
        }
        // Chaque façade est un quadrilatère partagé en deux triangles.
        // Le z reprend directement le y de la scène, orienté vers le sud.
        for (quint32 j = 0; j < n; ++j) {
            const quint32 a = base + j, b = base + (j + 1) % n;
            const quint32 c = b + n, d = a + n;
            maillage.indices.append(a);
            maillage.indices.append(b);
            maillage.indices.append(c);
            maillage.indices.append(a);
            maillage.indices.append(c);
            maillage.indices.append(d);
        }
        const QVector<int> toit = trianguler(batiment.contour);
        for (int indice : toit) {
            maillage.indices.append(base + n + quint32(indice));
        }
        base += 2 * n;
    }
    return maillage;
}
