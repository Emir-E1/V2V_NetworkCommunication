// Point d'entrée du programme : crée l'application Qt et ouvre la fenêtre principale.
// Ce fichier doit rester minimal : la logique va dans les dossiers de src/.
//
// Lancement :  v2v-simulator              -> ouvre data/mulhouse.osm s'il existe
//              v2v-simulator carte.osm    -> ouvre le fichier donné

#include <QApplication>
#include <QFileInfo>
#include <QLocale>
#include <QTimer>

#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    // QApplication gère la boucle d'événements (clics, clavier, redessin).
    QApplication app(argc, argv);
    QApplication::setApplicationName("Simulateur V2V");
    // Nombres à la française partout (1 234,5) : QLocale() renverra ce réglage.
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));

    MainWindow fenetre;
    fenetre.showMaximized();  // plein écran : s'adapte à la taille de l'écran de chacun

    // Carte à ouvrir : celle passée en argument, sinon celle du dossier data/ du projet.
    const QStringList args = QApplication::arguments();
    const QString chemin = args.size() > 1 ? args.at(1)
                                           : QStringLiteral(V2V_DOSSIER_DONNEES "/mulhouse.osm");
    // Chargée juste après l'affichage de la fenêtre (singleShot 0), pour que la carte
    // soit centrée avec la vraie taille de la fenêtre maximisée.
    if (QFileInfo::exists(chemin))
        QTimer::singleShot(0, &fenetre, [&fenetre, chemin] { fenetre.chargerCarte(chemin); });

    // Lance la boucle d'événements ; rend la main quand la fenêtre est fermée.
    return app.exec();
}
