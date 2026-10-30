// Construction de la fenêtre principale et liaison entre la carte, le panneau,
// la barre de contrôle du temps et le moteur de simulation (qui tourne dans son propre fil).

#include "ui/MainWindow.h"

#include "map/MapView.h"
#include "map/OsmLoader.h"
#include "ui/FenetreComparaison.h"
#include "vue3d/Batiments.h"
#include "vue3d/Vue3D.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QStackedWidget>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

// Dossier data/ du projet, fourni par CMake (voir CMakeLists.txt).
#ifndef V2V_DOSSIER_DONNEES
#define V2V_DOSSIER_DONNEES "data"
#endif

namespace
{
// Facteurs de vitesse du temps proposés par « Ralentir » / « Accélérer ».
constexpr double FACTEURS_TEMPS[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
constexpr int NOMBRE_FACTEURS = sizeof(FACTEURS_TEMPS) / sizeof(FACTEURS_TEMPS[0]);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_vue(new MapView(this))
    , m_vue3d(new Vue3D(this))
    , m_pile(new QStackedWidget(this))
{
    setWindowTitle("Simulateur V2V 3D");
    resize(1280, 800);
    // Les deux vues sont créées dès le départ (la vue 3D a besoin d'être dans la fenêtre
    // avant son premier affichage) ; la pile n'en montre qu'une à la fois.
    m_pile->addWidget(m_vue);
    m_pile->addWidget(m_vue3d);
    setCentralWidget(m_pile);

    // ---- Moteur de simulation dans son propre fil d'exécution ----
    // Le moteur n'a pas de parent : un objet ne peut pas changer de fil s'il a un parent.
    // Il sera détruit quand le fil s'arrête (voir le destructeur).
    m_fil = new QThread(this);
    m_moteur = new MoteurSimulation;
    m_moteur->moveToThread(m_fil);
    connect(m_fil, &QThread::finished, m_moteur, &QObject::deleteLater);
    // Signaux du moteur : Qt les livre dans le fil de l'interface (connexion en file d'attente).
    connect(m_moteur, &MoteurSimulation::nouvelInstantane, this, &MainWindow::recevoirInstantane);
    connect(m_moteur, &MoteurSimulation::nouvelleMesure, this, &MainWindow::recevoirMesure);
    m_fil->start();

    m_comparaison = new FenetreComparaison(this);

    // ---- Actions : une action = une commande, réutilisable dans le menu ET la barre d'outils ----
    auto *actionOuvrir = new QAction("Ouvrir une carte…", this);
    actionOuvrir->setShortcut(QKeySequence::Open);  // Ctrl+O
    actionOuvrir->setToolTip("Ouvrir un fichier OpenStreetMap (.osm) — Ctrl+O");
    connect(actionOuvrir, &QAction::triggered, this, &MainWindow::ouvrirCarte);

    // Choix de la vue : deux boutons dont un seul est enfoncé (groupe exclusif).
    m_action2D = new QAction("Carte 2D", this);
    m_action2D->setCheckable(true);
    m_action2D->setShortcut(Qt::Key_2);
    m_action2D->setToolTip("Carte vue de dessus : zone, itinéraire, intersections — touche 2");
    m_action3D = new QAction("Vue 3D", this);
    m_action3D->setCheckable(true);
    m_action3D->setShortcut(Qt::Key_3);
    m_action3D->setToolTip("Ville en 3D : bâtiments, voitures, arcs d'interférence, portée radio — touche 3");
    auto *groupeVues = new QActionGroup(this);
    groupeVues->addAction(m_action2D);
    groupeVues->addAction(m_action3D);
    connect(m_action2D, &QAction::triggered, this, [this] { afficherVue3D(false); });
    connect(m_action3D, &QAction::triggered, this, [this] { afficherVue3D(true); });

    m_actionRecentrer = new QAction("Recentrer", this);
    m_actionRecentrer->setShortcut(Qt::Key_R);
    m_actionRecentrer->setToolTip("Afficher toute la carte — R");
    connect(m_actionRecentrer, &QAction::triggered, this, [this] {
        if (m_pile->currentWidget() == m_vue3d)
            m_vue3d->recentrer();
        else
            m_vue->recentrer();
    });

    m_actionZone = new QAction("Choisir la zone", this);
    m_actionZone->setCheckable(true);  // bouton enfoncé tant qu'on trace la zone
    m_actionZone->setShortcut(Qt::Key_Z);
    m_actionZone->setToolTip("Tracer à la souris la zone de simulation (Échap pour annuler) — Z");
    connect(m_actionZone, &QAction::toggled, this, [this](bool actif) {
        if (actif)
            afficherVue3D(false);  // la zone se trace sur la carte 2D, vue de dessus
    });
    connect(m_actionZone, &QAction::toggled, m_vue, &MapView::setModeSelectionZone);
    connect(m_actionZone, &QAction::toggled, this, [this](bool actif) {
        if (actif && m_actionItineraire)
            m_actionItineraire->setChecked(false);  // un seul mode "clic sur la carte" à la fois
        if (actif)
            statusBar()->showMessage("Cliquez-glissez sur la carte pour tracer la zone de simulation (Échap pour annuler)");
    });

    m_actionEffacerZone = new QAction("Effacer la zone", this);
    m_actionEffacerZone->setShortcut(Qt::Key_Delete);
    m_actionEffacerZone->setToolTip("Revenir à toute la carte — Suppr");
    connect(m_actionEffacerZone, &QAction::triggered, this, [this] {
        m_vue->effacerZone();
        majInfosZone();
        construireGraphe();  // le graphe couvre de nouveau toute la carte
    });

    // Étape 2 : tester le plus court chemin en cliquant un départ puis une arrivée.
    m_actionItineraire = new QAction("Itinéraire", this);
    m_actionItineraire->setCheckable(true);
    m_actionItineraire->setShortcut(Qt::Key_I);
    m_actionItineraire->setToolTip("Cliquer un départ puis une arrivée pour voir le plus court chemin — I");
    connect(m_actionItineraire, &QAction::toggled, this, [this](bool actif) {
        if (actif) {
            afficherVue3D(false);             // les points se cliquent sur la carte 2D
            m_actionZone->setChecked(false);  // un seul mode "clic sur la carte" à la fois
        }
        m_vue->setModeItineraire(actif);
        m_depart = -1;
        if (actif) {
            statusBar()->showMessage("Itinéraire : cliquez le point de départ sur la carte");
            m_infosItineraire->setText("Cliquez le départ (A) sur la carte.");
        } else {
            m_vue->effacerItineraire();
            m_infosItineraire->setText("Activez « Itinéraire » puis cliquez deux points.");
        }
    });

    // Étape 2 : montrer les sommets du graphe (intersections et extrémités de routes).
    m_actionIntersections = new QAction("Intersections", this);
    m_actionIntersections->setCheckable(true);
    m_actionIntersections->setShortcut(Qt::Key_G);
    m_actionIntersections->setToolTip("Afficher les sommets du graphe routier — G");
    connect(m_actionIntersections, &QAction::toggled, m_vue, &MapView::setIntersectionsVisibles);

    // Étape 4 : montrer les arêtes du graphe d'interférences sur la carte.
    m_actionAretes = new QAction("Arêtes d'interférence", this);
    m_actionAretes->setCheckable(true);
    m_actionAretes->setShortcut(Qt::Key_E);
    m_actionAretes->setToolTip("Afficher les arêtes du graphe d'interférences — E");
    connect(m_actionAretes, &QAction::toggled, m_vue, &MapView::setAretesVisibles);
    connect(m_actionAretes, &QAction::toggled, m_vue3d, &Vue3D::setAretesVisibles);

    // ---- Contrôle du temps (étape 3) : barre du bas ----
    m_actionLecture = new QAction("Démarrer", this);
    m_actionLecture->setCheckable(true);
    m_actionLecture->setShortcut(Qt::Key_Space);
    m_actionLecture->setToolTip("Démarrer / mettre en pause la simulation — Espace");
    connect(m_actionLecture, &QAction::toggled, this, [this](bool enMarche) {
        m_actionLecture->setText(enMarche ? "Pause" : "Démarrer");
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, enMarche] { moteur->setEnMarche(enMarche); },
                                  Qt::QueuedConnection);
        m_images = 0;
        m_chronoImages.restart();
        statusBar()->showMessage(enMarche ? "Simulation en marche" : "Simulation en pause");
        if (!enMarche)
            m_imagesParSeconde->setText("en pause");
    });

