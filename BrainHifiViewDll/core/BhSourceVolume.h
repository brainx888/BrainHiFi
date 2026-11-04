#pragma once
/*
 * CBhSourceVolume - BrainHifi copy of a MEG volumetric source image
 * (CVolumSourcImag::m_pTimeVoxel).
 *
 * Data layout (as CVolumSourcImag):  [time][x][y][z][item]
 *     index = t*nW*nH*nD*nItems + (x*nH*nD + y*nD + z)*nItems + item
 * Grid node (x,y,z) sits at  m_dOriginMm + (x,y,z)*step  in the same
 * head-centred mm frame as CAnaData (CVolumSourcImag::m_psImgCntMm_XYZido).
 * Nearest lookup truncates like CVolumSourcImag::getPositionDataPoint().
 */
#include "BhExport.h"	
#include "CBrainHifiData.h"
#include <cstdint>
#include <string>
#include <vector>

class BH_API CBhSourceVolume : public CBhFunVolume
{
public:
	CBhSourceVolume();
	~CBhSourceVolume();

public:

	bool create(int64_t nTime, int64_t nW, int64_t nH, int64_t nD, int64_t nItems,
	            double dStepX, double dStepY, double dStepZ, const double dOriginMm[3]);
	void clear();
	bool isEmpty() const;

	double* data() { return m_vData.empty() ? nullptr : m_vData.data(); }
	const double* data() const { return m_vData.empty() ? nullptr : m_vData.data(); }
	int64_t valueCount() const { return m_nTime * m_nW * m_nH * m_nD * m_nItems; }

	int64_t index(int64_t iTime, int64_t x, int64_t y, int64_t z, int64_t iItem) const;
	bool isInside(int64_t x, int64_t y, int64_t z) const;
	double valueAt(int64_t iTime, int64_t x, int64_t y, int64_t z, int64_t iItem) const;

	void voxelToHeadMm(double vx, double vy, double vz, double dMm[3]) const;
	void headMmToVoxel(const double dMm[3], double dVxl[3]) const;

	/// Value at a head-coordinate point; false when outside the source grid.
	bool sampleHeadMm(int64_t iTime, int64_t iItem, const double dMm[3], bool bTrilinear, double& dValue) const;

	/// Min / max over one time slice and item.
	bool valueRange(int64_t iTime, int64_t iItem, double& dMin, double& dMax) const;
	/// Grid position of the largest |value| (or largest value when bAbs is false).
	bool peakVoxel(int64_t iTime, int64_t iItem, bool bAbs, int64_t nVxl[3], double& dValue) const;

//	int64_t m_nTime = 0;
//	int64_t m_nW = 0;
//	int64_t m_nH = 0;
//	int64_t m_nD = 0;
//	int64_t m_nItems = 0;
//	int64_t m_nTimeCurrent = 0;
//	int64_t m_nItemActive = 0;
//	double m_dStepX = 1.0;
//	double m_dStepY = 1.0;
//	double m_dStepZ = 1.0;
//	double m_dOriginMm[3] = { 0.0, 0.0, 0.0 };
//	std::wstring m_sName;
//
//private:
//	std::vector<double> m_vData;
};
