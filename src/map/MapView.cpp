#include "map/MapView.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPixmapCache>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr double RAYON_TERRE_M = 6371000.0;  // même valeur que OsmCarte::versMetres
constexpr double ZOOM_MAX = 20.0;            // 20 pixels par mètre : on voit le détail d'un carrefour
constexpr double PAS_ZOOM = 1.2;             // un cran de molette = zoom de 20 %
constexpr double TAILLE_MIN_ZONE_M = 20.0;   // en dessous, on considère que c'était un simple clic

// Toutes les routes dans UN seul objet graphique, dessiné à la main.
//
// Pourquoi pas un objet Qt par route, ou un grand QPainterPath par type ?
//   - 5 000 objets : Qt passe son temps à les gérer (collision, tri...).
//   - un grand QPainterPath : Qt doit calculer le contour de tout le réseau à
//     chaque image (~700 ms mesurés), même quand on ne voit qu'un quartier.
// Ici chaque route est une simple polyligne avec son rectangle englobant :
// on ne dessine que celles qui touchent la partie visible de la carte.
// Le même principe servira pour les 2 000 véhicules (étape 3).
class CoucheRoutes : public QGraphicsItem
{
public:
    struct Trace
    {
        QPolygonF points;  // points de la route, en mètres
        QRectF limites;    // rectangle englobant, pour savoir vite si elle est visible
    };

    explicit CoucheRoutes(std::array<QVector<Trace>, 4> traces)
        : m_traces(std::move(traces))
    {
        for (const auto &type : m_traces)
            for (const Trace &t : type)
                m_limites |= t.limites;
        // Marge de quelques mètres : les traits épais débordent un peu des points.
        m_limites.adjust(-10, -10, 10, 10);
        // Indispensable pour que option->exposedRect donne la partie réellement visible.
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
        // Cache image à l'échelle de l'écran : tant qu'on ne zoome pas, les routes ne
        // sont pas redessinées (déplacement de la carte, et plus tard animation des
        // véhicules par-dessus, deviennent quasi gratuits).
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    }

    QRectF boundingRect() const override { return m_limites; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        // Partie visible, élargie de quelques pixels : une route juste hors de l'écran
        // peut quand même dépasser dans l'image à cause de l'épaisseur de son trait.
        const double marge = 4.0 / std::max(painter->worldTransform().m11(), 1e-9);
        const QRectF visible = option->exposedRect.adjusted(-marge, -marge, marge, marge);
        painter->setRenderHint(QPainter::Antialiasing);
        // Des petites rues (indice 3) aux autoroutes (indice 0) : les grandes routes
        // sont dessinées en dernier, donc par-dessus.
        for (int i = 3; i >= 0; --i) {
            const auto type = static_cast<TypeRoute>(i);
            QPen stylo(MapView::couleurRoute(type), MapView::epaisseurRoute(type),
                       Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            stylo.setCosmetic(true);  // épaisseur en pixels écran, constante quel que soit le zoom
            painter->setPen(stylo);
            for (const Trace &t : m_traces[i])
                if (t.limites.intersects(visible))
                    painter->drawPolyline(t.points);
        }
    }

private:
    std::array<QVector<Trace>, 4> m_traces;  // une liste de routes par TypeRoute
    QRectF m_limites;
};

// Les intersections du graphe routier, dessinées comme des petits points.
// Même principe que CoucheRoutes : un seul objet, seuls les points visibles sont dessinés.
class CouchePoints : public QGraphicsItem
{
public:
    explicit CouchePoints(QVector<QPointF> positions)
        : m_positions(std::move(positions))
    {
        m_limites = QPolygonF(m_positions).boundingRect().adjusted(-10, -10, 10, 10);
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption);
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    }

    QRectF boundingRect() const override { return m_limites; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        QVector<QPointF> visibles;
        for (const QPointF &p : m_positions)
            if (option->exposedRect.contains(p))
                visibles.append(p);
        QPen stylo(QColor(0x33, 0x41, 0x55), 4.0, Qt::SolidLine, Qt::RoundCap);
        stylo.setCosmetic(true);  // 4 pixels quel que soit le zoom
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(stylo);
        painter->drawPoints(visibles.constData(), visibles.size());  // un seul appel : rapide
    }

private:
    QVector<QPointF> m_positions;
    QRectF m_limites;
};
}

