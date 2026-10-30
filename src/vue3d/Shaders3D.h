#pragma once

/// @file Shaders3D.h
/// @brief Programmes exécutés par la carte graphique (« shaders ») pour la vue 3D.
///
/// Un shader est un petit programme écrit en GLSL (langage proche du C) que la carte
/// graphique exécute en parallèle sur des milliers de points ou de pixels :
///   - le « vertex shader » place chaque sommet à l'écran (projection 3D -> 2D) ;
///   - le « fragment shader » calcule la couleur de chaque pixel (éclairage, brouillard...).
/// Vue3D.cpp compile ces textes au démarrage (QOpenGLShaderProgram).
///
/// Repère 3D commun : x = est, y = hauteur, z = sud, en mètres (un point (x, y) de la
/// carte 2D est le point (x, 0, y) au sol).
///
/// Réglages partagés par tous les programmes (« uniforms », envoyés par Vue3D.cpp) :
///   uVueProj  : matrice perspective x vue (monde -> écran)
///   uOeil     : position de la caméra
///   uHorizon  : couleur du ciel à l'horizon, vers laquelle tout s'estompe au loin
///   uBrouillard : distance (m) à laquelle le brouillard devient marqué
///
/// Le brouillard (couleur qui tend vers celle de l'horizon avec la distance) donne la
/// sensation de profondeur et cache la limite de la carte au loin.

namespace Shaders3D
{

// Morceau commun ajouté au début de chaque fragment shader qui en a besoin.
#define V2V_BROUILLARD                                                                  \
    "uniform vec3 uHorizon;\n"                                                          \
    "uniform float uBrouillard;\n"                                                      \
    "vec3 brouillard(vec3 couleur, float distance) {\n"                                \
    "    float f = 1.0 - exp(-pow(distance / uBrouillard, 2.0));\n"                   \
    "    return mix(couleur, uHorizon, clamp(f, 0.0, 1.0));\n"                         \
    "}\n"

// ---------------------------------------------------------------------------
// Ciel et sol : un seul triangle qui couvre tout l'écran. Pour chaque pixel on calcule
// la direction du regard : vers le haut on dessine le dégradé du ciel ; vers le bas on
// calcule où le regard touche le sol (y = 0) et on y dessine une grille de 100 m.
// Le sol n'est donc pas un objet 3D : pas de problème de sol « infini » ni de bord visible.
// ---------------------------------------------------------------------------
inline const char *const CIEL_VERTEX = R"(#version 330 core
out vec2 vEcran;
void main() {
    // Sommets 0, 1, 2 -> triangle (-1,-1), (3,-1), (-1,3) : il couvre l'écran entier.
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    vEcran = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

inline const char *const CIEL_FRAGMENT = "#version 330 core\n" V2V_BROUILLARD R"(
in vec2 vEcran;
out vec4 fragColor;
uniform mat4 uInverseVueProj;   // écran -> monde
uniform vec3 uOeil;
uniform vec3 uZenith;           // couleur du ciel en haut
uniform vec3 uSol;              // couleur du sol
uniform vec3 uGrille;           // couleur des lignes de la grille
void main() {
    vec4 loin = uInverseVueProj * vec4(vEcran, 1.0, 1.0);
    vec3 dir = normalize(loin.xyz / loin.w - uOeil);
    if (dir.y >= -0.0001) {
        // Ciel : dégradé de l'horizon (clair) vers le zénith (bleu).
        fragColor = vec4(mix(uHorizon, uZenith, smoothstep(0.0, 0.5, dir.y)), 1.0);
        return;
    }
    float t = -uOeil.y / dir.y;          // distance jusqu'au sol le long du regard
    vec2 p = (uOeil + dir * t).xz / 100.0;  // position au sol, en carreaux de 100 m
    // Grille anti-crénelée : épaisseur d'environ un pixel quel que soit le zoom
    // (fwidth = variation d'un pixel au suivant). Elle s'efface quand les carreaux
    // deviennent trop petits à l'écran (vue de très loin).
    vec2 w = max(fwidth(p), vec2(1e-6));
    vec2 d = abs(fract(p - 0.5) - 0.5) / w;
    float ligne = 1.0 - clamp(min(d.x, d.y), 0.0, 1.0);
    ligne *= 1.0 - smoothstep(0.04, 0.2, max(w.x, w.y));
    vec3 couleur = mix(uSol, uGrille, ligne * 0.6);
    fragColor = vec4(brouillard(couleur, t), 1.0);
}
)";

// ---------------------------------------------------------------------------
// Routes (et contour de la zone de simulation) : rubans plats posés au sol.
// Chaque sommet connaît le point de l'axe de la route (aCentre) et la direction vers
// le bord du ruban (aDecalage, déjà allongée dans les virages). Le shader choisit la
// demi-largeur : la vraie largeur de la route, mais jamais moins de quelques pixels à
// l'écran (sinon, vue de loin, une rue de 6 m ferait moins d'un pixel et disparaîtrait).
// ---------------------------------------------------------------------------
inline const char *const ROUTES_VERTEX = R"(#version 330 core
layout(location = 0) in vec2 aCentre;     // x, z du point de l'axe (mètres)
layout(location = 1) in vec2 aDecalage;   // direction vers le bord (longueur >= 1 dans les virages)
layout(location = 2) in float aType;      // 0 autoroute ... 3 rue locale, 4 contour de zone
uniform mat4 uVueProj;
uniform vec3 uOeil;
uniform float uPixelMonde;                // mètres couverts par un pixel, à 1 m de distance
uniform float uDemiLargeur[5];            // demi-largeur réelle (m) par type
uniform float uLargeurMinPixels[5];       // largeur minimale à l'écran (pixels) par type
uniform vec3 uCouleur[5];
out vec3 vCouleur;
out float vDistance;
void main() {
    int type = int(aType + 0.5);
    vec3 centre = vec3(aCentre.x, 0.0, aCentre.y);
    float distanceOeil = distance(centre, uOeil);
    float demi = max(uDemiLargeur[type], 0.5 * uLargeurMinPixels[type] * distanceOeil * uPixelMonde);
    vec3 p = centre + vec3(aDecalage.x, 0.0, aDecalage.y) * demi;
    gl_Position = uVueProj * vec4(p, 1.0);
    vCouleur = uCouleur[type];
    vDistance = distance(p, uOeil);
}
)";

