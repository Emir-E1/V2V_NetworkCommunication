# Télécharge les routes de Mulhouse depuis OpenStreetMap (API Overpass)
# et les enregistre dans data/mulhouse.osm.
#
# Pourquoi un script plutôt qu'un export manuel sur openstreetmap.org :
# tout le monde dans l'équipe obtient exactement la même zone et les mêmes
# types de routes, donc les résultats des différentes versions sont comparables.
#
# Lancer depuis la racine du projet :   python data/telecharger_carte.py
# Rien à installer : seulement la bibliothèque standard de Python.

import pathlib
import sys
import urllib.parse
import urllib.request

# Zone téléchargée en degrés : sud, ouest, nord, est.
# Couvre Mulhouse et sa proche banlieue (environ 8 km x 8 km).
ZONE = (47.72, 7.28, 47.79, 7.39)

# Types de routes gardés : seulement celles où circulent des voitures
# (pas de chemins piétons, pistes cyclables, allées de parking...).
TYPES = ("motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street"
         "|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link")

# Requête Overpass : les routes de la zone, puis "(._;>;)" ajoute tous les
# nœuds (points GPS) qui les composent. Sortie au format XML .osm classique.
BBOX = ",".join(str(v) for v in ZONE)
REQUETE = f'[out:xml][timeout:120];(way["highway"~"^({TYPES})$"]({BBOX}););(._;>;);out body;'

# Plusieurs serveurs Overpass publics : si le premier est saturé, on essaie le suivant.
SERVEURS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
]

SORTIE = pathlib.Path(__file__).resolve().parent / "mulhouse.osm"


def telecharger(url):
    donnees = urllib.parse.urlencode({"data": REQUETE}).encode()
    requete = urllib.request.Request(url, data=donnees, headers={"User-Agent": "simulateur-v2v-uha"})
    with urllib.request.urlopen(requete, timeout=180) as reponse:
        return reponse.read()


for serveur in SERVEURS:
    print(f"Téléchargement depuis {serveur} ...")
    try:
        contenu = telecharger(serveur)
    except Exception as e:  # serveur indisponible, délai dépassé, réseau...
        print(f"  échec : {e}")
        continue
    # Overpass renvoie une page d'erreur (et non un .osm) quand il est surchargé.
    if b"<osm" not in contenu[:500]:
        print("  réponse invalide (serveur surchargé ?)")
        continue
    SORTIE.write_bytes(contenu)
    print(f"OK : {SORTIE} ({len(contenu) / 1e6:.1f} Mo, "
          f"{contenu.count(b'<way ')} routes, {contenu.count(b'<node ')} nœuds)")
    sys.exit(0)

print("Aucun serveur n'a répondu, réessayer dans quelques minutes.")
sys.exit(1)
