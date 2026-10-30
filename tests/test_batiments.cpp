// Vérifie les contours, la lecture compacte et les volumes sans ouvrir de fenêtre.
// Un dossier de données facultatif ajoute la mesure sur les bâtiments de Mulhouse.

#include "vue3d/Batiments.h"
#include "map/OsmLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>

// Les vérifications restent actives en Release, y compris pour les chronométrages.
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << __FILE__ << ':' << __LINE__ << " : " << #condition << '\n'; \
            return 1; \
        } \
    } while (false)

namespace
{
double aireDouble(const QPointF &a, const QPointF &b, const QPointF &c)
{
    return (b.x() - a.x()) * (c.y() - a.y())
         - (b.y() - a.y()) * (c.x() - a.x());
}

int verifierTriangulation(const QVector<QPointF> &contour)
{
    const QVector<int> triangles = Batiments::trianguler(contour);
    CHECK(triangles.size() % 3 == 0);
    CHECK(triangles.size() / 3 <= contour.size() - 2);
    double aire = 0.0;
    for (qsizetype i = 0; i < contour.size(); ++i) {
        aire += aireDouble(contour[0], contour[i], contour[(i + 1) % contour.size()]);
    }
    CHECK(aire != 0.0);
    double somme = 0.0;
    for (qsizetype i = 0; i < triangles.size(); i += 3) {
        for (int j = 0; j < 3; ++j) {
            CHECK(triangles[i + j] >= 0 && triangles[i + j] < contour.size());
        }
        const double triangle = aireDouble(contour[triangles[i]],
                                            contour[triangles[i + 1]],
                                            contour[triangles[i + 2]]);
        CHECK(triangle * aire >= 0.0);
        somme += std::abs(triangle);
    }
    CHECK(std::abs(somme - std::abs(aire)) <= 1e-9 * std::abs(aire));
    return 0;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QVector<QPointF> triangle = {{0, 0}, {3, 0}, {0, 2}};
    const QVector<QPointF> carre = {{-2, -1}, {2, -1}, {2, 3}, {-2, 3}};
    const QVector<QPointF> formeL = {{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}};
    const QVector<QPointF> petitL = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
    const QVector<QPointF> formeU = {{0, 0}, {4, 0}, {4, 4}, {3, 4},
                                    {3, 1}, {1, 1}, {1, 4}, {0, 4}};
    const QVector<QPointF> alignes = {{0, 0}, {1, 0}, {2, 0}, {2, 1},
                                     {2, 2}, {1, 2}, {0, 2}, {0, 1}};
    const QVector<QPointF> repetes = {{0, 0}, {3, 0}, {3, 0}, {3, 1},
                                     {1, 1}, {1, 1}, {1, 3}, {0, 3}};
    CHECK(Batiments::trianguler({}).isEmpty());
    CHECK(Batiments::trianguler({{0, 0}, {1, 0}}).isEmpty());
    CHECK(Batiments::trianguler(triangle).size() == 3);
    CHECK(Batiments::trianguler(carre).size() == 6);
    for (QVector<QPointF> contour : {triangle, carre, formeL, petitL, formeU, alignes, repetes}) {
        // Le choix du premier sommet ne doit pas changer la surface couverte,
        // même lorsqu'une diagonale passe exactement par un autre sommet.
        for (qsizetype depart = 0; depart < contour.size(); ++depart) {
            CHECK(verifierTriangulation(contour) == 0);
            std::reverse(contour.begin(), contour.end());
            CHECK(verifierTriangulation(contour) == 0);
            if (contour.size() == 4) {
                CHECK(Batiments::trianguler(contour).size() == 6);
            }
            std::reverse(contour.begin(), contour.end());
            std::rotate(contour.begin(), contour.begin() + 1, contour.end());
        }
    }
    const QVector<int> croise = Batiments::trianguler({{0, 0}, {2, 2}, {0, 2}, {2, 0}});
    CHECK(croise.size() % 3 == 0 && croise.size() <= 6);
    for (int indice : croise) {
        CHECK(indice >= 0 && indice < 4);
    }
    CHECK(Batiments::trianguler({{0, 0}, {1, 0}, {2, 0}, {2, 0}}).isEmpty());

    // Les angles restent ordonnés, avec un rayon variable : le contour est
    // étoilé et simple. La graine fixe rend chaque échec reproductible.
    std::mt19937 hasard(20261030);
    std::uniform_int_distribution<int> nombrePoints(3, 60);
    std::uniform_real_distribution<double> rayon(0.2, 10.0);
    std::uniform_real_distribution<double> decalage(-0.2, 0.2);
    constexpr double pi = 3.14159265358979323846;
    for (int essai = 0; essai < 1000; ++essai) {
        QVector<QPointF> contour;
        const int n = nombrePoints(hasard);
        contour.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double angle = 2.0 * pi * (i + decalage(hasard)) / n;
            const double r = rayon(hasard);
            contour.append({r * std::cos(angle), r * std::sin(angle)});
        }
        CHECK(verifierTriangulation(contour) == 0);
        std::reverse(contour.begin(), contour.end());
        CHECK(verifierTriangulation(contour) == 0);
    }

