// Apparence de l'application (voir Theme.h) : palette claire ou sombre, feuille de style,
// et icônes dessinées au QPainter.

#include "ui/Theme.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QtMath>

#include <cmath>
#include <utility>

namespace
{
bool g_sombre = false;

// Palette : la couleur de chaque « rôle » (fond de fenêtre, texte, champ de saisie, bouton,
// sélection...). Tous les widgets de Qt la suivent, ce qui suffit à changer de thème.
QPalette palette(bool sombre)
{
    QPalette p;
    const QColor fond = sombre ? QColor(0x1E, 0x21, 0x28) : QColor(0xF3, 0xF4, 0xF6);
    const QColor champ = sombre ? QColor(0x15, 0x17, 0x1C) : QColor(0xFF, 0xFF, 0xFF);
    const QColor texte = sombre ? QColor(0xE5, 0xE7, 0xEB) : QColor(0x1F, 0x29, 0x37);
    const QColor bouton = sombre ? QColor(0x2A, 0x2E, 0x37) : QColor(0xFF, 0xFF, 0xFF);
    const QColor bord = sombre ? QColor(0x3A, 0x3F, 0x4A) : QColor(0xD1, 0xD5, 0xDB);
    const QColor survol = sombre ? QColor(0x32, 0x37, 0x41) : QColor(0xE5, 0xE7, 0xEB);
    const QColor estompe = sombre ? QColor(0x6B, 0x72, 0x80) : QColor(0x9C, 0xA3, 0xAF);  // désactivé
    // Texte secondaire (aides, notes) : plus discret que le texte normal mais bien lisible
    // (contraste d'au moins 6 pour 1 avec le fond, au-dessus du minimum conseillé de 4,5).
    const QColor secondaire = sombre ? QColor(0x9C, 0xA3, 0xAF) : QColor(0x4B, 0x55, 0x63);
    p.setColor(QPalette::Window, fond);
    p.setColor(QPalette::WindowText, texte);
    p.setColor(QPalette::Base, champ);
    p.setColor(QPalette::AlternateBase, sombre ? QColor(0x1B, 0x1E, 0x24) : QColor(0xF9, 0xFA, 0xFB));
    p.setColor(QPalette::Text, texte);
    p.setColor(QPalette::Button, bouton);
    p.setColor(QPalette::ButtonText, texte);
    p.setColor(QPalette::BrightText, Qt::white);
    // Sélection (ligne du tableau...) : texte blanc sur bleu foncé dans les deux thèmes, pour
    // un contraste suffisant (le bleu plus clair de l'accent sombre ne le serait pas).
    p.setColor(QPalette::Highlight, QColor(0x25, 0x63, 0xEB));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, Theme::accent());
    p.setColor(QPalette::ToolTipBase, sombre ? QColor(0xE5, 0xE7, 0xEB) : QColor(0x1F, 0x29, 0x37));
    p.setColor(QPalette::ToolTipText, sombre ? QColor(0x1F, 0x29, 0x37) : QColor(0xF9, 0xFA, 0xFB));
    p.setColor(QPalette::PlaceholderText, secondaire);  // utilisé pour les aides et notes du panneau
    // Rôles utilisés par Fusion pour les reliefs et par la feuille de style ci-dessous.
    p.setColor(QPalette::Light, sombre ? QColor(0x3A, 0x3F, 0x4A) : Qt::white);
    p.setColor(QPalette::Midlight, survol);
    p.setColor(QPalette::Mid, bord);
    p.setColor(QPalette::Dark, sombre ? QColor(0x11, 0x13, 0x18) : QColor(0x9C, 0xA3, 0xAF));
    p.setColor(QPalette::Shadow, sombre ? QColor(0x00, 0x00, 0x00) : QColor(0x6B, 0x72, 0x80));
    // Éléments désactivés (boutons grisés, par exemple avant le chargement d'une carte).
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, estompe);
    return p;
}

