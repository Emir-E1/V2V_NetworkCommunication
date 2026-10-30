#pragma once

/// @file Vue3D.h
/// @brief Vue 3D de la ville et de la simulation (version 3D du projet).
///
/// Même contenu que la carte 2D (MapView), vu en perspective et dessiné par la carte
/// graphique (OpenGL) :
///   - le sol, les routes (rubans plats, largeur selon le type) et la zone de simulation ;
///   - les bâtiments d'OpenStreetMap en volume (voir Batiments.h) ;
///   - chaque véhicule en petite voiture 3D, de la couleur de son canal radio et tournée
///     dans son sens de marche ;
///   - les arêtes du graphe d'interférences, en arcs au-dessus de la ville ;
///   - le véhicule choisi à la souris : sa portée radio (bulle + disque au sol), ses liens
///     vers ses voisins, une fiche d'informations, et une caméra qui peut le suivre.
///
/// La vue ne calcule rien de la simulation : elle reçoit les mêmes données que la carte
/// 2D (positions, canaux, arêtes), transmises par MainWindow.
///
/// Pourquoi OpenGL et pas QGraphicsView : 74 000 bâtiments (environ un million de
/// triangles) et des milliers de voitures en perspective, 25 fois par seconde, ne sont
/// possibles qu'avec la carte graphique. Toutes les voitures sont dessinées en UN seul
/// appel (« instanciation ») : la forme d'une voiture est envoyée une fois, puis à chaque
/// image seulement la position, la direction et la couleur de chaque véhicule.
///
/// Souris : glisser = déplacer ; clic droit (ou Ctrl + clic) glisser = tourner autour du
/// centre ; molette = zoom vers la souris ; clic sur un véhicule = le choisir ;
/// double-clic (ou F) = le suivre ; Échap = annuler le choix.

#include "map/OsmData.h"
#include "vue3d/Batiments.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QVector3D>
#include <QVector>

#include <memory>

class Vue3D : public QOpenGLWidget, protected QOpenGLExtraFunctions
{
    Q_OBJECT

public:
    explicit Vue3D(QWidget *parent = nullptr);
    ~Vue3D() override;  // libère les tableaux stockés sur la carte graphique

    // ---- Données affichées (mêmes repères et unités que MapView : mètres) ----

    /// Nouvelle carte : routes, cadrage de la caméra. Oublie bâtiments, zone et véhicules.
    void afficherCarte(const OsmCarte &carte);
    /// Volumes des bâtiments (voir Batiments::construireMaillage).
    void afficherBatiments(MaillageBatiments maillage);
    /// Zone de simulation (rectangle vide = pas de zone) : dessinée en contour bleu au sol.
    void setZone(const QRectF &zone);
    /// Appelé ~25 fois par seconde : position, canal (-1 = pas encore coloré), direction
    /// (radians, atan2(dy, dx) dans le repère de la carte) et vitesse (m/s) de chaque véhicule.
    void afficherVehicules(const QVector<QPointF> &positions, const QVector<int> &couleurs,
                           const QVector<float> &caps, const QVector<float> &vitesses);
    /// Arêtes du graphe d'interférences (paires d'indices de véhicules).
    void afficherAretes(const QVector<QPair<int, int>> &aretes);
    void setAretesVisibles(bool visibles);
    /// Retire tous les véhicules (nouvelle simulation, graphe vide...) et le véhicule choisi.
    void effacerVehicules();
    /// Portée radio R (mètres) : rayon de la bulle autour du véhicule choisi.
    void setRayon(double rayon);
    /// Caméra en vue d'ensemble de la carte (ou de la zone de simulation si elle existe).
    void recentrer();
    /// Message d'accueil au centre de la vue (« Appuyez sur Espace... ») ; titre vide = aucun.
    void setInvite(const QString &titre, const QString &detail);
    const QString &invite() const { return m_inviteTitre; }

    /// Véhicule choisi (-1 = aucun) et suivi par la caméra (utile aux tests).
    int vehiculeChoisi() const { return m_choisi; }
    bool suiviActif() const { return m_suivi; }
    /// Positions (mètres) des véhicules affichés.
    const QVector<QPointF> &positions() const { return m_positions; }
    /// Position à l'écran (pixels du widget) d'un point au sol, pour les tests.
    QPointF versEcran(const QPointF &pointCarte) const;

signals:
    /// Position GPS du point du sol sous la souris (barre d'état, comme la carte 2D).
    void positionSouris(double lat, double lon);

protected:
    void initializeGL() override;
    void paintGL() override;
    void showEvent(QShowEvent *event) override;  // recadrage demandé pendant que la vue était cachée
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /// Un objet dessinable : ses tableaux de nombres rangés sur la carte graphique.
    struct Forme
    {
        QOpenGLVertexArrayObject vao;  ///< mémorise comment lire les tableaux ci-dessous
        QOpenGLBuffer sommets{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer indices{QOpenGLBuffer::IndexBuffer};
        QOpenGLBuffer instances{QOpenGLBuffer::VertexBuffer};  ///< une ligne par véhicule / par arc
        int nombre = 0;                ///< indices (ou sommets, ou instances) à dessiner
    };

