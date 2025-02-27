// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2023 Wouter Deconinck

#pragma once

#include "algorithms/calorimetry/ImagingClusterReco.h"
#include "extensions/jana/JOmniFactory.h"


namespace eicrecon {

class ImagingClusterReco_factory :
    public JOmniFactory<ImagingClusterReco_factory, ImagingClusterRecoConfig> {

public:
    using AlgoT = eicrecon::ImagingClusterReco;
private:
    std::unique_ptr<AlgoT> m_algo;

    PodioInput<edm4eic::ProtoCluster> m_protos_input {this};
    PodioInput<edm4hep::SimCalorimeterHit> m_mchits_input {this};

    PodioOutput<edm4eic::Cluster> m_clusters_output {this};
    PodioOutput<edm4eic::MCRecoClusterParticleAssociation> m_assocs_output {this};
    PodioOutput<edm4eic::Cluster> m_layers_output {this};

    ParameterRef<int> m_trackStopLayer {this, "trackStopLayer", config().trackStopLayer};

public:
    void Configure() {
        m_algo = std::make_unique<AlgoT>(GetPrefix());
        m_algo->applyConfig(config());
        m_algo->init(logger());
    }

    void ChangeRun(int64_t run_number) {
    }

    void Process(int64_t run_number, uint64_t event_number) {
        m_algo->process({m_protos_input(), m_mchits_input()},
                        {m_clusters_output().get(), m_assocs_output().get(), m_layers_output().get()});
    }
};

} // eicrecon
