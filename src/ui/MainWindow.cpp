// Construction de la fenêtre principale et liaison entre la carte, le panneau,
// la barre de contrôle du temps et le moteur de simulation (qui tourne dans son propre fil).

#include "ui/MainWindow.h"

#include "map/MapView.h"
#include "map/OsmLoader.h"
#include "ui/FenetreComparaison.h"
#include "ui/Theme.h"
#include "vue3d/Batiments.h"
#include "vue3d/Vue3D.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QHeaderView>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
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
#include <QtMath>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

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
    // (leurs icônes sont posées par majIcones(), qui les redessine quand le thème change)
    m_actionOuvrir = new QAction("Ouvrir une carte…", this);
    m_actionOuvrir->setShortcut(QKeySequence::Open);  // Ctrl+O
    m_actionOuvrir->setToolTip("Ouvrir un fichier OpenStreetMap (.osm) — Ctrl+O");
    connect(m_actionOuvrir, &QAction::triggered, this, &MainWindow::ouvrirCarte);

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
    connect(m_actionIntersections, &QAction::toggled, this, [this](bool visibles) {
        if (visibles)
            afficherVue3D(false);  // les intersections se voient sur la carte 2D
        m_vue->setIntersectionsVisibles(visibles);
    });

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
        m_actionLecture->setIcon(Theme::icone(enMarche ? Theme::Icone::Pause : Theme::Icone::Lecture));
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, enMarche] { moteur->setEnMarche(enMarche); },
                                  Qt::QueuedConnection);
        m_images = 0;
        m_chronoImages.restart();
        statusBar()->showMessage(enMarche ? "Simulation en marche" : "Simulation en pause");
        if (!enMarche)
            m_imagesParSeconde->setText("en pause");
        if (enMarche)
            m_attendDemarrage = false;  // le message d'accueil a rempli son rôle
        majInvite();
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
        m_attendDemarrage = true;
        majInvite();
    });

    // Fenêtre de comparaison des algorithmes (étape 5) : barre d'outils, menu et panneau.
    m_actionComparaison = new QAction("Comparaison", this);
    m_actionComparaison->setToolTip("Tableau et courbes des 4 algorithmes : canaux, temps de calcul, stabilité");
    connect(m_actionComparaison, &QAction::triggered, this, [this] {
        m_comparaison->show();
        m_comparaison->raise();
        m_comparaison->activateWindow();
    });

    // Thème sombre de l'interface (les cartes 2D et 3D gardent leurs couleurs).
    m_actionThemeSombre = new QAction("Thème sombre", this);
    m_actionThemeSombre->setCheckable(true);
    m_actionThemeSombre->setShortcut(QKeySequence("Ctrl+T"));
    m_actionThemeSombre->setToolTip("Interface claire ou sombre — Ctrl+T");
    connect(m_actionThemeSombre, &QAction::toggled, this, &MainWindow::changerTheme);

    // Tant qu'aucune carte n'est chargée, ces commandes n'ont pas de sens.
    for (QAction *a : {m_actionRecentrer, m_actionZone, m_actionEffacerZone, m_actionItineraire, m_actionIntersections,
                       m_actionAretes, m_actionLecture, m_actionRalentir, m_actionAccelerer, m_actionReinitialiser})
        a->setEnabled(false);

    // ---- Menu ----
    auto *menuFichier = menuBar()->addMenu("&Fichier");
    menuFichier->addAction(m_actionOuvrir);
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
    menuSimulation->addAction(m_actionComparaison);
    menuSimulation->addAction("Exporter les mesures (CSV)…", this, [this] { m_comparaison->exporterAvecDialogue(this); });

    auto *menuAffichage = menuBar()->addMenu("&Affichage");
    menuAffichage->addAction(m_actionThemeSombre);

    // ---- Barres d'outils, avec icône et texte ----
    // Barre principale : ce qui sert dans les deux vues.
    auto *barre = addToolBar("Outils");
    barre->setMovable(false);
    barre->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    barre->setContextMenuPolicy(Qt::PreventContextMenu);  // pas de menu « masquer la barre »
    barre->addAction(m_actionOuvrir);
    barre->addSeparator();
    barre->addAction(m_action2D);
    barre->addAction(m_action3D);
    barre->addSeparator();
    barre->addAction(m_actionRecentrer);
    barre->addAction(m_actionAretes);
    barre->addAction(m_actionComparaison);
    // Outils qui se manient à la souris sur la carte 2D : cette barre n'est affichée qu'en 2D
    // (voir afficherVue3D). En 3D, les mêmes commandes restent dans le menu Carte et l'onglet
    // Carte du panneau ; les choisir fait revenir à la carte 2D.
    m_barre2D = addToolBar("Outils de la carte 2D");
    m_barre2D->setMovable(false);
    m_barre2D->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_barre2D->setContextMenuPolicy(Qt::PreventContextMenu);
    m_barre2D->addAction(m_actionZone);
    m_barre2D->addAction(m_actionEffacerZone);
    m_barre2D->addSeparator();
    m_barre2D->addAction(m_actionItineraire);
    m_barre2D->addAction(m_actionIntersections);

    creerBarreTemps();
    // Icônes posées avant de construire le panneau : ses boutons en ont aussi, et sa largeur
    // est mesurée icônes comprises (sinon le texte des boutons serait coupé).
    majIcones();

    // ---- Panneau de contrôle à droite, figé (ni fermable ni détachable), en 3 onglets ----
    auto *dock = new QDockWidget("Contrôles", this);
    dock->setWidget(creerPanneau());
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
    // Tracer une zone ou cliquer un itinéraire se fait sur la carte 2D : on quitte ces modes,
    // et on cache leur barre d'outils.
    if (vue3d) {
        m_actionZone->setChecked(false);
        m_actionItineraire->setChecked(false);
    }
    m_barre2D->setVisible(!vue3d);
    m_aide->setText(vue3d ? "Glisser : déplacer · Clic droit + glisser : tourner · Molette : zoom · "
                            "Clic sur un véhicule : sa portée et ses voisins · Double-clic : le suivre"
                          : "Molette : zoom · Clic-glisser : déplacer · Z : tracer la zone de simulation");
    majInvite();
    m_pile->currentWidget()->setFocus();  // le clavier (Échap, F...) va à la vue affichée
}

