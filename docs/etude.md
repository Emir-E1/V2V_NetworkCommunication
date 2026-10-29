# Étude comparative des algorithmes de coloration

Allocation de canaux radio dans un réseau V2V simulé sur Mulhouse : comparaison du **Glouton**, de
**Welsh–Powell**, de **DSATUR** et d'une **recherche locale** (TabuCol), sur le nombre de canaux, le temps de
calcul et la stabilité de l'allocation.

Données brutes : [`resultats/etude.csv`](resultats/etude.csv) (une ligne par recalcul, 2 745 lignes).
Résumé produit par l'outil : [`resultats/etude_resume.txt`](resultats/etude_resume.txt).

## 1. Protocole

Comme le prévoit le cahier des charges (§7.3), plusieurs densités, portées et graines, résultats moyennés :

| Paramètre | Valeurs |
|---|---|
| Carte | Mulhouse et proche banlieue, environ 8 km × 8 km (5 817 intersections, 12 479 tronçons) |
| Nombre de véhicules N | 1000, 2000, 3000 |
| Portée radio R (identique pour tous) | 200 m, 300 m, 500 m |
| Graines aléatoires | 1, 2, 3, 4, 5 (positions de départ, destinations, vitesses) |
| Durée simulée | 60 s, pas de 0,04 s, recalcul chaque seconde (61 recalculs) |
| Recalculs mesurés | 9 configurations × 5 graines × 61 = **2 745** |

À chaque recalcul, comme dans l'application : le graphe d'interférences est construit (grille spatiale), puis
les **quatre algorithmes colorent le même graphe**. Pour chacun on mesure :

- **k** : nombre de canaux ;
- **temps** de calcul de la coloration (ms, compilation Release, un cœur de portable) ;
- **stabilité** S(t) = 1 − (véhicules dont le canal a changé) / N, par rapport au recalcul précédent du même
  algorithme (le premier recalcul de chaque simulation, sans prédécesseur, est exclu des moyennes) ;
- **validité** : aucune arête entre deux véhicules de même canal ;
- **optimalité** : on calcule aussi une **borne inférieure** du nombre minimal de canaux, la taille d'une grande
  clique (groupe de véhicules tous en interférence deux à deux : ils ont forcément tous un canal différent).
  Si k est égal à cette borne, la coloration est **prouvée optimale**. La borne peut être trop basse : « non
  optimal » veut seulement dire « optimalité non prouvée ».

Reproduire : `build\etude_coloration.exe data\mulhouse.osm > resultats.csv` (environ 90 s).

## 2. Les graphes obtenus

| N | R | Arêtes | Degré moyen | Degré max | Borne (clique) | Construction (grille) |
|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 200 m | 1 957 | 3,9 | 14,9 | 10,7 | 0,46 ms |
| 1000 | 300 m | 3 762 | 7,5 | 23,9 | 14,0 | 0,58 ms |
| 1000 | 500 m | 9 136 | 18,3 | 45,1 | 22,6 | 0,90 ms |
| 2000 | 200 m | 7 836 | 7,8 | 26,6 | 16,5 | 1,20 ms |
| 2000 | 300 m | 15 145 | 15,1 | 43,1 | 23,1 | 1,68 ms |
| 2000 | 500 m | 36 647 | 36,7 | 84,3 | 38,8 | 2,83 ms |
| 3000 | 200 m | 17 378 | 11,6 | 36,4 | 21,8 | 2,26 ms |
| 3000 | 300 m | 33 671 | 22,5 | 60,4 | 31,1 | 3,24 ms |
| 3000 | 500 m | 81 568 | 54,4 | 120,1 | 53,1 | 5,90 ms |

Les véhicules se concentrent sur les grands axes et aux carrefours : les graphes contiennent de **grandes cliques**
(23 véhicules tous voisins en moyenne pour 2000 véhicules à 300 m), bien plus que ce que laisserait penser le
degré moyen. Le nombre de canaux est donc surtout imposé par ces zones denses.

## 3. Résultats

### 3.1 Nombre de canaux k (moyenne)