MapView::MapView(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));

    setRenderHint(QPainter::Antialiasing);              // routes lissées
    setDragMode(QGraphicsView::ScrollHandDrag);         // clic-glisser = déplacer la carte
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);  // la molette zoome là où est la souris
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // comme une carte en ligne : pas de barres
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Redessiner toute la vue à chaque changement : sinon l'échelle graphique (dessinée
    // en coordonnées écran) laisse des traces quand on déplace la carte.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setBackgroundBrush(QColor(0xF5, 0xF5, 0xF2));      // fond neutre : les véhicules colorés ressortiront
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);                // pour suivre la souris même sans clic

    // Le cache image des routes fait la taille de l'écran. Sur un écran haute
    // résolution (Retina, 4K) il dépasse la limite par défaut de Qt (10 Mo) et serait
    // recalculé à chaque image. 64 Mo couvrent un écran 4K. Unité : kilo-octets.
    QPixmapCache::setCacheLimit(std::max(QPixmapCache::cacheLimit(), 64 * 1024));
}

// ---------------------------------------------------------------------------
// Style des routes : des gris, du plus clair (petites rues) au plus foncé (autoroutes).
// La carte reste discrète pour que les véhicules (étape 3) et leurs canaux radio
// colorés (étape 5) soient bien visibles par-dessus.
// ---------------------------------------------------------------------------
QColor MapView::couleurRoute(TypeRoute type)
{
    switch (type) {
    case TypeRoute::Autoroute:  return QColor(0x5B, 0x63, 0x70);
    case TypeRoute::Principale: return QColor(0x80, 0x88, 0x94);
    case TypeRoute::Secondaire: return QColor(0xA8, 0xAE, 0xB8);
    case TypeRoute::Locale:     return QColor(0xCC, 0xD0, 0xD6);
    }
    return Qt::gray;
}

double MapView::epaisseurRoute(TypeRoute type)
{
    switch (type) {
    case TypeRoute::Autoroute:  return 3.0;
    case TypeRoute::Principale: return 2.4;
    case TypeRoute::Secondaire: return 1.7;
    case TypeRoute::Locale:     return 1.1;
    }
    return 1.0;
}

void MapView::afficherCarte(const OsmCarte &carte)
{
    scene()->clear();  // supprime aussi l'ancienne zone, l'itinéraire... : on oublie nos pointeurs
    m_voile = nullptr;
    m_cadre = nullptr;
    m_itineraire = nullptr;
    m_marqueurs[0] = m_marqueurs[1] = nullptr;
    m_intersections = nullptr;
    m_zone = QRectF();
    // Un tracé de zone commencé sur l'ancienne carte n'a plus de sens : on l'annule.
    if (m_modeSelection) {
        setModeSelectionZone(false);
        emit modeSelectionTermine();  // pour que le bouton "Choisir la zone" se relâche
    }

    m_latRef = (carte.latMin + carte.latMax) / 2.0;
    m_lonRef = (carte.lonMin + carte.lonMax) / 2.0;

    // Conversion de chaque route en polyligne (en mètres), rangée selon son type.
    std::array<QVector<CoucheRoutes::Trace>, 4> traces;
    for (const OsmRoute &route : carte.routes) {
        CoucheRoutes::Trace trace;
        trace.points.reserve(route.noeuds.size());
        for (qint64 id : route.noeuds) {
            const auto it = carte.noeuds.constFind(id);
            if (it != carte.noeuds.constEnd())  // toujours vrai : le parseur retire les nœuds manquants
                trace.points.append(carte.versMetres(it->lat, it->lon));
        }
        if (trace.points.size() < 2)
            continue;
        // +1 m de chaque côté : une route parfaitement horizontale ou verticale aurait
        // sinon un rectangle de hauteur (ou largeur) nulle, jamais jugé "visible" par Qt.
        trace.limites = trace.points.boundingRect().adjusted(-1, -1, 1, 1);
        traces[static_cast<int>(route.type)].append(std::move(trace));
    }
    scene()->addItem(new CoucheRoutes(std::move(traces)));  // la scène devient propriétaire de l'objet

    // Rectangle de la carte en mètres : coin nord-ouest et coin sud-est.
    m_limitesCarte = QRectF(carte.versMetres(carte.latMax, carte.lonMin),
                            carte.versMetres(carte.latMin, carte.lonMax)).normalized();
    // Marge autour de la carte pour pouvoir la déplacer un peu au-delà des bords.
    const double mx = m_limitesCarte.width() / 2.0;
    const double my = m_limitesCarte.height() / 2.0;
    scene()->setSceneRect(m_limitesCarte.adjusted(-mx, -my, mx, my));

    recentrer();
}

