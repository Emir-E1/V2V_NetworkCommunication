// Lecteur de fichiers OpenStreetMap (.osm) — étape 1 du projet.
//
// Rôle : transformer le fichier XML de la carte en une OsmCarte (voir OsmData.h)
// qui ne contient que les routes où une voiture peut rouler.
// Principe en deux temps :
//   1. lecture du fichier du début à la fin : on note tous les nœuds (points GPS)
//      et toutes les voies ("way") avec leurs tags utiles (highway, oneway...) ;
//   2. tri : on garde les voies carrossables, on règle leur sens de circulation,
//      et on ne conserve que les nœuds qu'elles utilisent.
// Mesuré sur data/mulhouse.osm (4,7 Mo) : environ 120 ms.

#include "map/OsmLoader.h"

#include <QFile>
#include <QIODevice>
#include <QStringView>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

namespace
{
// Les voies peuvent précéder leurs nœuds dans le fichier : on attend la fin
// de la lecture pour vérifier les références et ne garder que les routes utiles.
// Sens de circulation tel qu'écrit dans le tag "oneway" du fichier.
enum class Sens
{
    Implicite,  // pas de tag oneway : on déduira le sens du type de route (autoroute, rond-point)
    Direct,     // oneway=yes : sens unique dans l'ordre des nœuds
    Inverse,    // oneway=-1 : sens unique dans l'ordre inverse
    Double      // oneway=no (ou valeur inconnue) : double sens
};

// Une voie lue dans le fichier, en attente du tri final (étape 2 du principe ci-dessus).
struct RouteEnAttente
{
    OsmRoute route;
    std::optional<TypeRoute> type;  // vide = pas carrossable (trottoir, piste cyclable...)
    Sens sens = Sens::Implicite;
    bool autoroute = false;         // highway=motorway : sens unique par défaut
    bool rondPoint = false;         // junction=roundabout : sens unique par défaut
    bool accesInterdit = false;     // access=no ou private : interdit aux véhicules
    bool surface = false;           // area=yes : une place, pas une route
};
}

std::optional<TypeRoute> OsmLoader::typeDepuisHighway(const QString &highway)
{
    // Une vue compare le texte sans en créer une copie pour chaque catégorie.
    const QStringView valeur(highway);
    if (valeur == u"motorway" || valeur == u"motorway_link"
        || valeur == u"trunk" || valeur == u"trunk_link")
    {
        return TypeRoute::Autoroute;
    }
    if (valeur == u"primary" || valeur == u"primary_link")
    {
        return TypeRoute::Principale;
    }
    if (valeur == u"secondary" || valeur == u"secondary_link"
        || valeur == u"tertiary" || valeur == u"tertiary_link")
    {
        return TypeRoute::Secondaire;
    }
    if (valeur == u"unclassified" || valeur == u"residential" || valeur == u"living_street")
    {
        return TypeRoute::Locale;
    }
    return std::nullopt;
}