// Feuille de style (syntaxe proche du CSS des pages web). Elle donne surtout des formes et
// des espacements ; les couleurs viennent de la palette (« palette(mid) »...). Seuls les
// boutons enfoncés ont leurs couleurs propres (%1 fond, %2 texte, %3 bord), choisies par
// feuilleDeStyle() selon le thème : fond bleu pâle et texte bleu foncé en thème clair. Les
// icônes restent de la couleur du texte : lisibles sur ce fond pâle comme dans les menus.
const char *const FEUILLE_DE_STYLE = R"(
QToolBar { border: none; padding: 3px 6px; spacing: 2px; }
QToolBar::separator { width: 1px; background: palette(mid); margin: 5px 6px; }
QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 3px 7px; }
QToolButton:hover { background: palette(midlight); }
QToolButton:checked { background: %1; color: %2; border: 1px solid %3; }
QPushButton { border: 1px solid palette(mid); border-radius: 6px; padding: 5px 12px; background: palette(button); }
QPushButton:hover { border-color: palette(highlight); }
QPushButton:pressed { background: palette(midlight); }
QGroupBox { font-weight: 600; border: 1px solid palette(mid); border-radius: 8px; margin-top: 12px; padding: 12px 8px 8px 8px; }
QGroupBox QWidget { font-weight: normal; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QTabWidget::pane { border: none; }
QTabBar::tab { padding: 7px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { border-bottom: 2px solid palette(highlight); font-weight: 600; }
QTabBar::tab:hover { background: palette(midlight); }
QTableWidget { border: 1px solid palette(mid); border-radius: 6px; }
QHeaderView::section { padding: 4px 6px; border: none; border-bottom: 1px solid palette(mid); background: palette(alternate-base); font-weight: 600; }
QStatusBar { border-top: 1px solid palette(mid); }
QDockWidget::title { padding: 6px 8px; }
QScrollArea { border: none; }
)";

QString feuilleDeStyle(bool sombre)
{
    return sombre ? QString(FEUILLE_DE_STYLE).arg("#1E3A5F", "#BFDBFE", "#3B82F6")   // contraste 8,5 pour 1
                  : QString(FEUILLE_DE_STYLE).arg("#DBEAFE", "#1D4ED8", "#93C5FD");  // contraste 6,5 pour 1
}

// Dessine une icône sur une grille de 20 x 20, dans la couleur `c`, à l'échelle `facteur`
// (2 = écran haute résolution).
QPixmap dessiner(Theme::Icone type, const QColor &c, int facteur)
{
    QPixmap image(20 * facteur, 20 * facteur);
    image.setDevicePixelRatio(facteur);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto plein = [&p, &c] { p.setBrush(c); };
    using I = Theme::Icone;
    switch (type) {
    case I::Ouvrir: {  // dossier
        QPainterPath d;
        d.moveTo(2.5, 15.5); d.lineTo(2.5, 4.5); d.lineTo(7.5, 4.5); d.lineTo(9.5, 6.5);
        d.lineTo(17.5, 6.5); d.lineTo(17.5, 15.5); d.closeSubpath();
        p.drawPath(d);
        break;
    }
    case I::Recentrer:  // quatre coins de cadre et un point au centre
        for (int sx : {-1, 1})
            for (int sy : {-1, 1}) {
                const QPointF coin(10 + sx * 7, 10 + sy * 7);
                p.drawLine(coin, coin - QPointF(sx * 4, 0));
                p.drawLine(coin, coin - QPointF(0, sy * 4));
            }
        plein();
        p.drawEllipse(QPointF(10, 10), 1.8, 1.8);
        break;
    case I::Zone:
    case I::EffacerZone: {  // rectangle en pointillés (barré pour « effacer »)
        QPen pointilles = p.pen();
        pointilles.setDashPattern({2.0, 1.6});
        p.setPen(pointilles);
        p.drawRoundedRect(QRectF(3, 4, 14, 12), 1.5, 1.5);
        if (type == I::EffacerZone) {
            p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(7, 7.5), QPointF(13, 12.5));
            p.drawLine(QPointF(13, 7.5), QPointF(7, 12.5));
        }
        break;
    }
    case I::Itineraire: {  // trajet entre un départ et une arrivée
        QPainterPath d;
        d.moveTo(4.5, 15.5);
        d.cubicTo(4.5, 9, 15.5, 11, 15.5, 4.5);
        p.drawPath(d);
        plein();
        p.drawEllipse(QPointF(4.5, 15.5), 2.2, 2.2);
        p.drawEllipse(QPointF(15.5, 4.5), 2.2, 2.2);
        break;
    }
    case I::Intersections: {  // petit graphe : sommets reliés
        const QPointF a(4, 5), b(16, 4), cc(10, 11), d(4, 16), e(16, 16);
        for (const auto &[u, v] : {std::pair{a, cc}, std::pair{b, cc}, std::pair{cc, d}, std::pair{cc, e}, std::pair{d, e}})
            p.drawLine(u, v);
        plein();
        for (const QPointF &s : {a, b, cc, d, e})
            p.drawEllipse(s, 1.9, 1.9);
        break;
    }
    case I::Aretes: {  // deux véhicules reliés par des arcs
        QPainterPath d;
        d.moveTo(3.5, 15); d.cubicTo(5, 4, 15, 4, 16.5, 15);
        d.moveTo(3.5, 15); d.cubicTo(6, 9, 11, 9, 11.5, 15.5);
        p.drawPath(d);
        plein();
        for (const QPointF &s : {QPointF(3.5, 15), QPointF(16.5, 15), QPointF(11.5, 15.5)})
            p.drawEllipse(s, 1.9, 1.9);
        break;
    }
    case I::Comparaison:  // diagramme en barres
        p.drawLine(QPointF(3, 17), QPointF(17, 17));
        plein();
        p.drawRoundedRect(QRectF(4.5, 10, 2.8, 5.5), 0.8, 0.8);
        p.drawRoundedRect(QRectF(8.6, 4, 2.8, 11.5), 0.8, 0.8);
        p.drawRoundedRect(QRectF(12.7, 7.5, 2.8, 8), 0.8, 0.8);
        break;
    case I::Lecture:  // triangle « lecture »
        plein();
        p.drawPolygon(QPolygonF({QPointF(6, 3.5), QPointF(16, 10), QPointF(6, 16.5)}));
        break;
    case I::Pause:
        plein();
        p.drawRoundedRect(QRectF(5, 4, 3.4, 12), 1, 1);
        p.drawRoundedRect(QRectF(11.6, 4, 3.4, 12), 1, 1);
        break;
    case I::Ralentir:
    case I::Accelerer: {  // deux triangles vers la gauche ou vers la droite
        plein();
        const double s = type == I::Accelerer ? 1.0 : -1.0;
        for (double x0 : {3.0, 10.0}) {
            const double a = 10 + s * (x0 - 10), b = 10 + s * (x0 + 7 - 10);
            p.drawPolygon(QPolygonF({QPointF(a, 5), QPointF(b, 10), QPointF(a, 15)}));
        }
        break;
    }
    case I::Reinitialiser: {  // flèche circulaire
        p.drawArc(QRectF(4, 4, 12, 12), 100 * 16, 290 * 16);
        plein();
        // Pointe de la flèche au bout de l'arc (en haut, vers la droite).
        const double angle = qDegreesToRadians(100.0);
        const QPointF bout(10 + 6 * std::cos(angle), 10 - 6 * std::sin(angle));
        p.drawPolygon(QPolygonF({bout + QPointF(3.2, 0), bout + QPointF(-1.2, -2.8), bout + QPointF(-1.2, 2.8)}));
        break;
    }
    }
    p.end();
    return image;
}
}  // namespace