    m_actionRalentir = new QAction("Ralentir", this);
    m_actionRalentir->setShortcut(Qt::Key_Minus);
    m_actionRalentir->setToolTip("Diviser par 2 la vitesse du temps — touche -");
    connect(m_actionRalentir, &QAction::triggered, this, [this] { changerFacteurTemps(-1); });

    m_actionAccelerer = new QAction("Accélérer", this);
    m_actionAccelerer->setShortcut(Qt::Key_Plus);
    m_actionAccelerer->setToolTip("Multiplier par 2 la vitesse du temps — touche +");
    connect(m_actionAccelerer, &QAction::triggered, this, [this] { changerFacteurTemps(+1); });

    m_actionReinitialiser = new QAction("Réinitialiser", this);
    m_actionReinitialiser->setShortcut(QKeySequence("Ctrl+R"));
    m_actionReinitialiser->setToolTip("Recommencer la simulation au temps 0, même scénario (même graine) — Ctrl+R");
    connect(m_actionReinitialiser, &QAction::triggered, this, [this] {
        m_actionLecture->setChecked(false);
        m_comparaison->vider();
        const int generation = ++m_generation;
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, generation] { moteur->reinitialiser(generation); },
                                  Qt::QueuedConnection);
    });

    // Tant qu'aucune carte n'est chargée, ces commandes n'ont pas de sens.
    for (QAction *a : {m_actionRecentrer, m_actionZone, m_actionEffacerZone, m_actionItineraire, m_actionIntersections,
                       m_actionAretes, m_actionLecture, m_actionRalentir, m_actionAccelerer, m_actionReinitialiser})
        a->setEnabled(false);

    // ---- Menu ----
    auto *menuFichier = menuBar()->addMenu("&Fichier");
    menuFichier->addAction(actionOuvrir);
    menuFichier->addSeparator();
    menuFichier->addAction("&Quitter", QKeySequence::Quit, this, &QWidget::close);

    auto *menuCarte = menuBar()->addMenu("&Carte");
    menuCarte->addAction(m_action2D);
    menuCarte->addAction(m_action3D);
    menuCarte->addSeparator();
    menuCarte->addAction(m_actionRecentrer);
    menuCarte->addAction(m_actionZone);
    menuCarte->addAction(m_actionEffacerZone);
    menuCarte->addSeparator();
    menuCarte->addAction(m_actionItineraire);
    menuCarte->addAction(m_actionIntersections);
    menuCarte->addAction(m_actionAretes);

    auto *menuSimulation = menuBar()->addMenu("&Simulation");
    menuSimulation->addAction(m_actionLecture);
    menuSimulation->addAction(m_actionRalentir);
    menuSimulation->addAction(m_actionAccelerer);
    menuSimulation->addAction(m_actionReinitialiser);
    menuSimulation->addSeparator();
    menuSimulation->addAction("Comparaison des algorithmes…", this, [this] {
        m_comparaison->show();
        m_comparaison->raise();
    });
    menuSimulation->addAction("Exporter les mesures (CSV)…", this, [this] { m_comparaison->exporterAvecDialogue(this); });

    // ---- Barre d'outils (texte seul : plus lisible que des icônes inventées) ----
    auto *barre = addToolBar("Outils");
    barre->setMovable(false);
    barre->setToolButtonStyle(Qt::ToolButtonTextOnly);
    barre->addAction(actionOuvrir);
    barre->addSeparator();
    barre->addAction(m_action2D);
    barre->addAction(m_action3D);
    barre->addSeparator();
    barre->addAction(m_actionRecentrer);
    barre->addAction(m_actionZone);
    barre->addAction(m_actionEffacerZone);
    barre->addSeparator();
    barre->addAction(m_actionItineraire);
    barre->addAction(m_actionIntersections);
    barre->addAction(m_actionAretes);

    creerBarreTemps();

    // ---- Panneau de contrôle à droite, figé (ni fermable ni détachable) ----
    auto *dock = new QDockWidget("Contrôles", this);
    // Zone défilante : sur un petit écran (portable), le panneau reste utilisable.
    auto *defilement = new QScrollArea;
    QWidget *panneau = creerPanneau();
    defilement->setWidget(panneau);
    defilement->setWidgetResizable(true);
    defilement->setFrameShape(QFrame::NoFrame);
    defilement->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Largeur = celle dont le panneau a besoin + la barre de défilement verticale :
    // sinon le texte et les boutons sont coupés à droite.
    // (qMax : la largeur minimale imposée au panneau compte plus que celle estimée par Qt.)
    defilement->setMinimumWidth(qMax(panneau->minimumWidth(), panneau->minimumSizeHint().width())
                                + defilement->verticalScrollBar()->sizeHint().width());
    dock->setWidget(defilement);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // ---- Barre d'état : message à gauche ; indicateurs de la coloration et
    //      coordonnées GPS à droite (cahier des charges §5) ----
    m_indicateurs = new QLabel;
    statusBar()->addPermanentWidget(m_indicateurs);
    m_coordonnees = new QLabel;
    m_coordonnees->setMinimumWidth(200);
    statusBar()->addPermanentWidget(m_coordonnees);
    statusBar()->showMessage("Aucune carte chargée — Fichier > Ouvrir une carte… (Ctrl+O)");

    // ---- Réactions aux événements de la carte ----
    const auto afficherCoordonnees = [this](double lat, double lon) {
        m_coordonnees->setText(QString("%1° N   %2° E")
                                   .arg(QLocale().toString(lat, 'f', 5))
                                   .arg(QLocale().toString(lon, 'f', 5)));
    };
    connect(m_vue, &MapView::positionSouris, this, afficherCoordonnees);
    connect(m_vue3d, &Vue3D::positionSouris, this, afficherCoordonnees);
    connect(m_vue, &MapView::zoneChoisie, this, [this] {
        majInfosZone();
        construireGraphe();  // le graphe se limite à la nouvelle zone
    });
    connect(m_vue, &MapView::clicCarte, this, &MainWindow::clicItineraire);
    // Le tracé est fini (ou annulé) : on relâche le bouton "Choisir la zone".
    connect(m_vue, &MapView::modeSelectionTermine, this, [this] {
        m_actionZone->setChecked(false);
        majInfosZone();
    });

    // Version 3D : on ouvre directement sur la vue 3D (la carte 2D reste à un clic, touche 2).
    afficherVue3D(true);
}