| N | R | Glouton | Welsh–Powell | DSATUR | Recherche locale | Borne |
|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 200 m | 10,90 | 10,73 | 10,71 | **10,71** | 10,70 |
| 1000 | 300 m | 14,86 | 14,20 | 14,08 | **14,03** | 14,01 |
| 1000 | 500 m | 25,44 | 23,54 | 23,04 | **22,88** | 22,63 |
| 2000 | 200 m | 17,32 | 16,57 | 16,50 | **16,49** | 16,46 |
| 2000 | 300 m | 25,11 | 23,59 | 23,28 | **23,23** | 23,12 |
| 2000 | 500 m | 44,61 | 41,83 | 39,83 | **39,62** | 38,84 |
| 3000 | 200 m | 23,15 | 22,02 | 21,90 | **21,88** | 21,83 |
| 3000 | 300 m | 33,90 | 31,94 | 31,51 | **31,38** | 31,09 |
| 3000 | 500 m | 61,23 | 57,75 | 54,79 | **54,56** | 53,08 |
| **Ensemble** | | 28,50 | 26,91 | 26,18 | **26,09** | |

### 3.2 Part des colorations prouvées optimales (k = borne)

| N | R | Glouton | Welsh–Powell | DSATUR | Recherche locale |
|---:|---:|---:|---:|---:|---:|
| 1000 | 200 m | 81 % | 98 % | 100 % | 100 % |
| 1000 | 300 m | 42 % | 83 % | 93 % | 98 % |
| 1000 | 500 m | 4 % | 39 % | 69 % | 79 % |
| 2000 | 200 m | 37 % | 90 % | 97 % | 98 % |
| 2000 | 300 m | 10 % | 63 % | 86 % | 91 % |
| 2000 | 500 m | 0 % | 8 % | 43 % | 49 % |
| 3000 | 200 m | 28 % | 83 % | 93 % | 95 % |
| 3000 | 300 m | 7 % | 50 % | 68 % | 76 % |
| 3000 | 500 m | 0 % | 9 % | 30 % | 33 % |
| **Ensemble** | | 23 % | 58 % | 75 % | **80 %** |

### 3.3 Temps de calcul de la coloration (moyenne / maximum, ms)

| N | R | Glouton | Welsh–Powell | DSATUR | Recherche locale |
|---:|---:|---:|---:|---:|---:|
| 1000 | 300 m | 0,03 / 0,08 | 0,10 / 0,17 | 0,62 / 0,85 | 3,3 / 6,9 |
| 2000 | 300 m | 0,09 / 0,13 | 0,25 / 0,41 | 2,3 / 3,2 | 5,7 / 8,8 |
| 3000 | 300 m | 0,17 / 0,27 | 0,46 / 1,15 | 5,1 / 6,0 | 9,5 / 12,7 |
| 3000 | 500 m | 0,27 / 0,59 | 0,71 / 1,36 | 11,9 / 17,3 | 19,6 / 25,0 |
| **Ensemble** | | 0,11 / 0,59 | 0,29 / 1,36 | 3,4 / 17,3 | 7,3 / 25,0 |

L'application exécute à chaque recalcul la construction du graphe puis les quatre colorations, l'une après l'autre.
En temps réel (×1), ce total a une seconde entière de marge. Il ne dépasse la durée d'une image (40 ms à 25 images/s)
que dans le cas le plus dense, 3000 véhicules à 500 m (38 ms en moyenne, 50 ms au maximum, au-dessus de 40 ms dans
76 recalculs sur 305) : l'animation peut alors marquer un court arrêt à chaque recalcul.

### 3.4 Stabilité S (moyenne)

| N | R | Glouton | Welsh–Powell | DSATUR | Recherche locale |
|---:|---:|---:|---:|---:|---:|
| 1000 | 200 m | **0,91** | 0,71 | 0,69 | 0,69 |
| 2000 | 300 m | **0,77** | 0,37 | 0,31 | 0,31 |
| 3000 | 500 m | **0,55** | 0,17 | 0,11 | 0,11 |
| **Ensemble** | | **0,76** | 0,40 | 0,34 | 0,34 |

### 3.5 Validité

**100 %** des 2 745 × 4 = 10 980 colorations sont valides (aucune arête entre deux véhicules de même canal),
vérifiées automatiquement à chaque recalcul.

