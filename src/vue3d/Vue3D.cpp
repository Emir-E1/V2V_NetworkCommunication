// Vue 3D de la ville et de la simulation (voir Vue3D.h).
//
// Organisation d'une image (paintGL), dans cet ordre :
//   1. le ciel et le sol (un triangle plein écran, voir Shaders3D.h) ;
//   2. ce qui est posé à plat sur le sol : routes (petites d'abord, les grandes passent
//      par-dessus), contour de la zone, disque de portée du véhicule choisi ;
//   3. les objets en relief, avec le tampon de profondeur (un objet proche cache un objet
//      lointain) : bâtiments puis voitures ;
//   4. les objets transparents par-dessus : arcs des arêtes d'interférence, bulle de portée ;
//   5. les textes (fiche du véhicule, aide, boussole) avec QPainter.
// Les éléments posés à plat sont dessinés sans tampon de profondeur, dans l'ordre : sinon
// deux surfaces à la même hauteur (sol et route) « clignotent » l'une à travers l'autre.

#include "vue3d/Vue3D.h"

#include "map/MapView.h"
#include "vue3d/Shaders3D.h"

#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVector2D>
#include <QVector4D>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float CHAMP_VISION_DEG = 45.0f;   // angle d'ouverture vertical de la caméra
constexpr float DISTANCE_MIN = 12.0f;       // zoom maximal : on voit une voiture de près
constexpr float TANGAGE_MIN = 6.0f;         // presque à l'horizontale
constexpr float TANGAGE_MAX = 89.0f;        // presque à la verticale (90 rendrait le calcul instable)
constexpr int ARETES_MAX = 100000;          // même limite que la carte 2D : au-delà, illisible
constexpr float LONGUEUR_VOITURE_MIN_PX = 10.0f;  // taille minimale d'une voiture à l'écran
constexpr int MORCEAUX_ARC = 16;            // doit valoir MORCEAUX dans ARCS_VERTEX
constexpr double RAYON_TERRE_M = 6371000.0; // même valeur que OsmCarte::versMetres

// Routes : demi-largeur réelle (m) et largeur minimale à l'écran (pixels), par type
// (0 autoroute, 1 principale, 2 secondaire, 3 locale, 4 contour de la zone de simulation).
// Les largeurs minimales sont celles de la carte 2D, pour retrouver le même dessin vu de loin.
constexpr float DEMI_LARGEUR_ROUTE[5] = {9.0f, 6.0f, 4.5f, 3.2f, 2.5f};
constexpr float LARGEUR_MIN_ROUTE_PX[5] = {3.0f, 2.4f, 1.7f, 1.1f, 2.5f};

// Couleurs de la scène : maquette d'architecte claire, cohérente avec la carte 2D.
const QColor COULEUR_SOL(0xE6, 0xE4, 0xDD);
const QColor COULEUR_GRILLE(0xCB, 0xC8, 0xBF);
const QColor COULEUR_HORIZON(0xEC, 0xEF, 0xF2);
const QColor COULEUR_ZENITH(0x9F, 0xBF, 0xDD);
const QColor COULEUR_MUR(0xEF, 0xEC, 0xE6);
const QColor COULEUR_TOIT(0xFA, 0xF9, 0xF6);
const QColor COULEUR_ZONE(0x25, 0x63, 0xEB);
const QColor COULEUR_ARETE(0x1E, 0x29, 0x3B);
// Direction vers le soleil (sud-ouest, assez haut) : x = est, y = haut, z = sud.
const QVector3D LUMIERE = QVector3D(-0.45f, 1.0f, 0.55f).normalized();

QVector3D versVec3(const QColor &c)
{
    return {float(c.redF()), float(c.greenF()), float(c.blueF())};
}

// Couleur d'un véhicule : celle de son canal sur la carte 2D (même palette, même légende).
QVector3D couleurVehicule(int canal)
{
    return versVec3(MapView::couleurCanal(canal));
}

// ---------------------------------------------------------------------------
// Rubans des routes. Format d'un sommet : 5 nombres (cx, cz, dx, dz, type) où (cx, cz)
// est un point de l'axe de la route et (dx, dz) la direction vers le bord du ruban.
// La demi-largeur est choisie par le shader (voir ROUTES_VERTEX), ce qui permet d'élargir
// les routes vues de loin sans recalculer ces tableaux.
// ---------------------------------------------------------------------------
constexpr int FLOATS_ROUTE = 5;

void ajouterSommetRoute(QVector<float> &s, const QPointF &centre, double dx, double dz, float type)
{
    s << float(centre.x()) << float(centre.y()) << float(dx) << float(dz) << type;
}

// Petit disque (rayon = demi-largeur) au bout d'une route ou dans un virage serré :
// bouche les encoches entre deux routes qui se rejoignent en biais.
void ajouterDisque(QVector<float> &s, QVector<quint32> &indices, const QPointF &centre, float type)
{
    constexpr int cotes = 12;
    const quint32 base = quint32(s.size() / FLOATS_ROUTE);
    ajouterSommetRoute(s, centre, 0.0, 0.0, type);
    for (int i = 0; i < cotes; ++i) {
        const double a = 2.0 * M_PI * i / cotes;
        ajouterSommetRoute(s, centre, std::cos(a), std::sin(a), type);
    }
    for (int i = 0; i < cotes; ++i)
        indices << base << base + 1 + i << base + 1 + (i + 1) % cotes;
}

// Ruban le long d'une polyligne (en mètres). `ferme` : le dernier point rejoint le premier.
// À chaque point, le bord est décalé selon la moyenne des perpendiculaires des deux
// segments voisins, allongée dans les virages (« onglet ») pour garder une largeur
// constante ; l'allongement est limité, et un disque bouche les virages très serrés.
void ajouterRuban(QVector<float> &s, QVector<quint32> &indices, QVector<QPointF> points, float type, bool ferme)
{
    // Points consécutifs confondus : ils ne donnent aucune direction, on les retire.
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (ferme && points.size() > 1 && points.front() == points.back())
        points.removeLast();
    const int n = points.size();
    if (n < 2)
        return;

    const auto unitaire = [](QPointF v) {
        const double l = std::hypot(v.x(), v.y());
        return l > 1e-9 ? v / l : QPointF(1.0, 0.0);
    };
    // Les 2n sommets du ruban doivent se suivre (base, base + 1...) : les disques des
    // virages serrés ne sont ajoutés qu'après, à la fin.
    const quint32 base = quint32(s.size() / FLOATS_ROUTE);
    QVector<QPointF> virages;
    for (int i = 0; i < n; ++i) {
        const bool premier = i == 0 && !ferme, dernier = i == n - 1 && !ferme;
        const QPointF avant = unitaire(premier ? points[1] - points[0] : points[i] - points[(i - 1 + n) % n]);
        const QPointF apres = unitaire(dernier ? points[n - 1] - points[n - 2] : points[(i + 1) % n] - points[i]);
        // Perpendiculaires (vers la gauche) des deux segments, puis leur moyenne.
        const QPointF p1(-avant.y(), avant.x()), p2(-apres.y(), apres.x());
        QPointF m = p1 + p2;
        const double l = std::hypot(m.x(), m.y());
        m = l > 1e-6 ? m / l : p2;  // demi-tour complet : on garde la perpendiculaire du segment suivant
        const double cosDemiAngle = m.x() * p2.x() + m.y() * p2.y();
        const double onglet = 1.0 / std::max(cosDemiAngle, 0.4);  // allongement limité à 2,5
        if (cosDemiAngle < 0.75 && !premier && !dernier)
            virages.append(points[i]);
        ajouterSommetRoute(s, points[i], m.x() * onglet, m.y() * onglet, type);
        ajouterSommetRoute(s, points[i], -m.x() * onglet, -m.y() * onglet, type);
    }
    const int segments = ferme ? n : n - 1;
    for (int i = 0; i < segments; ++i) {
        const quint32 a = base + 2 * i, b = a + 1;
        const quint32 c = base + 2 * ((i + 1) % n), d = c + 1;
        indices << a << b << c << b << d << c;
    }
    if (!ferme) {
        virages.append(points.front());
        virages.append(points.back());
    }
    for (const QPointF &p : virages)
        ajouterDisque(s, indices, p, type);
}