void MapView::recentrer()
{
    if (m_limitesCarte.isEmpty())
        return;
    fitInView(m_limitesCarte, Qt::KeepAspectRatio);
    // Très petite carte : fitInView zoomerait au-delà du maximum autorisé.
    if (echelle() > ZOOM_MAX) {
        const double facteur = ZOOM_MAX / echelle();
        scale(facteur, facteur);
    }
}

double MapView::echelle() const
{
    // La vue n'est jamais tournée : m11 est directement le facteur de zoom (pixels par mètre).
    return transform().m11();
}

// ---------------------------------------------------------------------------
// Zoom à la molette
// ---------------------------------------------------------------------------
void MapView::wheelEvent(QWheelEvent *event)
{
    const double crans = event->angleDelta().y() / 120.0;  // 120 = un cran de molette classique
    if (crans == 0.0 || m_limitesCarte.isEmpty())
        return;

    // Zoom minimal : la carte entière tient deux fois dans la fenêtre (on ne la perd pas de vue).
    // (plafonné à ZOOM_MAX : sur une toute petite carte, le minimum dépasserait le maximum)
    const double zoomMin = std::min(ZOOM_MAX, 0.5 * std::min(viewport()->width() / m_limitesCarte.width(),
                                                             viewport()->height() / m_limitesCarte.height()));
    const double cible = std::clamp(echelle() * std::pow(PAS_ZOOM, crans), zoomMin, ZOOM_MAX);
    const double facteur = cible / echelle();
    scale(facteur, facteur);
    event->accept();
}

// ---------------------------------------------------------------------------
// Sélection de la zone de simulation
// ---------------------------------------------------------------------------
void MapView::setModeSelectionZone(bool actif)
{
    m_modeSelection = actif;
    m_selectionEnCours = false;
    if (actif) {
        setDragMode(QGraphicsView::NoDrag);  // le clic-glisser trace un rectangle
        viewport()->setCursor(Qt::CrossCursor);
        setFocus();  // pour recevoir Échap même si on a activé le mode depuis un bouton
    } else {
        viewport()->unsetCursor();
        setDragMode(QGraphicsView::ScrollHandDrag);  // remet la main pour déplacer
        afficherZone(m_zone);  // si on annule en plein tracé, on revient à l'ancienne zone
    }
}

void MapView::effacerZone()
{
    m_zone = QRectF();
    afficherZone(m_zone);
}

void MapView::afficherZone(const QRectF &zone)
{
    if (zone.isEmpty()) {
        if (m_voile)
            m_voile->hide();
        if (m_cadre)
            m_cadre->hide();
        return;
    }

    // Création à la première utilisation (et après chaque nouvelle carte).
    if (!m_voile) {
        m_voile = scene()->addPath(QPainterPath(), Qt::NoPen, QColor(17, 24, 39, 45));
        m_voile->setZValue(10);  // au-dessus des routes
        QPen contour(QColor(0x25, 0x63, 0xEB), 2.0);
        contour.setCosmetic(true);
        m_cadre = scene()->addPath(QPainterPath(), contour);
        m_cadre->setZValue(11);
    }

    // Voile = toute la scène MOINS la zone (règle "pair-impair" : le rectangle intérieur
    // est un trou). Effet : la zone choisie reste claire, le reste est assombri.
    QPainterPath voile;
    voile.setFillRule(Qt::OddEvenFill);
    voile.addRect(sceneRect());
    voile.addRect(zone);
    m_voile->setPath(voile);

    QPainterPath cadre;
    cadre.addRect(zone);
    m_cadre->setPath(cadre);

    m_voile->show();
    m_cadre->show();
}