void MainWindow::afficherVue3D(bool vue3d)
{
    m_pile->setCurrentWidget(vue3d ? static_cast<QWidget *>(m_vue3d) : m_vue);
    (vue3d ? m_action3D : m_action2D)->setChecked(true);
    // Tracer une zone ou cliquer un itinéraire se fait sur la carte 2D : on quitte ces modes.
    if (vue3d) {
        m_actionZone->setChecked(false);
        m_actionItineraire->setChecked(false);
    }
    m_aide->setText(vue3d ? "Glisser : déplacer   ·   Clic droit + glisser : tourner   ·   Molette : zoom<br>"
                            "Clic sur un véhicule : sa portée et ses voisins   ·   Double-clic : le suivre"
                          : "Molette : zoom   ·   Clic-glisser : déplacer");
    m_pile->currentWidget()->setFocus();  // le clavier (Échap, F...) va à la vue affichée
}

QWidget *MainWindow::creerPanneau()
{
    auto *panneau = new QWidget;
    panneau->setMinimumWidth(270);
    auto *layout = new QVBoxLayout(panneau);
    layout->setSpacing(12);

    // -- Infos sur la carte chargée --
    auto *groupeCarte = new QGroupBox("Carte");
    m_infosCarte = new QLabel("Aucune carte chargée.");
    m_infosCarte->setWordWrap(true);
    m_infosCarte->setTextInteractionFlags(Qt::TextSelectableByMouse);
    (new QVBoxLayout(groupeCarte))->addWidget(m_infosCarte);
    layout->addWidget(groupeCarte);

    // -- Zone de simulation : les mêmes actions que la barre d'outils, en boutons --
    auto *groupeZone = new QGroupBox("Zone de simulation");
    auto *layoutZone = new QVBoxLayout(groupeZone);
    m_infosZone = new QLabel;
    m_infosZone->setWordWrap(true);
    layoutZone->addWidget(m_infosZone);
    auto *boutons = new QHBoxLayout;
    for (QAction *a : {m_actionZone, m_actionEffacerZone}) {
        auto *bouton = new QToolButton;
        bouton->setDefaultAction(a);  // le bouton suit l'action : texte, état coché, activé...
        bouton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        boutons->addWidget(bouton);
    }
    layoutZone->addLayout(boutons);
    layout->addWidget(groupeZone);

    // -- Graphe routier (étape 2) : taille du graphe et affichage des intersections --
    auto *groupeGraphe = new QGroupBox("Graphe routier");
    auto *layoutGraphe = new QVBoxLayout(groupeGraphe);
    m_infosGraphe = new QLabel("Chargez une carte pour construire le graphe.");
    m_infosGraphe->setWordWrap(true);
    layoutGraphe->addWidget(m_infosGraphe);
    auto *boutonIntersections = new QToolButton;
    boutonIntersections->setDefaultAction(m_actionIntersections);
    boutonIntersections->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layoutGraphe->addWidget(boutonIntersections);
    layout->addWidget(groupeGraphe);

    // -- Itinéraire de test : vérifie visuellement le plus court chemin (Dijkstra / A*) --
    auto *groupeItineraire = new QGroupBox("Plus court chemin");
    auto *layoutItineraire = new QVBoxLayout(groupeItineraire);
    m_infosItineraire = new QLabel("Activez « Itinéraire » puis cliquez deux points.");
    m_infosItineraire->setWordWrap(true);
    layoutItineraire->addWidget(m_infosItineraire);
    auto *boutonItineraire = new QToolButton;
    boutonItineraire->setDefaultAction(m_actionItineraire);
    boutonItineraire->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layoutItineraire->addWidget(boutonItineraire);
    layout->addWidget(groupeItineraire);

    // -- Simulation (étape 3) : réglages. La portée et l'intervalle de recalcul
    //    s'appliquent tout de suite ; le nombre de véhicules et la graine
    //    demandent de relancer (bouton « Appliquer »). --
    auto *groupeSimulation = new QGroupBox("Simulation");
    auto *layoutSimulation = new QVBoxLayout(groupeSimulation);
    auto *formulaire = new QFormLayout;
    m_nombreVehicules = new QSpinBox;
    m_nombreVehicules->setRange(10, 10000);
    m_nombreVehicules->setSingleStep(100);
    m_nombreVehicules->setValue(2000);  // au moins 2000 exigés par le sujet
    m_nombreVehicules->setGroupSeparatorShown(true);
    formulaire->addRow("Véhicules", m_nombreVehicules);
    m_rayon = new QDoubleSpinBox;
    m_rayon->setRange(20, 2000);
    m_rayon->setSingleStep(25);
    m_rayon->setDecimals(0);
    m_rayon->setSuffix(" m");
    m_rayon->setValue(300);
    m_rayon->setToolTip("Portée radio R commune à tous les véhicules");
    formulaire->addRow("Portée R", m_rayon);
    m_intervalle = new QDoubleSpinBox;
    m_intervalle->setRange(0.2, 30);
    m_intervalle->setSingleStep(0.2);
    m_intervalle->setDecimals(1);
    m_intervalle->setSuffix(" s");
    m_intervalle->setValue(1.0);
    m_intervalle->setToolTip("Temps simulé entre deux recalculs du graphe d'interférences et de la coloration");
    formulaire->addRow("Recalcul toutes les", m_intervalle);
    m_graine = new QSpinBox;
    m_graine->setRange(0, 999999);
    m_graine->setValue(42);
    m_graine->setToolTip("Même graine = même scénario : utile pour comparer les algorithmes");
    formulaire->addRow("Graine aléatoire", m_graine);
    layoutSimulation->addLayout(formulaire);
    auto *boutonAppliquer = new QPushButton("Appliquer et relancer");
    connect(boutonAppliquer, &QPushButton::clicked, this, &MainWindow::lancerSimulation);
    layoutSimulation->addWidget(boutonAppliquer);
    m_infosSimulation = new QLabel("Chargez une carte pour lancer la simulation.");
    m_infosSimulation->setWordWrap(true);
    layoutSimulation->addWidget(m_infosSimulation);
    layout->addWidget(groupeSimulation);

    connect(m_rayon, &QDoubleSpinBox::valueChanged, this, [this](double rayon) {
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, rayon] { moteur->setRayon(rayon); }, Qt::QueuedConnection);
        m_vue3d->setRayon(rayon);  // taille de la bulle de portée dans la vue 3D
    });
    connect(m_intervalle, &QDoubleSpinBox::valueChanged, this, [this](double secondes) {
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, secondes] { moteur->setIntervalleRecalcul(secondes); },
                                  Qt::QueuedConnection);
    });

    // -- Graphe d'interférences (étape 4) : statistiques demandées par le cahier --
    auto *groupeInterference = new QGroupBox("Graphe d'interférences");
    auto *layoutInterference = new QVBoxLayout(groupeInterference);
    m_infosInterference = new QLabel("Pas encore calculé.");
    m_infosInterference->setWordWrap(true);
    layoutInterference->addWidget(m_infosInterference);
    auto *boutonAretes = new QToolButton;
    boutonAretes->setDefaultAction(m_actionAretes);
    boutonAretes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layoutInterference->addWidget(boutonAretes);
    layout->addWidget(groupeInterference);

    // -- Coloration (étape 5) : algorithme affiché, résultats des deux algorithmes --
    auto *groupeColoration = new QGroupBox("Coloration (canaux radio)");
    auto *layoutColoration = new QVBoxLayout(groupeColoration);
    auto *ligneAlgo = new QHBoxLayout;
    ligneAlgo->addWidget(new QLabel("Couleurs affichées :"));
    m_algoAffiche = new QComboBox;
    for (Coloration::Algo algo : Coloration::TOUS_LES_ALGOS)
        m_algoAffiche->addItem(Coloration::nom(algo), int(algo));
    m_algoAffiche->setCurrentIndex(int(Coloration::Algo::WelshPowell));  // un des deux algorithmes obligatoires
    ligneAlgo->addWidget(m_algoAffiche, 1);
    layoutColoration->addLayout(ligneAlgo);
    connect(m_algoAffiche, &QComboBox::currentIndexChanged, this, [this] {
        const auto algo = static_cast<Coloration::Algo>(m_algoAffiche->currentData().toInt());
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, algo] { moteur->setAlgoAffiche(algo); },
                                  Qt::QueuedConnection);
        if (!m_comparaison->mesures().isEmpty())
            recevoirMesure(m_comparaison->mesures().last());  // met à jour indicateurs et légende
    });
    m_infosColoration = new QLabel("Pas encore calculée.");
    m_infosColoration->setWordWrap(true);
    layoutColoration->addWidget(m_infosColoration);
    m_legendeCanaux = new QLabel;
    layoutColoration->addWidget(m_legendeCanaux);
    auto *boutonsColoration = new QHBoxLayout;
    auto *boutonComparaison = new QPushButton("Comparaison…");
    boutonComparaison->setToolTip("Tableau et courbes : nombre de canaux, temps de calcul, stabilité");
    connect(boutonComparaison, &QPushButton::clicked, this, [this] {
        m_comparaison->show();
        m_comparaison->raise();
        m_comparaison->activateWindow();
    });
    auto *boutonCsv = new QPushButton("Exporter CSV…");
    connect(boutonCsv, &QPushButton::clicked, this, [this] { m_comparaison->exporterAvecDialogue(this); });
    boutonsColoration->addWidget(boutonComparaison);
    boutonsColoration->addWidget(boutonCsv);
    layoutColoration->addLayout(boutonsColoration);
    layout->addWidget(groupeColoration);

    // -- Légende : un petit trait de la couleur et de l'épaisseur de chaque type de route --
    auto *groupeLegende = new QGroupBox("Légende");
    auto *layoutLegende = new QVBoxLayout(groupeLegende);
    const QList<QPair<TypeRoute, QString>> types = {
        {TypeRoute::Autoroute, "Autoroute, voie rapide"},
        {TypeRoute::Principale, "Route principale"},
        {TypeRoute::Secondaire, "Route secondaire"},
        {TypeRoute::Locale, "Rue, route locale"},
    };
    for (const auto &[type, texte] : types) {
        QPixmap trait(28, 12);
        trait.fill(Qt::transparent);
        QPainter p(&trait);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(MapView::couleurRoute(type), MapView::epaisseurRoute(type) + 1.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(3, 6, 25, 6);
        p.end();

        auto *ligne = new QHBoxLayout;
        auto *icone = new QLabel;
        icone->setPixmap(trait);
        ligne->addWidget(icone);
        ligne->addWidget(new QLabel(texte), 1);
        layoutLegende->addLayout(ligne);
    }
    layout->addWidget(groupeLegende);

    // -- Aide rapide pour se déplacer (texte selon la vue, voir afficherVue3D) --
    m_aide = new QLabel;
    m_aide->setStyleSheet("color: palette(mid);");
    m_aide->setWordWrap(true);
    layout->addWidget(m_aide);

    layout->addStretch();  // pousse les groupes vers le haut
    majInfosZone();
    return panneau;
}

