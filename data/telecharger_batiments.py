# Télécharge les bâtiments de Mulhouse depuis OpenStreetMap (API Overpass)
# et les enregistre, sous une forme compacte, dans data/mulhouse_batiments.txt.
#
# Sert à la vue 3D : chaque bâtiment y est dessiné comme un volume
# (son contour au sol, monté jusqu'à sa hauteur).
#
# Pourquoi un format compact plutôt que le .osm brut : les ~74 000 bâtiments de
# la zone pèsent environ 70 Mo en XML OSM, contre environ 5 Mo ici. Le dépôt Git
# reste léger et le fichier se lit en une fraction de seconde au lancement.
#
# Format du fichier produit (texte, une ligne par bâtiment) :
#   hauteur_dm  n  lat0  lon0  dlat1  dlon1  ...  dlat(n-1)  dlon(n-1)
#   - hauteur en décimètres (85 = 8,5 m) ;
#   - n = nombre de points du contour (le premier point n'est pas répété à la fin) ;
#   - latitude / longitude en millionièmes de degré (environ 10 cm de précision) ;
#     à partir du 2e point on écrit l'écart avec le point précédent : les nombres
#     sont bien plus courts (quelques chiffres au lieu de huit).
# Les lignes qui commencent par # sont des commentaires.
#
# Hauteur : celle d'OSM quand elle est connue (tag height, sinon building:levels
# x 3 m). Sinon elle est ESTIMÉE selon le type de bâtiment et sa surface au sol
# (environ 90 % des bâtiments de Mulhouse n'ont pas de hauteur dans OSM).
#
# Lancer depuis la racine du projet :   python data/telecharger_batiments.py
# Rien à installer : seulement la bibliothèque standard de Python.

import json
import math
import pathlib
import re
import sys
import urllib.parse
import urllib.request
import zlib

# Même zone que data/telecharger_carte.py (sud, ouest, nord, est), pour que les
# bâtiments couvrent exactement la carte des routes.
ZONE = (47.72, 7.28, 47.79, 7.39)
BBOX = ",".join(str(v) for v in ZONE)

# Les bâtiments simples (way) avec leur géométrie, puis les bâtiments "multipolygones"
# (relation : grands bâtiments faits de plusieurs morceaux de contour).
REQUETE = (f'[out:json][timeout:180];'
           f'way["building"]({BBOX});out tags geom;'
           f'relation["building"]["type"="multipolygon"]({BBOX});out body geom;')

SERVEURS = [
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]

SORTIE = pathlib.Path(__file__).resolve().parent / "mulhouse_batiments.txt"

# Hauteur par défaut (en mètres) selon le tag building, quand OSM ne donne ni
# height ni building:levels. Valeurs typiques d'une ville française.
HAUTEUR_PAR_TYPE = {
    "house": 7, "detached": 7, "semidetached_house": 7, "terrace": 8, "bungalow": 4,
    "apartments": 15, "residential": 11, "dormitory": 15, "hotel": 16,
    "garage": 3, "garages": 3, "shed": 3, "hut": 3, "carport": 3, "kiosk": 3, "cabin": 3,
    "greenhouse": 3, "roof": 4, "toilets": 3,
    "industrial": 9, "warehouse": 9, "manufacture": 10, "factory": 10,
    "retail": 7, "commercial": 10, "supermarket": 7, "service": 4,
    "office": 15, "school": 11, "kindergarten": 6, "university": 15, "college": 12,
    "hospital": 18, "public": 12, "civic": 12, "government": 14, "train_station": 14,
    "church": 18, "cathedral": 25, "chapel": 8, "mosque": 12, "synagogue": 12, "temple": 12,
    "sports_hall": 10, "stadium": 15, "parking": 9, "farm_auxiliary": 6, "barn": 7,
}


def nombre(texte):
    """Premier nombre d'un tag OSM ("12", "12.5 m", "12,5", "3;4"...), ou None."""
    trouve = re.search(r"\d+(?:[.,]\d+)?", texte or "")
    return float(trouve.group().replace(",", ".")) if trouve else None


def surface_m2(points):
    """Surface du contour (formule du lacet) après passage en mètres."""
    lat0 = math.radians(points[0][0])
    m_par_deg = 6371000.0 * math.pi / 180.0
    xy = [(lon * m_par_deg * math.cos(lat0), lat * m_par_deg) for lat, lon in points]
    s = sum(x1 * y2 - x2 * y1 for (x1, y1), (x2, y2) in zip(xy, xy[1:] + xy[:1]))
    return abs(s) / 2.0


def hauteur(tags, points, identifiant):
    """Hauteur du bâtiment en mètres : celle d'OSM si connue, sinon une estimation."""
    h = nombre(tags.get("height"))
    if h is None:
        etages = nombre(tags.get("building:levels"))
        if etages is not None:
            h = etages * 3.0
    if h is None:
        type_ = tags.get("building", "yes")
        if type_ in HAUTEUR_PAR_TYPE:
            h = HAUTEUR_PAR_TYPE[type_]
        else:
            # Type inconnu ("yes", le cas le plus courant) : on devine d'après la
            # surface au sol (abri, maison, petit immeuble, immeuble, hangar).
            s = surface_m2(points)
            h = 3 if s < 25 else 7 if s < 150 else 10 if s < 400 else 13 if s < 1500 else 10
        # Petite variation (+/- 12 %) fixée par l'identifiant OSM : une rue de maisons
        # n'a pas toutes ses toitures exactement à la même hauteur. Toujours la même
        # pour un bâtiment donné, donc le fichier reste reproductible.
        h *= 0.88 + 0.24 * (zlib.crc32(str(identifiant).encode()) % 1000) / 1000.0
    return min(max(h, 2.5), 150.0)