// ---------------------------------------------------------------------------
// Forme d'une voiture (en mètres, avant vers +x, toit vers +y) : 6 pavés.
// Format d'un sommet : 7 nombres (x, y, z, nx, ny, nz, partie), partie : 0 carrosserie
// (couleur du canal), 1 vitres, 2 pneus. Chaque face a ses propres sommets, pour que sa
// normale (direction perpendiculaire, utilisée par l'éclairage) soit nette.
// ---------------------------------------------------------------------------
constexpr int FLOATS_VOITURE = 7;

void ajouterPave(QVector<float> &s, QVector<quint32> &indices, QVector3D min, QVector3D max, float partieCotes,
                 float partieDessus)
{
    // Pour chaque face : sa normale et ses 4 coins (dans le sens inverse des aiguilles d'une
    // montre, vus de l'extérieur).
    struct Face { QVector3D normale; QVector3D coins[4]; };
    const Face faces[6] = {
        {{1, 0, 0}, {{max.x(), min.y(), min.z()}, {max.x(), max.y(), min.z()}, {max.x(), max.y(), max.z()}, {max.x(), min.y(), max.z()}}},
        {{-1, 0, 0}, {{min.x(), min.y(), max.z()}, {min.x(), max.y(), max.z()}, {min.x(), max.y(), min.z()}, {min.x(), min.y(), min.z()}}},
        {{0, 1, 0}, {{min.x(), max.y(), min.z()}, {min.x(), max.y(), max.z()}, {max.x(), max.y(), max.z()}, {max.x(), max.y(), min.z()}}},
        {{0, -1, 0}, {{min.x(), min.y(), max.z()}, {min.x(), min.y(), min.z()}, {max.x(), min.y(), min.z()}, {max.x(), min.y(), max.z()}}},
        {{0, 0, 1}, {{max.x(), min.y(), max.z()}, {max.x(), max.y(), max.z()}, {min.x(), max.y(), max.z()}, {min.x(), min.y(), max.z()}}},
        {{0, 0, -1}, {{min.x(), min.y(), min.z()}, {min.x(), max.y(), min.z()}, {max.x(), max.y(), min.z()}, {max.x(), min.y(), min.z()}}},
    };
    for (const Face &f : faces) {
        const quint32 base = quint32(s.size() / FLOATS_VOITURE);
        const float partie = f.normale.y() > 0.5f ? partieDessus : partieCotes;
        for (const QVector3D &c : f.coins)
            s << c.x() << c.y() << c.z() << f.normale.x() << f.normale.y() << f.normale.z() << partie;
        indices << base << base + 1 << base + 2 << base << base + 2 << base + 3;
    }
}

void formeVoiture(QVector<float> &s, QVector<quint32> &indices)
{
    // Carrosserie (4,4 m x 1,8 m) et habitacle (vitres sur les côtés, toit de la couleur du canal).
    ajouterPave(s, indices, {-2.2f, 0.32f, -0.9f}, {2.2f, 1.0f, 0.9f}, 0, 0);
    ajouterPave(s, indices, {-1.25f, 1.0f, -0.78f}, {0.85f, 1.52f, 0.78f}, 1, 0);
    // Quatre roues, qui dépassent un peu sous et à côté de la carrosserie.
    for (float x : {-1.35f, 1.35f})
        for (float z : {-0.82f, 0.82f})
            ajouterPave(s, indices, {x - 0.36f, 0.0f, z - 0.14f}, {x + 0.36f, 0.62f, z + 0.14f}, 2, 2);
}

// Demi-sphère de rayon 1 (la bulle de portée), posée sur le sol : 3 nombres par sommet.
void formeBulle(QVector<float> &s, QVector<quint32> &indices)
{
    constexpr int meridiens = 48, paralleles = 12;
    for (int j = 0; j <= paralleles; ++j) {
        const double phi = M_PI / 2.0 * j / paralleles;  // 0 = au sol, pi/2 = au sommet
        for (int i = 0; i <= meridiens; ++i) {
            const double theta = 2.0 * M_PI * i / meridiens;
            s << float(std::cos(phi) * std::cos(theta)) << float(std::sin(phi)) << float(std::cos(phi) * std::sin(theta));
        }
    }
    for (int j = 0; j < paralleles; ++j)
        for (int i = 0; i < meridiens; ++i) {
            const quint32 a = j * (meridiens + 1) + i, b = a + 1, c = a + meridiens + 1, d = c + 1;
            indices << a << c << b << b << c << d;
        }
}

// Disque de rayon 1 au sol (portée vue de dessus), en éventail autour du centre.
void formeDisque(QVector<float> &s, QVector<quint32> &indices)
{
    constexpr int cotes = 96;
    s << 0.0f << 0.0f << 0.0f;
    for (int i = 0; i < cotes; ++i) {
        const double a = 2.0 * M_PI * i / cotes;
        s << float(std::cos(a)) << 0.0f << float(std::sin(a));
    }
    for (int i = 0; i < cotes; ++i)
        indices << 0 << 1 + i << 1 + (i + 1) % cotes;
}

// Arc d'interférence de référence : pour chaque point le long de l'arc (t de 0 à 1),
// deux sommets (bords gauche et droit du ruban), dessinés en bande de triangles.
void formeArc(QVector<float> &s)
{
    for (int i = 0; i <= MORCEAUX_ARC; ++i) {
        const float t = float(i) / MORCEAUX_ARC;
        s << t << -1.0f << t << 1.0f;
    }
}

// Indique à OpenGL comment lire un attribut dans le tableau actuellement lié :
// `taille` nombres, à partir du nombre `decalage`, `pas` nombres par ligne.
// parInstance : true = une valeur par véhicule (ou par arc) au lieu d'une par sommet.
void attribut(QOpenGLExtraFunctions *gl, int numero, int taille, int pas, int decalage, bool parInstance = false)
{
    gl->glEnableVertexAttribArray(numero);
    gl->glVertexAttribPointer(numero, taille, GL_FLOAT, GL_FALSE, pas * int(sizeof(float)),
                              reinterpret_cast<const void *>(qintptr(decalage) * qintptr(sizeof(float))));
    gl->glVertexAttribDivisor(numero, parInstance ? 1 : 0);
}