bool MainWindow::chargerCarte(const QString &chemin)
{
    // Curseur "sablier" pendant la lecture (une fraction de seconde pour Mulhouse).
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QElapsedTimer chrono;
    chrono.start();

    OsmCarte carte;
    QString erreur;
    const bool ok = OsmLoader::chargerFichier(chemin, carte, &erreur);
    if (!ok) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, "Carte non chargée",
                             QString("Impossible de lire « %1 ».\n\n%2").arg(QFileInfo(chemin).fileName(), erreur));
        return false;
    }

    m_carte = std::move(carte);
    m_vue->afficherCarte(m_carte);
    m_vue3d->afficherCarte(m_carte);

    // Bâtiments de la vue 3D : fichier « <nom de la carte>_batiments.txt » rangé à côté de
    // la carte (data/mulhouse_batiments.txt, voir data/telecharger_batiments.py).
    // Sans ce fichier, la vue 3D montre la ville sans bâtiments.
    const QFileInfo infoCarte(chemin);
    const QString cheminBatiments = infoCarte.dir().filePath(infoCarte.completeBaseName() + "_batiments.txt");
    QVector<Batiment> batiments;
    if (QFileInfo::exists(cheminBatiments) && Batiments::charger(cheminBatiments, m_carte, batiments))
        m_vue3d->afficherBatiments(Batiments::construireMaillage(batiments));
    const qint64 ms = chrono.elapsed();
    QApplication::restoreOverrideCursor();

    const QLocale fr;
    m_infosCarte->setText(QString("<b>%1</b><br>%2 routes · %3 nœuds<br>%4")
                              .arg(infoCarte.fileName().toHtmlEscaped(),
                                   fr.toString(m_carte.routes.size()),
                                   fr.toString(m_carte.noeuds.size()),
                                   batiments.isEmpty() ? QString("Pas de bâtiments pour la vue 3D")
                                                       : QString("%1 bâtiments (vue 3D)").arg(fr.toString(batiments.size()))));
    setWindowTitle(QString("Simulateur V2V 3D — %1").arg(infoCarte.fileName()));
    statusBar()->showMessage(QString("Carte chargée en %1 ms").arg(ms));

    for (QAction *a : {m_actionRecentrer, m_actionZone, m_actionEffacerZone, m_actionItineraire, m_actionIntersections,
                       m_actionAretes, m_actionRalentir, m_actionAccelerer, m_actionReinitialiser})
        a->setEnabled(true);
    changerFacteurTemps(0);  // désactive Ralentir/Accélérer si on est déjà au minimum/maximum
    majInfosZone();
    construireGraphe();  // relance aussi la simulation sur le nouveau graphe
    return true;
}