bool OsmLoader::charger(QIODevice &flux, OsmCarte &carte, QString *erreur)
{
    // Réutiliser une carte ne doit laisser ni anciennes routes ni ancienne erreur.
    carte = {};
    if (erreur != nullptr)
    {
        erreur->clear();
    }

    QHash<qint64, OsmNoeud> tousLesNoeuds;
    QVector<RouteEnAttente> voies;
    // Ces capacités évitent les réallocations sur un extrait de la taille de Mulhouse.
    // Les conteneurs grandissent automatiquement si le fichier est plus volumineux.
    tousLesNoeuds.reserve(30000);
    voies.reserve(6000);
    QXmlStreamReader lecteur(&flux);

    // QXmlStreamReader avance dans le flux sans charger tout le document XML.
    while (!lecteur.atEnd())
    {
        lecteur.readNext();
        if (!lecteur.isStartElement())
        {
            continue;
        }

        if (lecteur.name() == u"node")
        {
            const auto attributs = lecteur.attributes();
            const OsmNoeud noeud{attributs.value(u"id").toLongLong(),
                                 attributs.value(u"lat").toDouble(),
                                 attributs.value(u"lon").toDouble()};
            tousLesNoeuds.insert(noeud.id, noeud);
            // Les tags internes d'un nœud ne servent pas au réseau routier.
            lecteur.skipCurrentElement();
        }
        else if (lecteur.name() == u"way")
        {
            RouteEnAttente voie;
            voie.route.id = lecteur.attributes().value(u"id").toLongLong();
            voie.route.noeuds.reserve(16);

            // Cette boucle reste dans la voie courante et ignore les sous-éléments
            // inconnus. Les vues d'attributs ne sont utilisées qu'avant d'avancer.
            while (lecteur.readNextStartElement())
            {
                const auto attributs = lecteur.attributes();
                if (lecteur.name() == u"nd")
                {
                    voie.route.noeuds.append(attributs.value(u"ref").toLongLong());
                }
                else if (lecteur.name() == u"tag")
                {
                    const QStringView cle = attributs.value(u"k");
                    const QStringView valeur = attributs.value(u"v");
                    if (cle == u"highway")
                    {
                        // L'interface publique prend un QString ; les autres tags
                        // sont comparés directement, sans copie de leur texte.
                        voie.type = typeDepuisHighway(valeur.toString());
                        voie.autoroute = valeur == u"motorway" || valeur == u"motorway_link";
                    }
                    else if (cle == u"name")
                    {
                        voie.route.nom = valeur.toString();
                    }
                    else if (cle == u"access")
                    {
                        voie.accesInterdit = valeur == u"no" || valeur == u"private";
                    }
                    else if (cle == u"area")
                    {
                        voie.surface = valeur == u"yes";
                    }
                    else if (cle == u"junction")
                    {
                        voie.rondPoint = valeur == u"roundabout" || valeur == u"circular";
                    }
                    else if (cle == u"oneway")
                    {
                        if (valeur == u"-1" || valeur == u"reverse")
                        {
                            voie.sens = Sens::Inverse;
                        }
                        else if (valeur == u"yes" || valeur == u"true" || valeur == u"1")
                        {
                            voie.sens = Sens::Direct;
                        }
                        else
                        {
                            // Un tag explicite désactive la règle implicite, y
                            // compris pour reversible et alternating.
                            voie.sens = Sens::Double;
                        }
                    }
                }
                lecteur.skipCurrentElement();
            }
            voies.append(std::move(voie));
        }
    }

    // On ne publie aucun résultat partiel si le document est mal formé.
    if (lecteur.hasError())
    {
        if (erreur != nullptr)
        {
            *erreur = QStringLiteral("XML invalide à la ligne %1.").arg(lecteur.lineNumber());
        }
        return false;
    }

    carte.routes.reserve(voies.size());
    carte.noeuds.reserve(tousLesNoeuds.size());
    for (RouteEnAttente &voie : voies)
    {
        if (!voie.type.has_value() || voie.accesInterdit || voie.surface)
        {
            continue;
        }

        auto &references = voie.route.noeuds;
        // Un extrait OSM peut omettre des points référencés : on les enlève avant
        // de décider si la voie possède encore assez de points pour être tracée.
        references.erase(std::remove_if(references.begin(), references.end(),
                                        [&tousLesNoeuds](qint64 id)
                                        {
                                            return !tousLesNoeuds.contains(id);
                                        }),
                         references.end());
        if (references.size() < 2)
        {
            continue;
        }

        voie.route.type = *voie.type;
        voie.route.sensUnique = voie.sens == Sens::Direct || voie.sens == Sens::Inverse
            || (voie.sens == Sens::Implicite && (voie.autoroute || voie.rondPoint));
        if (voie.sens == Sens::Inverse)
        {
            std::reverse(references.begin(), references.end());
        }

        // Les limites et la carte finale utilisent uniquement les points des
        // routes retenues, pour ne pas inclure les bâtiments ou chemins piétons.
        for (qint64 id : references)
        {
            const auto position = tousLesNoeuds.constFind(id);
            const OsmNoeud &noeud = position.value();
            if (carte.noeuds.isEmpty())
            {
                carte.latMin = carte.latMax = noeud.lat;
                carte.lonMin = carte.lonMax = noeud.lon;
            }
            else
            {
                carte.latMin = std::min(carte.latMin, noeud.lat);
                carte.latMax = std::max(carte.latMax, noeud.lat);
                carte.lonMin = std::min(carte.lonMin, noeud.lon);
                carte.lonMax = std::max(carte.lonMax, noeud.lon);
            }
            carte.noeuds.insert(id, noeud);
        }
        carte.routes.append(std::move(voie.route));
    }

    if (carte.routes.isEmpty())
    {
        if (erreur != nullptr)
        {
            *erreur = QStringLiteral("Aucune route carrossable trouvée dans le fichier");
        }
        return false;
    }
    return true;
}

bool OsmLoader::chargerFichier(const QString &chemin, OsmCarte &carte, QString *erreur)
{
    // QFile ferme automatiquement le fichier en quittant cette fonction.
    QFile fichier(chemin);
    if (!fichier.open(QIODevice::ReadOnly))
    {
        carte = {};
        if (erreur != nullptr)
        {
            *erreur = QStringLiteral("Impossible d'ouvrir le fichier « %1 ».").arg(chemin);
        }
        return false;
    }
    return charger(fichier, carte, erreur);
}