inline const char *const ROUTES_FRAGMENT = "#version 330 core\n" V2V_BROUILLARD R"(
in vec3 vCouleur;
in float vDistance;
out vec4 fragColor;
void main() {
    fragColor = vec4(brouillard(vCouleur, vDistance), 1.0);
}
)";

// ---------------------------------------------------------------------------
// Bâtiments : murs et toits (voir Batiments.h). La normale (direction perpendiculaire
// à la surface, qui sert à l'éclairage) n'est pas stockée : elle est recalculée pour
// chaque pixel à partir de la variation de la position d'un pixel à l'autre (dFdx, dFdy).
// Cela divise par deux la mémoire utilisée pour un million de triangles.
// ---------------------------------------------------------------------------
inline const char *const BATIMENTS_VERTEX = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aTeinte;
uniform mat4 uVueProj;
out vec3 vMonde;
out float vTeinte;
void main() {
    vMonde = aPosition;
    vTeinte = aTeinte;
    gl_Position = uVueProj * vec4(aPosition, 1.0);
}
)";

inline const char *const BATIMENTS_FRAGMENT = "#version 330 core\n" V2V_BROUILLARD R"(
in vec3 vMonde;
in float vTeinte;
out vec4 fragColor;
uniform vec3 uOeil;
uniform vec3 uLumiere;      // direction vers le soleil (normalisée)
uniform vec3 uCouleurMur;
uniform vec3 uCouleurToit;
uniform vec3 uCible;        // point visé par la caméra (ou véhicule suivi)
uniform float uCoupe;       // rayon (m) du « couloir de vue » ; 0 = rien n'est effacé
void main() {
    // Caméra proche du sol : elle pourrait se retrouver DANS un bâtiment, ou en avoir un
    // devant elle qui cache le véhicule suivi. On efface donc, comme dans une maquette
    // coupée, les bâtiments situés dans un couloir entre la caméra et le point visé, et
    // ceux qui touchent presque la caméra. Le point visé reste ainsi toujours visible.
    if (uCoupe > 0.0) {
        vec3 axe = uCible - uOeil;
        float longueur = length(axe);
        vec3 direction = axe / longueur;
        float t = dot(vMonde - uOeil, direction);              // avancée le long du couloir
        float ecart = length(vMonde - uOeil - direction * t);  // distance à l'axe du couloir
        if ((t > 0.0 && t < longueur - 6.0 && ecart < uCoupe) || distance(vMonde, uOeil) < 0.35 * longueur)
            discard;
    }
    vec3 n = normalize(cross(dFdx(vMonde), dFdy(vMonde)));  // tournée vers la caméra
    bool toit = n.y > 0.7;
    vec3 base = toit ? uCouleurToit : uCouleurMur;
    base *= 0.97 + 0.06 * vTeinte;                    // légère différence entre voisins
    float soleil = max(dot(n, uLumiere), 0.0);
    float ciel = 0.5 + 0.5 * n.y;                     // lumière diffuse venant du ciel
    vec3 couleur = base * (0.50 + 0.38 * soleil + 0.17 * ciel);
    // Pied des murs un peu plus sombre (« occlusion ambiante » simplifiée) :
    // ancre visuellement les bâtiments au sol.
    if (!toit)
        couleur *= mix(0.78, 1.0, smoothstep(0.0, 5.0, vMonde.y));
    fragColor = vec4(brouillard(couleur, distance(vMonde, uOeil)), 1.0);
}
)";