MainWindow::~MainWindow()
{
    // Arrête la boucle d'événements du fil puis attend qu'il ait fini :
    // le moteur (et son minuteur) sont détruits proprement avant la fenêtre.
    m_fil->quit();
    m_fil->wait();
}

void MainWindow::creerBarreTemps()
{
    // Barre du bas, comme sur la maquette du cahier des charges : contrôle du temps.
    auto *barre = new QToolBar("Contrôle du temps", this);
    barre->setMovable(false);
    barre->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addToolBar(Qt::BottomToolBarArea, barre);

    barre->addAction(m_actionLecture);
    barre->addSeparator();
    barre->addAction(m_actionRalentir);
    m_facteurTemps = new QLabel;
    m_facteurTemps->setMinimumWidth(48);
    m_facteurTemps->setAlignment(Qt::AlignCenter);
    m_facteurTemps->setToolTip("Vitesse du temps simulé par rapport au temps réel");
    barre->addWidget(m_facteurTemps);
    barre->addAction(m_actionAccelerer);
    barre->addSeparator();
    barre->addAction(m_actionReinitialiser);
    barre->addSeparator();
    m_tempsSimule = new QLabel;
    barre->addWidget(m_tempsSimule);

    // Ressort : pousse le compteur d'images par seconde tout à droite.
    auto *ressort = new QWidget;
    ressort->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    barre->addWidget(ressort);
    m_imagesParSeconde = new QLabel;
    m_imagesParSeconde->setToolTip("Images affichées par seconde (objectif : 25)");
    barre->addWidget(m_imagesParSeconde);

    changerFacteurTemps(0);  // affiche « ×1 »
    m_tempsSimule->setText("Temps simulé : 00:00:00");
}

