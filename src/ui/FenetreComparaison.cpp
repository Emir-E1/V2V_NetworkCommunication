// Fenêtre « Comparaison » : tableau récapitulatif + 3 courbes + export CSV.

#include "ui/FenetreComparaison.h"

#include <QChart>
#include <QChartView>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineSeries>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTextStream>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
// Couleurs des deux algorithmes, identiques dans les trois graphiques.
const QColor COULEUR_GLOUTON(0xF5, 0x82, 0x31);       // orange
const QColor COULEUR_WELSH_POWELL(0x43, 0x63, 0xD8);  // bleu

// Au-delà, on retire les plus anciens points des courbes (l'historique complet
// reste dans m_mesures pour le tableau et l'export).
constexpr int POINTS_MAX = 3600;
}

FenetreComparaison::FenetreComparaison(QWidget *parent)
    : QWidget(parent, Qt::Window)  // fenêtre séparée, pas un morceau de la fenêtre principale
{
    setWindowTitle("Comparaison des algorithmes de coloration");
    resize(1200, 820);

    // -- Tableau : une ligne par algorithme, moyennes et maximums sur toute la simulation --
    m_tableau = new QTableWidget(2, 6);
    m_tableau->setHorizontalHeaderLabels({"Canaux k (moyen)", "Canaux k (max)", "Temps moyen (ms)",
                                          "Temps max (ms)", "Stabilité moyenne", "Colorations valides"});
    m_tableau->setVerticalHeaderLabels({Coloration::nom(Coloration::Algo::Glouton),
                                        Coloration::nom(Coloration::Algo::WelshPowell)});
    m_tableau->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableau->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableau->setSelectionMode(QAbstractItemView::NoSelection);
    m_tableau->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Hauteur exacte : en-tête + 2 lignes + cadre (pas de ligne vide en dessous).
    m_tableau->setFixedHeight(m_tableau->horizontalHeader()->sizeHint().height()
                              + 2 * m_tableau->verticalHeader()->defaultSectionSize()
                              + 2 * m_tableau->frameWidth());

    m_resume = new QLabel;
    m_resume->setWordWrap(true);

    auto *boutonCsv = new QPushButton("Exporter en CSV…");
    connect(boutonCsv, &QPushButton::clicked, this, [this] { exporterAvecDialogue(this); });

    auto *hautDroite = new QVBoxLayout;
    hautDroite->addWidget(m_resume);
    hautDroite->addStretch();
    hautDroite->addWidget(boutonCsv);

    // -- Trois graphiques --
    m_couleurs = creerGraphique("Nombre de canaux k(t)", "canaux");
    m_durees = creerGraphique("Temps de calcul de la coloration", "ms");
    m_stabilites = creerGraphique("Stabilité S(t) de l'allocation", "S");
    m_stabilites.axeY->setRange(0.0, 1.0);  // S est toujours entre 0 et 1

    auto vue = [](QChart *graphique) {
        auto *v = new QChartView(graphique);
        v->setRenderHint(QPainter::Antialiasing);
        return v;
    };

    auto *grille = new QGridLayout(this);
    auto *haut = new QHBoxLayout;
    haut->addWidget(m_tableau, 3);
    haut->addLayout(hautDroite, 1);
    grille->addLayout(haut, 0, 0, 1, 2);
    grille->addWidget(vue(m_couleurs.graphique), 1, 0);
    grille->addWidget(vue(m_durees.graphique), 1, 1);
    grille->addWidget(vue(m_stabilites.graphique), 2, 0, 1, 2);
    grille->setRowStretch(1, 1);
    grille->setRowStretch(2, 1);

    majTableau();
}

FenetreComparaison::Courbes FenetreComparaison::creerGraphique(const QString &titre, const QString &uniteY)
{
    Courbes c;
    c.graphique = new QChart;
    c.graphique->setTitle(titre);
    c.graphique->legend()->setAlignment(Qt::AlignBottom);

    c.glouton = new QLineSeries;
    c.glouton->setName(Coloration::nom(Coloration::Algo::Glouton));
    c.glouton->setPen(QPen(COULEUR_GLOUTON, 2));
    c.welshPowell = new QLineSeries;
    c.welshPowell->setName(Coloration::nom(Coloration::Algo::WelshPowell));
    c.welshPowell->setPen(QPen(COULEUR_WELSH_POWELL, 2));
    c.graphique->addSeries(c.glouton);
    c.graphique->addSeries(c.welshPowell);

    c.axeX = new QValueAxis;
    c.axeX->setTitleText("temps simulé (s)");
    c.axeX->setLabelFormat("%.0f");
    c.axeX->setRange(0, 10);
    c.axeY = new QValueAxis;
    c.axeY->setTitleText(uniteY);
    c.axeY->setRange(0, 1);
    c.graphique->addAxis(c.axeX, Qt::AlignBottom);
    c.graphique->addAxis(c.axeY, Qt::AlignLeft);
    for (QLineSeries *s : {c.glouton, c.welshPowell}) {
        s->attachAxis(c.axeX);
        s->attachAxis(c.axeY);
    }
    return c;
}