// ---------------------------------------------------------------------------
// Voitures : une seule forme de voiture (carrosserie, vitres, roues) dessinée une fois
// par véhicule grâce à l'instanciation. Données par véhicule (« instance ») :
// position, direction (cap), taille minimale à l'écran, couleur de son canal radio.
// Vue de loin, une voiture de 4,4 m ferait moins d'un pixel : on l'agrandit pour qu'elle
// garde une taille minimale à l'écran (comme les pastilles de la carte 2D).
// ---------------------------------------------------------------------------
inline const char *const VOITURES_VERTEX = R"(#version 330 core
layout(location = 0) in vec3 aPosition;   // sommet de la forme de voiture (mètres, avant = +x)
layout(location = 1) in vec3 aNormale;
layout(location = 2) in float aPartie;    // 0 carrosserie, 1 vitres, 2 roues
layout(location = 3) in vec4 aVehicule;   // x, z, cap (radians), facteur de taille minimale à l'écran
layout(location = 4) in vec3 aCouleur;    // couleur du canal radio
uniform mat4 uVueProj;
uniform vec3 uOeil;
uniform float uPixelMonde;
uniform float uLongueurMinPixels;
const float LONGUEUR_VOITURE = 4.4;
out vec3 vNormale;
out vec3 vCouleur;
out float vDistance;
flat out int vNumero;  // numéro du véhicule (sert seulement à l'image de choix, voir CHOIX_FRAGMENT)
void main() {
    vNumero = gl_InstanceID;
    vec3 centre = vec3(aVehicule.x, 0.0, aVehicule.y);
    float distanceOeil = distance(centre, uOeil);
    // Taille réelle de près ; de loin, au moins uLongueurMinPixels x aVehicule.w pixels
    // (aVehicule.w vaut plus que 1 pour le véhicule choisi : il ressort même vu de loin).
    float echelle = max(1.0, uLongueurMinPixels * aVehicule.w * distanceOeil * uPixelMonde / LONGUEUR_VOITURE);
    // Rotation autour de l'axe vertical : l'avant (+x) pointe vers (cos cap, sin cap) au sol.
    float c = cos(aVehicule.z), s = sin(aVehicule.z);
    vec3 l = aPosition * echelle;
    vec3 p = centre + vec3(l.x * c - l.z * s, l.y, l.x * s + l.z * c);
    vNormale = vec3(aNormale.x * c - aNormale.z * s, aNormale.y, aNormale.x * s + aNormale.z * c);
    if (aPartie < 0.5)
        vCouleur = aCouleur;
    else if (aPartie < 1.5)
        vCouleur = mix(vec3(0.13, 0.17, 0.23), aCouleur, 0.12);  // vitres teintées
    else
        vCouleur = vec3(0.09, 0.09, 0.10);                        // pneus
    vDistance = distance(p, uOeil);
    gl_Position = uVueProj * vec4(p, 1.0);
}
)";