template<typename T>
void envoyer(QOpenGLBuffer &tampon, const QVector<T> &donnees)
{
    tampon.bind();
    tampon.allocate(donnees.constData(), int(donnees.size() * sizeof(T)));
}
}  // namespace

Vue3D::Vue3D(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);  // reçoit le clavier (F, Échap) après un clic
    setMouseTracking(true);           // coordonnées GPS sous la souris, même sans clic
    setMinimumSize(200, 150);
}

Vue3D::~Vue3D()
{
    // Les tableaux de la carte graphique ne peuvent être libérés qu'avec le contexte
    // OpenGL actif (sinon fuite, ou plantage avec certains pilotes).
    makeCurrent();
    for (Forme *f : {&m_routes, &m_zoneForme, &m_batiments, &m_voitures, &m_arcs, &m_bulle, &m_disque}) {
        f->vao.destroy();
        f->sommets.destroy();
        f->indices.destroy();
        f->instances.destroy();
    }
    m_vaoVide.destroy();
    m_progCiel.reset();
    m_progRoutes.reset();
    m_progBatiments.reset();
    m_progVoitures.reset();
    m_progArcs.reset();
    m_progPortee.reset();
    m_progChoix.reset();
    m_imageChoix.reset();
    doneCurrent();
}

// ---------------------------------------------------------------------------
// Initialisation OpenGL : compilation des shaders et création des formes fixes.
// Appelée une fois, juste avant le premier affichage de la vue.
// ---------------------------------------------------------------------------
void Vue3D::initializeGL()
{
    initializeOpenGLFunctions();
    // Carte graphique utilisée (utile pour comprendre une vue 3D lente : carte intégrée ?).
    qInfo("Vue 3D : OpenGL %s sur %s", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
          reinterpret_cast<const char *>(glGetString(GL_RENDERER)));

    const auto compiler = [this](std::unique_ptr<QOpenGLShaderProgram> &prog, const char *vertex,
                                 const char *fragment, const char *nom) {
        prog = std::make_unique<QOpenGLShaderProgram>();
        if (prog->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
            && prog->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) && prog->link())
            return true;
        m_erreurGL = QString("Programme « %1 » : %2").arg(nom, prog->log());
        qWarning("Vue 3D : %s", qPrintable(m_erreurGL));
        return false;
    };
    m_pret = compiler(m_progCiel, Shaders3D::CIEL_VERTEX, Shaders3D::CIEL_FRAGMENT, "ciel")
             && compiler(m_progRoutes, Shaders3D::ROUTES_VERTEX, Shaders3D::ROUTES_FRAGMENT, "routes")
             && compiler(m_progBatiments, Shaders3D::BATIMENTS_VERTEX, Shaders3D::BATIMENTS_FRAGMENT, "bâtiments")
             && compiler(m_progVoitures, Shaders3D::VOITURES_VERTEX, Shaders3D::VOITURES_FRAGMENT, "voitures")
             && compiler(m_progArcs, Shaders3D::ARCS_VERTEX, Shaders3D::ARCS_FRAGMENT, "arcs")
             && compiler(m_progPortee, Shaders3D::PORTEE_VERTEX, Shaders3D::PORTEE_FRAGMENT, "portée")
             && compiler(m_progChoix, Shaders3D::VOITURES_VERTEX, Shaders3D::CHOIX_FRAGMENT, "choix");
    if (!m_pret)
        return;

    m_vaoVide.create();
    for (Forme *f : {&m_routes, &m_zoneForme, &m_batiments, &m_voitures, &m_arcs, &m_bulle, &m_disque}) {
        f->vao.create();
        f->sommets.create();
        f->indices.create();
        f->instances.create();
        f->instances.setUsagePattern(QOpenGLBuffer::StreamDraw);  // réécrit à chaque image
    }

    // Routes, zone : 5 nombres par sommet (voir ajouterRuban).
    for (Forme *f : {&m_routes, &m_zoneForme}) {
        f->vao.bind();
        f->sommets.bind();
        attribut(this, 0, 2, FLOATS_ROUTE, 0);
        attribut(this, 1, 2, FLOATS_ROUTE, 2);
        attribut(this, 2, 1, FLOATS_ROUTE, 4);
        f->indices.bind();  // le VAO retient aussi le tableau d'indices
    }
    // Bâtiments : x, y, z, teinte.
    m_batiments.vao.bind();
    m_batiments.sommets.bind();
    attribut(this, 0, 3, 4, 0);
    attribut(this, 1, 1, 4, 3);
    m_batiments.indices.bind();

    // Voiture : la forme (fixe) et une ligne par véhicule (x, z, cap, taille, r, g, b, inutilisé).
    QVector<float> s;
    QVector<quint32> indices;
    formeVoiture(s, indices);
    m_voitures.vao.bind();
    envoyer(m_voitures.sommets, s);
    attribut(this, 0, 3, FLOATS_VOITURE, 0);
    attribut(this, 1, 3, FLOATS_VOITURE, 3);
    attribut(this, 2, 1, FLOATS_VOITURE, 6);
    envoyer(m_voitures.indices, indices);
    m_voitures.nombre = indices.size();
    m_voitures.instances.bind();
    attribut(this, 3, 4, 8, 0, true);
    attribut(this, 4, 3, 8, 4, true);

    // Arc : le gabarit (t, côté) et une ligne par arc (12 nombres, voir preparerArcs).
    s.clear();
    formeArc(s);
    m_arcs.vao.bind();
    envoyer(m_arcs.sommets, s);
    attribut(this, 0, 1, 2, 0);
    attribut(this, 1, 1, 2, 1);
    m_arcs.instances.bind();
    attribut(this, 2, 4, 12, 0, true);
    attribut(this, 3, 4, 12, 4, true);
    attribut(this, 4, 4, 12, 8, true);

    // Bulle et disque de portée : 3 nombres par sommet.
    for (auto [forme, creer] : {std::pair{&m_bulle, &formeBulle}, std::pair{&m_disque, &formeDisque}}) {
        s.clear();
        indices.clear();
        creer(s, indices);
        forme->vao.bind();
        envoyer(forme->sommets, s);
        attribut(this, 0, 3, 3, 0);
        envoyer(forme->indices, indices);
        forme->nombre = indices.size();
    }
    glBindVertexArray(0);

    // Tout ce qui a pu arriver avant l'initialisation devra être envoyé.
    m_routesModifiees = m_zoneModifiee = m_batimentsModifies = m_vehiculesModifies = m_arcsModifies = true;
}

