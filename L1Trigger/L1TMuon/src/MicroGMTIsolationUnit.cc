#include "L1Trigger/L1TMuon/interface/MicroGMTIsolationUnit.h"

#include "L1Trigger/L1TMuon/interface/GMTInternalMuon.h"
#include "DataFormats/L1TMuon/interface/MuonCaloSum.h"
#include "DataFormats/L1Trigger/interface/Muon.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"


l1t::MicroGMTIsolationUnit::MicroGMTIsolationUnit () : m_fwVersion(0), m_initialSums(false)
{
}

l1t::MicroGMTIsolationUnit::~MicroGMTIsolationUnit ()
{
}

void
l1t::MicroGMTIsolationUnit::initialise(L1TMuonGlobalParamsHelper* microGMTParamsHelper) {
  m_fwVersion = microGMTParamsHelper->fwVersion();
  m_BEtaExtrapolation = l1t::MicroGMTExtrapolationLUTFactory::create(microGMTParamsHelper->bEtaExtrapolationLUT(), MicroGMTConfiguration::ETA_OUT, m_fwVersion);
  m_BPhiExtrapolation = l1t::MicroGMTExtrapolationLUTFactory::create(microGMTParamsHelper->bPhiExtrapolationLUT(), MicroGMTConfiguration::PHI_OUT, m_fwVersion);
  m_OEtaExtrapolation = l1t::MicroGMTExtrapolationLUTFactory::create(microGMTParamsHelper->oEtaExtrapolationLUT(), MicroGMTConfiguration::ETA_OUT, m_fwVersion);
  m_OPhiExtrapolation = l1t::MicroGMTExtrapolationLUTFactory::create(microGMTParamsHelper->oPhiExtrapolationLUT(), MicroGMTConfiguration::PHI_OUT, m_fwVersion);
  m_FEtaExtrapolation = l1t::MicroGMTExtrapolationLUTFactory::create(microGMTParamsHelper->fEtaExtrapolationLUT(), MicroGMTConfiguration::ETA_OUT, m_fwVersion);
  m_FPhiExtrapolation = l1t::MicroGMTExtrapolationLUTFactory::create(microGMTParamsHelper->fPhiExtrapolationLUT(), MicroGMTConfiguration::PHI_OUT, m_fwVersion);
  m_IdxSelMemEta = l1t::MicroGMTCaloIndexSelectionLUTFactory::create(microGMTParamsHelper->idxSelMemEtaLUT(), MicroGMTConfiguration::ETA, m_fwVersion);
  m_IdxSelMemPhi = l1t::MicroGMTCaloIndexSelectionLUTFactory::create(microGMTParamsHelper->idxSelMemPhiLUT(), MicroGMTConfiguration::PHI, m_fwVersion);
  m_RelIsoCheckMem = l1t::MicroGMTRelativeIsolationCheckLUTFactory::create(microGMTParamsHelper->relIsoCheckMemLUT(), m_fwVersion);
  m_AbsIsoCheckMem = l1t::MicroGMTAbsoluteIsolationCheckLUTFactory::create(microGMTParamsHelper->absIsoCheckMemLUT(), m_fwVersion);

  m_etaExtrapolationLUTs[tftype::bmtf] = m_BEtaExtrapolation;
  m_phiExtrapolationLUTs[tftype::bmtf] = m_BPhiExtrapolation;
  m_etaExtrapolationLUTs[tftype::omtf_pos] = m_OEtaExtrapolation;
  m_etaExtrapolationLUTs[tftype::omtf_neg] = m_OEtaExtrapolation;
  m_phiExtrapolationLUTs[tftype::omtf_pos] = m_OPhiExtrapolation;
  m_phiExtrapolationLUTs[tftype::omtf_neg] = m_OPhiExtrapolation;
  m_etaExtrapolationLUTs[tftype::emtf_pos] = m_FEtaExtrapolation;
  m_etaExtrapolationLUTs[tftype::emtf_neg] = m_FEtaExtrapolation;
  m_phiExtrapolationLUTs[tftype::emtf_pos] = m_FPhiExtrapolation;
  m_phiExtrapolationLUTs[tftype::emtf_neg] = m_FPhiExtrapolation;

  m_caloInputsToDisable = microGMTParamsHelper->caloInputsToDisable();
  m_maskedCaloInputs = microGMTParamsHelper->maskedCaloInputs();
}

