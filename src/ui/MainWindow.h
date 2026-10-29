#pragma once  // évite d'inclure ce fichier deux fois dans la même compilation

#include <QMainWindow>

// Déclarations anticipées : on dit juste que ces classes existent,
// l'#include complet est dans le .cpp (compilation plus rapide).
class QGraphicsScene;
class QGraphicsView;

// Fenêtre principale du simulateur.
//   - au centre : la carte (QGraphicsView), où seront dessinés routes, véhicules et graphe
//   - à droite  : le panneau de contrôle (démarrer/pause, rayon R, choix de l'algorithme...)
class MainWindow : public QMainWindow
{
    Q_OBJECT  // obligatoire pour utiliser les signaux/slots de Qt

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    // La scène contient les objets à dessiner (routes, véhicules) ;
    // la vue est le "widget caméra" qui l'affiche avec zoom et déplacement.
    QGraphicsScene *m_scene;
    QGraphicsView *m_vue;
};