void MainWindow::changerFacteurTemps(int delta)
{
    m_indiceFacteur = std::clamp(m_indiceFacteur + delta, 0, NOMBRE_FACTEURS - 1);
    const double facteur = FACTEURS_TEMPS[m_indiceFacteur];
    m_facteurTemps->setText(QString("×%1").arg(QLocale().toString(facteur)));
    m_actionRalentir->setEnabled(m_indiceFacteur > 0 && !m_carte.routes.isEmpty());
    m_actionAccelerer->setEnabled(m_indiceFacteur < NOMBRE_FACTEURS - 1 && !m_carte.routes.isEmpty());
    QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, facteur] { moteur->setFacteurTemps(facteur); },
                              Qt::QueuedConnection);
}

void MainWindow::lancerSimulation()
{
    m_actionLecture->setChecked(false);  // une nouvelle simulation démarre en pause
    m_comparaison->vider();
    const int generation = ++m_generation;  // tout message de l'ancienne simulation sera ignoré
    m_vue3d->effacerVehicules();  // nouveaux véhicules : l'ancien véhicule choisi n'a plus de sens
    m_infosInterference->setText("Pas encore calculé.");
    m_infosColoration->setText("Pas encore calculée.");
    m_indicateurs->clear();
    majLegendeCanaux(0);

    const bool grapheVide = m_graphe.sommets().size() < 2;
    m_actionLecture->setEnabled(!grapheVide);
    m_actionReinitialiser->setEnabled(!grapheVide);
    if (grapheVide) {
        m_vue->effacerVehicules();
        m_infosSimulation->setText("Pas de simulation : le graphe routier est vide.");
        // On envoie quand même le graphe vide au moteur : il oublie l'ancienne
        // simulation (sinon « Réinitialiser » la ferait réapparaître).
    }

    ParametresSimulation parametres;
    parametres.nombreVehicules = m_nombreVehicules->value();
    parametres.rayon = m_rayon->value();
    parametres.graine = static_cast<unsigned>(m_graine->value());
    const double intervalle = m_intervalle->value();
    // Le graphe est copié dans la lambda : le moteur travaille sur sa propre copie,
    // l'interface peut reconstruire le sien sans risque d'accès simultané.
    QMetaObject::invokeMethod(
        m_moteur, [moteur = m_moteur, graphe = m_graphe, parametres, intervalle, generation] {
            moteur->demarrer(graphe, parametres, intervalle, generation);
        },
        Qt::QueuedConnection);
    if (!grapheVide)
        statusBar()->showMessage("Simulation prête : appuyez sur « Démarrer » (Espace)");
}