void FenetreComparaison::ajouterPoint(Courbes &courbes, double t, double yGlouton, double yWelshPowell)
{
    courbes.glouton->append(t, yGlouton);
    courbes.welshPowell->append(t, yWelshPowell);
    if (courbes.glouton->count() > POINTS_MAX) {
        courbes.glouton->remove(0);
        courbes.welshPowell->remove(0);
    }
    // Axe X : du premier point affiché au dernier. Axe Y : de 0 au maximum vu (+10 %).
    courbes.axeX->setRange(courbes.glouton->at(0).x(), std::max(t, courbes.glouton->at(0).x() + 1.0));
    if (courbes.axeY != m_stabilites.axeY) {
        courbes.yMax = std::max({courbes.yMax, yGlouton, yWelshPowell});
        courbes.axeY->setRange(0.0, courbes.yMax * 1.1 + 1e-9);
    }
}

void FenetreComparaison::ajouterMesure(const Mesure &mesure)
{
    m_mesures.append(mesure);
    ajouterPoint(m_couleurs, mesure.temps, mesure.glouton.couleurs, mesure.welshPowell.couleurs);
    ajouterPoint(m_durees, mesure.temps, mesure.glouton.dureeMs, mesure.welshPowell.dureeMs);
    ajouterPoint(m_stabilites, mesure.temps, mesure.glouton.stabilite, mesure.welshPowell.stabilite);
    // Le tableau n'est utile que si la fenêtre est ouverte : on évite de le recalculer pour rien.
    if (isVisible())
        majTableau();
}

void FenetreComparaison::vider()
{
    m_mesures.clear();
    for (Courbes *c : {&m_couleurs, &m_durees, &m_stabilites}) {
        c->glouton->clear();
        c->welshPowell->clear();
        c->yMax = 0.0;
        c->axeX->setRange(0, 10);
    }
    m_couleurs.axeY->setRange(0, 1);
    m_durees.axeY->setRange(0, 1);
    majTableau();
}

void FenetreComparaison::majTableau()
{
    const QLocale fr;
    // Moyennes et maximums d'un algorithme sur toutes les mesures.
    const auto remplir = [&](int ligne, auto champ) {
        double sommeK = 0, maxK = 0, sommeT = 0, maxT = 0, sommeS = 0;
        int valides = 0;
        for (const Mesure &m : m_mesures) {
            const MesureAlgo &a = champ(m);
            sommeK += a.couleurs;
            maxK = std::max(maxK, double(a.couleurs));
            sommeT += a.dureeMs;
            maxT = std::max(maxT, a.dureeMs);
            sommeS += a.stabilite;
            valides += a.valide ? 1 : 0;
        }
        const int n = m_mesures.size();
        const auto texte = [&](double v, int decimales) { return n ? fr.toString(v, 'f', decimales) : QString("–"); };
        const QStringList cases = {texte(n ? sommeK / n : 0, 2), texte(maxK, 0), texte(n ? sommeT / n : 0, 3),
                                   texte(maxT, 3), texte(n ? sommeS / n : 0, 3),
                                   n ? QString("%1 / %2").arg(valides).arg(n) : QString("–")};
        for (int col = 0; col < cases.size(); ++col) {
            auto *item = new QTableWidgetItem(cases[col]);
            item->setTextAlignment(Qt::AlignCenter);
            m_tableau->setItem(ligne, col, item);
        }
    };
    remplir(0, [](const Mesure &m) -> const MesureAlgo & { return m.glouton; });
    remplir(1, [](const Mesure &m) -> const MesureAlgo & { return m.welshPowell; });

    if (m_mesures.isEmpty()) {
        m_resume->setText("Aucune mesure pour l'instant : lancez la simulation.");
        return;
    }
    double aretes = 0, degre = 0, duree = 0;
    for (const Mesure &m : m_mesures) {
        aretes += m.interference.aretes;
        degre += m.interference.degreMoyen;
        duree += m.interference.dureeMs;
    }
    const int n = m_mesures.size();
    m_resume->setText(QString("<b>%1 recalculs</b> sur %2 s simulées · %3 véhicules<br>"
                              "Graphe d'interférences : %4 arêtes et degré moyen %5 en moyenne, "
                              "construit en %6 ms (grille spatiale).")
                          .arg(fr.toString(n), fr.toString(m_mesures.last().temps, 'f', 0),
                               fr.toString(m_mesures.last().vehicules), fr.toString(aretes / n, 'f', 0),
                               fr.toString(degre / n, 'f', 2), fr.toString(duree / n, 'f', 3)));
}