void MainWindow::majInvite()
{
    // Message d'accueil au centre de la vue, tant qu'une simulation prête n'a pas encore été
    // lancée : sans lui, on arrive sur des véhicules immobiles sans savoir quoi faire.
    const bool afficher = m_attendDemarrage && m_actionLecture->isEnabled() && !m_actionLecture->isChecked();
    const QString titre = afficher ? QString("Appuyez sur Espace pour démarrer la simulation") : QString();
    m_vue3d->setInvite(titre, "Puis cliquez une voiture : sa portée radio et ses voisins apparaissent");
    m_vue->setInvite(titre, "Molette : zoom · Glisser : déplacer · Z : choisir une zone de simulation");
}

void MainWindow::majIcones()
{
    // Les icônes sont dessinées dans la couleur du texte du thème : on les redessine à chaque
    // changement de thème (sinon, en thème sombre, des icônes foncées sur fond foncé).
    using I = Theme::Icone;
    const QList<QPair<QAction *, I>> icones = {
        {m_actionOuvrir, I::Ouvrir},         {m_action2D, I::Carte2D},
        {m_action3D, I::Vue3D},              {m_actionRecentrer, I::Recentrer},
        {m_actionZone, I::Zone},             {m_actionEffacerZone, I::EffacerZone},
        {m_actionItineraire, I::Itineraire}, {m_actionIntersections, I::Intersections},
        {m_actionAretes, I::Aretes},         {m_actionComparaison, I::Comparaison},
        {m_actionRalentir, I::Ralentir},     {m_actionAccelerer, I::Accelerer},
        {m_actionReinitialiser, I::Reinitialiser},
    };
    for (const auto &[action, icone] : icones)
        action->setIcon(Theme::icone(icone));
    m_actionLecture->setIcon(Theme::icone(m_actionLecture->isChecked() ? I::Pause : I::Lecture));
}

