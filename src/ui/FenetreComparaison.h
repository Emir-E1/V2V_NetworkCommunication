#pragma once

/// @file FenetreComparaison.h
/// @brief Fenêtre « Comparaison » des algorithmes de coloration — étape 5.
///
/// Demandée par le cahier des charges (§5) : un tableau et des courbes qui
/// comparent les algorithmes au cours du temps, sur le même scénario :
///   - k(t)  : nombre de couleurs (canaux radio) utilisées ;
///   - temps de calcul de la coloration (ms) ;
///   - S(t)  : stabilité = part des véhicules qui gardent leur canal entre deux recalculs.
/// Elle garde l'historique de toutes les mesures, qu'on peut exporter en CSV.

#include "ui/MoteurSimulation.h"

#include <QWidget>

class QLabel;
class QLineSeries;
class QTableWidget;
class QValueAxis;
class QChart;

class FenetreComparaison : public QWidget
{
    Q_OBJECT

public:
    explicit FenetreComparaison(QWidget *parent = nullptr);

    /// Ajoute une mesure (un recalcul) : met à jour le tableau et les courbes.
    void ajouterMesure(const Mesure &mesure);
    /// Efface l'historique (nouvelle simulation).
    void vider();

    const QVector<Mesure> &mesures() const { return m_mesures; }

    /// Écrit toutes les mesures dans un fichier CSV (séparateur « ; », virgule décimale :
    /// s'ouvre directement dans un tableur en français). Renvoie false et remplit *erreur en cas d'échec.
    bool exporterCsv(const QString &chemin, QString *erreur = nullptr) const;

    /// Ouvre la boîte « Enregistrer sous » puis exporte. Utilisé par le bouton de cette
    /// fenêtre et par celui du panneau principal.
    void exporterAvecDialogue(QWidget *parent);

protected:
    void showEvent(QShowEvent *event) override;

private:
    /// Une courbe par algorithme dans un graphique.
    struct Courbes
    {
        QChart *graphique = nullptr;
        QLineSeries *glouton = nullptr;
        QLineSeries *welshPowell = nullptr;
        QValueAxis *axeX = nullptr;
        QValueAxis *axeY = nullptr;
        double yMax = 0.0;
    };
    Courbes creerGraphique(const QString &titre, const QString &uniteY);
    void ajouterPoint(Courbes &courbes, double t, double yGlouton, double yWelshPowell);
    void majTableau();

    QVector<Mesure> m_mesures;
    QTableWidget *m_tableau = nullptr;
    QLabel *m_resume = nullptr;
    Courbes m_couleurs;
    Courbes m_durees;
    Courbes m_stabilites;
};