def contour(geometrie):
    """Liste de (lat, lon) arrondis au millionième de degré, sans doublons ni point de fermeture."""
    points = []
    for p in geometrie:
        if p is None:  # nœud manquant dans la réponse : contour inutilisable
            return []
        q = (round(p["lat"] * 1e6), round(p["lon"] * 1e6))
        if not points or q != points[-1]:
            points.append(q)
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()
    return points if len(points) >= 3 else []


def assembler_anneaux(morceaux):
    """Recolle les morceaux de contour d'une relation en anneaux fermés."""
    restants = [m for m in morceaux if len(m) >= 2]
    anneaux = []
    while restants:
        anneau = list(restants.pop(0))
        progres = True
        while anneau[0] != anneau[-1] and progres:
            progres = False
            for i, m in enumerate(restants):
                if m[0] == anneau[-1]:
                    anneau += m[1:]
                elif m[-1] == anneau[-1]:
                    anneau += m[-2::-1]
                else:
                    continue
                restants.pop(i)
                progres = True
                break
        if anneau[0] == anneau[-1]:
            anneaux.append(anneau)
    return anneaux


def a_garder(tags):
    """Exclut les bâtiments souterrains ou détruits (rien à dessiner au-dessus du sol)."""
    if tags.get("building") in ("no", "demolished", "construction"):
        return False
    if tags.get("location") == "underground":
        return False
    couche = nombre(tags.get("layer"))
    return not (couche is not None and tags.get("layer", "").startswith("-"))


def telecharger(url):
    donnees = urllib.parse.urlencode({"data": REQUETE}).encode()
    requete = urllib.request.Request(url, data=donnees, headers={"User-Agent": "simulateur-v2v-uha"})
    with urllib.request.urlopen(requete, timeout=300) as reponse:
        return reponse.read()


def convertir(reponse):
    """Réponse JSON d'Overpass -> lignes du fichier compact."""
    elements = json.loads(reponse)["elements"]
    batiments = []  # (hauteur_m, points, identifiant)
    ways_gardes = set()
    for e in elements:
        if e["type"] != "way" or not a_garder(e.get("tags", {})):
            continue
        points = contour(e.get("geometry", []))
        if points:
            ways_gardes.add(e["id"])
            batiments.append((e.get("tags", {}), points, e["id"]))
    for e in elements:
        if e["type"] != "relation" or not a_garder(e.get("tags", {})):
            continue
        # Contours extérieurs seulement (les cours intérieures sont remplies : plus
        # simple, et invisible vu de la rue). Un morceau déjà gardé comme bâtiment
        # simple est ignoré, sinon il serait dessiné deux fois au même endroit.
        morceaux = [[(p["lat"], p["lon"]) for p in m.get("geometry", []) if p]
                    for m in e.get("members", [])
                    if m.get("role") == "outer" and m.get("ref") not in ways_gardes]
        for anneau in assembler_anneaux(morceaux):
            points = contour({"lat": la, "lon": lo} for la, lo in anneau)
            if points:
                batiments.append((e.get("tags", {}), points, e["id"]))

    lignes = []
    for tags, points, identifiant in batiments:
        h = hauteur(tags, [(la / 1e6, lo / 1e6) for la, lo in points], identifiant)
        champs = [round(h * 10), len(points), points[0][0], points[0][1]]
        for (la1, lo1), (la2, lo2) in zip(points, points[1:]):
            champs += [la2 - la1, lo2 - lo1]
        lignes.append(" ".join(str(c) for c in champs))
    return lignes


for serveur in SERVEURS:
    print(f"Téléchargement depuis {serveur} ...")
    try:
        contenu = telecharger(serveur)
        lignes = convertir(contenu)
    except Exception as e:  # serveur indisponible, délai dépassé, réponse invalide...
        print(f"  échec : {e}")
        continue
    entete = [
        "# Bâtiments de Mulhouse extraits d'OpenStreetMap par data/telecharger_batiments.py",
        "# Données © les contributeurs d'OpenStreetMap, disponibles sous licence ODbL.",
        f"# Zone (sud, ouest, nord, est) : {BBOX}",
        "# Une ligne par bâtiment : hauteur_dm n lat0 lon0 dlat1 dlon1 ... (millionièmes de degré,",
        "# écarts avec le point précédent à partir du 2e point). Détails dans le script.",
    ]
    SORTIE.write_text("\n".join(entete + lignes) + "\n", encoding="utf-8")
    print(f"OK : {SORTIE} ({SORTIE.stat().st_size / 1e6:.1f} Mo, {len(lignes)} bâtiments)")
    sys.exit(0)

print("Aucun serveur n'a répondu, réessayer dans quelques minutes.")
sys.exit(1)