// ---------------------------------------------------------------------------
// Données reçues de MainWindow
// ---------------------------------------------------------------------------
void Vue3D::afficherCarte(const OsmCarte &carte)
{
    m_latRef = (carte.latMin + carte.latMax) / 2.0;
    m_lonRef = (carte.lonMin + carte.lonMax) / 2.0;
    m_limitesCarte = QRectF(carte.versMetres(carte.latMax, carte.lonMin), carte.versMetres(carte.latMin, carte.lonMax))
                         .normalized();

    // Une liste d'indices par type de route : on les dessinera des petites rues aux
    // autoroutes, pour que les grandes routes passent par-dessus aux carrefours.
    m_sommetsRoutes.clear();
    QVector<quint32> indicesParType[4];
    for (const OsmRoute &route : carte.routes) {
        QVector<QPointF> points;
        points.reserve(route.noeuds.size());
        for (qint64 id : route.noeuds) {
            const auto it = carte.noeuds.constFind(id);
            if (it != carte.noeuds.constEnd())
                points.append(carte.versMetres(it->lat, it->lon));
        }
        const int type = static_cast<int>(route.type);
        ajouterRuban(m_sommetsRoutes, indicesParType[type], points, float(type), false);
    }
    m_indicesRoutes.clear();
    for (int type = 3; type >= 0; --type) {
        m_debutType[type] = m_indicesRoutes.size();
        m_nombreType[type] = indicesParType[type].size();
        m_indicesRoutes += indicesParType[type];
    }
    m_routesModifiees = true;

    m_maillageBatiments = MaillageBatiments();  // les bâtiments de l'ancienne carte n'ont plus de sens
    m_batimentsModifies = true;
    setZone(QRectF());
    effacerVehicules();
    recentrer();
}

void Vue3D::afficherBatiments(MaillageBatiments maillage)
{
    m_maillageBatiments = std::move(maillage);
    m_batimentsModifies = true;
    update();
}

void Vue3D::setZone(const QRectF &zone)
{
    m_zone = zone;
    m_sommetsZone.clear();
    m_indicesZone.clear();
    if (!zone.isEmpty())
        ajouterRuban(m_sommetsZone, m_indicesZone,
                     {zone.topLeft(), zone.topRight(), zone.bottomRight(), zone.bottomLeft()}, 4.0f, true);
    m_zoneModifiee = true;
    update();
}

void Vue3D::afficherVehicules(const QVector<QPointF> &positions, const QVector<int> &couleurs,
                              const QVector<float> &caps, const QVector<float> &vitesses)
{
    // QVector partage ses données (copie à l'écriture) : ces affectations ne recopient rien.
    m_positions = positions;
    m_couleurs = couleurs;
    m_caps = caps;
    m_vitesses = vitesses;
    if (m_choisi >= m_positions.size())
        choisir(-1, false);  // le véhicule choisi n'existe plus (moins de véhicules)
    if (m_suivi && m_choisi >= 0) {
        const QPointF p = m_positions[m_choisi];
        m_cible = QVector3D(float(p.x()), 0.0f, float(p.y()));  // la caméra accompagne le véhicule
    }
    m_vehiculesModifies = m_arcsModifies = true;
    update();
}

void Vue3D::afficherAretes(const QVector<QPair<int, int>> &aretes)
{
    m_aretes = aretes;
    calculerVoisinsChoisi();
    m_vehiculesModifies = m_arcsModifies = true;
    update();
}

void Vue3D::setAretesVisibles(bool visibles)
{
    m_aretesVisibles = visibles;
    m_arcsModifies = true;
    update();
}

void Vue3D::effacerVehicules()
{
    m_positions.clear();
    m_couleurs.clear();
    m_caps.clear();
    m_vitesses.clear();
    m_aretes.clear();
    choisir(-1, false);
    m_vehiculesModifies = m_arcsModifies = true;
    update();
}

void Vue3D::setRayon(double rayon)
{
    m_rayon = rayon;
    update();
}

void Vue3D::setInvite(const QString &titre, const QString &detail)
{
    if (titre == m_inviteTitre && detail == m_inviteDetail)
        return;
    m_inviteTitre = titre;
    m_inviteDetail = detail;
    update();
}

// ---------------------------------------------------------------------------
// Caméra
// ---------------------------------------------------------------------------
void Vue3D::recentrer()
{
    const QRectF r = m_zone.isEmpty() ? m_limitesCarte : m_zone;
    if (r.isEmpty())
        return;
    // Vue cachée (carte 2D affichée) : sa taille, donc la forme de l'image, n'est peut-être
    // pas la bonne. Le cadrage calculé maintenant sera refait à son affichage (showEvent).
    if (!isVisible())
        m_recentrerAuProchainAffichage = true;

    // Toute la carte (ou la zone), vue de biais depuis le sud-sud-ouest : on devine le
    // relief des bâtiments sans perdre la vue d'ensemble.
    m_cible = QVector3D(float(r.center().x()), 0.0f, float(r.center().y()));
    m_lacet = -18.0f;
    m_tangage = 52.0f;
    m_suivi = false;

    // Distance la plus courte qui montre les 4 coins du rectangle (avec une petite marge),
    // trouvée par dichotomie : plus la caméra recule, plus les coins se rapprochent du
    // centre de l'image. Le point de départ « haut » montre toujours tout : à cette
    // distance, la sphère qui contient le rectangle tient dans le plus petit angle de vue.
    const auto coinsVisibles = [this, &r](float distance) {
        m_distance = distance;
        const QMatrix4x4 vp = vueProjection();
        for (const QPointF &coin : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()}) {
            const QVector4D c = vp * QVector4D(float(coin.x()), 0.0f, float(coin.y()), 1.0f);
            if (c.w() <= 0.0f || std::abs(c.x() / c.w()) > 0.95f || std::abs(c.y() / c.w()) > 0.95f)
                return false;
        }
        return true;
    };
    const double demiVertical = qDegreesToRadians(CHAMP_VISION_DEG / 2.0);
    const double largeurSurHauteur = width() > 0 && height() > 0 ? double(width()) / height() : 1.0;
    const double demiHorizontal = std::atan(std::tan(demiVertical) * largeurSurHauteur);
    const double rayon = std::hypot(r.width(), r.height()) / 2.0;
    float bas = DISTANCE_MIN;
    float haut = float(1.1 * rayon / std::sin(std::min(demiVertical, demiHorizontal)));
    for (int i = 0; i < 40 && haut - bas > 1.0f; ++i) {
        const float milieu = 0.5f * (bas + haut);
        (coinsVisibles(milieu) ? haut : bas) = milieu;
    }
    m_distance = std::max(haut, DISTANCE_MIN);
    update();
}

void Vue3D::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);  // à ce moment la vue a déjà sa taille définitive
    if (m_recentrerAuProchainAffichage) {
        m_recentrerAuProchainAffichage = false;
        recentrer();
    }
}

QVector3D Vue3D::oeil() const
{
    // Coordonnées sphériques autour du point visé : lacet = angle autour de la verticale,
    // tangage = hauteur au-dessus de l'horizon. Lacet 0 : la caméra est au sud, regard vers le nord.
    const float l = qDegreesToRadians(m_lacet), t = qDegreesToRadians(m_tangage);
    return m_cible + m_distance * QVector3D(std::sin(l) * std::cos(t), std::sin(t), std::cos(l) * std::cos(t));
}

QMatrix4x4 Vue3D::vueProjection() const
{
    // Plans proche et lointain : ce qui est plus près ou plus loin n'est pas dessiné. Leur
    // rapport fixe la précision du tampon de profondeur : on les adapte au zoom.
    const float proche = std::clamp(m_distance * 0.01f, 0.5f, 50.0f);
    const float diagonale = float(std::hypot(m_limitesCarte.width(), m_limitesCarte.height()));
    const float loin = m_distance * 4.0f + 2.0f * diagonale + 2000.0f;
    QMatrix4x4 projection;
    projection.perspective(CHAMP_VISION_DEG, float(width()) / float(std::max(height(), 1)), proche, loin);
    QMatrix4x4 vue;
    vue.lookAt(oeil(), m_cible, QVector3D(0.0f, 1.0f, 0.0f));
    return projection * vue;
}