void MainWindow::recevoirInstantane(const Instantane &instantane)
{
    // Image d'une simulation déjà remplacée (encore en route au moment du redémarrage) : ignorée.
    if (instantane.generation != m_generation) {
        m_moteur->instantaneAffiche();
        return;
    }
    // Les deux vues reçoivent tout : seule celle qui est affichée se redessine.
    if (instantane.positions.isEmpty()) {
        m_vue->effacerVehicules();
        m_vue3d->effacerVehicules();
    } else {
        m_vue->afficherVehicules(instantane.positions, instantane.couleurs);
        m_vue3d->afficherVehicules(instantane.positions, instantane.couleurs, instantane.caps, instantane.vitesses);
        if (instantane.nouvellesAretes) {
            m_vue->afficherAretes(instantane.aretes);
            m_vue3d->afficherAretes(instantane.aretes);
        }
    }

    const int secondes = static_cast<int>(instantane.temps);
    m_tempsSimule->setText(QString("Temps simulé : %1:%2:%3")
                               .arg(secondes / 3600, 2, 10, QChar('0'))
                               .arg(secondes / 60 % 60, 2, 10, QChar('0'))
                               .arg(secondes % 60, 2, 10, QChar('0')));
    const QLocale fr;
    m_infosSimulation->setText(QString("<b>%1 véhicules</b> · %2<br>Vitesse moyenne %3 km/h · %4 arrivées<br>"
                                       "Calcul d'un pas : %5 ms")
                                   .arg(fr.toString(instantane.positions.size()),
                                        m_actionLecture->isChecked() ? "en marche" : "en pause",
                                        fr.toString(instantane.vitesseMoyenne * 3.6, 'f', 0),
                                        fr.toString(instantane.arrivees),
                                        fr.toString(instantane.dureePasMs, 'f', 2)));

    // Images par seconde réellement affichées (objectif du cahier : 25).
    if (m_actionLecture->isChecked()) {
        ++m_images;
        if (m_chronoImages.elapsed() >= 1000) {
            m_imagesParSeconde->setText(QString("%1 images/s").arg(fr.toString(m_images * 1000.0 / m_chronoImages.elapsed(), 'f', 0)));
            m_images = 0;
            m_chronoImages.restart();
        }
    }
    m_moteur->instantaneAffiche();  // le moteur peut envoyer l'image suivante
}

void MainWindow::recevoirMesure(const Mesure &mesure)
{
    if (mesure.generation != m_generation)
        return;  // mesure d'une simulation déjà remplacée

    // recevoirMesure est aussi rappelée avec la dernière mesure quand on change
    // d'algorithme affiché : on n'ajoute à l'historique que les nouvelles mesures.
    if (m_comparaison->mesures().isEmpty() || &mesure != &m_comparaison->mesures().last())
        m_comparaison->ajouterMesure(mesure);

    const QLocale fr;
    const StatsInterference &s = mesure.interference;
    m_infosInterference->setText(QString("<b>%1 arêtes</b> entre %2 véhicules<br>Degré moyen %3 · maximal %4<br>"
                                         "Construit en %5 ms (grille spatiale)")
                                     .arg(fr.toString(s.aretes), fr.toString(s.sommets),
                                          fr.toString(s.degreMoyen, 'f', 2), fr.toString(s.degreMax),
                                          fr.toString(s.dureeMs, 'f', 2)));

    const auto ligne = [&](const QString &nom, const MesureAlgo &a) {
        return QString("<b>%1</b> : %2 canaux · %3 ms · S = %4 · %5")
            .arg(nom, fr.toString(a.couleurs), fr.toString(a.dureeMs, 'f', 2), fr.toString(a.stabilite, 'f', 2),
                 a.valide ? "<span style='color:#15803d'>valide</span>"
                          : QString("<span style='color:#b91c1c'>%1 conflits</span>").arg(a.conflits));
    };
    QStringList lignes;
    for (Coloration::Algo a : Coloration::TOUS_LES_ALGOS)
        lignes << ligne(Coloration::nom(a), mesure.algo(a));
    m_infosColoration->setText(lignes.join("<br>"));

    // Barre d'état : indicateurs de l'algorithme dont les couleurs sont affichées.
    const auto algo = static_cast<Coloration::Algo>(m_algoAffiche->currentData().toInt());
    const MesureAlgo &affiche = mesure.algo(algo);
    m_indicateurs->setText(QString("%1 : %2 canaux · %3 ms · stabilité %4 · %5   ")
                               .arg(Coloration::nom(algo), fr.toString(affiche.couleurs),
                                    fr.toString(affiche.dureeMs, 'f', 2), fr.toString(affiche.stabilite, 'f', 2),
                                    affiche.valide ? "valide" : "CONFLITS"));
    majLegendeCanaux(affiche.couleurs);
}

void MainWindow::majLegendeCanaux(int nombre)
{
    if (nombre <= 0) {
        m_legendeCanaux->clear();
        return;
    }
    // Une pastille par canal utilisé, dans la couleur des véhicules sur la carte.
    constexpr int maxAffiches = 16, taille = 14;
    const int affiches = std::min(nombre, maxAffiches);
    QPixmap image(affiches * (taille + 3), taille);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    for (int c = 0; c < affiches; ++c) {
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(MapView::couleurCanal(c));
        p.drawEllipse(QRectF(c * (taille + 3) + 1, 1, taille - 2, taille - 2));
    }
    p.end();
    m_legendeCanaux->setPixmap(image);
    m_legendeCanaux->setToolTip(QString("%1 canaux radio utilisés%2").arg(nombre).arg(nombre > maxAffiches ? " (16 premiers affichés)" : ""));
}

