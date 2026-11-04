#include "BhSourceVolume.h"
#include <algorithm>
#include <cmath>

CBhSourceVolume::CBhSourceVolume()
{

} 
CBhSourceVolume::~CBhSourceVolume()
{

} 


bool CBhSourceVolume::create(int64_t nTime, int64_t nW, int64_t nH, int64_t nD, int64_t nItems,
                             double dStepX, double dStepY, double dStepZ, const double dOriginMm[3])
{
	clear();
	if (nTime <= 0 || nW <= 0 || nH <= 0 || nD <= 0 || nItems <= 0) return false;
	if (dStepX <= 0.0 || dStepY <= 0.0 || dStepZ <= 0.0) return false;
	m_nTime = nTime;
	m_nW = nW;
	m_nH = nH;
	m_nD = nD;
	m_nItems = nItems;
	m_dStepX = dStepX;
	m_dStepY = dStepY;
	m_dStepZ = dStepZ;
	for (int i = 0; i < 3; i++) m_dOriginMm[i] = dOriginMm ? dOriginMm[i] : 0.0;
	m_vData.assign((size_t)valueCount(), 0.0);
	return true;
}

void CBhSourceVolume::clear()
{
	m_vData.clear();
	m_vData.shrink_to_fit();
	m_nTime = m_nW = m_nH = m_nD = m_nItems = 0;
	m_nTimeCurrent = m_nItemActive = 0;
}

bool CBhSourceVolume::isEmpty() const
{
	return m_vData.empty();
}

int64_t CBhSourceVolume::index(int64_t iTime, int64_t x, int64_t y, int64_t z, int64_t iItem) const
{
	const int64_t nVxl = x * m_nH * m_nD + y * m_nD + z;
	return iTime * m_nW * m_nH * m_nD * m_nItems + nVxl * m_nItems + iItem;
}

bool CBhSourceVolume::isInside(int64_t x, int64_t y, int64_t z) const
{
	return x >= 0 && y >= 0 && z >= 0 && x < m_nW && y < m_nH && z < m_nD;
}

double CBhSourceVolume::valueAt(int64_t iTime, int64_t x, int64_t y, int64_t z, int64_t iItem) const
{
	if (m_vData.empty() || !isInside(x, y, z)) return 0.0;
	if (iTime < 0 || iTime >= m_nTime || iItem < 0 || iItem >= m_nItems) return 0.0;
	return m_vData[(size_t)index(iTime, x, y, z, iItem)];
}

void CBhSourceVolume::voxelToHeadMm(double vx, double vy, double vz, double dMm[3]) const
{
	dMm[0] = vx * m_dStepX + m_dOriginMm[0];
	dMm[1] = vy * m_dStepY + m_dOriginMm[1];
	dMm[2] = vz * m_dStepZ + m_dOriginMm[2];
}

void CBhSourceVolume::headMmToVoxel(const double dMm[3], double dVxl[3]) const
{
	dVxl[0] = (dMm[0] - m_dOriginMm[0]) / m_dStepX;
	dVxl[1] = (dMm[1] - m_dOriginMm[1]) / m_dStepY;
	dVxl[2] = (dMm[2] - m_dOriginMm[2]) / m_dStepZ;
}