float Vue3D::pixelMonde() const
{
    // Un objet à 1 m de la caméra : la hauteur de l'image couvre 2 tan(champ/2) mètres.
    const float pixels = float(height() * devicePixelRatioF());
    return 2.0f * std::tan(qDegreesToRadians(CHAMP_VISION_DEG / 2.0f)) / std::max(pixels, 1.0f);
}

bool Vue3D::pointAuSol(const QPointF &pixel, QVector3D &sol) const
{
    // Rayon partant de la caméra et passant par le pixel : on « dé-projette » deux points
    // du pixel (plan proche et plan lointain), puis on cherche où ce rayon touche y = 0.
    const QMatrix4x4 inverse = vueProjection().inverted();
    const float x = float(2.0 * pixel.x() / std::max(width(), 1) - 1.0);
    const float y = float(1.0 - 2.0 * pixel.y() / std::max(height(), 1));
    const QVector3D a = inverse.map(QVector3D(x, y, -1.0f));
    const QVector3D b = inverse.map(QVector3D(x, y, 1.0f));
    const QVector3D direction = b - a;
    if (direction.y() > -1e-6f)
        return false;  // regard vers le ciel
    sol = a + direction * (-a.y() / direction.y());
    sol.setY(0.0f);
    // Trop loin (presque à l'horizon) : un petit mouvement de souris ferait un bond énorme.
    return (sol - oeil()).length() < 30.0f * m_distance + 20000.0f;
}

QPointF Vue3D::versEcran(const QPointF &pointCarte) const
{
    const QVector3D p = vueProjection().map(QVector3D(float(pointCarte.x()), 0.0f, float(pointCarte.y())));
    return {(p.x() + 1.0) * 0.5 * width(), (1.0 - p.y()) * 0.5 * height()};
}

// ---------------------------------------------------------------------------
// Véhicule choisi
// ---------------------------------------------------------------------------
void Vue3D::choisir(int vehicule, bool suivre)
{
    m_choisi = vehicule;
    m_suivi = vehicule >= 0 && vehicule < m_positions.size() && suivre;
    if (m_suivi) {
        const QPointF p = m_positions[vehicule];
        m_cible = QVector3D(float(p.x()), 0.0f, float(p.y()));
        m_distance = std::min(m_distance, float(m_rayon * 3.0));  // assez près pour voir la bulle
    }
    calculerVoisinsChoisi();
    m_vehiculesModifies = m_arcsModifies = true;
    update();
}

void Vue3D::calculerVoisinsChoisi()
{
    m_estVoisin.fill(0, m_positions.size());
    m_nombreVoisins = 0;
    if (m_choisi < 0)
        return;
    for (const auto &[a, b] : m_aretes) {
        const int autre = a == m_choisi ? b : (b == m_choisi ? a : -1);
        if (autre >= 0 && autre < m_estVoisin.size() && !m_estVoisin[autre]) {
            m_estVoisin[autre] = 1;
            ++m_nombreVoisins;
        }
    }
}

int Vue3D::vehiculeSous(const QPointF &pixel)
{
    if (!m_pret || m_positions.isEmpty() || width() <= 0 || height() <= 0)
        return -1;
    // Image de choix (voir CHOIX_FRAGMENT) : même caméra, mêmes voitures que l'image
    // affichée, mais chaque voiture est peinte avec la couleur qui code son numéro. Les
    // bâtiments n'y écrivent que leur profondeur : une voiture cachée derrière un bâtiment
    // n'y apparaît pas, et de deux voitures qui se chevauchent, seule la plus proche reste.
    // L'image fait la taille de la vue en pixels réels ; dessinée seulement au clic.
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    const QSize taille(qRound(width() * dpr), qRound(height() * dpr));
    if (!m_imageChoix || m_imageChoix->size() != taille)
        m_imageChoix = std::make_unique<QOpenGLFramebufferObject>(taille, QOpenGLFramebufferObject::Depth);
    m_imageChoix->bind();
    glViewport(0, 0, taille.width(), taille.height());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);  // les couleurs doivent rester exactes : ce sont des numéros
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    envoyerDonnees();  // dernières positions, si l'image affichée n'est pas encore à jour
    const QMatrix4x4 vp = vueProjection();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);  // bâtiments : profondeur seulement
    dessinerBatiments(vp);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    dessinerVoitures(m_progChoix.get(), vp);

    // Carré de 14 pixels (d'interface) de rayon autour du clic : on garde la voiture dont un
    // pixel est le plus proche du clic (on peut viser un peu à côté d'une petite voiture).
    const int rayon = qRound(14 * dpr);
    const int cx = qRound(pixel.x() * dpr);
    const int cy = taille.height() - 1 - qRound(pixel.y() * dpr);  // OpenGL : ligne 0 en bas
    const int x0 = std::clamp(cx - rayon, 0, taille.width() - 1), x1 = std::clamp(cx + rayon, 0, taille.width() - 1);
    const int y0 = std::clamp(cy - rayon, 0, taille.height() - 1), y1 = std::clamp(cy + rayon, 0, taille.height() - 1);
    const int largeur = x1 - x0 + 1, hauteur = y1 - y0 + 1;
    QVector<uchar> pixels(largeur * hauteur * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x0, y0, largeur, hauteur, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    m_imageChoix->release();
    glEnable(GL_DITHER);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    doneCurrent();

    int meilleur = -1;
    int meilleureDistance = rayon * rayon + 1;
    for (int y = 0; y < hauteur; ++y)
        for (int x = 0; x < largeur; ++x) {
            const uchar *p = &pixels[(y * largeur + x) * 4];
            const int numero = p[0] | (p[1] << 8) | (p[2] << 16);  // 0 = pas de voiture
            const int dx = x0 + x - cx, dy = y0 + y - cy;
            if (numero > 0 && dx * dx + dy * dy < meilleureDistance) {
                meilleureDistance = dx * dx + dy * dy;
                meilleur = numero - 1;
            }
        }
    return meilleur < m_positions.size() ? meilleur : -1;
}

// ---------------------------------------------------------------------------
// Préparation des tableaux envoyés à chaque image
// ---------------------------------------------------------------------------
void Vue3D::preparerVehicules()
{
    // 8 nombres par véhicule : x, z, cap, facteur de taille minimale, r, g, b, (inutilisé).
    // Quand un véhicule est choisi, les autres sont estompés sauf ses voisins : on voit
    // d'un coup d'œil avec qui il communique (et que leurs canaux sont bien différents).
    const QVector3D sol = versVec3(COULEUR_SOL);
    QVector<float> lignes;
    lignes.reserve(m_positions.size() * 8);
    for (int i = 0; i < m_positions.size(); ++i) {
        QVector3D couleur = couleurVehicule(i < m_couleurs.size() ? m_couleurs[i] : -1);
        float taille = 1.0f;
        if (m_choisi >= 0) {
            if (i == m_choisi)
                taille = 2.2f;  // vu de loin, bien visible au centre de sa bulle
            else if (!m_estVoisin.value(i))
                couleur = couleur * 0.3f + sol * 0.7f;
        }
        lignes << float(m_positions[i].x()) << float(m_positions[i].y()) << m_caps.value(i, 0.0f) << taille
               << couleur.x() << couleur.y() << couleur.z() << 0.0f;
    }
    envoyer(m_voitures.instances, lignes);
    m_voitures.instances.release();
}

