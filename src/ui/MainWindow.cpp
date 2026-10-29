// Construction de la fenêtre principale. Pour l'instant elle est vide :
// chaque étape du projet viendra brancher ici sa partie (carte, contrôles, courbes).

#include "ui/MainWindow.h"

#include <QDockWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_scene(new QGraphicsScene(this))  // "this" = parent Qt : libéré automatiquement avec la fenêtre
    , m_vue(new QGraphicsView(m_scene, this))
{
    setWindowTitle("Simulateur V2V — Mulhouse");
    resize(1280, 800);

    // Carte (étape 1 : le chargement OSM et le dessin des routes se brancheront sur m_scene).
    m_vue->setRenderHint(QPainter::Antialiasing);         // traits lissés
    m_vue->setDragMode(QGraphicsView::ScrollHandDrag);    // déplacer la carte au clic-glisser
    setCentralWidget(m_vue);

    // Panneau de contrôle à droite : les boutons et réglages des étapes 3 à 5 iront dans ce layout.
    auto *panneau = new QWidget;
    auto *layout = new QVBoxLayout(panneau);
    layout->addWidget(new QLabel("Simulation, graphe d'interférences et coloration\nà venir."));
    layout->addStretch();  // pousse les éléments vers le haut

    // QDockWidget = panneau latéral ; on le fige pour qu'il ne soit ni fermé ni détaché.
    auto *dock = new QDockWidget("Contrôles", this);
    dock->setWidget(panneau);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Menu Fichier ("Ouvrir une carte" sera ajouté à l'étape 1).
    auto *menuFichier = menuBar()->addMenu("&Fichier");
    menuFichier->addAction("&Quitter", QKeySequence::Quit, this, &QWidget::close);

    // Barre d'état en bas : messages courts pour l'utilisateur.
    statusBar()->showMessage("Aucune carte chargée");
}
