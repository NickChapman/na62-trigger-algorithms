/*
 * MUVAlgo.cpp
 *
 *  Created on: 24 Mar 2016
 *      Author: lorenza
 */

#include "MUVAlgo.h"

#include <eventBuilding/Event.h>
#include <eventBuilding/SourceIDManager.h>
#include <l0/MEPFragment.h>
#include <l0/Subevent.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <options/Logging.h>

#include "../common/decoding/TrbFragmentDecoder.h"
#include "L1TriggerProcessor.h"

namespace na62 {

uint MUV3Algo::AlgoID_; //0 for CHOD, 1 for RICH, 2 for KTAG, 3 for LAV, 4 for IRCSAC, 5 for Straw, 6 for MUV3, 7 for NewCHOD
uint MUV3Algo::AlgoLogic_[16];
uint MUV3Algo::AlgoRefTimeSourceID_[16];
double MUV3Algo::AlgoOnlineTimeWindow_[16];

MUV3ParsConfFile* MUV3Algo::InfoMUV3_ = MUV3ParsConfFile::GetInstance();
int * MUV3Algo::PmtGeo_ = InfoMUV3_->getGeoPMTMap();

void MUV3Algo::initialize(uint i, l1MUV &l1MUV3Struct) {

	AlgoID_ = l1MUV3Struct.configParams.l1TrigMaskID;
	AlgoLogic_[i] = l1MUV3Struct.configParams.l1TrigLogic;
	AlgoRefTimeSourceID_[i] = l1MUV3Struct.configParams.l1TrigRefTimeSourceID; //0 for L0TP, 1 for CHOD, 2 for RICH
	AlgoOnlineTimeWindow_[i] = l1MUV3Struct.configParams.l1TrigOnlineTimeWindow;
//	LOG_INFO("MUV3 mask: " << i << " logic " << AlgoLogic_[i] << " refTimeSourceID " << AlgoRefTimeSourceID_[i] << " online time window " << AlgoOnlineTimeWindow_[i]);
}

uint_fast8_t MUV3Algo::processMUV3Trigger0(uint l0MaskID, DecoderHandler& decoder, L1InfoToStorage* l1Info) {

	using namespace l0;

//	LOG_INFO("MUV3: event timestamp = " << std::hex << decoder.getDecodedEvent()->getTimestamp() << std::dec);
//	LOG_INFO("MUV3: event reference fine time from L0TP " << std::hex << (uint)decoder.getDecodedEvent()->getFinetime() << std::dec);

	/*
	 * TODO: The same logic needs to be developed for RICHRefTime
	 */
	double refTimeL0TP = 0.;
	double averageCHODHitTime = 0.;
	bool isCHODRefTime = false;
	if (!AlgoRefTimeSourceID_[l0MaskID]) {
		refTimeL0TP = decoder.getDecodedEvent()->getFinetime() * 0.097464731802;
//		LOG_INFO("L1 reference finetime from L0TP (ns) " << refTimeL0TP);
	} else if (AlgoRefTimeSourceID_[l0MaskID] == 1) {
		if (l1Info->isL1CHODProcessed()) {
			isCHODRefTime = 1;
			averageCHODHitTime = l1Info->getCHODAverageTime();
		} else
			LOG_ERROR("MUV3Algo.cpp: Not able to use averageCHODHitTime as Reference Time even if requested!");
	} else
		LOG_ERROR("L1 Reference Time Source ID not recognised !!");

	TrbFragmentDecoder& muv3Packet = (TrbFragmentDecoder&) decoder.getDecodedMUV3Fragment(0);

	if (!muv3Packet.isReady() || muv3Packet.isBadFragment()) {
		LOG_ERROR("MUV3: This looks like a Bad Packet!!!! ");
		l1Info->setL1MUV3BadData();
		return 0;
	}

	uint nTiles = 0;
	uint pmtID[2] = { 0 };
	bool tileID[152] = { false };

	/**
	 * Get Arrays with hit Info
	 */

	const uint64_t* const edgeTime = muv3Packet.getTimes();
	const uint_fast8_t* const edgeChID = muv3Packet.getChIDs();
	const bool* const edgeIsLeading = muv3Packet.getIsLeadings();
	const uint_fast8_t* const edgeTdcID = muv3Packet.getTdcIDs();
	double time, dtL0TP, dtCHOD;
	uint pmtID1, pmtID2;

	uint numberOfEdgesOfCurrentBoard = muv3Packet.getNumberOfEdgesStored();

	if (!numberOfEdgesOfCurrentBoard)
		l1Info->setL1MUV3EmptyPacket();

//	LOG_INFO("MUV3: Tel62 ID " << muv3Packet.getFragmentNumber() << " - Number of Edges found " << numberOfEdgesOfCurrentBoard);

	for (uint iEdge = 0; iEdge != numberOfEdgesOfCurrentBoard; iEdge++) {
//		LOG_INFO("Edge " << iEdge << " ID " << edgeIsLeading[iEdge]);
//		LOG_INFO("Edge " << iEdge << " chID " << (uint) edgeChID[iEdge]);
//		LOG_INFO("Edge " << iEdge << " tdcID " << (uint) edgeTdcID[iEdge]);
//		LOG_INFO("Edge " << iEdge << " time " << std::hex << edgeTime[iEdge] << std::dec);

		/**
		 * Process leading edges only
		 *
		 */
		if (edgeIsLeading[iEdge]) { //in time with trigger
			const int roChID = (edgeTdcID[iEdge] * 32) + edgeChID[iEdge];
			if (PmtGeo_[roChID] > 151)
				pmtID1 = PmtGeo_[roChID] - 200;
			else
				pmtID1 = PmtGeo_[roChID];

			if (pmtID1 < 144) {
				time = (edgeTime[iEdge] - decoder.getDecodedEvent()->getTimestamp() * 256.) * 0.097464731802;
//				LOG_INFO("Edge time (ns) " << time);

				if (!isCHODRefTime) {
					dtL0TP = fabs(time - refTimeL0TP);
					dtCHOD = -1.0e+28;
				} else
					dtCHOD = fabs(time - averageCHODHitTime);
//				LOG_INFO("dtL0TP " << dtL0TP << " dtCHOD " << dtCHOD);

				if ((!isCHODRefTime && dtL0TP < AlgoOnlineTimeWindow_[l0MaskID])
						|| (isCHODRefTime && dtCHOD < AlgoOnlineTimeWindow_[l0MaskID])) {
					tileID[pmtID1] = 1;
				}
			}
		}
	}

	for (uint i = 0; i != 152; ++i) {
		if (tileID[i]) {
			nTiles++;
			if (nTiles > 2) {
				l1Info->setL1MUV3NTiles(nTiles);
				l1Info->setL1MUV3Processed();
//				LOG_INFO("MUVAlgo: Returning nTiles > 2 !!!!");
				return 1;
			}
			pmtID[nTiles - 1] = i;
		}
	}

//	LOG_INFO("MUV: Analysing Event " << decoder.getDecodedEvent()->getEventNumber());
//	LOG_INFO("Timestamp " << std::hex << decoder.getDecodedEvent()->getTimestamp() << std::dec);
//	LOG_INFO("Number of Tiles " << nTiles);

	if (!nTiles || (nTiles == 1)) {
		l1Info->setL1MUV3NTiles(nTiles);
		l1Info->setL1MUV3Processed();
		return 0;
	} else if ((fabs(pmtID[0] - pmtID[1]) == 1 && ((pmtID[0] + pmtID[1]) % 24) != 23) || fabs(pmtID[0] - pmtID[1]) == 12) {
		l1Info->setL1MUV3NTiles(nTiles);
		l1Info->setL1MUV3Processed();
		return 0;
	} else {
		l1Info->setL1MUV3NTiles(nTiles);
		l1Info->setL1MUV3Processed();
		return 1;
	}

//	LOG_ERROR("MUV3Algo.cpp -> Attention: no case is provided for this !!!!");
}

uint_fast8_t MUV3Algo::processMUV3Trigger1(uint l0MaskID, DecoderHandler& decoder, L1InfoToStorage* l1Info) {

	using namespace l0;

//	LOG_INFO("MUV3: event timestamp = " << std::hex << decoder.getDecodedEvent()->getTimestamp() << std::dec);
//	LOG_INFO("MUV3: event reference fine time from L0TP " << std::hex << (uint)decoder.getDecodedEvent()->getFinetime() << std::dec);

	/*
	 * TODO: The same logic needs to be developed for RICHRefTime
	 */
	double refTimeL0TP = 0.;
	double averageCHODHitTime = 0.;
	bool isCHODRefTime = false;
	if (!AlgoRefTimeSourceID_[l0MaskID]) {
		refTimeL0TP = decoder.getDecodedEvent()->getFinetime() * 0.097464731802;
//		LOG_INFO("L1 reference finetime from L0TP (ns) " << refTimeL0TP);
	} else if (AlgoRefTimeSourceID_[l0MaskID] == 1) {
		if (l1Info->isL1CHODProcessed()) {
			isCHODRefTime = 1;
			averageCHODHitTime = l1Info->getCHODAverageTime();
		} else
			LOG_ERROR("MUV3Algo.cpp: Not able to use averageCHODHitTime as Reference Time even if requested!");
	} else
		LOG_ERROR("L1 Reference Time Source ID not recognised !!");

	TrbFragmentDecoder& muv3Packet = (TrbFragmentDecoder&) decoder.getDecodedMUV3Fragment(0);

	if (!muv3Packet.isReady() || muv3Packet.isBadFragment()) {
		LOG_ERROR("MUV3: This looks like a Bad Packet!!!! ");
		l1Info->setL1MUV3BadData();
		return 0;
	}

	/**
	 * Get Arrays with hit Info
	 */

	const uint64_t* const edgeTime = muv3Packet.getTimes();
	const uint_fast8_t* const edgeChID = muv3Packet.getChIDs();
	const bool* const edgeIsLeading = muv3Packet.getIsLeadings();
	const uint_fast8_t* const edgeTdcID = muv3Packet.getTdcIDs();
	double time, dtL0TP, dtCHOD;
	uint pmtID1, pmtID2;

	uint numberOfEdgesOfCurrentBoard = muv3Packet.getNumberOfEdgesStored();

	if (!numberOfEdgesOfCurrentBoard)
		l1Info->setL1MUV3EmptyPacket();

//	LOG_INFO("MUV3: Tel62 ID " << muv3Packet.getFragmentNumber() << " - Number of Edges found " << numberOfEdgesOfCurrentBoard);

	for (uint iEdge = 0; iEdge != numberOfEdgesOfCurrentBoard; iEdge++) {
//		LOG_INFO("Edge " << iEdge << " ID " << edgeIsLeading[iEdge]);
//		LOG_INFO("Edge " << iEdge << " chID " << (uint) edgeChID[iEdge]);
//		LOG_INFO("Edge " << iEdge << " tdcID " << (uint) edgeTdcID[iEdge]);
//		LOG_INFO("Edge " << iEdge << " time " << std::hex << edgeTime[iEdge] << std::dec);

		/**
		 * Process leading edges only
		 *
		 */
		if (edgeIsLeading[iEdge]) {
			const int roChID1 = (edgeTdcID[iEdge] * 32) + edgeChID[iEdge];
			if (PmtGeo_[roChID1] > 151)
				pmtID1 = PmtGeo_[roChID1] - 200;
			else
				pmtID1 = PmtGeo_[roChID1];
			for (uint jEdge = 0; jEdge != numberOfEdgesOfCurrentBoard; jEdge++) {
				if (edgeIsLeading[jEdge] && jEdge != iEdge) {
					const int roChID2 = (edgeTdcID[jEdge] * 32) + edgeChID[jEdge];
					if (PmtGeo_[roChID2] > 151)
						pmtID2 = PmtGeo_[roChID2] - 200;
					else
						pmtID2 = PmtGeo_[roChID2];

					if (pmtID1 < 144 && pmtID2 < 144) {
						if (((pmtID1 % 12) <= 5 && (pmtID2 % 12) >= 6) || ((pmtID2 % 12) <= 5 && (pmtID1 % 12) >= 6)) {
//							LOG_INFO("left and right! pmt1= " << pmtID1 << " pmt2= " << pmtID2);
							/*
							 * Must add time constraints!!!!!
							 */
							l1Info->setL1MUV3Processed();
//							LOG_INFO("MUVAlgo: Returning with left-right hits !!!!");
							return 1;
						}
					}
				}
			}
		}
	}

//	LOG_INFO("MUV: Analysing Event " << decoder.getDecodedEvent()->getEventNumber());
//	LOG_INFO("Timestamp " << std::hex << decoder.getDecodedEvent()->getTimestamp() << std::dec);
//	LOG_INFO("No left-right hits have been found ");

	l1Info->setL1MUV3Processed();
	return 0;
}

uint_fast8_t MUV3Algo::processMUV3Trigger2(uint l0MaskID, DecoderHandler& decoder, L1InfoToStorage* l1Info) {

	using namespace l0;

//	LOG_INFO("MUV3: event timestamp = " << std::hex << decoder.getDecodedEvent()->getTimestamp() << std::dec);
//	LOG_INFO("MUV3: event reference fine time from L0TP " << std::hex << (uint)decoder.getDecodedEvent()->getFinetime() << std::dec);

	/*
	 * TODO: The same logic needs to be developed for RICHRefTime
	 */
	double refTimeL0TP = 0.;
	double averageCHODHitTime = 0.;
	bool isCHODRefTime = false;
	if (!AlgoRefTimeSourceID_[l0MaskID]) {
		refTimeL0TP = decoder.getDecodedEvent()->getFinetime() * 0.097464731802;
//		LOG_INFO("L1 reference finetime from L0TP (ns) " << refTimeL0TP);
	} else if (AlgoRefTimeSourceID_[l0MaskID] == 1) {
		if (l1Info->isL1CHODProcessed()) {
			isCHODRefTime = 1;
			averageCHODHitTime = l1Info->getCHODAverageTime();
		} else
			LOG_ERROR("MUV3Algo.cpp: Not able to use averageCHODHitTime as Reference Time even if requested!");
	} else
		LOG_ERROR("L1 Reference Time Source ID not recognised !!");

	TrbFragmentDecoder& muv3Packet = (TrbFragmentDecoder&) decoder.getDecodedMUV3Fragment(0);

	if (!muv3Packet.isReady() || muv3Packet.isBadFragment()) {
		LOG_ERROR("MUV3: This looks like a Bad Packet!!!! ");
		l1Info->setL1MUV3BadData();
		return 0;
	}

	/**
	 * Get Arrays with hit Info
	 */
	const uint64_t* const edgeTime = muv3Packet.getTimes();
	const uint_fast8_t* const edgeChID = muv3Packet.getChIDs();
	const bool* const edgeIsLeading = muv3Packet.getIsLeadings();
	const uint_fast8_t* const edgeTdcID = muv3Packet.getTdcIDs();
	double time, dtL0TP, dtCHOD;
	uint pmtID1, pmtID2;

	uint numberOfEdgesOfCurrentBoard = muv3Packet.getNumberOfEdgesStored();

	if (!numberOfEdgesOfCurrentBoard)
		l1Info->setL1MUV3EmptyPacket();
//	LOG_INFO("MUV3: Tel62 ID " << muv3Packet.getFragmentNumber() << " - Number of Edges found " << numberOfEdgesOfCurrentBoard);

	for (uint iEdge = 0; iEdge != numberOfEdgesOfCurrentBoard; iEdge++) {
//		LOG_INFO("Edge " << iEdge << " ID " << edgeIsLeading[iEdge]);
//		LOG_INFO("Edge " << iEdge << " chID " << (uint) edgeChID[iEdge]);
//		LOG_INFO("Edge " << iEdge << " tdcID " << (uint) edgeTdcID[iEdge]);
//		LOG_INFO("Edge " << iEdge << " time " << std::hex << edgeTime[iEdge] << std::dec);

		if (edgeIsLeading[iEdge]) {
			const int roChID1 = (edgeTdcID[iEdge] * 32) + edgeChID[iEdge];
			if (PmtGeo_[roChID1] > 151)
				pmtID1 = PmtGeo_[roChID1] - 200;
			else
				pmtID1 = PmtGeo_[roChID1];
			for (uint jEdge = 0; jEdge != numberOfEdgesOfCurrentBoard; jEdge++) {
				if (edgeIsLeading[jEdge] && jEdge != iEdge) {
					const int roChID2 = (edgeTdcID[jEdge] * 32) + edgeChID[jEdge];
					if (PmtGeo_[roChID2] > 151)
						pmtID2 = PmtGeo_[roChID2] - 200;
					else
						pmtID2 = PmtGeo_[roChID2];

					if (pmtID1 < 144 && pmtID2 < 144) {
						if ((fabs(pmtID1 - pmtID2) == 1 && ((pmtID1 + pmtID2) % 24) != 23) || fabs(pmtID1 - pmtID2) == 12) {
//							LOG_INFO("neighbours! pmt1= " << pmtID1 << " pmt2= " << pmtID2);
							/*
							 * Must add time constraints!!!!!
							 */
							l1Info->setL1MUV3Processed();
//							LOG_INFO("MUVAlgo: Returning with neighbour hits !!!!");
							return 1;
						}
					}
				}
			}
		}
	}

//	LOG_INFO("MUV: Analysing Event " << decoder.getDecodedEvent()->getEventNumber());
//	LOG_INFO("Timestamp " << std::hex << decoder.getDecodedEvent()->getTimestamp() << std::dec);
//	LOG_INFO("No neighbour hits have been found ");

	l1Info->setL1MUV3Processed();
	return 0;
}

void MUV3Algo::writeData(L1Algo* algoPacket, uint l0MaskID, L1InfoToStorage* l1Info) {

	if (AlgoID_ != algoPacket->algoID)
		LOG_ERROR("Algo ID does not match with Algo ID already written within the packet!");

	algoPacket->algoID = AlgoID_;
	algoPacket->onlineTimeWindow = (uint) AlgoOnlineTimeWindow_[l0MaskID];
//	algoPacket->qualityFlags = (l1Info->isL1MUV3Processed() << 6) | (l1Info->isL1MUV3EmptyPacket() << 4) | (l1Info->isL1MUV3BadData() << 2) | AlgoRefTimeSourceID_[l0MaskID];
	algoPacket->qualityFlags = (l1Info->isL1MUV3Processed() << 6) | (l1Info->isL1MUV3EmptyPacket() << 4) | (l1Info->isL1MUV3BadData() << 2)
			| ((uint) l1Info->getL1MUV3TrgWrd());

	algoPacket->l1Data[0] = l1Info->getL1MUV3NTiles();
	if (!AlgoRefTimeSourceID_[l0MaskID]) {
		algoPacket->l1Data[1] = l1Info->getL1RefTimeL0TP();
	} else if (AlgoRefTimeSourceID_[l0MaskID] == 1) {
		algoPacket->l1Data[1] = l1Info->getCHODAverageTime(); //this is a double!!!
	} else
		LOG_ERROR("L1 Reference Time Source ID not recognised !!");

	algoPacket->numberOfWords = (sizeof(L1Algo) / 4.);
//	LOG_INFO("l0MaskID " << l0MaskID);
//	LOG_INFO("algoID " << (uint)algoPacket->algoID);
//	LOG_INFO("quality Flags " << (uint)algoPacket->qualityFlags);
//	LOG_INFO("online TW " << (uint)algoPacket->onlineTimeWindow);
//	LOG_INFO("Data Words " << algoPacket->l1Data[0] << " " << algoPacket->l1Data[1]);

}

}
/* namespace na62 */