void Vue3D::preparerArcs()
{
    // 12 nombres par arc : xA, zA, xB, zB, couleur A + opacité, couleur B + épaisseur (pixels).
    // Arêtes ordinaires : fines et discrètes. Arêtes du véhicule choisi : épaisses, en
    // dégradé de la couleur de son canal vers celle du voisin.
    if (!m_aretesVisibles && m_choisi < 0) {
        m_arcs.nombre = 0;  // rien à dessiner : inutile de parcourir les arêtes
        return;
    }
    // Épaisseurs en pixels d'interface : le shader les convertit en pixels réels (uEchelleEcran),
    // elles restent donc justes si la fenêtre passe sur un écran agrandi à 125 % ou 200 %.
    const QVector3D gris = versVec3(COULEUR_ARETE);
    QVector<float> lignes;
    if (m_aretesVisibles)
        lignes.reserve(std::min<qsizetype>(m_aretes.size(), ARETES_MAX) * 12 + 12 * 64);  // une seule allocation
    QVector<float> lignesChoisi;  // dessinées en dernier, donc par-dessus
    int ordinaires = 0;
    for (const auto &[a, b] : m_aretes) {
        if (a >= m_positions.size() || b >= m_positions.size())
            continue;  // véhicules d'une ancienne simulation
        const QPointF pa = m_positions[a], pb = m_positions[b];
        if (m_choisi >= 0 && (a == m_choisi || b == m_choisi)) {
            const QVector3D ca = couleurVehicule(m_couleurs.value(a, -1)), cb = couleurVehicule(m_couleurs.value(b, -1));
            lignesChoisi << float(pa.x()) << float(pa.y()) << float(pb.x()) << float(pb.y()) << ca.x() << ca.y()
                         << ca.z() << 0.95f << cb.x() << cb.y() << cb.z() << 3.2f;
        } else if (m_aretesVisibles && ordinaires < ARETES_MAX) {
            ++ordinaires;
            lignes << float(pa.x()) << float(pa.y()) << float(pb.x()) << float(pb.y()) << gris.x() << gris.y()
                   << gris.z() << (m_choisi >= 0 ? 0.10f : 0.30f) << gris.x() << gris.y() << gris.z() << 1.4f;
        }
    }
    lignes += lignesChoisi;
    m_arcs.nombre = lignes.size() / 12;
    envoyer(m_arcs.instances, lignes);
    m_arcs.instances.release();
}

void Vue3D::envoyerDonnees()
{
    if (m_routesModifiees) {
        m_routes.vao.bind();
        envoyer(m_routes.sommets, m_sommetsRoutes);
        envoyer(m_routes.indices, m_indicesRoutes);
        m_routesModifiees = false;
    }
    if (m_zoneModifiee) {
        m_zoneForme.vao.bind();
        envoyer(m_zoneForme.sommets, m_sommetsZone);
        envoyer(m_zoneForme.indices, m_indicesZone);
        m_zoneForme.nombre = m_indicesZone.size();
        m_zoneModifiee = false;
    }
    if (m_batimentsModifies) {
        m_batiments.vao.bind();
        envoyer(m_batiments.sommets, m_maillageBatiments.sommets);
        envoyer(m_batiments.indices, m_maillageBatiments.indices);
        m_batiments.nombre = m_maillageBatiments.indices.size();
        m_batimentsModifies = false;
    }
    glBindVertexArray(0);  // les tableaux d'instances ne font pas partie d'un VAO lié
    if (m_vehiculesModifies) {
        preparerVehicules();
        m_vehiculesModifies = false;
    }
    if (m_arcsModifies) {
        preparerArcs();
        m_arcsModifies = false;
    }
}

// ---------------------------------------------------------------------------
// Dessin d'une image
// ---------------------------------------------------------------------------
void Vue3D::reglagesCommuns(QOpenGLShaderProgram *programme, const QMatrix4x4 &vp)
{
    programme->bind();
    programme->setUniformValue("uVueProj", vp);
    programme->setUniformValue("uOeil", oeil());
    programme->setUniformValue("uHorizon", versVec3(COULEUR_HORIZON));
    // Brouillard d'autant plus lointain que la caméra est loin : la vue d'ensemble reste nette.
    programme->setUniformValue("uBrouillard", m_distance * 3.0f + 2500.0f);
}

void Vue3D::dessinerBatiments(const QMatrix4x4 &vp)
{
    if (m_batiments.nombre == 0)
        return;
    reglagesCommuns(m_progBatiments.get(), vp);
    m_progBatiments->setUniformValue("uLumiere", LUMIERE);
    m_progBatiments->setUniformValue("uCouleurMur", versVec3(COULEUR_MUR));
    m_progBatiments->setUniformValue("uCouleurToit", versVec3(COULEUR_TOIT));
    // Maquette coupée, de près seulement (vue rasante dans les rues) : de loin, la caméra
    // est bien au-dessus des toits et rien n'est effacé.
    m_progBatiments->setUniformValue("uCible", m_cible);
    m_progBatiments->setUniformValue("uCoupe", m_distance < 800.0f ? 0.2f * m_distance + 8.0f : 0.0f);
    m_batiments.vao.bind();
    glDrawElements(GL_TRIANGLES, m_batiments.nombre, GL_UNSIGNED_INT, nullptr);
}

void Vue3D::dessinerVoitures(QOpenGLShaderProgram *programme, const QMatrix4x4 &vp)
{
    if (m_positions.isEmpty())
        return;
    reglagesCommuns(programme, vp);
    programme->setUniformValue("uLumiere", LUMIERE);
    programme->setUniformValue("uPixelMonde", pixelMonde());
    programme->setUniformValue("uLongueurMinPixels", LONGUEUR_VOITURE_MIN_PX * float(devicePixelRatioF()));
    m_voitures.vao.bind();
    glDrawElementsInstanced(GL_TRIANGLES, m_voitures.nombre, GL_UNSIGNED_INT, nullptr, m_positions.size());
}

void Vue3D::melangeTransparent()
{
    // Couleur : moyenne pondérée par l'opacité (mélange classique). Opacité de l'image :
    // inchangée (reste 1). Sinon l'image elle-même deviendrait transparente là où on
    // dessine les arcs, et paraîtrait délavée une fois enregistrée ou affichée.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
}