int
l1t::MicroGMTIsolationUnit::getCaloIndex(MicroGMTConfiguration::InterMuon& mu) const
{
  // handle the wrap-around of phi:
  int phi = (mu.hwGlobalPhi() + mu.hwDPhi())%576;
  if (phi < 0) {
    phi = 576+phi;
  }

  int phiIndex = m_IdxSelMemPhi->lookup(phi);
  int eta = mu.hwEta()+mu.hwDEta();
  eta = MicroGMTConfiguration::getTwosComp(eta, 9);
  int etaIndex = m_IdxSelMemEta->lookup(eta);
  mu.setHwCaloEta(etaIndex);
  mu.setHwCaloPhi(phiIndex);

  return phiIndex + etaIndex*36;
}

void
l1t::MicroGMTIsolationUnit::extrapolateMuons(MicroGMTConfiguration::InterMuonList& inputmuons) const {
  int outputShiftPhi = 3;
  int outputShiftEta = 3;
  if (m_fwVersion >= 0x4010000) {
    outputShiftPhi = 2;
    outputShiftEta = 0;
  }

  for (auto &mu : inputmuons) {
    // get input format
    std::shared_ptr<MicroGMTExtrapolationLUT> phiExtrapolationLUT = m_phiExtrapolationLUTs.at(mu->trackFinderType());
    int ptRedInWidth = phiExtrapolationLUT->getPtRedInWidth();
    int ptMask = (1 << ptRedInWidth) - 1;
    int etaRedInWidth = phiExtrapolationLUT->getEtaRedInWidth();
    int redEtaShift = 8 - etaRedInWidth;

    // only use LSBs of pt:
    int ptRed = mu->hwPt() & ptMask;
    // here we drop the LSBs and mask the MSB
    int etaAbsRed = (std::abs(mu->hwEta()) >> redEtaShift) & ((1 << etaRedInWidth) - 1);

    int deltaPhi = 0;
    int deltaEta = 0;

    if (mu->hwPt() < (1 << ptRedInWidth)) { // extrapolation only for "low" pT muons
      int sign = 1;
      if (mu->hwSign() == 1) {
        sign = -1;
      }
      deltaPhi = (phiExtrapolationLUT->lookup(etaAbsRed, ptRed) << outputShiftPhi) * sign;
      deltaEta = (m_etaExtrapolationLUTs.at(mu->trackFinderType())->lookup(etaAbsRed, ptRed) << outputShiftEta);
      if (mu->hwEta() > 0) {
        deltaEta *= -1;
      }
    }

    mu->setExtrapolation(deltaEta, deltaPhi);
  }
}