    QTemporaryDir dossier;
    CHECK(dossier.isValid());
    QFile fichier(dossier.filePath(QStringLiteral("batiments.txt")));
    CHECK(fichier.open(QIODevice::WriteOnly));
    const QByteArray texte = "# Contours de test\r\n\r\n \t\r\n"
        "85 3 47750000 7350000 100 -200 -50 300\r\n"
        "120 4 47751000 7351000 -100 200 200 -100 -50 -150\r\n"
        "85 3 47750000 7350000 100 -200\r\n"
        "85 2 47750000 7350000 100 -200\r\n"
        "0 3 47750000 7350000 100 -200 -50 300\r\n"
        "85 3 47750000 7350000 100 -200 -50 300 999\r\n"
        "85 3 47750000 7350000 100 -200 -50 300x\r\n"
        "999999999999999999999999 3 0 0 1 0 0 1\r\n"
        "85 2147483647 0 0\r\n"
        "# Dernier commentaire sans saut de ligne";
    CHECK(fichier.write(texte) == texte.size());
    fichier.close();
    OsmCarte carte;
    carte.latMin = 47.72;
    carte.latMax = 47.79;
    carte.lonMin = 7.28;
    carte.lonMax = 7.39;
    QVector<Batiment> batiments;
    QString erreur = QStringLiteral("Ancienne erreur");
    CHECK(Batiments::charger(fichier.fileName(), carte, batiments, &erreur));
    CHECK(erreur.isEmpty());
    CHECK(batiments.size() == 2);
    CHECK(batiments[0].hauteur == 8.5f && batiments[1].hauteur == 12.0f);
    const QVector<QVector<QPointF>> coordonnees = {
        {{47.750000, 7.350000}, {47.750100, 7.349800}, {47.750050, 7.350100}},
        {{47.751000, 7.351000}, {47.750900, 7.351200},
         {47.751100, 7.351100}, {47.751050, 7.350950}}
    };
    for (qsizetype i = 0; i < batiments.size(); ++i) {
        CHECK(batiments[i].contour.size() == coordonnees[i].size());
        for (qsizetype j = 0; j < coordonnees[i].size(); ++j) {
            const QPointF attendu = carte.versMetres(coordonnees[i][j].x(), coordonnees[i][j].y());
            const QPointF ecart = batiments[i].contour[j] - attendu;
            CHECK(std::hypot(ecart.x(), ecart.y()) <= 1e-6);
        }
    }
    const QString absent = dossier.filePath(QStringLiteral("absent.txt"));
    CHECK(!Batiments::charger(absent, carte, batiments, &erreur));
    CHECK(!erreur.isEmpty() && erreur.contains(absent));
    CHECK(erreur.startsWith(QStringLiteral("Impossible d'ouvrir « ")));
    CHECK(batiments.isEmpty());
    CHECK(!Batiments::charger(absent, carte, batiments));