## 4. Analyse

**Nombre de canaux.** L'ordre est le même dans les 9 configurations : Glouton > Welsh–Powell > DSATUR ≥ recherche
locale. L'écart grandit avec la densité : de 0,2 canal (1000 véhicules, 200 m) à 6,7 canaux (3000 véhicules, 500 m)
entre le Glouton et la recherche locale. Traiter d'abord les sommets les plus contraints (Welsh–Powell par degré,
DSATUR par saturation) paie d'autant plus que les zones denses sont nombreuses.

**Optimalité.** Sur des graphes peu denses, DSATUR est presque toujours optimal (93 à 100 % des cas prouvés à 200 m).
La recherche locale améliore DSATUR surtout quand le réseau est dense (+10 points à 1000 véhicules et 500 m), mais
le gain moyen reste faible (0,1 canal sur l'ensemble) : DSATUR est déjà proche de l'optimum sur ces graphes
« géométriques » dominés par de grandes cliques. Quand la preuve d'optimalité échoue (réseaux très denses), l'écart
moyen à la borne reste inférieur à 1,5 canal, et la borne elle-même peut être trop basse.

**Temps de calcul.** Le Glouton et Welsh–Powell sont presque gratuits (< 1,5 ms). DSATUR coûte environ 5 à 17 fois plus que
Welsh–Powell mais reste sous 18 ms dans le pire cas mesuré ; la recherche locale, limitée par son budget de
20 000 déplacements, ne dépasse pas 25 ms. Tous sont compatibles avec un recalcul chaque seconde, et la construction du graphe
d'interférences (grille spatiale : 0,5 à 6 ms) n'est jamais le goulot d'étranglement.

**Stabilité : le résultat le plus marquant.** Le Glouton est de loin le plus stable (0,76 contre 0,34 à 0,40). Son
ordre de traitement est fixe (numéro du véhicule) : d'un recalcul à l'autre, un véhicule dont le voisinage a peu
changé retrouve la même couleur. Welsh–Powell et DSATUR choisissent l'ordre selon les degrés et la saturation, qui
changent dès que les véhicules bougent : les couleurs sont « redistribuées » à chaque recalcul. La recherche locale
partant de DSATUR, elle hérite de son instabilité. Mieux colorer coûte donc des changements de canal fréquents, ce qui,
dans un vrai réseau, signifie des réaffectations radio (et de la signalisation) à chaque recalcul.

**Effet de la densité.** Passer de 200 à 500 m de portée multiplie le nombre d'arêtes par 4,7 et le nombre de
canaux (recherche locale) par 2,1 à 2,5 ; la stabilité de tous les algorithmes baisse, car chaque déplacement
modifie plus de voisinages.

## 5. Conclusion

| Critère | Meilleur | Commentaire |
|---|---|---|
| Nombre de canaux | Recherche locale, puis DSATUR | DSATUR est optimal dans 75 % des cas prouvés, la recherche locale dans 80 % |
| Temps de calcul | Glouton | moins de 0,6 ms dans tous les cas |
| Stabilité | Glouton | 2 fois plus stable que les autres |
| Compromis | DSATUR | près de l'optimum, rapide (quelques ms) ; mais instable |

Aucun algorithme ne gagne sur tous les critères : il faut choisir entre **économiser des canaux** (DSATUR, recherche
locale) et **éviter les changements de canal** (Glouton). Le risque identifié dans le cahier des charges
(« recolorations qui changent beaucoup les canaux ») est confirmé ; la piste naturelle est une **coloration
incrémentale** qui part des couleurs précédentes et ne recolore que les véhicules en conflit, pour combiner un faible
nombre de canaux et une bonne stabilité.

## 6. Limites de l'étude

- Une seule carte (Mulhouse) et une seule machine ; les temps dépendent du processeur.
- Déplacement simplifié (pas de feux, de files ni de dépassements) : des véhicules peuvent se superposer, ce qui
  crée des cliques plus grandes que dans la réalité.
- Portée identique pour tous les véhicules (cas par défaut du sujet).
- La borne de clique est obtenue par une recherche gloutonne : elle prouve l'optimalité quand k l'atteint, mais pas
  l'inverse.