void MainWindow::changerTheme(bool sombre)
{
    Theme::appliquer(sombre);
    majIcones();
    m_comparaison->setThemeSombre(sombre);
    if (!m_comparaison->mesures().isEmpty())
        recevoirMesure(m_comparaison->mesures().last());  // couleurs du tableau des résultats
}

QWidget *MainWindow::creerPanneau()
{
    // Panneau rangé en 3 onglets, du plus utilisé au moins utilisé : Simulation (réglages,
    // état, graphe d'interférences), Coloration (résultats des 4 algorithmes), Carte (carte,
    // zone, graphe routier, itinéraire, légende). Sous les onglets, l'aide de la vue affichée.
    auto *panneau = new QWidget;
    auto *layoutPanneau = new QVBoxLayout(panneau);
    layoutPanneau->setContentsMargins(6, 6, 6, 6);
    m_onglets = new QTabWidget;
    m_onglets->setDocumentMode(true);  // onglets sans cadre, plus légers
    layoutPanneau->addWidget(m_onglets, 1);

    // Chaque onglet défile s'il est plus haut que l'écran (petit portable). La largeur du
    // panneau est celle dont l'onglet le plus large a besoin, plus la barre de défilement :
    // sinon le texte et les boutons seraient coupés à droite.
    int largeurPanneau = 0;
    const auto nouvelOnglet = [this, &largeurPanneau](const QString &titre, QWidget *contenu) {
        contenu->setMinimumWidth(280);
        auto *defilement = new QScrollArea;
        defilement->setWidget(contenu);
        defilement->setWidgetResizable(true);
        defilement->setFrameShape(QFrame::NoFrame);
        defilement->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Applique d'abord la feuille de style (marges, coins arrondis) au contenu : sans
        // cela, la largeur mesurée ne compterait pas ces marges.
        contenu->ensurePolished();
        largeurPanneau = std::max(largeurPanneau, std::max(contenu->minimumWidth(), contenu->minimumSizeHint().width())
                                                      + defilement->verticalScrollBar()->sizeHint().width());
        m_onglets->addTab(defilement, titre);
    };
    const auto nouveauGroupe = [](const QString &titre, QVBoxLayout *&layout) {
        auto *groupe = new QGroupBox(titre);
        layout = new QVBoxLayout(groupe);
        return groupe;
    };
    const auto boutonPour = [](QAction *action) {
        auto *bouton = new QToolButton;
        bouton->setDefaultAction(action);  // le bouton suit l'action : texte, icône, état coché, activé...
        bouton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        bouton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return bouton;
    };
    const auto texte = [](const QString &contenu) {
        auto *label = new QLabel(contenu);
        label->setWordWrap(true);
        return label;
    };

    // ===================== Onglet 1 : Simulation =====================
    auto *simulation = new QWidget;
    auto *layoutSimulation = new QVBoxLayout(simulation);
    layoutSimulation->setSpacing(12);

    // Réglages (étape 3). La portée et l'intervalle de recalcul s'appliquent tout de suite ;
    // le nombre de véhicules et la graine demandent de relancer (bouton « Appliquer »).
    QVBoxLayout *layoutReglages = nullptr;
    QGroupBox *groupeReglages = nouveauGroupe("Réglages", layoutReglages);
    auto *formulaire = new QFormLayout;
    m_nombreVehicules = new QSpinBox;
    m_nombreVehicules->setRange(10, 10000);
    m_nombreVehicules->setSingleStep(100);
    m_nombreVehicules->setValue(2000);  // au moins 2000 exigés par le sujet
    m_nombreVehicules->setGroupSeparatorShown(true);
    m_nombreVehicules->setToolTip("Nombre de véhicules simulés (au moins 2000 demandés par le sujet)");
    formulaire->addRow("Véhicules", m_nombreVehicules);
    m_rayon = new QDoubleSpinBox;
    m_rayon->setRange(20, 2000);
    m_rayon->setSingleStep(25);
    m_rayon->setDecimals(0);
    m_rayon->setSuffix(" m");
    m_rayon->setValue(300);
    m_rayon->setToolTip("Portée radio R commune à tous les véhicules (s'applique tout de suite)");
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
    layoutReglages->addLayout(formulaire);
    auto *boutonAppliquer = new QPushButton("Appliquer et relancer");
    boutonAppliquer->setToolTip("Recrée la simulation avec ce nombre de véhicules et cette graine");
    connect(boutonAppliquer, &QPushButton::clicked, this, &MainWindow::lancerSimulation);
    layoutReglages->addWidget(boutonAppliquer);
    layoutSimulation->addWidget(groupeReglages);

    connect(m_rayon, &QDoubleSpinBox::valueChanged, this, [this](double rayon) {
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, rayon] { moteur->setRayon(rayon); }, Qt::QueuedConnection);
        m_vue3d->setRayon(rayon);  // taille de la bulle de portée dans la vue 3D
    });
    connect(m_intervalle, &QDoubleSpinBox::valueChanged, this, [this](double secondes) {
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, secondes] { moteur->setIntervalleRecalcul(secondes); },
                                  Qt::QueuedConnection);
    });

    // État de la simulation en cours.
    QVBoxLayout *layoutEtat = nullptr;
    QGroupBox *groupeEtat = nouveauGroupe("En ce moment", layoutEtat);
    m_infosSimulation = texte("Chargez une carte pour lancer la simulation.");
    layoutEtat->addWidget(m_infosSimulation);
    layoutSimulation->addWidget(groupeEtat);

    // Graphe d'interférences (étape 4) : statistiques demandées par le cahier.
    QVBoxLayout *layoutInterference = nullptr;
    QGroupBox *groupeInterference = nouveauGroupe("Graphe d'interférences", layoutInterference);
    m_infosInterference = texte("Pas encore calculé.");
    layoutInterference->addWidget(m_infosInterference);
    layoutInterference->addWidget(boutonPour(m_actionAretes));
    layoutSimulation->addWidget(groupeInterference);
    layoutSimulation->addStretch();  // pousse les groupes vers le haut
    nouvelOnglet("Simulation", simulation);

    // ===================== Onglet 2 : Coloration =====================
    auto *coloration = new QWidget;
    auto *layoutColoration = new QVBoxLayout(coloration);
    layoutColoration->setSpacing(12);

    QVBoxLayout *layoutResultats = nullptr;
    QGroupBox *groupeResultats = nouveauGroupe("Canaux radio (étape 5)", layoutResultats);
    auto *ligneAlgo = new QHBoxLayout;
    ligneAlgo->addWidget(new QLabel("Couleurs affichées :"));
    m_algoAffiche = new QComboBox;
    for (Coloration::Algo algo : Coloration::TOUS_LES_ALGOS)
        m_algoAffiche->addItem(Coloration::nom(algo), int(algo));
    m_algoAffiche->setCurrentIndex(int(Coloration::Algo::WelshPowell));  // un des deux algorithmes obligatoires
    m_algoAffiche->setToolTip("Algorithme dont les couleurs (canaux) sont dessinées sur la carte");
    ligneAlgo->addWidget(m_algoAffiche, 1);
    layoutResultats->addLayout(ligneAlgo);

    // Résultats du dernier recalcul : une ligne par algorithme. Plus lisible que du texte :
    // on compare les colonnes d'un coup d'œil. Cliquer une ligne affiche ses couleurs.
    m_tableauColoration = new QTableWidget(Coloration::NOMBRE_ALGOS, 5);
    m_tableauColoration->setHorizontalHeaderLabels({"Algorithme", "Canaux", "ms", "S", ""});
    m_tableauColoration->horizontalHeaderItem(1)->setToolTip("Nombre de canaux radio utilisés (k) : moins = mieux");
    m_tableauColoration->horizontalHeaderItem(2)->setToolTip("Temps de calcul de la coloration, en millisecondes");
    m_tableauColoration->horizontalHeaderItem(3)->setToolTip(
        "Stabilité S : part des véhicules qui gardent leur canal depuis le recalcul précédent (1 = aucun changement)");
    m_tableauColoration->horizontalHeaderItem(4)->setToolTip("Validité : deux voisins n'ont jamais le même canal");
    m_tableauColoration->verticalHeader()->hide();
    m_tableauColoration->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableauColoration->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tableauColoration->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableauColoration->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableauColoration->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableauColoration->setFocusPolicy(Qt::NoFocus);
    m_tableauColoration->setShowGrid(false);
    m_tableauColoration->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tableauColoration->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (Coloration::Algo algo : Coloration::TOUS_LES_ALGOS) {
        const int ligne = int(algo);
        // Petit carré de la couleur de l'algorithme dans les courbes de la fenêtre Comparaison.
        QPixmap carre(10, 10);
        carre.fill(FenetreComparaison::couleurAlgo(algo));
        auto *nom = new QTableWidgetItem(QIcon(carre), Coloration::nom(algo));
        m_tableauColoration->setItem(ligne, 0, nom);
        for (int colonne = 1; colonne < 5; ++colonne) {
            auto *case_ = new QTableWidgetItem("–");
            case_->setTextAlignment(Qt::AlignCenter);
            m_tableauColoration->setItem(ligne, colonne, case_);
        }
    }
    // Hauteur exacte : en-tête + une ligne par algorithme + cadre (pas de ligne vide en dessous).
    m_tableauColoration->setFixedHeight(m_tableauColoration->horizontalHeader()->sizeHint().height()
                                        + Coloration::NOMBRE_ALGOS * m_tableauColoration->verticalHeader()->defaultSectionSize()
                                        + 2 * m_tableauColoration->frameWidth() + 2);
    layoutResultats->addWidget(m_tableauColoration);
    auto *noteTableau = texte("En gras : le moins de canaux. Cliquez une ligne pour afficher ses couleurs.");
    noteTableau->setForegroundRole(QPalette::PlaceholderText);
    layoutResultats->addWidget(noteTableau);
    m_legendeCanaux = new QLabel;
    layoutResultats->addWidget(m_legendeCanaux);
    layoutColoration->addWidget(groupeResultats);

    // Liste déroulante et tableau restent d'accord : changer l'un change l'autre.
    connect(m_algoAffiche, &QComboBox::currentIndexChanged, this, [this] {
        const auto algo = static_cast<Coloration::Algo>(m_algoAffiche->currentData().toInt());
        QMetaObject::invokeMethod(m_moteur, [moteur = m_moteur, algo] { moteur->setAlgoAffiche(algo); },
                                  Qt::QueuedConnection);
        const QSignalBlocker bloque(m_tableauColoration);  // évite l'aller-retour tableau -> liste
        m_tableauColoration->selectRow(int(algo));
        if (!m_comparaison->mesures().isEmpty())
            recevoirMesure(m_comparaison->mesures().last());  // met à jour indicateurs et légende
    });
    connect(m_tableauColoration, &QTableWidget::itemSelectionChanged, this, [this] {
        const QList<QTableWidgetItem *> choix = m_tableauColoration->selectedItems();
        if (!choix.isEmpty())
            m_algoAffiche->setCurrentIndex(choix.first()->row());
    });
    m_tableauColoration->selectRow(m_algoAffiche->currentIndex());

    QVBoxLayout *layoutAnalyse = nullptr;
    QGroupBox *groupeAnalyse = nouveauGroupe("Analyse", layoutAnalyse);
    layoutAnalyse->addWidget(texte("Courbes k(t), temps de calcul et S(t) des 4 algorithmes depuis le début, "
                                   "et export des mesures pour un tableur."));
    auto *boutonsAnalyse = new QHBoxLayout;
    boutonsAnalyse->addWidget(boutonPour(m_actionComparaison));
    auto *boutonCsv = new QPushButton("Exporter CSV…");
    boutonCsv->setToolTip("Une ligne par recalcul : canaux, temps et stabilité de chaque algorithme");
    connect(boutonCsv, &QPushButton::clicked, this, [this] { m_comparaison->exporterAvecDialogue(this); });
    boutonsAnalyse->addWidget(boutonCsv);
    layoutAnalyse->addLayout(boutonsAnalyse);
    layoutColoration->addWidget(groupeAnalyse);
    layoutColoration->addStretch();
    nouvelOnglet("Coloration", coloration);

    // ===================== Onglet 3 : Carte =====================
    auto *carte = new QWidget;
    auto *layoutCarte = new QVBoxLayout(carte);
    layoutCarte->setSpacing(12);

    QVBoxLayout *layoutInfosCarte = nullptr;
    QGroupBox *groupeCarte = nouveauGroupe("Carte", layoutInfosCarte);
    m_infosCarte = texte("Aucune carte chargée.");
    m_infosCarte->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layoutInfosCarte->addWidget(m_infosCarte);
    layoutCarte->addWidget(groupeCarte);

    // Zone de simulation : les mêmes actions que la barre d'outils, en boutons.
    QVBoxLayout *layoutZone = nullptr;
    QGroupBox *groupeZone = nouveauGroupe("Zone de simulation", layoutZone);
    m_infosZone = texte(QString());
    layoutZone->addWidget(m_infosZone);
    auto *boutonsZone = new QHBoxLayout;
    boutonsZone->addWidget(boutonPour(m_actionZone));
    boutonsZone->addWidget(boutonPour(m_actionEffacerZone));
    layoutZone->addLayout(boutonsZone);
    layoutCarte->addWidget(groupeZone);

    // Graphe routier (étape 2) : taille du graphe et affichage des intersections.
    QVBoxLayout *layoutGraphe = nullptr;
    QGroupBox *groupeGraphe = nouveauGroupe("Graphe routier", layoutGraphe);
    m_infosGraphe = texte("Chargez une carte pour construire le graphe.");
    layoutGraphe->addWidget(m_infosGraphe);
    layoutGraphe->addWidget(boutonPour(m_actionIntersections));
    layoutCarte->addWidget(groupeGraphe);

    // Itinéraire de test : vérifie visuellement le plus court chemin (Dijkstra / A*).
    QVBoxLayout *layoutItineraire = nullptr;
    QGroupBox *groupeItineraire = nouveauGroupe("Plus court chemin", layoutItineraire);
    m_infosItineraire = texte("Activez « Itinéraire » puis cliquez deux points.");
    layoutItineraire->addWidget(m_infosItineraire);
    layoutItineraire->addWidget(boutonPour(m_actionItineraire));
    layoutCarte->addWidget(groupeItineraire);

    // Légende : un petit trait de la couleur et de l'épaisseur de chaque type de route.
    QVBoxLayout *layoutLegende = nullptr;
    QGroupBox *groupeLegende = nouveauGroupe("Légende des routes", layoutLegende);
    const QList<QPair<TypeRoute, QString>> types = {
        {TypeRoute::Autoroute, "Autoroute, voie rapide"},
        {TypeRoute::Principale, "Route principale"},
        {TypeRoute::Secondaire, "Route secondaire"},
        {TypeRoute::Locale, "Rue, route locale"},
    };
    for (const auto &[type, nom] : types) {
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
        ligne->addWidget(new QLabel(nom), 1);
        layoutLegende->addLayout(ligne);
    }
    layoutCarte->addWidget(groupeLegende);
    layoutCarte->addStretch();
    nouvelOnglet("Carte", carte);

    // -- Aide rapide pour se déplacer (texte selon la vue, voir afficherVue3D) --
    m_aide = texte(QString());
    m_aide->setForegroundRole(QPalette::PlaceholderText);  // gris discret, lisible dans les deux thèmes
    m_aide->setContentsMargins(4, 0, 4, 0);
    layoutPanneau->addWidget(m_aide);

    panneau->setMinimumWidth(largeurPanneau + layoutPanneau->contentsMargins().left()
                             + layoutPanneau->contentsMargins().right());
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
    barre->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    barre->setContextMenuPolicy(Qt::PreventContextMenu);
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
    // Tableau des résultats remis à blanc : texte, gras, couleur et bulle d'aide (sinon un
    // tiret garderait par exemple la bulle « Coloration valide » du calcul précédent).
    for (int ligne = 0; ligne < m_tableauColoration->rowCount(); ++ligne)
        for (int colonne = 1; colonne < m_tableauColoration->columnCount(); ++colonne) {
            QTableWidgetItem *caseTableau = m_tableauColoration->item(ligne, colonne);
            caseTableau->setText("–");
            caseTableau->setFont(m_tableauColoration->font());
            caseTableau->setForeground(QBrush());
            caseTableau->setToolTip(QString());
        }
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
    m_attendDemarrage = true;
    majInvite();
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

    // Tableau des résultats : en gras (couleur d'accent) le plus petit nombre de canaux,
    // c'est-à-dire le meilleur algorithme sur ce critère (il peut y en avoir plusieurs).
    int minimum = std::numeric_limits<int>::max();
    for (Coloration::Algo a : Coloration::TOUS_LES_ALGOS)
        minimum = std::min(minimum, mesure.algo(a).couleurs);
    const QColor vert = Theme::estSombre() ? QColor(0x4A, 0xDE, 0x80) : QColor(0x15, 0x80, 0x3D);
    const QColor rouge = Theme::estSombre() ? QColor(0xF8, 0x71, 0x71) : QColor(0xB9, 0x1C, 0x1C);
    for (Coloration::Algo a : Coloration::TOUS_LES_ALGOS) {
        const MesureAlgo &m = mesure.algo(a);
        const int ligne = int(a);
        QTableWidgetItem *canaux = m_tableauColoration->item(ligne, 1);
        canaux->setText(fr.toString(m.couleurs));
        QFont police = canaux->font();
        police.setBold(m.couleurs == minimum);
        canaux->setFont(police);
        canaux->setForeground(m.couleurs == minimum ? QBrush(Theme::accent()) : QBrush());
        m_tableauColoration->item(ligne, 2)->setText(fr.toString(m.dureeMs, 'f', 2));
        m_tableauColoration->item(ligne, 3)->setText(fr.toString(m.stabilite, 'f', 2));
        QTableWidgetItem *validite = m_tableauColoration->item(ligne, 4);
        validite->setText(m.valide ? "✓" : QString("✗ %1").arg(m.conflits));
        validite->setForeground(m.valide ? vert : rouge);
        validite->setToolTip(m.valide ? "Coloration valide : aucune arête entre deux véhicules de même canal"
                                      : QString("%1 conflits : arêtes entre deux véhicules de même canal").arg(m.conflits));
    }

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
    // Une pastille par canal utilisé, dans la couleur des véhicules sur la carte, 12 par ligne
    // (une seule longue ligne déborderait du panneau dès 16 canaux).
    constexpr int maxAffiches = 36, parLigne = 12, taille = 14;
    const int affiches = std::min(nombre, maxAffiches);
    const int lignes = (affiches + parLigne - 1) / parLigne;
    const qreal dpr = devicePixelRatioF();
    QPixmap image(qCeil(std::min(affiches, parLigne) * (taille + 3) * dpr), qCeil(lignes * (taille + 3) * dpr));
    image.setDevicePixelRatio(dpr);  // net sur un écran agrandi (125 %, Retina...)
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    for (int c = 0; c < affiches; ++c) {
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(MapView::couleurCanal(c));
        p.drawEllipse(QRectF(c % parLigne * (taille + 3) + 1, c / parLigne * (taille + 3) + 1, taille - 2, taille - 2));
    }
    p.end();
    m_legendeCanaux->setPixmap(image);
    m_legendeCanaux->setToolTip(QString("%1 canaux radio utilisés%2")
                                    .arg(nombre)
                                    .arg(nombre > maxAffiches ? QString(" (%1 premiers affichés)").arg(maxAffiches) : QString()));
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
