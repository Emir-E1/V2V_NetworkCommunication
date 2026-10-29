#pragma once  // évite d'inclure ce fichier deux fois dans la même compilation

#include "map/OsmData.h"
#include "roadgraph/RoadGraph.h"

#include <QMainWindow>

// Déclarations anticipées : on dit juste que ces classes existent,
// l'#include complet est dans le .cpp (compilation plus rapide).
class MapView;
class QAction;
class QLabel;

// Fenêtre principale du simulateur.
//   - au centre : la carte (MapView), où seront dessinés routes, véhicules et graphe
//   - en haut   : la barre d'outils (ouvrir une carte, recentrer, choisir la zone)
//   - à droite  : le panneau de contrôle (infos carte, zone, légende ; puis simulation...)
//   - en bas    : la barre d'état (messages, coordonnées GPS sous la souris)
class MainWindow : public QMainWindow
{
    Q_OBJECT  // obligatoire pour utiliser les signaux/slots de Qt

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Lit un fichier .osm et l'affiche. Public car main.cpp l'appelle au démarrage.
    // Renvoie false (après avoir prévenu l'utilisateur) si le fichier est illisible.
    bool chargerCarte(const QString &chemin);

private:
    void ouvrirCarte();        // menu Fichier > Ouvrir une carte...
    void majInfosZone();       // met à jour le panneau quand la zone change
    void construireGraphe();   // (re)construit le graphe routier sur la zone actuelle
    void clicItineraire(const QPointF &pos);  // 1er clic = départ A, 2e clic = arrivée B
    QWidget *creerPanneau();   // construit le panneau de contrôle de droite

    MapView *m_vue;
    OsmCarte m_carte;          // la carte chargée
    GrapheRoutier m_graphe;    // graphe routier de la zone (étape 2), utilisé par la simulation
    int m_depart = -1;         // sommet de départ de l'itinéraire de test (-1 = pas encore choisi)

    QLabel *m_infosCarte = nullptr;
    QLabel *m_infosZone = nullptr;
    QLabel *m_infosGraphe = nullptr;
    QLabel *m_infosItineraire = nullptr;
    QLabel *m_coordonnees = nullptr;
    QAction *m_actionRecentrer = nullptr;
    QAction *m_actionZone = nullptr;
    QAction *m_actionEffacerZone = nullptr;
    QAction *m_actionItineraire = nullptr;
    QAction *m_actionIntersections = nullptr;
};