void Vue3D::paintGL()
{
    // QPainter (textes de l'image précédente) a pu changer l'état d'OpenGL : on le remet.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);  // sinon glClear n'efface pas la profondeur
    glClearColor(float(COULEUR_HORIZON.redF()), float(COULEUR_HORIZON.greenF()), float(COULEUR_HORIZON.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_pret) {
        dessinerTextes();
        return;
    }
    envoyerDonnees();

    const QMatrix4x4 vp = vueProjection();
    const QVector3D oeil = this->oeil();
    const float pixel = pixelMonde();
    const float dpr = float(devicePixelRatioF());

    // 1. Ciel et sol.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    reglagesCommuns(m_progCiel.get(), vp);
    m_progCiel->setUniformValue("uInverseVueProj", vp.inverted());
    m_progCiel->setUniformValue("uZenith", versVec3(COULEUR_ZENITH));
    m_progCiel->setUniformValue("uSol", versVec3(COULEUR_SOL));
    m_progCiel->setUniformValue("uGrille", versVec3(COULEUR_GRILLE));
    m_vaoVide.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 2. Routes (des petites rues aux autoroutes) puis contour de la zone.
    reglagesCommuns(m_progRoutes.get(), vp);
    m_progRoutes->setUniformValue("uPixelMonde", pixel);
    float largeursMin[5];
    QVector3D couleurs[5];
    for (int t = 0; t < 5; ++t) {
        largeursMin[t] = LARGEUR_MIN_ROUTE_PX[t] * dpr;
        couleurs[t] = t < 4 ? versVec3(MapView::couleurRoute(static_cast<TypeRoute>(t))) : versVec3(COULEUR_ZONE);
    }
    m_progRoutes->setUniformValueArray("uDemiLargeur", DEMI_LARGEUR_ROUTE, 5, 1);
    m_progRoutes->setUniformValueArray("uLargeurMinPixels", largeursMin, 5, 1);
    m_progRoutes->setUniformValueArray("uCouleur", couleurs, 5);
    m_routes.vao.bind();
    for (int type = 3; type >= 0; --type)
        if (m_nombreType[type] > 0)
            glDrawElements(GL_TRIANGLES, m_nombreType[type], GL_UNSIGNED_INT,
                           reinterpret_cast<const void *>(qintptr(m_debutType[type]) * qintptr(sizeof(quint32))));
    if (m_zoneForme.nombre > 0) {
        m_zoneForme.vao.bind();
        glDrawElements(GL_TRIANGLES, m_zoneForme.nombre, GL_UNSIGNED_INT, nullptr);
    }

    // Disque de portée du véhicule choisi (transparent, posé au sol).
    const bool choix = m_choisi >= 0 && m_choisi < m_positions.size();
    QVector3D centreChoisi, couleurChoisi;
    if (choix) {
        centreChoisi = QVector3D(float(m_positions[m_choisi].x()), 0.0f, float(m_positions[m_choisi].y()));
        couleurChoisi = couleurVehicule(m_couleurs.value(m_choisi, -1));
        glEnable(GL_BLEND);
        melangeTransparent();
        m_progPortee->bind();
        m_progPortee->setUniformValue("uVueProj", vp);
        m_progPortee->setUniformValue("uOeil", oeil);
        m_progPortee->setUniformValue("uCentre", centreChoisi);
        m_progPortee->setUniformValue("uRayon", float(m_rayon));
        m_progPortee->setUniformValue("uCouleur", couleurChoisi);
        m_progPortee->setUniformValue("uDisque", 1);
        m_disque.vao.bind();
        glDrawElements(GL_TRIANGLES, m_disque.nombre, GL_UNSIGNED_INT, nullptr);
        glDisable(GL_BLEND);
    }

    // 3. Objets en relief : le tampon de profondeur décide qui cache qui.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    dessinerBatiments(vp);
    dessinerVoitures(m_progVoitures.get(), vp);

    // 4. Transparents : ils sont cachés par les bâtiments mais n'écrivent pas de
    //    profondeur (deux arcs qui se croisent restent visibles tous les deux).
    glEnable(GL_BLEND);
    melangeTransparent();
    glDepthMask(GL_FALSE);
    if (m_arcs.nombre > 0) {
        reglagesCommuns(m_progArcs.get(), vp);
        m_progArcs->setUniformValue("uEcran", QVector2D(float(width()) * dpr, float(height()) * dpr));
        m_progArcs->setUniformValue("uEchelleEcran", dpr);
        m_arcs.vao.bind();
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 2 * (MORCEAUX_ARC + 1), m_arcs.nombre);
    }
    if (choix) {
        m_progPortee->bind();
        m_progPortee->setUniformValue("uDisque", 0);
        m_bulle.vao.bind();
        glDrawElements(GL_TRIANGLES, m_bulle.nombre, GL_UNSIGNED_INT, nullptr);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);

    // 5. Textes par-dessus l'image 3D.
    dessinerTextes();
}

void Vue3D::dessinerTextes()
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QLocale fr;

    // Cartouche blanc arrondi, comme l'échelle de la carte 2D.
    const auto cartouche = [&p](const QRectF &r) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 225));
        p.drawRoundedRect(r, 8.0, 8.0);
    };

    if (!m_pret) {
        p.setPen(QColor(0x37, 0x41, 0x51));
        p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                   "La vue 3D a besoin d'OpenGL 3.3, non disponible sur cet ordinateur.\n"
                   "Mettez à jour le pilote de la carte graphique, ou utilisez la carte 2D (touche 2).\n\n"
                   + m_erreurGL);
        return;
    }
    if (m_limitesCarte.isEmpty()) {
        p.setPen(QColor(0x37, 0x41, 0x51));
        p.drawText(rect(), Qt::AlignCenter, "Aucune carte chargée — Fichier > Ouvrir une carte… (Ctrl+O)");
        return;
    }

    // Aide en bas à gauche ; dans une vue étroite, elle passe à la ligne au lieu d'être coupée.
    const QString aide = "Glisser : déplacer  ·  Clic droit + glisser : tourner  ·  Molette : zoom  ·  "
                         "Clic sur un véhicule : ses voisins";
    const QFontMetricsF mesure(font());
    const int drapeauxAide = Qt::AlignCenter | Qt::TextWordWrap;
    const double largeurAide = std::max(40.0, std::min(mesure.horizontalAdvance(aide) + 1.0, width() - 48.0));
    const double hauteurAide = mesure.boundingRect(QRectF(0, 0, largeurAide, 1e6), drapeauxAide, aide).height();
    const QRectF zoneAide(12, height() - 12 - hauteurAide - 12, largeurAide + 24, hauteurAide + 12);
    cartouche(zoneAide);
    p.setPen(QColor(0x37, 0x41, 0x51));
    p.drawText(zoneAide.adjusted(12, 6, -12, -6), drapeauxAide, aide);

    // Message d'accueil (simulation pas encore démarrée), même dessin que la carte 2D.
    if (!m_inviteTitre.isEmpty())
        MapView::dessinerInvite(p, rect(), m_inviteTitre, m_inviteDetail, font());

    // Boussole en haut à droite : la flèche rouge montre le nord à l'écran.
    const QPointF centre(width() - 44.0, 44.0);
    const QPointF ici = versEcran(QPointF(m_cible.x(), m_cible.z()));
    const QPointF nord = versEcran(QPointF(m_cible.x(), m_cible.z() - m_distance * 0.1));
    QPointF direction = nord - ici;
    const double longueur = std::hypot(direction.x(), direction.y());
    direction = longueur > 1e-6 ? direction / longueur : QPointF(0, -1);
    const QPointF cote(-direction.y(), direction.x());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 225));
    p.drawEllipse(centre, 30, 30);
    QPainterPath fleche;
    fleche.moveTo(centre + direction * 22);
    fleche.lineTo(centre + cote * 7);
    fleche.lineTo(centre - cote * 7);
    fleche.closeSubpath();
    p.setBrush(QColor(0xDC, 0x26, 0x26));
    p.drawPath(fleche);
    QPainterPath queue;
    queue.moveTo(centre - direction * 22);
    queue.lineTo(centre + cote * 7);
    queue.lineTo(centre - cote * 7);
    queue.closeSubpath();
    p.setBrush(QColor(0x94, 0xA3, 0xB8));
    p.drawPath(queue);
    QFont gras = font();
    gras.setBold(true);
    p.setFont(gras);
    p.setPen(QColor(0x37, 0x41, 0x51));
    p.drawText(QRectF(centre + direction * 40 - QPointF(10, 10), QSizeF(20, 20)), Qt::AlignCenter, "N");

    // Fiche du véhicule choisi en haut à gauche.
    if (m_choisi >= 0 && m_choisi < m_positions.size()) {
        const int canal = m_couleurs.value(m_choisi, -1);
        const QStringList lignes = {
            QString("Véhicule n° %1").arg(fr.toString(m_choisi)),
            canal >= 0 ? QString("Canal radio %1").arg(canal + 1) : QString("Pas encore coloré"),
            QString("%1 voisin%2 à moins de %3 m").arg(fr.toString(m_nombreVoisins), m_nombreVoisins > 1 ? "s" : "",
                                                       fr.toString(m_rayon, 'f', 0)),
            QString("%1 km/h").arg(fr.toString(m_vitesses.value(m_choisi, 0.0f) * 3.6, 'f', 0)),
            m_suivi ? QString("Caméra : suit le véhicule (Échap)") : QString("Double-clic ou F : le suivre"),
        };
        const double hauteurLigne = mesure.height() + 4;
        double largeur = 0;
        for (const QString &l : lignes)
            largeur = std::max(largeur, mesure.horizontalAdvance(l));
        const QRectF fiche(12, 12, largeur + 52, hauteurLigne * lignes.size() + 18);
        cartouche(fiche);
        const QColor couleur = MapView::couleurCanal(canal);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(couleur);
        p.drawEllipse(QPointF(fiche.left() + 20, fiche.top() + 9 + hauteurLigne * 1.5), 7, 7);
        for (int i = 0; i < lignes.size(); ++i) {
            p.setFont(i == 0 ? gras : font());
            p.setPen(i == 4 ? QColor(0x6B, 0x72, 0x80) : QColor(0x1F, 0x29, 0x37));
            p.drawText(QRectF(fiche.left() + 36, fiche.top() + 9 + hauteurLigne * i, largeur + 10, hauteurLigne),
                       Qt::AlignVCenter | Qt::AlignLeft, lignes[i]);
        }
    }
}