    // ---- Caméra : elle tourne autour d'un point visé au sol (m_cible) ----
    QVector3D oeil() const;
    QMatrix4x4 vueProjection() const;
    /// Point du sol (y = 0) sous un pixel du widget ; false si le regard passe au-dessus de l'horizon.
    bool pointAuSol(const QPointF &pixel, QVector3D &sol) const;
    /// Mètres couverts par un pixel de l'image, pour un objet à 1 m de la caméra.
    float pixelMonde() const;

    // ---- Préparation des données pour la carte graphique ----
    void envoyerDonnees();          ///< copie sur la carte graphique ce qui a changé
    void preparerVehicules();       ///< une ligne par véhicule : position, cap, taille, couleur
    void preparerArcs();            ///< une ligne par arc : extrémités, couleurs, épaisseur
    void calculerVoisinsChoisi();   ///< voisins du véhicule choisi, d'après les arêtes
    void choisir(int vehicule, bool suivre);
    /// Véhicule visible sous la souris (à quelques pixels près), ou -1. Dessine pour cela
    /// une image de choix jamais affichée (voir CHOIX_FRAGMENT dans Shaders3D.h).
    int vehiculeSous(const QPointF &pixel);
    void dessinerTextes();          ///< fiche du véhicule, aide, boussole (QPainter)
    void melangeTransparent();      ///< réglage OpenGL du dessin des objets transparents
    /// Active un programme et lui donne les réglages communs (matrice, caméra, brouillard).
    void reglagesCommuns(QOpenGLShaderProgram *programme, const QMatrix4x4 &vp);
    void dessinerBatiments(const QMatrix4x4 &vp);
    /// Toutes les voitures en un appel, avec le programme d'affichage ou celui de l'image de choix.
    void dessinerVoitures(QOpenGLShaderProgram *programme, const QMatrix4x4 &vp);

    // ---- Programmes (shaders) et formes ----
    bool m_pret = false;  ///< OpenGL initialisé sans erreur (sinon un message remplace la 3D)
    QString m_erreurGL;
    std::unique_ptr<QOpenGLShaderProgram> m_progCiel, m_progRoutes, m_progBatiments, m_progVoitures, m_progArcs,
        m_progPortee, m_progChoix;
    std::unique_ptr<QOpenGLFramebufferObject> m_imageChoix;  ///< image de choix (créée au premier clic)
    QOpenGLVertexArrayObject m_vaoVide;  ///< le ciel n'a pas de tableau, mais OpenGL exige un VAO
    Forme m_routes, m_zoneForme, m_batiments, m_voitures, m_arcs, m_bulle, m_disque;
    int m_debutType[4] = {0, 0, 0, 0};   ///< premier indice des routes de chaque type
    int m_nombreType[4] = {0, 0, 0, 0};  ///< nombre d'indices des routes de chaque type

    // ---- Données reçues (gardées ici pour être renvoyées si besoin) ----
    QVector<float> m_sommetsRoutes;      ///< format : voir ajouterRuban() dans Vue3D.cpp
    QVector<quint32> m_indicesRoutes;
    QVector<float> m_sommetsZone;
    QVector<quint32> m_indicesZone;
    MaillageBatiments m_maillageBatiments;
    QVector<QPointF> m_positions;
    QVector<int> m_couleurs;
    QVector<float> m_caps;
    QVector<float> m_vitesses;
    QVector<QPair<int, int>> m_aretes;
    bool m_aretesVisibles = false;
    double m_rayon = 300.0;
    QString m_inviteTitre, m_inviteDetail;  ///< message d'accueil (voir setInvite)
    QRectF m_limitesCarte;  ///< rectangle de la carte, en mètres
    QRectF m_zone;          ///< zone de simulation (vide = toute la carte)
    double m_latRef = 0.0, m_lonRef = 0.0;  ///< centre de la projection, pour mètres -> GPS

    // Ce qui doit être renvoyé à la carte graphique à la prochaine image.
    bool m_routesModifiees = false, m_zoneModifiee = false, m_batimentsModifies = false;
    bool m_vehiculesModifies = false, m_arcsModifies = false;

    // ---- Véhicule choisi ----
    int m_choisi = -1;
    bool m_suivi = false;
    QVector<char> m_estVoisin;   ///< m_estVoisin[i] = 1 si i est voisin du véhicule choisi
    int m_nombreVoisins = 0;

    // ---- Caméra et souris ----
    QVector3D m_cible;           ///< point visé au sol
    float m_lacet = 0.0f;        ///< rotation autour de la verticale (degrés, 0 = regard vers le nord)
    float m_tangage = 55.0f;     ///< hauteur du regard (degrés, 90 = vue de dessus)
    float m_distance = 3000.0f;  ///< distance caméra -> point visé (mètres)
    bool m_recentrerAuProchainAffichage = false;  ///< recentrer() demandé pendant que la vue était cachée
    QPointF m_posAppui, m_posPrecedente;
    bool m_aGlisse = false;          ///< la souris a bougé pendant l'appui : ce n'est pas un clic
    bool m_apresDoubleClic = false;  ///< le prochain relâchement termine un double-clic
};