inline const char *const VOITURES_FRAGMENT = "#version 330 core\n" V2V_BROUILLARD R"(
in vec3 vNormale;
in vec3 vCouleur;
in float vDistance;
out vec4 fragColor;
uniform vec3 uLumiere;
void main() {
    vec3 n = normalize(vNormale);
    float lumiere = 0.62 + 0.30 * max(dot(n, uLumiere), 0.0) + 0.08 * n.y;
    fragColor = vec4(brouillard(vCouleur * lumiere, vDistance), 1.0);
}
)";

// Image de choix, jamais affichée : chaque voiture y est peinte avec une couleur qui code
// son numéro (numéro + 1 sur les trois composantes rouge, vert, bleu : jusqu'à 16 millions
// de véhicules ; 0 = pas de véhicule). Au clic, Vue3D dessine cette image avec les
// bâtiments (qui cachent les voitures situées derrière eux) et lit la couleur sous la
// souris : on choisit exactement la voiture visible, même si deux se chevauchent.
inline const char *const CHOIX_FRAGMENT = R"(#version 330 core
flat in int vNumero;
out vec4 fragColor;
void main() {
    int n = vNumero + 1;
    fragColor = vec4(float(n & 255), float((n >> 8) & 255), float((n >> 16) & 255), 255.0) / 255.0;
}
)";