bool FenetreComparaison::exporterCsv(const QString &chemin, QString *erreur) const
{
    // QSaveFile écrit d'abord dans un fichier temporaire, puis le renomme à la fin
    // (commit) : si l'écriture échoue (disque plein, clé USB retirée...), un ancien
    // export du même nom n'est pas abîmé.
    QSaveFile fichier(chemin);
    if (!fichier.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (erreur)
            *erreur = QString("Impossible d'écrire « %1 » : %2").arg(chemin, fichier.errorString());
        return false;
    }
    QTextStream sortie(&fichier);
    // Format français : « ; » entre les colonnes et virgule décimale (ouverture directe
    // dans Excel / LibreOffice). En Python : pandas.read_csv(f, sep=";", decimal=",").
    const QLocale fr(QLocale::French, QLocale::France);
    const auto nombre = [&](double v, int decimales) { return fr.toString(v, 'f', decimales).remove(QChar(0x202F)).remove(QChar(0xA0)); };
    sortie << "temps_s;vehicules;aretes;degre_moyen;degre_max;interference_ms;"
              "glouton_k;glouton_ms;glouton_stabilite;glouton_valide;"
              "welsh_powell_k;welsh_powell_ms;welsh_powell_stabilite;welsh_powell_valide\n";
    for (const Mesure &m : m_mesures) {
        sortie << nombre(m.temps, 2) << ';' << m.vehicules << ';' << m.interference.aretes << ';'
               << nombre(m.interference.degreMoyen, 3) << ';' << m.interference.degreMax << ';'
               << nombre(m.interference.dureeMs, 4) << ';'
               << m.glouton.couleurs << ';' << nombre(m.glouton.dureeMs, 4) << ';'
               << nombre(m.glouton.stabilite, 4) << ';' << (m.glouton.valide ? 1 : 0) << ';'
               << m.welshPowell.couleurs << ';' << nombre(m.welshPowell.dureeMs, 4) << ';'
               << nombre(m.welshPowell.stabilite, 4) << ';' << (m.welshPowell.valide ? 1 : 0) << '\n';
    }
    // Les écritures sont mises en mémoire tampon : on vide le tampon et on vérifie
    // qu'il n'y a pas eu d'erreur AVANT de remplacer le fichier.
    sortie.flush();
    if (sortie.status() != QTextStream::Ok || !fichier.commit()) {
        if (erreur)
            *erreur = QString("Échec de l'écriture de « %1 » : %2").arg(chemin, fichier.errorString());
        return false;
    }
    return true;
}

void FenetreComparaison::showEvent(QShowEvent *event)
{
    // Le tableau n'est pas mis à jour quand la fenêtre est cachée : on le rafraîchit à l'ouverture.
    majTableau();
    QWidget::showEvent(event);
}

void FenetreComparaison::exporterAvecDialogue(QWidget *parent)
{
    if (m_mesures.isEmpty()) {
        QMessageBox::information(parent, "Export CSV", "Aucune mesure à exporter : lancez d'abord la simulation.");
        return;
    }
    const QString chemin = QFileDialog::getSaveFileName(parent, "Exporter les mesures", "mesures_coloration.csv",
                                                        "Fichiers CSV (*.csv)");
    if (chemin.isEmpty())
        return;
    QString erreur;
    if (exporterCsv(chemin, &erreur))
        QMessageBox::information(parent, "Export CSV",
                                 QString("%1 mesures exportées dans « %2 ».").arg(m_mesures.size()).arg(chemin));
    else
        QMessageBox::warning(parent, "Export CSV", erreur);
}