    // Même lecture en LF, avec signes explicites et sans saut final ; recharger
    // doit remplacer les bâtiments précédents, y compris par un fichier vide.
    CHECK(fichier.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray lf = "# LF\n\n85\t3 +47750000 +7350000 +100 -200 -50 +300";
    CHECK(fichier.write(lf) == lf.size());
    fichier.close();
    CHECK(Batiments::charger(fichier.fileName(), carte, batiments, &erreur));
    CHECK(erreur.isEmpty() && batiments.size() == 1);
    CHECK(fichier.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fichier.close();
    CHECK(Batiments::charger(fichier.fileName(), carte, batiments));
    CHECK(batiments.isEmpty());

    const QVector<Batiment> volumes = {{carre, 8.5f}, {formeL, 12.0f}};
    const MaillageBatiments maillage = Batiments::construireMaillage(volumes);
    const qsizetype totalPoints = carre.size() + formeL.size();
    CHECK(maillage.sommets.size() == 4 * 2 * totalPoints);
    CHECK(maillage.indices.size() == 3 * ((2 * 4 + 4 - 2) + (2 * 6 + 6 - 2)));
    for (quint32 indice : maillage.indices) {
        CHECK(indice < quint32(maillage.sommets.size() / 4));
    }
    qsizetype base = 0, debutIndices = 0;
    for (const Batiment &batiment : volumes) {
        const qsizetype n = batiment.contour.size();
        const float teinte = maillage.sommets[4 * base + 3];
        CHECK(teinte >= 0.0f && teinte <= 1.0f);
        for (qsizetype j = 0; j < 2 * n; ++j) {
            const qsizetype sommet = 4 * (base + j);
            CHECK(maillage.sommets[sommet] == float(batiment.contour[j % n].x()));
            CHECK(maillage.sommets[sommet + 1] == (j < n ? 0.0f : batiment.hauteur));
            CHECK(maillage.sommets[sommet + 2] == float(batiment.contour[j % n].y()));
            CHECK(maillage.sommets[sommet + 3] == teinte);
        }
        for (qsizetype j = 0; j < n; ++j) {
            const qsizetype a = base + j, b = base + (j + 1) % n;
            const QVector<quint32> mur = {quint32(a), quint32(b), quint32(b + n),
                                          quint32(a), quint32(b + n), quint32(a + n)};
            for (int k = 0; k < 6; ++k) {
                CHECK(maillage.indices[debutIndices + 6 * j + k] == mur[k]);
            }
        }
        const QVector<int> toit = Batiments::trianguler(batiment.contour);
        CHECK(toit.size() == 3 * (n - 2));
        for (qsizetype j = 0; j < toit.size(); ++j) {
            const quint32 indice = maillage.indices[debutIndices + 6 * n + j];
            CHECK(indice == quint32(base + n + toit[j]));
            CHECK(maillage.sommets[4 * indice + 1] == batiment.hauteur);
        }
        base += 2 * n;
        debutIndices += 6 * n + toit.size();
    }
    const MaillageBatiments bis = Batiments::construireMaillage(volumes);
    CHECK(bis.sommets == maillage.sommets && bis.indices == maillage.indices);
    const MaillageBatiments vide = Batiments::construireMaillage({});
    CHECK(vide.sommets.isEmpty() && vide.indices.isEmpty());

    if (application.arguments().size() > 1) {
        const QDir donnees(application.arguments()[1]);
        const bool carteChargee = OsmLoader::chargerFichier(
            donnees.filePath(QStringLiteral("mulhouse.osm")), carte, &erreur);
        if (!carteChargee) {
            std::cerr << erreur.toStdString() << '\n';
        }
        CHECK(carteChargee);
        QElapsedTimer chronometre;
        chronometre.start();
        const bool charge = Batiments::charger(
            donnees.filePath(QStringLiteral("mulhouse_batiments.txt")), carte, batiments, &erreur);
        const double lectureMs = chronometre.nsecsElapsed() / 1e6;
        if (!charge) {
            std::cerr << erreur.toStdString() << '\n';
        }
        CHECK(charge);
        chronometre.restart();
        const MaillageBatiments ville = Batiments::construireMaillage(batiments);
        const double maillageMs = chronometre.nsecsElapsed() / 1e6;

        // Ces diagnostics sont hors chronométrage : ils contrôlent aussi les
        // cas dégénérés du fichier réel et un éventuel décalage de projection.
        qsizetype points = 0, toitsIncomplets = 0, indicesToits = 0;
        double xMin = std::numeric_limits<double>::infinity();
        double xMax = -xMin, yMin = xMin, yMax = -xMin;
        for (const Batiment &batiment : batiments) {
            const qsizetype n = batiment.contour.size();
            points += n;
            const qsizetype toit = Batiments::trianguler(batiment.contour).size();
            indicesToits += toit;
            if (toit < 3 * (n - 2)) {
                ++toitsIncomplets;
            }
            for (const QPointF &point : batiment.contour) {
                xMin = std::min(xMin, point.x());
                xMax = std::max(xMax, point.x());
                yMin = std::min(yMin, point.y());
                yMax = std::max(yMax, point.y());
            }
        }
        std::cout << "Mulhouse : " << batiments.size() << " batiments, " << points
                  << " points, " << ville.indices.size() / 3 << " triangles, lecture "
                  << lectureMs << " ms, maillage " << maillageMs << " ms, total "
                  << lectureMs + maillageMs << " ms, " << toitsIncomplets
                  << " toits avec moins de n - 2 triangles\n";
        CHECK(batiments.size() > 70000);
        CHECK(toitsIncomplets * 100 <= batiments.size());
        CHECK(ville.sommets.size() == 8 * points);
        CHECK(ville.indices.size() == 6 * points + indicesToits);
        const QPointF nordOuest = carte.versMetres(carte.latMax, carte.lonMin);
        const QPointF sudEst = carte.versMetres(carte.latMin, carte.lonMax);
        CHECK(xMin <= sudEst.x() && xMax >= nordOuest.x());
        CHECK(yMin <= sudEst.y() && yMax >= nordOuest.y());
        CHECK(lectureMs + maillageMs < 300.0);
    }
    std::cout << "test_batiments : OK\n";
    return 0;
}
