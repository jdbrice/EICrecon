// Created by Dmitry Romanov
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#include "TrackParamTruthInit.h"

#include <Evaluator/DD4hepUnits.h>
#include <TParticlePDG.h>
#include <edm4eic/EDM4eicVersion.h>
#include <edm4hep/Vector3d.h>
#include <edm4hep/Vector3f.h>
#include <fmt/core.h>
#include <spdlog/common.h>
#include <stdlib.h>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#if EDM4EIC_VERSION_MAJOR >= 5
#include <edm4eic/Cov6f.h>
#endif

#include "extensions/spdlog/SpdlogFormatters.h" // IWYU pragma: keep


void eicrecon::TrackParamTruthInit::init(const std::shared_ptr<spdlog::logger> &logger) {
    m_log = logger;

    // TODO make a service?
    m_pdg_db = std::make_shared<TDatabasePDG>();
}

std::unique_ptr<edm4eic::TrackParametersCollection>
eicrecon::TrackParamTruthInit::produce(const edm4hep::MCParticleCollection* mcparticles) {
    // MCParticles uses numerical values in its specified units,
    // while m_cfg is in the DD4hep unit system

    // Create output collection
    auto track_parameters = std::make_unique<edm4eic::TrackParametersCollection>();

    // Loop over input particles
    for (const auto& mcparticle: *mcparticles) {

        // require generatorStatus == 1 for stable generated particles in HepMC3 and DDSim gun
        if (mcparticle.getGeneratorStatus() != 1 ) {
            m_log->trace("ignoring particle with generatorStatus = {}", mcparticle.getGeneratorStatus());
            continue;
        }

        // require close to interaction vertex
        auto v = mcparticle.getVertex();
        if (abs(v.x) * dd4hep::mm > m_cfg.m_maxVertexX ||
            abs(v.y) * dd4hep::mm > m_cfg.m_maxVertexY ||
            abs(v.z) * dd4hep::mm > m_cfg.m_maxVertexZ) {
            m_log->trace("ignoring particle with vs = {} [mm]", v);
            continue;
        }

        // require minimum momentum
        const auto& p = mcparticle.getMomentum();
        const auto pmag = std::hypot(p.x, p.y, p.z);
        if (pmag * dd4hep::GeV < m_cfg.m_minMomentum) {
            m_log->trace("ignoring particle with p = {} GeV ", pmag);
            continue;
        }

        // require minimum pseudorapidity
        const auto phi   = std::atan2(p.y, p.x);
        const auto theta = std::atan2(std::hypot(p.x, p.y), p.z);
        const auto eta   = -std::log(std::tan(theta/2));
        if (eta > m_cfg.m_maxEtaForward || eta < -std::abs(m_cfg.m_maxEtaBackward)) {
            m_log->trace("ignoring particle with Eta = {}", eta);
            continue;
        }

        // get the particle charge
        // note that we cannot trust the mcparticles charge, as DD4hep
        // sets this value to zero! let's lookup by PDGID instead
        //const double charge = m_pidSvc->particle(mcparticle.getPDG()).charge;
        const auto pdg = mcparticle.getPDG();
        const auto* particle = m_pdg_db->GetParticle(pdg);
        if (particle == nullptr) {
            m_log->debug("particle with PDG {} not in TDatabasePDG", pdg);
            continue;
        }
        double charge = std::copysign(1.0,particle->Charge());
        if (abs(charge) < std::numeric_limits<double>::epsilon()) {
            m_log->trace("ignoring neutral particle");
            continue;
        }

        // modify initial momentum to avoid bleeding truth to results when fit fails
        const auto pinit = pmag * (1.0 + m_cfg.m_momentumSmear * m_normDist(generator));

        // Insert into edm4eic::TrackParameters, which uses numerical values in its specified units
        auto track_parameter = track_parameters->create();
        track_parameter.setType(-1); // type --> seed(-1)
        track_parameter.setLoc({static_cast<float>(std::hypot(v.x, v.y)), static_cast<float>(v.z)}); // 2d location on surface [mm]
        track_parameter.setTheta(theta); // theta [rad]
        track_parameter.setPhi(phi); // phi [rad]
        track_parameter.setQOverP(charge / (pinit / dd4hep::GeV)); // Q/p [e/GeV]
        track_parameter.setTime(mcparticle.getTime()); // time [ns]
        #if EDM4EIC_VERSION_MAJOR >= 5
          edm4eic::Cov6f cov;
          cov(0,0) = 1.0; // loc0
          cov(1,1) = 1.0; // loc1
          cov(2,2) = 0.01; // theta
          cov(3,3) = 0.05; // phi
          cov(4,4) = 0.1; // qOverP
          cov(5,5) = 10e9; // time
          track_parameter.setCovariance(cov);
        #else
          track_parameter.setCharge(charge); // charge
          track_parameter.setLocError({1.0, 1.0}); // sqrt(variance) of location [mm]
          track_parameter.setMomentumError({0.01, 0.05, 0.1}); // sqrt(variance) on theta, phi, q/p [rad, rad, e/GeV]
          track_parameter.setTimeError(10e9); // error on time [ns]
        #endif

        // Debug output
        if (m_log->level() <= spdlog::level::debug) {
            m_log->debug("Invoke track finding seeded by truth particle with:");
            m_log->debug("   p     = {} GeV (smeared to {} GeV)", pmag / dd4hep::GeV, pinit / dd4hep::GeV);
            m_log->debug("   q     = {}", charge);
            m_log->debug("   q/p   = {} e/GeV (smeared to {} e/GeV)", charge / (pmag / dd4hep::GeV), charge / (pinit / dd4hep::GeV));
            m_log->debug("   theta = {}", theta);
            m_log->debug("   phi   = {}", phi);
        }
    }

    return std::move(track_parameters);

}
