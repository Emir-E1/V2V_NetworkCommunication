#pragma once  // évite d'inclure ce fichier deux fois dans la même compilation

#include "map/OsmData.h"
#include "roadgraph/RoadGraph.h"
#include "ui/MoteurSimulation.h"

#include <QElapsedTimer>
#include <QMainWindow>

// Déclarations anticipées : on dit juste que ces classes existent,
// l'#include complet est dans le .cpp (compilation plus rapide).
class FenetreComparaison;
class MapView;
class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QThread;
class Vue3D;

/// Fenêtre principale du simulateur (disposition décrite dans le cahier des charges, §5) :
///   - au centre : la carte, au choix en 2D (MapView : routes, véhicules colorés, arêtes)
///     ou en 3D (Vue3D : la même chose en perspective, avec les bâtiments) ; touches 2 et 3
///   - en haut   : la barre d'outils (ouvrir une carte, zone, itinéraire, affichages)
///   - à droite  : le panneau de contrôle (carte, zone, graphe, simulation, coloration)
///   - en bas    : la barre de contrôle du temps (démarrer, pause, accélérer, ralentir,
///                 réinitialiser), puis la barre d'état avec les indicateurs de la coloration
class MainWindow : public QMainWindow
{
    Q_OBJECT  // obligatoire pour utiliser les signaux/slots de Qt

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;  // arrête proprement le fil de simulation

    // Lit un fichier .osm et l'affiche. Public car main.cpp l'appelle au démarrage.
    // Renvoie false (après avoir prévenu l'utilisateur) si le fichier est illisible.
    bool chargerCarte(const QString &chemin);

private:
    void ouvrirCarte();        // menu Fichier > Ouvrir une carte...
    void majInfosZone();       // met à jour le panneau quand la zone change
    void construireGraphe();   // (re)construit le graphe routier sur la zone actuelle
    void clicItineraire(const QPointF &pos);  // 1er clic = départ A, 2e clic = arrivée B
    QWidget *creerPanneau();   // construit le panneau de contrôle de droite
    void creerBarreTemps();    // construit la barre du bas (contrôle du temps)
    void afficherVue3D(bool vue3d);  // bascule entre la carte 2D et la vue 3D

    // ---- Simulation (étapes 3 à 5) ----
    void lancerSimulation();   // (re)crée la simulation sur le graphe actuel avec les réglages du panneau
    void recevoirInstantane(const Instantane &instantane);  // ~25 fois par seconde
    void recevoirMesure(const Mesure &mesure);              // après chaque recalcul
    void changerFacteurTemps(int delta);  // -1 = ralentir, +1 = accélérer
    void majLegendeCanaux(int nombre);    // pastilles de couleur des canaux utilisés

    MapView *m_vue;
    Vue3D *m_vue3d;            // vue 3D : reçoit les mêmes données que la carte 2D
    QStackedWidget *m_pile;    // montre l'une des deux vues à la fois
    OsmCarte m_carte;          // la carte chargée
    GrapheRoutier m_graphe;    // graphe routier de la zone (étape 2), copié dans le moteur de simulation
    int m_depart = -1;         // sommet de départ de l'itinéraire de test (-1 = pas encore choisi)

    // Fil d'exécution de la simulation (le moteur vit dedans, voir MoteurSimulation.h).
    QThread *m_fil = nullptr;
    MoteurSimulation *m_moteur = nullptr;
    FenetreComparaison *m_comparaison = nullptr;
    int m_generation = 0;      // numéro de la simulation en cours : les messages d'une ancienne sont ignorés
    int m_indiceFacteur = 2;   // indice dans la liste des facteurs de temps (2 = ×1)
    QElapsedTimer m_chronoImages;  // pour mesurer les images par seconde affichées
    int m_images = 0;

    QLabel *m_infosCarte = nullptr;
    QLabel *m_infosZone = nullptr;
    QLabel *m_infosGraphe = nullptr;
    QLabel *m_infosItineraire = nullptr;
    QLabel *m_infosSimulation = nullptr;
    QLabel *m_infosInterference = nullptr;
    QLabel *m_infosColoration = nullptr;
    QLabel *m_legendeCanaux = nullptr;
    QLabel *m_coordonnees = nullptr;
    QLabel *m_indicateurs = nullptr;  // barre d'état : indicateurs de la coloration en cours
    QLabel *m_tempsSimule = nullptr;
    QLabel *m_facteurTemps = nullptr;
    QLabel *m_imagesParSeconde = nullptr;
    QLabel *m_aide = nullptr;         // rappel des gestes de souris, selon la vue affichée

    QSpinBox *m_nombreVehicules = nullptr;
    QDoubleSpinBox *m_rayon = nullptr;
    QDoubleSpinBox *m_intervalle = nullptr;
    QSpinBox *m_graine = nullptr;
    QComboBox *m_algoAffiche = nullptr;

    QAction *m_action2D = nullptr;           // afficher la carte 2D (touche 2)
    QAction *m_action3D = nullptr;           // afficher la vue 3D (touche 3)
    QAction *m_actionRecentrer = nullptr;
    QAction *m_actionZone = nullptr;
    QAction *m_actionEffacerZone = nullptr;
    QAction *m_actionItineraire = nullptr;
    QAction *m_actionIntersections = nullptr;
    QAction *m_actionAretes = nullptr;
    QAction *m_actionLecture = nullptr;      // démarrer / pause
    QAction *m_actionRalentir = nullptr;
    QAction *m_actionAccelerer = nullptr;
    QAction *m_actionReinitialiser = nullptr;
};
