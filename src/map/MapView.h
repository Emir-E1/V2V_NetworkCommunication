#pragma once

// Affichage de la carte — étape 1 du projet.
//
// MapView est la "caméra" posée sur la scène Qt qui contient les routes.
// Elle gère :
//   - le dessin des routes (couleur et épaisseur selon leur importance)
//   - le zoom à la molette (centré sur la souris) et le déplacement au clic-glisser
//   - la sélection de la zone de simulation (rectangle tracé à la souris)
//   - l'échelle graphique en bas à gauche (ex. "500 m")
// Toutes les positions de la scène sont en MÈTRES (voir OsmCarte::versMetres),
// ce qui permettra de placer véhicules et rayons radio directement (étapes 3 et 4).

#include "map/OsmData.h"

#include <QColor>
#include <QGraphicsView>
#include <QRectF>

class QGraphicsPathItem;

class MapView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit MapView(QWidget *parent = nullptr);

    // Efface la scène puis dessine toutes les routes de "carte".
    void afficherCarte(const OsmCarte &carte);

    // Zoome pour que toute la carte tienne dans la fenêtre.
    void recentrer();

    // Active / désactive le mode "tracer la zone de simulation" :
    // le clic-glisser dessine alors un rectangle au lieu de déplacer la carte.
    void setModeSelectionZone(bool actif);

    // Supprime la zone de simulation choisie (retour à "toute la carte").
    void effacerZone();

    // Zone choisie, en mètres dans le repère de la scène. Rectangle vide = pas de zone.
    QRectF zone() const { return m_zone; }

    // Style des routes, partagé avec la légende du panneau de contrôle.
    static QColor couleurRoute(TypeRoute type);
    static double epaisseurRoute(TypeRoute type);  // en pixels écran

    // ---- Étape 2 : test du plus court chemin et affichage du graphe routier ----

    // Mode "itinéraire" : un simple clic (sans glisser) sur la carte émet clicCarte().
    // Le clic-glisser continue de déplacer la carte.
    void setModeItineraire(bool actif);
    // Dessine un itinéraire (suite de points en mètres) par-dessus les routes.
    void afficherItineraire(const QVector<QPointF> &points);
    // Pastille "A" (numero 0, départ) ou "B" (numero 1, arrivée) à une position en mètres.
    void afficherMarqueur(int numero, const QPointF &pos);
    // Retire l'itinéraire et les deux pastilles.
    void effacerItineraire();
    // Points des intersections du graphe routier (remplace les précédents).
    void afficherIntersections(const QVector<QPointF> &positions);
    void setIntersectionsVisibles(bool visibles);

    // Couleur de l'itinéraire, partagée avec le panneau.
    static QColor couleurItineraire() { return QColor(0x7C, 0x3A, 0xED); }

    // ---- Étapes 3 à 5 : véhicules et graphe d'interférences ----

    // Positions des véhicules (mètres) et canal radio de chacun (-1 = pas encore coloré).
    // Appelé ~25 fois par seconde pendant la simulation.
    void afficherVehicules(const QVector<QPointF> &positions, const QVector<int> &couleurs);
    // Arêtes d'interférence (paires d'indices de véhicules), dessinées entre leurs positions actuelles.
    void afficherAretes(const QVector<QPair<int, int>> &aretes);
    void setAretesVisibles(bool visibles);
    // Retire tous les véhicules (nouvelle carte, zone vide...).
    void effacerVehicules();
    // Couleur d'un canal radio : palette de couleurs bien distinctes, partagée avec la légende.
    static QColor couleurCanal(int canal);

signals:
    // Simple clic sur la carte en mode itinéraire (position en mètres).
    void clicCarte(const QPointF &pos);
    // Émis quand l'utilisateur a fini de tracer une zone (rectangle en mètres).
    void zoneChoisie(const QRectF &zone);
    // Émis quand le mode sélection se termine (zone tracée ou annulée), pour décocher le bouton.
    void modeSelectionTermine();
    // Position GPS sous la souris, pour l'afficher dans la barre d'état.
    void positionSouris(double lat, double lon);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // Premier affichage après un chargement fait avant que la fenêtre soit visible.
    void showEvent(QShowEvent *event) override;
    // Dessine par-dessus la carte, en coordonnées écran : l'échelle graphique.
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    void afficherZone(const QRectF &zone);  // met à jour le rectangle et l'assombrissement autour
    double echelle() const;                 // pixels écran par mètre (niveau de zoom actuel)

    // Repère de la projection (centre de la carte), pour reconvertir mètres -> GPS.
    double m_latRef = 0.0;
    double m_lonRef = 0.0;
    QRectF m_limitesCarte;  // rectangle englobant les routes, en mètres
    bool m_recentrerAuProchainAffichage = false;  // recentrer() demandé pendant que la vue était cachée

    // Sélection de zone
    bool m_modeSelection = false;
    bool m_selectionEnCours = false;
    QPointF m_debutSelection;  // coin de départ du rectangle, en mètres
    QRectF m_zone;
    QGraphicsPathItem *m_voile = nullptr;   // assombrit ce qui est HORS de la zone
    QGraphicsPathItem *m_cadre = nullptr;   // contour de la zone

    // Itinéraire de test (étape 2). Pointeurs remis à nullptr quand la scène est vidée.
    bool m_modeItineraire = false;
    QPoint m_posAppui;                       // où le bouton a été enfoncé, pour distinguer clic et glisser
    bool m_aGlisse = false;                  // la souris s'est éloignée de m_posAppui pendant l'appui
    QGraphicsPathItem *m_itineraire = nullptr;
    QGraphicsItem *m_marqueurs[2] = {nullptr, nullptr};  // pastilles A et B
    QGraphicsItem *m_intersections = nullptr;
    bool m_intersectionsVisibles = false;

    // Véhicules et arêtes : un seul objet graphique (voir CoucheVehicules dans MapView.cpp).
    class CoucheVehicules *m_vehicules = nullptr;
    bool m_aretesVisibles = false;
};