void Theme::appliquer(bool sombre)
{
    g_sombre = sombre;
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(palette(sombre));
    // Réappliquer la feuille de style (même texte) : les couleurs « palette(...) » qu'elle
    // contient sont relues avec la nouvelle palette.
    qApp->setStyleSheet(QString());
    qApp->setStyleSheet(feuilleDeStyle(sombre));
}

bool Theme::estSombre()
{
    return g_sombre;
}

QColor Theme::accent()
{
    // Même bleu que le contour de la zone de simulation ; plus clair en thème sombre pour
    // rester lisible en texte sur fond foncé (meilleur nombre de canaux du tableau).
    return g_sombre ? QColor(0x60, 0xA5, 0xFA) : QColor(0x25, 0x63, 0xEB);
}

QIcon Theme::icone(Icone type)
{
    const QColor couleur = QApplication::palette().color(QPalette::WindowText);
    QIcon resultat;
    resultat.addPixmap(dessiner(type, couleur, 1));
    resultat.addPixmap(dessiner(type, couleur, 2));
    // Version grisée pour un bouton désactivé (avant le chargement d'une carte, par exemple).
    const QColor grise = QApplication::palette().color(QPalette::Disabled, QPalette::WindowText);
    resultat.addPixmap(dessiner(type, grise, 1), QIcon::Disabled);
    resultat.addPixmap(dessiner(type, grise, 2), QIcon::Disabled);
    return resultat;
}
