#pragma once

// Lecture d'un fichier OpenStreetMap (.osm, format XML) — étape 1 du projet.
//
// Un fichier .osm contient :
//   - des <node> : des points GPS (id, latitude, longitude)
//   - des <way>  : des suites de points avec des tags (route, bâtiment, rivière...)
// Le parseur ne garde que les routes où une voiture peut rouler (tag "highway"
// carrossable, voir estCarrossable) et les nœuds qu'elles utilisent.
// Il remplit une OsmCarte (voir OsmData.h), lue ensuite par l'affichage
// (étape 1), le graphe routier (étape 2) et la simulation (étape 3).

#include "map/OsmData.h"

#include <optional>

class QIODevice;

namespace OsmLoader
{
// Lit un flux XML OSM déjà ouvert en lecture.
// Renvoie true si tout s'est bien passé ; sinon false et *erreur explique pourquoi
// (XML invalide, aucune route trouvée...). "carte" est vidée avant la lecture.
bool charger(QIODevice &flux, OsmCarte &carte, QString *erreur = nullptr);

// Raccourci : ouvre le fichier "chemin" puis appelle charger().
bool chargerFichier(const QString &chemin, OsmCarte &carte, QString *erreur = nullptr);

// Traduit la valeur du tag highway en TypeRoute.
// Renvoie std::nullopt si la route n'est pas carrossable (footway, cycleway, path...).
std::optional<TypeRoute> typeDepuisHighway(const QString &highway);
}