// ---------------------------------------------------------------------------
// Étape 2 : itinéraire de test et intersections du graphe routier
// ---------------------------------------------------------------------------
void MapView::setModeItineraire(bool actif)
{
    m_modeItineraire = actif;
}

void MapView::afficherItineraire(const QVector<QPointF> &points)
{
    if (!m_itineraire) {
        QPen stylo(couleurItineraire(), 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        stylo.setCosmetic(true);
        m_itineraire = scene()->addPath(QPainterPath(), stylo);
        m_itineraire->setZValue(20);  // au-dessus des routes et de la zone
        // Cache image : l'itinéraire ne change pas pendant l'animation des véhicules,
        // inutile de le recalculer à chaque image.
        m_itineraire->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    }
    QPainterPath chemin;
    if (!points.isEmpty())
        chemin.addPolygon(QPolygonF(points));  // polyligne ouverte (addPolygon ne referme pas)
    m_itineraire->setPath(chemin);
    m_itineraire->show();
}

void MapView::afficherMarqueur(int numero, const QPointF &pos)
{
    if (numero < 0 || numero > 1)
        return;
    if (!m_marqueurs[numero]) {
        // Pastille ronde avec une lettre. ItemIgnoresTransformations : elle garde la même
        // taille à l'écran quel que soit le zoom (comme une épingle sur une carte en ligne).
        auto *rond = new QGraphicsEllipseItem(-10, -10, 20, 20);
        rond->setBrush(numero == 0 ? QColor(0x16, 0xA3, 0x4A) : QColor(0xDC, 0x26, 0x26));  // vert / rouge
        rond->setPen(QPen(Qt::white, 2));
        rond->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        rond->setZValue(30);
        auto *lettre = new QGraphicsSimpleTextItem(numero == 0 ? "A" : "B", rond);
        QFont police = font();
        police.setBold(true);
        lettre->setFont(police);
        lettre->setBrush(Qt::white);
        lettre->setPos(-lettre->boundingRect().width() / 2, -lettre->boundingRect().height() / 2);
        scene()->addItem(rond);
        m_marqueurs[numero] = rond;
    }
    m_marqueurs[numero]->setPos(pos);
    m_marqueurs[numero]->show();
}

void MapView::effacerItineraire()
{
    if (m_itineraire)
        m_itineraire->hide();
    for (QGraphicsItem *m : m_marqueurs)
        if (m)
            m->hide();
}

void MapView::afficherIntersections(const QVector<QPointF> &positions)
{
    delete m_intersections;  // delete d'un objet de la scène = il est aussi retiré de la scène
    m_intersections = new CouchePoints(positions);
    m_intersections->setZValue(5);  // au-dessus des routes, sous la zone
    m_intersections->setVisible(m_intersectionsVisibles);
    scene()->addItem(m_intersections);
}

void MapView::setIntersectionsVisibles(bool visibles)
{
    m_intersectionsVisibles = visibles;
    if (m_intersections)
        m_intersections->setVisible(visibles);
}

void MapView::mousePressEvent(QMouseEvent *event)
{
    m_posAppui = event->position().toPoint();  // pour savoir au relâchement si c'était un clic
    m_aGlisse = false;

    if (m_modeSelection && event->button() == Qt::LeftButton) {
        m_selectionEnCours = true;
        m_debutSelection = mapToScene(event->position().toPoint());
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);  // comportement normal : déplacer la carte
}

void MapView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF p = mapToScene(event->position().toPoint());

    // Dès que la souris s'éloigne de plus de 4 pixels pendant l'appui, c'est un
    // glisser (déplacement de carte), même si elle revient ensuite au point de départ.
    if ((event->buttons() & Qt::LeftButton)
        && (event->position().toPoint() - m_posAppui).manhattanLength() > 4)
        m_aGlisse = true;

    // Conversion inverse de OsmCarte::versMetres : mètres -> latitude / longitude.
    if (!m_limitesCarte.isEmpty()) {
        const double lat = m_latRef - qRadiansToDegrees(p.y() / RAYON_TERRE_M);
        const double lon = m_lonRef + qRadiansToDegrees(p.x() / (RAYON_TERRE_M * qCos(qDegreesToRadians(m_latRef))));
        emit positionSouris(lat, lon);
    }

    if (m_selectionEnCours) {
        afficherZone(QRectF(m_debutSelection, p).normalized());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_selectionEnCours && event->button() == Qt::LeftButton) {
        const QRectF rect = QRectF(m_debutSelection, mapToScene(event->position().toPoint())).normalized();
        const bool assezGrand = rect.width() >= TAILLE_MIN_ZONE_M && rect.height() >= TAILLE_MIN_ZONE_M;
        if (assezGrand)
            m_zone = rect;
        setModeSelectionZone(false);  // réaffiche m_zone (la nouvelle, ou l'ancienne si simple clic)
        if (assezGrand)
            emit zoneChoisie(m_zone);
        emit modeSelectionTermine();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);

    // Mode itinéraire : la souris n'a jamais glissé pendant l'appui,
    // c'est donc un clic (et pas un déplacement de la carte).
    if (m_modeItineraire && event->button() == Qt::LeftButton && !m_aGlisse)
        emit clicCarte(mapToScene(event->position().toPoint()));
}