void MainWindow::construireGraphe()
{
    QElapsedTimer chrono;
    chrono.start();
    const bool ok = m_graphe.construire(m_carte, m_vue->zone());
    const qint64 ms = chrono.elapsed();

    // Les indices de sommets changent à chaque construction : l'ancien itinéraire n'a plus de sens.
    m_depart = -1;
    m_vue->effacerItineraire();
    if (m_actionItineraire->isChecked())
        m_infosItineraire->setText("Cliquez le départ (A) sur la carte.");

    QVector<QPointF> positions;
    positions.reserve(m_graphe.sommets().size());
    for (const Sommet &s : m_graphe.sommets())
        positions.append(s.pos);
    m_vue->afficherIntersections(positions);

    if (!ok) {
        m_infosGraphe->setText("Aucune route utilisable dans cette zone : agrandissez-la.");
        m_actionItineraire->setChecked(false);  // quitte proprement le mode (désactiver ne décoche pas)
        m_actionItineraire->setEnabled(false);
        lancerSimulation();  // arrête et efface la simulation précédente
        return;
    }
    m_actionItineraire->setEnabled(true);
    lancerSimulation();  // nouveau graphe = nouvelle simulation (les véhicules roulent sur ce graphe)

    const QLocale fr;
    m_infosGraphe->setText(QString("<b>%1 intersections · %2 tronçons</b><br>Construit en %3 ms. "
                                   "Toutes les intersections gardées sont reliées entre elles.")
                               .arg(fr.toString(m_graphe.sommets().size()),
                                    fr.toString(m_graphe.troncons().size()),
                                    fr.toString(ms)));
}

void MainWindow::clicItineraire(const QPointF &pos)
{
    const int sommet = m_graphe.sommetLePlusProche(pos);
    if (sommet < 0)
        return;
    const QPointF posSommet = m_graphe.sommets()[sommet].pos;

    // Premier clic (ou nouveau départ après un itinéraire affiché) : on fixe le départ A.
    if (m_depart < 0) {
        m_depart = sommet;
        m_vue->effacerItineraire();
        m_vue->afficherMarqueur(0, posSommet);
        m_infosItineraire->setText("Départ choisi. Cliquez l'arrivée (B).");
        statusBar()->showMessage("Itinéraire : cliquez le point d'arrivée");
        return;
    }

    // Second clic : arrivée B. On lance les deux algorithmes pour les comparer ;
    // ils doivent trouver la même longueur, A* en visitant moins de sommets.
    StatsChemin statsAStar, statsDijkstra;
    const QVector<int> chemin = m_graphe.plusCourtChemin(m_depart, sommet, GrapheRoutier::Algo::AStar, &statsAStar);
    m_graphe.plusCourtChemin(m_depart, sommet, GrapheRoutier::Algo::Dijkstra, &statsDijkstra);
    m_vue->afficherMarqueur(1, posSommet);
    m_depart = -1;  // le prochain clic choisira un nouveau départ

    if (chemin.isEmpty()) {
        m_infosItineraire->setText("Départ et arrivée identiques : cliquez plus loin.");
        return;
    }

    // Points de l'itinéraire = traces des tronçons mises bout à bout
    // (le premier point d'un tronçon est le dernier du précédent : on ne le répète pas).
    QVector<QPointF> points;
    double longueur = 0.0;
    for (int t : chemin) {
        const Troncon &troncon = m_graphe.troncons()[t];
        longueur += troncon.longueur;
        for (int i = points.isEmpty() ? 0 : 1; i < troncon.trace.size(); ++i)
            points.append(troncon.trace[i]);
    }
    m_vue->afficherItineraire(points);

    const QLocale fr;
    m_infosItineraire->setText(
        QString("<b><span style='color:%1'>%2 km</span></b> · %3 tronçons<br>"
                "A* : %4 ms, %5 sommets visités<br>"
                "Dijkstra : %6 ms, %7 sommets visités<br>"
                "Cliquez un nouveau départ pour recommencer.")
            .arg(MapView::couleurItineraire().name(),
                 fr.toString(longueur / 1000.0, 'f', 2),
                 fr.toString(chemin.size()),
                 fr.toString(statsAStar.dureeMs, 'f', 2),
                 fr.toString(statsAStar.sommetsVisites),
                 fr.toString(statsDijkstra.dureeMs, 'f', 2),
                 fr.toString(statsDijkstra.sommetsVisites)));
    statusBar()->showMessage("Itinéraire calculé");
}

void MainWindow::ouvrirCarte()
{
    const QString chemin = QFileDialog::getOpenFileName(
        this, "Ouvrir une carte OpenStreetMap", V2V_DOSSIER_DONNEES,
        "Fichiers OpenStreetMap (*.osm);;Tous les fichiers (*)");
    if (!chemin.isEmpty())  // vide = l'utilisateur a cliqué sur Annuler
        chargerCarte(chemin);
}

void MainWindow::majInfosZone()
{
    const QRectF zone = m_vue->zone();
    m_vue3d->setZone(zone);  // contour bleu au sol dans la vue 3D
    m_actionEffacerZone->setEnabled(!zone.isEmpty());

    if (m_carte.routes.isEmpty()) {
        m_infosZone->setText("Chargez une carte pour choisir la zone.");
        return;
    }
    if (zone.isEmpty()) {
        m_infosZone->setText("Aucune zone choisie : toute la carte sera simulée.");
        return;
    }

    // Nombre de routes qui ont au moins un point dans la zone :
    // donne une idée de la taille du réseau qui sera simulé.
    int routesDansZone = 0;
    for (const OsmRoute &route : m_carte.routes) {
        for (qint64 id : route.noeuds) {
            const OsmNoeud n = m_carte.noeuds.value(id);
            if (zone.contains(m_carte.versMetres(n.lat, n.lon))) {
                ++routesDansZone;
                break;
            }
        }
    }

    const QLocale fr;
    m_infosZone->setText(QString("<b>%1 km × %2 km</b><br>%3 routes dans la zone")
                             .arg(fr.toString(zone.width() / 1000.0, 'f', 2),
                                  fr.toString(zone.height() / 1000.0, 'f', 2),
                                  fr.toString(routesDansZone)));
    statusBar()->showMessage("Zone de simulation choisie");
}
