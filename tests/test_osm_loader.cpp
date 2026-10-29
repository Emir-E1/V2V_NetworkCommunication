// Test automatique du lecteur OSM (src/map/OsmLoader.cpp).
//
// Pourquoi : si quelqu'un modifie le lecteur et casse un cas (sens unique inversé,
// route piétonne gardée par erreur...), ce test échoue tout de suite au lieu de
// produire un bug discret dans le graphe routier ou la simulation.
// Lancer :  ctest --test-dir build --output-on-failure
// Mesurer la vraie carte :  build/test_osm_loader.exe data/mulhouse.osm

#include "map/OsmLoader.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <iostream>

// Contrairement à assert, cette vérification reste active en compilation Release.
// Elle quitte main avec un échec et indique précisément la condition en cause.
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
// QBuffer présente le texte comme un fichier ouvert, sans créer de fichier disque.
bool chargerXml(const QByteArray &xml, OsmCarte &carte, QString *erreur = nullptr)
{
    QBuffer flux;
    flux.setData(xml);
    if (!flux.open(QIODevice::ReadOnly))
    {
        return false;
    }
    return OsmLoader::charger(flux, carte, erreur);
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    // Toutes les catégories admises et plusieurs exclusions sont vérifiées
    // directement, pour détecter un oubli dans la table des types de routes.
    struct CasType
    {
        const char *highway;
        TypeRoute type;
    };
    const CasType types[] = {
        {"motorway", TypeRoute::Autoroute},
        {"motorway_link", TypeRoute::Autoroute},
        {"trunk", TypeRoute::Autoroute},
        {"trunk_link", TypeRoute::Autoroute},
        {"primary", TypeRoute::Principale},
        {"primary_link", TypeRoute::Principale},
        {"secondary", TypeRoute::Secondaire},
        {"secondary_link", TypeRoute::Secondaire},
        {"tertiary", TypeRoute::Secondaire},
        {"tertiary_link", TypeRoute::Secondaire},
        {"unclassified", TypeRoute::Locale},
        {"residential", TypeRoute::Locale},
        {"living_street", TypeRoute::Locale}
    };
    for (const CasType &cas : types)
    {
        const auto type = OsmLoader::typeDepuisHighway(QString::fromLatin1(cas.highway));
        CHECK(type.has_value());
        CHECK(*type == cas.type);
    }
    for (const char *highway : {"", "service", "footway", "cycleway", "path", "track",
                                "pedestrian", "steps", "construction", "inconnu"})
    {
        CHECK(!OsmLoader::typeDepuisHighway(QString::fromLatin1(highway)).has_value());
    }

    // Les voies et les nœuds sont volontairement mélangés. Les points 4, 5 et 6
    // sont éloignés : les garder par erreur fausserait aussi les limites GPS.
    const QByteArray xml = R"(<osm version="0.6">
        <node id="1" lat="47.75" lon="7.25"><tag k="name" v="Point"/></node>
        <way id="100">
            <nd ref="1"/><nd ref="999"/><nd ref="2"/>
            <tag k="name" v="Rue &amp; place"/><tag k="highway" v="residential"/>
        </way>
        <way id="101">
            <nd ref="4"/><nd ref="5"/><tag k="highway" v="footway"/>
        </way>
        <way id="102">
            <nd ref="1"/><nd ref="2"/><nd ref="3"/>
            <tag k="oneway" v="-1"/><tag k="highway" v="primary"/>
        </way>
        <way id="103">
            <nd ref="1"/><nd ref="3"/><nd ref="1"/>
            <tag k="junction" v="roundabout"/><tag k="highway" v="secondary"/>
        </way>
        <way id="104">
            <nd ref="6"/><nd ref="888"/><tag k="highway" v="residential"/>
        </way>
        <way id="105">
            <nd ref="777"/><nd ref="888"/><tag k="highway" v="residential"/>
        </way>
        <node id="2" lat="47.5" lon="7.75"/>
        <node id="3" lat="48" lon="7.5"/>
        <node id="4" lat="-80" lon="-150"/>
        <node id="5" lat="80" lon="150"/>
        <node id="6" lat="60" lon="60"/>
    </osm>)";

    OsmCarte carte;
    QString erreur = QStringLiteral("Ancienne erreur");
    CHECK(chargerXml(xml, carte, &erreur));
    CHECK(erreur.isEmpty());
    CHECK(carte.routes.size() == 3);
    CHECK(carte.routes[0].id == 100);
    CHECK(carte.routes[0].type == TypeRoute::Locale);
    CHECK(carte.routes[0].nom == QStringLiteral("Rue & place"));
    CHECK(!carte.routes[0].sensUnique);
    CHECK((carte.routes[0].noeuds == QVector<qint64>{1, 2}));
    CHECK(carte.routes[1].id == 102);
    CHECK(carte.routes[1].type == TypeRoute::Principale);
    CHECK(carte.routes[1].sensUnique);
    CHECK((carte.routes[1].noeuds == QVector<qint64>{3, 2, 1}));
    CHECK(carte.routes[1].nom.isEmpty());
    CHECK(carte.routes[2].id == 103);
    CHECK(carte.routes[2].type == TypeRoute::Secondaire);
    CHECK(carte.routes[2].sensUnique);
    CHECK((carte.routes[2].noeuds == QVector<qint64>{1, 3, 1}));
    CHECK(carte.noeuds.size() == 3);
    CHECK(carte.noeuds.contains(1));
    CHECK(carte.noeuds.contains(2));
    CHECK(carte.noeuds.contains(3));
    CHECK(!carte.noeuds.contains(4));
    CHECK(!carte.noeuds.contains(5));
    CHECK(!carte.noeuds.contains(6));
    CHECK(!carte.noeuds.contains(999));
    CHECK(carte.noeuds.value(2).id == 2);
    CHECK(carte.noeuds.value(2).lat == 47.5);
    CHECK(carte.noeuds.value(2).lon == 7.75);
    CHECK(carte.latMin == 47.5);
    CHECK(carte.latMax == 48.0);
    CHECK(carte.lonMin == 7.25);
    CHECK(carte.lonMax == 7.75);

    // Chaque cas utilise la même géométrie pour isoler la règle de sens unique.
    // Un tag explicite à double sens doit primer même sur une autoroute circulaire.
    struct CasSens
    {
        const char *highway;
        const char *tags;
        bool unique;
        bool inverse;
    };
    const CasSens sens[] = {
        {"residential", "<tag k='oneway' v='yes'/>", true, false},
        {"residential", "<tag k='oneway' v='true'/>", true, false},
        {"residential", "<tag k='oneway' v='1'/>", true, false},
        {"residential", "<tag k='oneway' v='-1'/>", true, true},
        {"residential", "<tag k='oneway' v='reverse'/>", true, true},
        {"motorway", "<tag k='junction' v='circular'/><tag k='oneway' v='no'/>", false, false},
        {"motorway", "<tag k='oneway' v='false'/>", false, false},
        {"motorway_link", "<tag k='oneway' v='0'/>", false, false},
        {"residential", "<tag k='oneway' v='reversible'/><tag k='junction' v='roundabout'/>", false, false},
        {"motorway", "<tag k='oneway' v='alternating'/>", false, false},
        {"motorway", "", true, false},
        {"motorway_link", "", true, false},
        {"trunk", "", false, false},
        {"trunk_link", "", false, false},
        {"residential", "<tag k='junction' v='roundabout'/>", true, false},
        {"residential", "<tag k='junction' v='circular'/>", true, false},
        {"residential", "", false, false}
    };
    const QByteArray debut = "<osm><node id='1' lat='-2' lon='-4'/>"
                             "<node id='2' lat='-1' lon='-3'/>"
                             "<way id='1'><nd ref='1'/><nd ref='2'/><tag k='highway' v='";
    for (const CasSens &cas : sens)
    {
        CHECK(chargerXml(debut + cas.highway + "'/>" + cas.tags + "</way></osm>", carte));
        CHECK(carte.routes.size() == 1);
        CHECK(carte.routes[0].sensUnique == cas.unique);
        CHECK(carte.routes[0].noeuds.front() == (cas.inverse ? 2 : 1));
        CHECK(carte.routes[0].noeuds.back() == (cas.inverse ? 1 : 2));
        CHECK(carte.latMin == -2.0);
        CHECK(carte.latMax == -1.0);
        CHECK(carte.lonMin == -4.0);
        CHECK(carte.lonMax == -3.0);
    }

    // Ces voies sont exclues malgré leur catégorie routière. Chaque échec doit
    // aussi effacer la carte précédente, y compris ses limites géographiques.
    for (const char *tags : {"<tag k='access' v='no'/>", "<tag k='access' v='private'/>",
                             "<tag k='area' v='yes'/>"})
    {
        CHECK(chargerXml(xml, carte));
        CHECK(!chargerXml(debut + "residential'/>" + tags + "</way></osm>", carte, &erreur));
        CHECK(erreur == QStringLiteral("Aucune route carrossable trouvée dans le fichier"));
        CHECK(carte.routes.isEmpty());
        CHECK(carte.noeuds.isEmpty());
        CHECK(carte.latMin == 0.0 && carte.latMax == 0.0);
        CHECK(carte.lonMin == 0.0 && carte.lonMax == 0.0);
    }
    CHECK(chargerXml(debut + "residential'/><tag k='access' v='yes'/>"
                            "<tag k='area' v='no'/></way></osm>", carte));
    CHECK(!chargerXml(debut + "footway'/></way></osm>", carte, &erreur));
    CHECK(!erreur.isEmpty());
    CHECK(!chargerXml("<osm/>", carte));

    // Une erreur après une route valide ne doit pas publier une carte partielle.
    // Le numéro de ligne rend le diagnostic exploitable dans un éditeur de texte.
    CHECK(chargerXml(xml, carte));
    CHECK(!chargerXml(debut + "residential'/></way>\n<node>\n</osm>", carte, &erreur));
    CHECK(!erreur.isEmpty());
    CHECK(erreur.contains(QStringLiteral("ligne 3")));
    CHECK(carte.routes.isEmpty());
    CHECK(carte.noeuds.isEmpty());
    CHECK(!chargerXml("<osm>", carte));

    // Le dossier temporaire garantit que ce chemin n'existe pas, sans toucher
    // aux fichiers du projet ni dépendre d'un nom absent sur la machine.
    QTemporaryDir dossier;
    CHECK(dossier.isValid());
    const QString cheminAbsent = dossier.filePath(QStringLiteral("absent.osm"));
    CHECK(chargerXml(xml, carte));
    CHECK(!OsmLoader::chargerFichier(cheminAbsent, carte, &erreur));
    CHECK(erreur.contains(cheminAbsent));
    CHECK(carte.routes.isEmpty());
    CHECK(carte.noeuds.isEmpty());
    CHECK(!OsmLoader::chargerFichier(cheminAbsent, carte));

    // Un chemin facultatif permet de mesurer le vrai jeu de données avec le
    // même exécutable ; seuls l'ouverture et le chargement sont chronométrés.
    if (application.arguments().size() > 1)
    {
        QElapsedTimer chronometre;
        chronometre.start();
        const bool charge = OsmLoader::chargerFichier(application.arguments()[1], carte, &erreur);
        const qint64 duree = chronometre.elapsed();
        if (!charge)
        {
            std::cerr << erreur.toStdString() << '\n';
        }
        CHECK(charge);
        std::cout << "Chargement : " << carte.noeuds.size() << " noeuds, "
                  << carte.routes.size() << " routes, " << duree << " ms\n";
    }

    std::cout << "test_osm_loader : OK\n";
    return 0;
}