bool CBhSourceVolume::sampleHeadMm(int64_t iTime, int64_t iItem, const double dMm[3], bool bTrilinear, double& dValue) const
{
	dValue = 0.0;
	if (m_vData.empty()) return false;
	double dVxl[3];
	headMmToVoxel(dMm, dVxl);
	if (dVxl[0] < 0.0 || dVxl[1] < 0.0 || dVxl[2] < 0.0) return false;
	if (!bTrilinear) {
		const int64_t x = (int64_t)dVxl[0];
		const int64_t y = (int64_t)dVxl[1];
		const int64_t z = (int64_t)dVxl[2];
		if (!isInside(x, y, z)) return false;
		dValue = valueAt(iTime, x, y, z, iItem);
		return true;
	}
	if (dVxl[0] > (double)(m_nW - 1) || dVxl[1] > (double)(m_nH - 1) || dVxl[2] > (double)(m_nD - 1)) return false;
	const int64_t x0 = (std::min)((int64_t)dVxl[0], (std::max)((int64_t)0, m_nW - 2));
	const int64_t y0 = (std::min)((int64_t)dVxl[1], (std::max)((int64_t)0, m_nH - 2));
	const int64_t z0 = (std::min)((int64_t)dVxl[2], (std::max)((int64_t)0, m_nD - 2));
	const int64_t x1 = (std::min)(x0 + 1, m_nW - 1);
	const int64_t y1 = (std::min)(y0 + 1, m_nH - 1);
	const int64_t z1 = (std::min)(z0 + 1, m_nD - 1);
	const double fx = (std::max)(0.0, (std::min)(1.0, dVxl[0] - (double)x0));
	const double fy = (std::max)(0.0, (std::min)(1.0, dVxl[1] - (double)y0));
	const double fz = (std::max)(0.0, (std::min)(1.0, dVxl[2] - (double)z0));
	const double c00 = valueAt(iTime, x0, y0, z0, iItem) * (1 - fx) + valueAt(iTime, x1, y0, z0, iItem) * fx;
	const double c10 = valueAt(iTime, x0, y1, z0, iItem) * (1 - fx) + valueAt(iTime, x1, y1, z0, iItem) * fx;
	const double c01 = valueAt(iTime, x0, y0, z1, iItem) * (1 - fx) + valueAt(iTime, x1, y0, z1, iItem) * fx;
	const double c11 = valueAt(iTime, x0, y1, z1, iItem) * (1 - fx) + valueAt(iTime, x1, y1, z1, iItem) * fx;
	const double c0 = c00 * (1 - fy) + c10 * fy;
	const double c1 = c01 * (1 - fy) + c11 * fy;
	dValue = c0 * (1 - fz) + c1 * fz;
	return true;
}

bool CBhSourceVolume::valueRange(int64_t iTime, int64_t iItem, double& dMin, double& dMax) const
{
	dMin = dMax = 0.0;
	if (m_vData.empty() || iTime < 0 || iTime >= m_nTime || iItem < 0 || iItem >= m_nItems) return false;
	bool bFirst = true;
	for (int64_t x = 0; x < m_nW; x++)
		for (int64_t y = 0; y < m_nH; y++)
			for (int64_t z = 0; z < m_nD; z++) {
				const double v = m_vData[(size_t)index(iTime, x, y, z, iItem)];
				if (!std::isfinite(v)) continue;
				if (bFirst) { dMin = dMax = v; bFirst = false; }
				dMin = (std::min)(dMin, v);
				dMax = (std::max)(dMax, v);
			}
	return !bFirst;
}

bool CBhSourceVolume::peakVoxel(int64_t iTime, int64_t iItem, bool bAbs, int64_t nVxl[3], double& dValue) const
{
	nVxl[0] = nVxl[1] = nVxl[2] = 0;
	dValue = 0.0;
	if (m_vData.empty() || iTime < 0 || iTime >= m_nTime || iItem < 0 || iItem >= m_nItems) return false;
	bool bFirst = true;
	double dBest = 0.0;
	for (int64_t x = 0; x < m_nW; x++)
		for (int64_t y = 0; y < m_nH; y++)
			for (int64_t z = 0; z < m_nD; z++) {
				const double v = m_vData[(size_t)index(iTime, x, y, z, iItem)];
				if (!std::isfinite(v)) continue;
				const double dScore = bAbs ? std::fabs(v) : v;
				if (bFirst || dScore > dBest) {
					dBest = dScore;
					dValue = v;
					nVxl[0] = x; nVxl[1] = y; nVxl[2] = z;
					bFirst = false;
				}
			}
	return !bFirst;
}
