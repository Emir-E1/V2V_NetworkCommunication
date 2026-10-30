// Point d'entrée du programme : crée l'application Qt et ouvre la fenêtre principale.
// Ce fichier doit rester minimal : la logique va dans les dossiers de src/.
//
// Lancement :  v2v-simulator              -> ouvre data/mulhouse.osm s'il existe
//              v2v-simulator carte.osm    -> ouvre le fichier donné

#include <QApplication>
#include <QFileInfo>
#include <QLocale>
#include <QSurfaceFormat>
#include <QTimer>

#include "ui/MainWindow.h"
#include "ui/Theme.h"

#ifdef _WIN32
// Ordinateurs portables à deux cartes graphiques (intégrée Intel + NVIDIA ou AMD) : sans
// rien dire, Windows lance souvent le programme sur la carte intégrée, bien plus lente
// pour la vue 3D. Les pilotes NVIDIA et AMD lisent ces deux variables exportées par le
// programme et choisissent alors la carte la plus puissante.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char *argv[])
{
    // Vue 3D : OpenGL 3.3 « core » (shaders modernes), tampon de profondeur (un objet proche
    // cache un objet lointain) et 4 échantillons par pixel (bords lissés). À régler AVANT de
    // créer QApplication : sous macOS c'est obligatoire pour que tous les contextes OpenGL
    // de l'application soient compatibles entre eux.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    // QApplication gère la boucle d'événements (clics, clavier, redessin).
    QApplication app(argc, argv);
    QApplication::setApplicationName("Simulateur V2V");
    // Nombres à la française partout (1 234,5) : QLocale() renverra ce réglage.
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
    // Style moderne (Fusion), identique sur tous les systèmes, en thème clair (voir ui/Theme.h).
    Theme::appliquer(false);

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