void
l1t::MicroGMTIsolationUnit::calculate5by1Sums(const MicroGMTConfiguration::CaloInputCollection& inputs, int bx)
{
  m_5by1TowerSums.clear();
  if (inputs.size(bx) == 0) return;

  for (int iphi = 0; iphi < 36; ++iphi) {
    int iphiIndexOffset = iphi*28;
    // ieta = 0 (tower -28) and ieta = 1 (tower 27)
    // 3by1 and 4by1 sums
    for (int ieta = 0; ieta < 2; ++ieta) {
      int sum = 0;
      for (int dIEta = 0-ieta; dIEta <= 2; ++dIEta) {
        if (m_caloInputsToDisable.test(ieta+dIEta) || m_maskedCaloInputs.test(ieta+dIEta)) continue; // only process if input link is enabled and not masked
        sum += inputs.at(bx, iphiIndexOffset+dIEta).etBits();
      }
      m_5by1TowerSums.push_back(sum);
    }
    // 5by1 sums
    for (int ieta = 2; ieta < 26; ++ieta) {
      int sum = 0;
      for (int dIEta = -2; dIEta <= 2; ++dIEta) {
        if (m_caloInputsToDisable.test(ieta+dIEta) || m_maskedCaloInputs.test(ieta+dIEta)) continue; // only process if input link is enabled and not masked
        sum += inputs.at(bx, iphiIndexOffset+dIEta).etBits();
      }
      m_5by1TowerSums.push_back(sum);
    }
    // ieta = 26 (tower 27) and ieta = 27 (tower 28)
    // 4by1 and 3by1 sums
    for (int ieta = 26; ieta < 28; ++ieta) {
      int sum = 0;
      for (int dIEta = -2; dIEta <= 27-ieta; ++dIEta) {
        if (m_caloInputsToDisable.test(ieta+dIEta) || m_maskedCaloInputs.test(ieta+dIEta)) continue; // only process if input link is enabled and not masked
        sum += inputs.at(bx, iphiIndexOffset+dIEta).etBits();
      }
      m_5by1TowerSums.push_back(sum);
    }
  }

  m_initialSums = true;
}


int
l1t::MicroGMTIsolationUnit::calculate5by5Sum(unsigned index) const
{
  if (index > m_5by1TowerSums.size()) {
    edm::LogWarning("energysum out of bounds!");
    return 0;
  }
  // phi wrap around:
  int returnSum = 0;
  for (int dIPhi = -2; dIPhi <= 2; ++dIPhi) {
    int currIndex = (index + dIPhi*28)%1008; // wrap-around at top
    if (currIndex < 0) currIndex = 1008+currIndex;
    if ((unsigned)currIndex < m_5by1TowerSums.size()) {
      returnSum += m_5by1TowerSums[currIndex];
    } else {
      edm::LogWarning("energysum out of bounds!");
    }
  }
  return std::min(31, returnSum);
}

void
l1t::MicroGMTIsolationUnit::isolate(MicroGMTConfiguration::InterMuonList& muons) const
{
  for (auto& mu : muons) {
    int caloIndex = getCaloIndex(*mu);
    int energySum = calculate5by5Sum(caloIndex);
    mu->setHwIsoSum(energySum);

    int absIso = m_AbsIsoCheckMem->lookup(energySum);
    int relIso = m_RelIsoCheckMem->lookup(energySum, mu->hwPt());

    mu->setHwRelIso(relIso);
    mu->setHwAbsIso(absIso);
  }
}

void l1t::MicroGMTIsolationUnit::setTowerSums(const MicroGMTConfiguration::CaloInputCollection& inputs, int bx) {
  m_towerEnergies.clear();
  if (bx < inputs.getFirstBX() || bx > inputs.getLastBX()) return;
  if (inputs.size(bx) == 0) return;
  for (auto input = inputs.begin(bx); input != inputs.end(bx); ++input) {
    if (m_caloInputsToDisable.test(input->hwEta()) || m_maskedCaloInputs.test(input->hwEta())) {
      continue; // only process if input link is enabled and not masked
    }
    if ( input->etBits() != 0 ) {
      m_towerEnergies[input->hwEta()*36+input->hwPhi()] = input->etBits();
    }
  }

  m_initialSums = true;

}

void l1t::MicroGMTIsolationUnit::isolatePreSummed(MicroGMTConfiguration::InterMuonList& muons) const
{
  for (const auto &mu : muons) {
    int caloIndex = getCaloIndex(*mu);
    int energySum = 0;
    if (m_towerEnergies.count(caloIndex) == 1) {
      energySum = m_towerEnergies.at(caloIndex);
    }

    mu->setHwIsoSum(energySum);

    int absIso = m_AbsIsoCheckMem->lookup(energySum);
    int relIso = m_RelIsoCheckMem->lookup(energySum, mu->hwPt());

    mu->setHwRelIso(relIso);
    mu->setHwAbsIso(absIso);
  }

}