void MapView::keyPressEvent(QKeyEvent *event)
{
    // Échap annule le tracé de zone en cours.
    if (m_modeSelection && event->key() == Qt::Key_Escape) {
        setModeSelectionZone(false);
        emit modeSelectionTermine();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Échelle graphique en bas à gauche, comme sur une carte papier.
// ---------------------------------------------------------------------------
void MapView::drawForeground(QPainter *painter, const QRectF &)
{
    if (m_limitesCarte.isEmpty())
        return;

    // Longueur "ronde" (1, 2 ou 5 × 10^k mètres) qui tient dans 120 pixels au zoom actuel.
    const double pixelsParMetre = echelle();
    const double maxMetres = 120.0 / pixelsParMetre;
    const double puissance = std::pow(10.0, std::floor(std::log10(maxMetres)));
    double longueur = puissance;
    for (double m : {5.0, 2.0, 1.0}) {
        if (m * puissance <= maxMetres) {
            longueur = m * puissance;
            break;
        }
    }
    const double largeur = longueur * pixelsParMetre;
    const QString texte = longueur >= 1000.0 ? QString("%1 km").arg(longueur / 1000.0)
                                             : QString("%1 m").arg(longueur);

    painter->save();
    painter->resetTransform();  // on dessine maintenant en pixels écran, plus en mètres
    painter->setRenderHint(QPainter::Antialiasing);

    const QPointF bas(16.0, viewport()->height() - 16.0);  // coin bas-gauche de la barre
    const QRectF fond(bas.x() - 8.0, bas.y() - 28.0, largeur + 16.0, 36.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 215));
    painter->drawRoundedRect(fond, 6.0, 6.0);

    QPen trait(QColor(0x37, 0x41, 0x51), 2.0);
    painter->setPen(trait);
    painter->drawLine(bas, bas + QPointF(largeur, 0.0));                                   // barre
    painter->drawLine(bas, bas + QPointF(0.0, -6.0));                                       // graduation gauche
    painter->drawLine(bas + QPointF(largeur, 0.0), bas + QPointF(largeur, -6.0));           // graduation droite
    painter->drawText(QRectF(bas.x(), bas.y() - 26.0, largeur, 18.0), Qt::AlignCenter, texte);

    painter->restore();
}
