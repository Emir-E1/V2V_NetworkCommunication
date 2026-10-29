// Déplacement des véhicules sur le réseau routier — étape 3 du projet.
// Les itinéraires viennent de A*, mais les distances parcourues suivent les
// segments de la trace en mètres. À chaque changement de route ou de trajet,
// on conserve le temps restant pour appliquer la nouvelle vitesse correctement.

#include "simulation/Simulation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
// Le tirage exclut directement le départ : pas de boucle aléatoire pour obtenir
// une destination différente. Les essais bornés protègent un graphe incomplet.
bool choisirDestination(Vehicule &vehicule, int depart, const GrapheRoutier &graphe,
                        std::mt19937 &aleatoire)
{
    const int nombre = static_cast<int>(graphe.sommets().size());
    std::uniform_int_distribution<int> destination(0, nombre - 2);
    for (int essai = 0; essai < 16; ++essai) {
        int arrivee = destination(aleatoire);
        if (arrivee >= depart) {
            ++arrivee;
        }
        vehicule.chemin = graphe.plusCourtChemin(depart, arrivee);
        if (!vehicule.chemin.isEmpty()) {
            vehicule.destination = arrivee;
            vehicule.etape = 0;
            vehicule.avancement = 0.0;
            return true;
        }
    }

    // Si les tirages échouent, un voisin accessible permet de repartir sans
    // parcourir toutes les destinations. Sur le graphe fortement connexe,
    // le tout premier tirage réussit et ce secours n'est jamais nécessaire.
    for (int indice : graphe.sortants(depart)) {
        const int arrivee = graphe.troncons()[indice].arrivee;
        if (arrivee == depart) {
            continue;
        }
        vehicule.chemin = graphe.plusCourtChemin(depart, arrivee);
        if (!vehicule.chemin.isEmpty()) {
            vehicule.destination = arrivee;
            vehicule.etape = 0;
            vehicule.avancement = 0.0;
            return true;
        }
    }
    vehicule.destination = -1;
    vehicule.vitesse = 0.0;
    return false;
}

// Renvoie true à la fin du tronçon ; sinon tout le temps disponible a été
// consommé. Le cumul est euclidien : Troncon::longueur sert aux trajets A*,
// mais sa distance Haversine diffère légèrement de celle de la trace affichée.
bool parcourirTrace(Vehicule &vehicule, const Troncon &troncon, double &temps)
{
    vehicule.vitesse = Simulation::vitesseParType(troncon.type) * vehicule.facteurVitesse;
    double cumul = 0.0;
    for (qsizetype i = 1; i < troncon.trace.size(); ++i) {
        const QPointF debut = troncon.trace[i - 1];
        const QPointF segment = troncon.trace[i] - debut;
        const double longueur = std::hypot(segment.x(), segment.y());
        if (longueur == 0.0) {
            continue;  // Deux points confondus ne définissent ni durée ni direction.
        }
        const double fin = cumul + longueur;
        if (vehicule.avancement < fin) {
            vehicule.direction = std::atan2(segment.y(), segment.x());
            const double duree = (fin - vehicule.avancement) / vehicule.vitesse;
            if (temps < duree) {
                vehicule.avancement += vehicule.vitesse * temps;
                vehicule.position = debut + segment * ((vehicule.avancement - cumul) / longueur);
                temps = 0.0;
                return false;
            }
            temps = std::max(0.0, temps - duree);
            vehicule.avancement = fin;
        }
        cumul = fin;
    }
    if (!troncon.trace.isEmpty()) {
        vehicule.position = troncon.trace.back();
    }
    return true;
}
}

double Simulation::vitesseParType(TypeRoute type)
{
    switch (type) {
    case TypeRoute::Autoroute:
        return 90.0 / 3.6;
    case TypeRoute::Principale:
    case TypeRoute::Secondaire:
        return 50.0 / 3.6;
    case TypeRoute::Locale:
        return 30.0 / 3.6;
    }
    return 30.0 / 3.6;
}

bool Simulation::initialiser(const GrapheRoutier *graphe, const ParametresSimulation &parametres)
{
    m_graphe = graphe;
    m_vehicules.clear();
    m_temps = 0.0;
    m_arrivees = 0;
    m_aleatoire.seed(parametres.graine);
    if (!graphe || graphe->sommets().size() < 2) {
        return false;
    }

    std::uniform_int_distribution<int> depart(0, static_cast<int>(graphe->sommets().size()) - 1);
    std::uniform_real_distribution<double> facteur(0.8, 1.2);
    m_vehicules.reserve(std::max(0, parametres.nombreVehicules));
    for (int id = 0; id < parametres.nombreVehicules; ++id) {
        Vehicule vehicule;
        vehicule.id = id;
        const int sommet = depart(m_aleatoire);
        vehicule.position = graphe->sommets()[sommet].pos;
        vehicule.facteurVitesse = facteur(m_aleatoire);
        vehicule.rayon = parametres.rayon;
        if (!choisirDestination(vehicule, sommet, *graphe, m_aleatoire)) {
            m_vehicules.clear();  // Ne pas publier une population partiellement initialisée.
            return false;
        }
        m_vehicules.append(std::move(vehicule));
    }
    avancer(0.0);  // Renseigne aussi la vitesse et la direction du premier segment.
    return true;
}

void Simulation::avancer(double dt)
{
    // Un pas invalide ne doit ni faire reculer les véhicules ni propager NaN.
    if (!std::isfinite(dt) || dt < 0.0) {
        return;
    }
    m_temps += dt;
    if (!m_graphe) {
        return;
    }
    const auto &troncons = m_graphe->troncons();
    for (Vehicule &vehicule : m_vehicules) {
        double restant = dt;
        qsizetype sansProgres = 0;
        while (!vehicule.chemin.isEmpty()) {
            const Troncon &troncon = troncons[vehicule.chemin[vehicule.etape]];
            const double avant = restant;
            if (!parcourirTrace(vehicule, troncon, restant)) {
                break;
            }
            ++vehicule.etape;
            vehicule.avancement = 0.0;
            if (vehicule.etape == vehicule.chemin.size()) {
                ++m_arrivees;
                if (!choisirDestination(vehicule, troncon.arrivee, *m_graphe, m_aleatoire)) {
                    break;
                }
            }
            // Une géométrie entièrement dégénérée ne doit pas boucler sans fin.
            // Aucune limite de franchissements n'est imposée aux routes normales.
            sansProgres = restant == avant ? sansProgres + 1 : 0;
            if (sansProgres > troncons.size()) {
                vehicule.vitesse = 0.0;
                break;
            }
        }
    }
}

void Simulation::setRayon(double rayon)
{
    for (Vehicule &vehicule : m_vehicules) {
        vehicule.rayon = rayon;
    }
}
