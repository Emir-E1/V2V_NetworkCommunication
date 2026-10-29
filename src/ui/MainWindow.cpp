// Construction de la fenêtre principale et liaison entre la carte et le panneau.
// Chaque étape du projet viendra ajouter ici sa partie (simulation, graphe, courbes).

#include "ui/MainWindow.h"

#include "map/MapView.h"
#include "map/OsmLoader.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
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

// Dossier data/ du projet, fourni par CMake (voir CMakeLists.txt).
#ifndef V2V_DOSSIER_DONNEES
#define V2V_DOSSIER_DONNEES "data"
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_vue(new MapView(this))
{
    setWindowTitle("Simulateur V2V");
    resize(1280, 800);
    setCentralWidget(m_vue);

    // ---- Actions : une action = une commande, réutilisable dans le menu ET la barre d'outils ----
    auto *actionOuvrir = new QAction("Ouvrir une carte…", this);
    actionOuvrir->setShortcut(QKeySequence::Open);  // Ctrl+O
    actionOuvrir->setToolTip("Ouvrir un fichier OpenStreetMap (.osm) — Ctrl+O");
    connect(actionOuvrir, &QAction::triggered, this, &MainWindow::ouvrirCarte);

    m_actionRecentrer = new QAction("Recentrer", this);
    m_actionRecentrer->setShortcut(Qt::Key_R);
    m_actionRecentrer->setToolTip("Afficher toute la carte — R");
    connect(m_actionRecentrer, &QAction::triggered, m_vue, &MapView::recentrer);

    m_actionZone = new QAction("Choisir la zone", this);
    m_actionZone->setCheckable(true);  // bouton enfoncé tant qu'on trace la zone
    m_actionZone->setShortcut(Qt::Key_Z);
    m_actionZone->setToolTip("Tracer à la souris la zone de simulation (Échap pour annuler) — Z");
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
        if (actif)
            m_actionZone->setChecked(false);  // un seul mode "clic sur la carte" à la fois
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

    // Tant qu'aucune carte n'est chargée, ces commandes n'ont pas de sens.
    for (QAction *a : {m_actionRecentrer, m_actionZone, m_actionEffacerZone, m_actionItineraire, m_actionIntersections})
        a->setEnabled(false);

    // ---- Menu ----
    auto *menuFichier = menuBar()->addMenu("&Fichier");
    menuFichier->addAction(actionOuvrir);
    menuFichier->addSeparator();
    menuFichier->addAction("&Quitter", QKeySequence::Quit, this, &QWidget::close);

    auto *menuCarte = menuBar()->addMenu("&Carte");
    menuCarte->addAction(m_actionRecentrer);
    menuCarte->addAction(m_actionZone);
    menuCarte->addAction(m_actionEffacerZone);
    menuCarte->addSeparator();
    menuCarte->addAction(m_actionItineraire);
    menuCarte->addAction(m_actionIntersections);

    // ---- Barre d'outils (texte seul : plus lisible que des icônes inventées) ----
    auto *barre = addToolBar("Outils");
    barre->setMovable(false);
    barre->setToolButtonStyle(Qt::ToolButtonTextOnly);
    barre->addAction(actionOuvrir);
    barre->addSeparator();
    barre->addAction(m_actionRecentrer);
    barre->addAction(m_actionZone);
    barre->addAction(m_actionEffacerZone);
    barre->addSeparator();
    barre->addAction(m_actionItineraire);
    barre->addAction(m_actionIntersections);

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

    // ---- Barre d'état : message à gauche, coordonnées GPS à droite ----
    m_coordonnees = new QLabel;
    m_coordonnees->setMinimumWidth(200);
    statusBar()->addPermanentWidget(m_coordonnees);
    statusBar()->showMessage("Aucune carte chargée — Fichier > Ouvrir une carte… (Ctrl+O)");

    // ---- Réactions aux événements de la carte ----
    connect(m_vue, &MapView::positionSouris, this, [this](double lat, double lon) {
        m_coordonnees->setText(QString("%1° N   %2° E")
                                   .arg(QLocale().toString(lat, 'f', 5))
                                   .arg(QLocale().toString(lon, 'f', 5)));
    });
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

    // -- Aide rapide pour se déplacer sur la carte --
    auto *aide = new QLabel("Molette : zoom   ·   Clic-glisser : déplacer");
    aide->setStyleSheet("color: palette(mid);");
    aide->setWordWrap(true);
    layout->addWidget(aide);

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
    const qint64 ms = chrono.elapsed();
    QApplication::restoreOverrideCursor();

    const QLocale fr;
    m_infosCarte->setText(QString("<b>%1</b><br>%2 routes · %3 nœuds")
                              .arg(QFileInfo(chemin).fileName().toHtmlEscaped(),
                                   fr.toString(m_carte.routes.size()),
                                   fr.toString(m_carte.noeuds.size())));
    setWindowTitle(QString("Simulateur V2V — %1").arg(QFileInfo(chemin).fileName()));
    statusBar()->showMessage(QString("Carte chargée en %1 ms").arg(ms));

    for (QAction *a : {m_actionRecentrer, m_actionZone, m_actionEffacerZone, m_actionItineraire, m_actionIntersections})
        a->setEnabled(true);
    majInfosZone();
    construireGraphe();
    return true;
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
        return;
    }
    m_actionItineraire->setEnabled(true);

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
