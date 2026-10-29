// Point d'entrée du programme : crée l'application Qt et ouvre la fenêtre principale.
// Ce fichier doit rester minimal : la logique va dans les dossiers de src/.

#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    // QApplication gère la boucle d'événements (clics, clavier, redessin).
    QApplication app(argc, argv);
    QApplication::setApplicationName("Simulateur V2V");

    MainWindow fenetre;
    fenetre.showMaximized();  // plein écran : s'adapte à la taille de l'écran de chacun

    // Lance la boucle d'événements ; rend la main quand la fenêtre est fermée.
    return app.exec();
}
