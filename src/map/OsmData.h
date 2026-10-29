#pragma once

// Structures de données de la carte, partagées par tout le projet.
//
// C'est le "contrat" entre le parseur OSM (qui remplit ces structures) et
// tout ce qui les lit : l'affichage de la carte (étape 1), le graphe routier
// (étape 2) et le déplacement des véhicules (étape 3).
// Modifier ce fichier = prévenir toute l'équipe, car tout le monde en dépend.

#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QtMath>

// Un point OSM ("node") : intersection, fin de route, ou simple point
// intermédiaire qui dessine la courbe d'une route.
struct OsmNoeud
{
    qint64 id = 0;
    double lat = 0.0;  // latitude en degrés (nord positif)
    double lon = 0.0;  // longitude en degrés (est positif)
};

// Importance d'une route, déduite du tag OSM "highway".
// Sert à l'affichage (épaisseur, couleur) et pourra servir aux vitesses (étape 3).
enum class TypeRoute
{
    Autoroute,   // motorway, trunk (+ leurs bretelles *_link)
    Principale,  // primary (+ primary_link)
    Secondaire,  // secondary, tertiary (+ leurs *_link)
    Locale       // unclassified, residential, living_street
};

// Une route OSM ("way" portant un tag highway) : une suite ordonnée de nœuds.
struct OsmRoute
{
    qint64 id = 0;
    QVector<qint64> noeuds;  // ids des nœuds, dans l'ordre de la route
    TypeRoute type = TypeRoute::Locale;
    QString nom;             // tag "name", vide si la route n'a pas de nom

    // true = on ne peut rouler que dans l'ordre de "noeuds" (utile à l'étape 2).
    // Cas oneway=-1 (sens unique "à l'envers") : le parseur a déjà inversé
    // la liste "noeuds", donc ici on n'a jamais à gérer le sens inverse.
    bool sensUnique = false;
};

// La carte complète une fois le fichier .osm lu.
struct OsmCarte
{
    // Seulement les nœuds utilisés par au moins une route gardée,
    // indexés par id pour les retrouver en temps constant.
    QHash<qint64, OsmNoeud> noeuds;
    QVector<OsmRoute> routes;

    // Rectangle géographique qui englobe tous les nœuds gardés (en degrés).
    double latMin = 0.0, latMax = 0.0, lonMin = 0.0, lonMax = 0.0;

    // Convertit une position GPS en mètres sur un plan, centré sur le milieu de la carte.
    // Repère de la scène Qt : x vers l'est, y vers le SUD (l'axe y de Qt pointe vers le bas).
    //
    // Projection "équirectangulaire" : sur une zone de quelques kilomètres, l'erreur est
    // négligeable (bien moins d'un mètre) et une distance entre deux points devient une
    // simple distance euclidienne. Tout le projet doit utiliser CETTE fonction, pour que
    // les distances (rayon radio R de l'étape 4, par exemple) soient cohérentes partout.
    QPointF versMetres(double lat, double lon) const
    {
        constexpr double rayonTerre = 6371000.0;  // en mètres
        const double latRef = (latMin + latMax) / 2.0;
        const double lonRef = (lonMin + lonMax) / 2.0;
        const double x = rayonTerre * qDegreesToRadians(lon - lonRef) * qCos(qDegreesToRadians(latRef));
        const double y = -rayonTerre * qDegreesToRadians(lat - latRef);
        return {x, y};
    }
};
