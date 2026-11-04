#pragma once
/*
 * CBhMriVolume - BrainHifi copy of an anatomical MRI volume (CAnaData).
 *
 * Voxel values are the 16-bit raw values of CAnaData::m_pSA; the low
 * 12 bits hold the intensity and the high bits the marks, so values are
 * read through m_uValueMask (default 0x0FFF).
 * Linear index follows CAnaData::xyz2dataIndx() for the stored layout
 * (default XYZ: x*nH*nD + y*nD + z, z fastest).
 * Head coordinates (mm) follow CAnaData::VoxelToWorld():
 *     mm = voxel * spacing + m_psImgCntMm_XYZido
 * Voxel (0,0,0) is the subject's right-bottom-front corner:
 *     x: right -> left, y: inferior -> superior, z: anterior -> posterior.
 */
#include "BhExport.h"
#include "CBrainHifiData.h"


class BH_API CBhMriVolume : public CBhANAVolume
{
public:
	CBhMriVolume();
	~CBhMriVolume();
public:

	bool create(int64_t nW, int64_t nH, int64_t nD,
	            double dSpaceX, double dSpaceY, double dSpaceZ,
	            const double dOriginMm[3], EBhLayout uLayout = EBhLayout::XYZ);
	void clear();
	bool isEmpty() const;

	uint16_t* data() { return m_vRaw.empty() ? nullptr : m_vRaw.data(); }
	const uint16_t* data() const { return m_vRaw.empty() ? nullptr : m_vRaw.data(); }
	int64_t voxelCount() const { return m_nW * m_nH * m_nD; }

	int64_t index(int64_t x, int64_t y, int64_t z) const;
	bool isInside(int64_t x, int64_t y, int64_t z) const;
	uint16_t valueAt(int64_t x, int64_t y, int64_t z) const;

	void voxelToHeadMm(double vx, double vy, double vz, double dMm[3]) const;
	void headMmToVoxel(const double dMm[3], double dVxl[3]) const;

	/// Intensity at the given percentiles (0..100) of non-zero voxels.
	bool intensityPercentiles(double dLowPct, double dHighPct, double& dLow, double& dHigh) const;
	uint16_t maxValue() const;

//public:
//	int64_t m_nW = 0;
//	int64_t m_nH = 0;
//	int64_t m_nD = 0;
//	double m_dSpaceX = 1.0;
//	double m_dSpaceY = 1.0;
//	double m_dSpaceZ = 1.0;
//	double m_dOriginMm[3] = { 0.0, 0.0, 0.0 };
//	EBhLayout m_uLayout = EBhLayout::XYZ;
//	uint16_t m_uValueMask = 0x0FFF;
//
//private:
//	std::vector<uint16_t> m_vRaw;
};
