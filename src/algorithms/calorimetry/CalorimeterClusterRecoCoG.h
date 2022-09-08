

#ifndef _CalorimeterClusterRecoCoG_h_
#define _CalorimeterClusterRecoCoG_h_

#include <random>

#include <services/geometry/dd4hep/JDD4hep_service.h>
#include <edm4hep/SimCalorimeterHit.h>
#include <eicd/ProtoCluster.h>
#include <eicd/Cluster.h>

#include <eicd/MCRecoClusterParticleAssociation.h>
#include <eicd/MutableMCRecoClusterParticleAssociation.h>
#include <eicd/MutableCluster.h>
#include <eicd/vector_utils.h>
#include <map>


using namespace dd4hep;

static double constWeight(double /*E*/, double /*tE*/, double /*p*/, int /*type*/) { return 1.0; }
    static double linearWeight(double E, double /*tE*/, double /*p*/, int /*type*/) { return E; }
    static double logWeight(double E, double tE, double base, int /*type*/) {
      return std::max(0., base + std::log(E / tE));
    }

    static const std::map<std::string, std::function<double(double, double, double, int)>> weightMethods={
      {"none", constWeight},
      {"linear", linearWeight},
      {"log", logWeight},
    };

class CalorimeterClusterRecoCoG {

    // Insert any member variables here

public:
    CalorimeterClusterRecoCoG() = default;
    ~CalorimeterClusterRecoCoG(){} // better to use smart pointer?
    virtual void AlgorithmInit() ;
    virtual void AlgorithmChangeRun() ;
    virtual void AlgorithmProcess() ;

    //-------- Configuration Parameters ------------
    std::string m_input_simhit_tag;
    std::string m_input_protoclust_tag;
    
    double m_sampFrac;//{this, "samplingFraction", 1.0};
    double m_logWeightBase;//{this, "logWeightBase", 3.6};
    double m_depthCorrection;//{this, "depthCorrection", 0.0};
    std::string m_energyWeight;//{this, "energyWeight", "log"};
    std::string m_moduleDimZName;//{this, "moduleDimZName", ""};
    // Constrain the cluster position eta to be within
    // the eta of the contributing hits. This is useful to avoid edge effects
    // for endcaps.
    bool m_enableEtaBounds;//{this, "enableEtaBounds", false};

    std::shared_ptr<JDD4hep_service> m_geoSvc;

    std::function<double(double, double, double, int)> weightFunc;

    
  //inputs EcalEndcapNTruthProtoClusters AND EcalEndcapNHits

  //inputs
    std::vector<const edm4hep::SimCalorimeterHit*> m_inputSimhits; //e.g. EcalEndcapNHits
    std::vector<const eicd::ProtoCluster*> m_inputProto; //e.g. EcalEndcapNTruthProtoClusters  //{"outputProtoClusters", Gaudi::DataHandle::Writer, this};

  //outputs
    std::vector<eicd::Cluster*> m_outputClusters;
    std::vector<eicd::MCRecoClusterParticleAssociation*> m_outputAssociations;


private:
eicd::Cluster reconstruct(const eicd::ProtoCluster* pcl) const {
    eicd::MutableCluster cl;
    cl.setNhits(pcl->hits_size());

    // no hits
    if (false) {
      LOG_INFO(default_cout_logger) << "hit size = " << pcl->hits_size() << LOG_END;
    }
    if (pcl->hits_size() == 0) {
      return cl;
    }

    // calculate total energy, find the cell with the maximum energy deposit
    float totalE = 0.;
    float maxE   = 0.;
    // Used to optionally constrain the cluster eta to those of the contributing hits
    float minHitEta = std::numeric_limits<float>::max();
    float maxHitEta = std::numeric_limits<float>::min();
    auto time       = pcl->getHits()[0].getTime();
    auto timeError  = pcl->getHits()[0].getTimeError();
    for (unsigned i = 0; i < pcl->getHits().size(); ++i) {
      const auto& hit   = pcl->getHits()[i];
      const auto weight = pcl->getWeights()[i];
      if (false) {
        LOG_INFO(default_cout_logger) << "hit energy = " << hit.getEnergy() << " hit weight: " << weight << LOG_END;
      }
      auto energy = hit.getEnergy() * weight;
      totalE += energy;
      if (energy > maxE) {
      }
      const float eta = eicd::eta(hit.getPosition());
      if (eta < minHitEta) {
        minHitEta = eta;
      }
      if (eta > maxHitEta) {
        maxHitEta = eta;
      }
    }
    cl.setEnergy(totalE / m_sampFrac);
    cl.setEnergyError(0.);
    cl.setTime(time);
    cl.setTimeError(timeError);

    // center of gravity with logarithmic weighting
    float tw = 0.;
    auto v   = cl.getPosition();
    for (unsigned i = 0; i < pcl->getHits().size(); ++i) {
      const auto& hit   = pcl->getHits()[i];
      const auto weight = pcl->getWeights()[i];
      float w           = weightFunc(hit.getEnergy() * weight, totalE, m_logWeightBase, 0);
      tw += w;
      v = v + (hit.getPosition() * w);
    }
    if (tw == 0.) {
      LOG_WARN(default_cout_logger) << "zero total weights encountered, you may want to adjust your weighting parameter." << LOG_END;
    }
    cl.setPosition(v / tw);
    cl.setPositionError({}); // @TODO: Covariance matrix

    // Optionally constrain the cluster to the hit eta values
    if (m_enableEtaBounds) {
      const bool overflow  = (eicd::eta(cl.getPosition()) > maxHitEta);
      const bool underflow = (eicd::eta(cl.getPosition()) < minHitEta);
      if (overflow || underflow) {
        const double newEta   = overflow ? maxHitEta : minHitEta;
        const double newTheta = eicd::etaToAngle(newEta);
        const double newR     = eicd::magnitude(cl.getPosition());
        const double newPhi   = eicd::angleAzimuthal(cl.getPosition());
        cl.setPosition(eicd::sphericalToVector(newR, newTheta, newPhi));
        if (false) {
          LOG_INFO(default_cout_logger) << "Bound cluster position to contributing hits due to " << (overflow ? "overflow" : "underflow")
                  << LOG_END;
        }
      }
    }

    // Additional convenience variables

    // best estimate on the cluster direction is the cluster position
    // for simple 2D CoG clustering
    cl.setIntrinsicTheta(eicd::anglePolar(cl.getPosition()));
    cl.setIntrinsicPhi(eicd::angleAzimuthal(cl.getPosition()));
    // TODO errors

    // Calculate radius
    // @TODO: add skewness
    if (cl.getNhits() > 1) {
      double radius = 0;
      for (const auto& hit : pcl->getHits()) {
        const auto delta = cl.getPosition() - hit.getPosition();
        radius += delta * delta;
      }
      radius = sqrt((1. / (cl.getNhits() - 1.)) * radius);
      cl.addToShapeParameters(radius);
      cl.addToShapeParameters(0 /* skewness */); // skewness not yet calculated
    }
    eicd::Cluster c(cl);
    return c;
  }

    
};

#endif // _CalorimeterClusterRecoCoG_h_