// ---------------------------------------------------------------------------
// Souris et clavier
// ---------------------------------------------------------------------------
void Vue3D::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    m_posAppui = m_posPrecedente = event->position();
    m_aGlisse = false;
    m_apresDoubleClic = false;
}

void Vue3D::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    QVector3D sol;
    if (!m_limitesCarte.isEmpty() && pointAuSol(pos, sol)) {
        // Conversion inverse de OsmCarte::versMetres : mètres -> latitude / longitude.
        const double lat = m_latRef - qRadiansToDegrees(sol.z() / RAYON_TERRE_M);
        const double lon = m_lonRef + qRadiansToDegrees(sol.x() / (RAYON_TERRE_M * qCos(qDegreesToRadians(m_latRef))));
        emit positionSouris(lat, lon);
    }
    if (event->buttons() == Qt::NoButton)
        return;
    // Tant que la souris reste à moins de 4 pixels du point d'appui, c'est peut-être un
    // clic qui tremble un peu : la caméra ne bouge pas (et ne quitte pas le suivi). Au-delà,
    // c'est un glisser, et le mouvement part du point d'appui (rien n'est perdu).
    if (!m_aGlisse) {
        if ((pos - m_posAppui).manhattanLength() <= 4)
            return;
        m_aGlisse = true;  // au relâchement, ce ne sera pas un clic
        m_posPrecedente = m_posAppui;
    }

    const QPointF delta = pos - m_posPrecedente;
    const bool tourner = (event->buttons() & (Qt::RightButton | Qt::MiddleButton))
                         || ((event->buttons() & Qt::LeftButton) && (event->modifiers() & Qt::ControlModifier));
    if (tourner) {
        m_lacet -= float(delta.x()) * 0.3f;
        m_tangage = std::clamp(m_tangage + float(delta.y()) * 0.25f, TANGAGE_MIN, TANGAGE_MAX);
    } else if (event->buttons() & Qt::LeftButton) {
        // Déplacement : le point du sol saisi reste sous la souris.
        QVector3D avant, apres;
        if (pointAuSol(m_posPrecedente, avant) && pointAuSol(pos, apres)) {
            m_cible += avant - apres;
            m_suivi = false;  // l'utilisateur reprend la main sur la caméra
        }
    }
    m_posPrecedente = pos;
    update();
}

void Vue3D::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_aGlisse || m_positions.isEmpty())
        return;
    // Relâchement qui termine un double-clic : la caméra vient de se recentrer sur le
    // véhicule, la souris montre maintenant autre chose. On ne change rien.
    if (m_apresDoubleClic) {
        m_apresDoubleClic = false;
        return;
    }
    // Clic sur un autre véhicule : on le choisit ; clic dans le vide : plus de choix.
    // Clic sur le véhicule déjà choisi : rien ne change (le suivi éventuel continue).
    const int vehicule = vehiculeSous(event->position());
    if (vehicule != m_choisi)
        choisir(vehicule, false);
}

void Vue3D::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_apresDoubleClic = true;
    const int vehicule = vehiculeSous(event->position());
    if (vehicule >= 0)
        choisir(vehicule, true);
}

void Vue3D::wheelEvent(QWheelEvent *event)
{
    const double crans = event->angleDelta().y() / 120.0;
    if (crans == 0.0 || m_limitesCarte.isEmpty())
        return;
    // Zoom vers la souris : toute la caméra (point visé et œil) se rapproche du point du sol
    // sous la souris, dans la même proportion que la distance ; ce point reste donc exactement
    // sous la souris. Il faut le mesurer AVANT de changer la distance. Pas de décalage quand
    // la caméra suit un véhicule (il reste au centre).
    QVector3D sol;
    const bool versSouris = !m_suivi && pointAuSol(event->position(), sol);
    const float diagonale = float(std::hypot(m_limitesCarte.width(), m_limitesCarte.height()));
    const float avant = m_distance;
    const float distanceMax = std::max(2.5f * diagonale, 2.0f * DISTANCE_MIN);  // on ne perd pas la carte de vue
    m_distance = std::clamp(float(m_distance * std::pow(0.85, crans)), DISTANCE_MIN, distanceMax);
    if (versSouris)
        m_cible += (sol - m_cible) * (1.0f - m_distance / avant);
    update();
    event->accept();
}

void Vue3D::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_choisi >= 0) {
        choisir(-1, false);
        return;
    }
    if (event->key() == Qt::Key_F && m_choisi >= 0) {
        choisir(m_choisi, !m_suivi);
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}
