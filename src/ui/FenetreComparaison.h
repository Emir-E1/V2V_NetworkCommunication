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

#include <array>

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

    /// Couleur d'un algorithme, identique dans tous les graphiques.
    static QColor couleurAlgo(Coloration::Algo algo);

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

    /// Courbes sur fond sombre ou clair, pour suivre le thème de l'interface.
    void setThemeSombre(bool sombre);

protected:
    void showEvent(QShowEvent *event) override;

private:
    /// Un graphique : une courbe par algorithme.
    struct Courbes
    {
        QChart *graphique = nullptr;
        std::array<QLineSeries *, Coloration::NOMBRE_ALGOS> series{};
        QValueAxis *axeX = nullptr;
        QValueAxis *axeY = nullptr;
        double yMax = 0.0;
        bool yFixe = false;  ///< axe Y figé (stabilité : toujours entre 0 et 1)
    };
    Courbes creerGraphique(const QString &titre, const QString &uniteY);
    /// Ajoute un point par algorithme ; `valeur(a)` donne la valeur de l'algorithme a.
    template<typename Fonction>
    void ajouterPoints(Courbes &courbes, double t, Fonction valeur);
    void majTableau();

    QVector<Mesure> m_mesures;
    QTableWidget *m_tableau = nullptr;
    QLabel *m_resume = nullptr;
    Courbes m_couleurs;
    Courbes m_durees;
    Courbes m_stabilites;
};