// ---------------------------------------------------------------------------
// Arêtes d'interférence : un arc au-dessus de la ville entre deux véhicules voisins.
// Chaque arc est un ruban tourné vers la caméra, découpé en 16 morceaux : le shader
// calcule le point de l'arc (une parabole) puis écarte les deux bords du ruban d'un
// nombre fixe de pixels à l'écran (OpenGL ne sait pas tracer de lignes épaisses
// partout, en particulier sous macOS).
// ---------------------------------------------------------------------------
inline const char *const ARCS_VERTEX = R"(#version 330 core
layout(location = 0) in float aT;          // position le long de l'arc, de 0 à 1
layout(location = 1) in float aCote;       // -1 ou +1 : bord gauche ou droit du ruban
layout(location = 2) in vec4 aExtremites;  // xA, zA, xB, zB
layout(location = 3) in vec4 aCouleurA;    // couleur côté A (rgb) + opacité
layout(location = 4) in vec4 aCouleurB;    // couleur côté B (rgb) + épaisseur (pixels de l'interface)
uniform mat4 uVueProj;
uniform vec3 uOeil;
uniform vec2 uEcran;                       // taille de l'image en pixels réels
uniform float uEchelleEcran;               // pixels réels par pixel d'interface (écran à 125 % : 1,25)
const float MORCEAUX = 16.0;
out vec3 vCouleur;
out float vOpacite;
out float vCote;
out float vDistance;
vec3 pointArc(float t) {
    vec3 a = vec3(aExtremites.x, 2.0, aExtremites.y);
    vec3 b = vec3(aExtremites.z, 2.0, aExtremites.w);
    vec3 p = mix(a, b, t);
    p.y += 4.0 * t * (1.0 - t) * (0.18 * distance(a, b) + 4.0);  // hauteur max au milieu
    return p;
}
void main() {
    vec3 p = pointArc(aT);
    vec4 c = uVueProj * vec4(p, 1.0);
    // Direction de l'arc à l'écran, entre le point précédent et le suivant. Un point à
    // l'écran vaut xy / w : sa variation le long de l'arc est proportionnelle à
    // (xy' w - xy w'), formule qui ne divise par aucun w. Elle reste donc juste quand
    // un des points voisins est derrière la caméra (w négatif).
    float pas = 1.0 / MORCEAUX;
    vec4 c1 = uVueProj * vec4(pointArc(min(aT + pas, 1.0)), 1.0);
    vec4 c0 = uVueProj * vec4(pointArc(max(aT - pas, 0.0)), 1.0);
    vec2 tangente = ((c1.xy - c0.xy) * c.w - c.xy * (c1.w - c0.w)) * uEcran;
    vec2 direction = dot(tangente, tangente) > 1e-20 ? normalize(tangente) : vec2(1.0, 0.0);
    vec2 normale = vec2(-direction.y, direction.x);
    // Écart des bords en pixels, converti dans les coordonnées d'avant la division par w.
    // Un point derrière la caméra (w <= 0) garde sa vraie position : OpenGL coupe alors
    // proprement le morceau d'arc invisible.
    if (c.w > 0.0)
        c.xy += normale * aCote * aCouleurB.w * uEchelleEcran / uEcran * c.w;
    gl_Position = c;
    vCouleur = mix(aCouleurA.rgb, aCouleurB.rgb, aT);
    vOpacite = aCouleurA.a;
    vCote = aCote;
    vDistance = distance(p, uOeil);
}
)";

inline const char *const ARCS_FRAGMENT = "#version 330 core\n" V2V_BROUILLARD R"(
in vec3 vCouleur;
in float vOpacite;
in float vCote;
in float vDistance;
out vec4 fragColor;
void main() {
    float bord = 1.0 - smoothstep(0.55, 1.0, abs(vCote));  // bords adoucis
    fragColor = vec4(brouillard(vCouleur, vDistance), vOpacite * bord);
}
)";

// ---------------------------------------------------------------------------
// Portée radio du véhicule choisi : demi-sphère transparente (bulle) de rayon R, plus
// un disque au sol. La bulle est presque invisible de face et plus marquée sur ses
// bords (comme une bulle de savon) pour ne pas cacher la ville.
// ---------------------------------------------------------------------------
inline const char *const PORTEE_VERTEX = R"(#version 330 core
layout(location = 0) in vec3 aPosition;   // point de la demi-sphère (ou du disque) de rayon 1
uniform mat4 uVueProj;
uniform vec3 uCentre;
uniform float uRayon;
out vec3 vLocal;
out vec3 vMonde;
void main() {
    vLocal = aPosition;
    vMonde = uCentre + aPosition * uRayon;
    gl_Position = uVueProj * vec4(vMonde, 1.0);
}
)";

inline const char *const PORTEE_FRAGMENT = R"(#version 330 core
in vec3 vLocal;
in vec3 vMonde;
out vec4 fragColor;
uniform vec3 uOeil;
uniform vec3 uCouleur;
uniform int uDisque;   // 1 = disque au sol, 0 = bulle
void main() {
    if (uDisque == 1) {
        float r = length(vLocal.xz);
        fragColor = vec4(uCouleur, 0.10 + 0.55 * smoothstep(0.965, 1.0, r));  // bord marqué
        return;
    }
    vec3 versOeil = normalize(uOeil - vMonde);
    float bord = 1.0 - abs(dot(normalize(vLocal), versOeil));
    fragColor = vec4(mix(uCouleur, vec3(1.0), 0.2), 0.04 + 0.42 * pow(bord, 3.0));
}
)";

#undef V2V_BROUILLARD

}  // namespace Shaders3D
