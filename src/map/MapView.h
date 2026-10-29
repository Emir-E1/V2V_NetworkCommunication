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

signals:
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
    // Dessine par-dessus la carte, en coordonnées écran : l'échelle graphique.
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    void afficherZone(const QRectF &zone);  // met à jour le rectangle et l'assombrissement autour
    double echelle() const;                 // pixels écran par mètre (niveau de zoom actuel)

    // Repère de la projection (centre de la carte), pour reconvertir mètres -> GPS.
    double m_latRef = 0.0;
    double m_lonRef = 0.0;
    QRectF m_limitesCarte;  // rectangle englobant les routes, en mètres

    // Sélection de zone
    bool m_modeSelection = false;
    bool m_selectionEnCours = false;
    QPointF m_debutSelection;  // coin de départ du rectangle, en mètres
    QRectF m_zone;
    QGraphicsPathItem *m_voile = nullptr;   // assombrit ce qui est HORS de la zone
    QGraphicsPathItem *m_cadre = nullptr;   // contour de la zone
};
