#include "BhMriVolume.h"
#include <algorithm>

CBhMriVolume::CBhMriVolume()
{
 }
CBhMriVolume::~CBhMriVolume()
{

}

bool CBhMriVolume::create(int64_t nW, int64_t nH, int64_t nD,
                          double dSpaceX, double dSpaceY, double dSpaceZ,
                          const double dOriginMm[3], EBhLayout uLayout)
{
	clear();
	if (nW <= 0 || nH <= 0 || nD <= 0) return false;
	if (dSpaceX <= 0.0 || dSpaceY <= 0.0 || dSpaceZ <= 0.0) return false;
	m_nW = nW;
	m_nH = nH;
	m_nD = nD;
	m_dSpaceX = dSpaceX;
	m_dSpaceY = dSpaceY;
	m_dSpaceZ = dSpaceZ;
	for (int i = 0; i < 3; i++) m_dOriginMm[i] = dOriginMm ? dOriginMm[i] : 0.0;
	m_uLayout = uLayout;
	m_vRaw.assign((size_t)(nW * nH * nD), 0);
	return true;
}

void CBhMriVolume::clear()
{
	m_vRaw.clear();
	m_vRaw.shrink_to_fit();
	m_nW = m_nH = m_nD = 0;
}

bool CBhMriVolume::isEmpty() const
{
	return m_vRaw.empty();
}

int64_t CBhMriVolume::index(int64_t x, int64_t y, int64_t z) const
{
	switch (m_uLayout) {
	case EBhLayout::XZY: return x * m_nD * m_nH + z * m_nH + y;
	case EBhLayout::YXZ: return y * m_nW * m_nD + x * m_nD + z;
	case EBhLayout::YZX: return y * m_nD * m_nW + z * m_nW + x;
	case EBhLayout::ZXY: return z * m_nW * m_nH + x * m_nH + y;
	case EBhLayout::ZYX: return z * m_nH * m_nW + y * m_nW + x;
	case EBhLayout::XYZ:
	default:             return x * m_nH * m_nD + y * m_nD + z;
	}
}

bool CBhMriVolume::isInside(int64_t x, int64_t y, int64_t z) const
{
	return x >= 0 && y >= 0 && z >= 0 && x < m_nW && y < m_nH && z < m_nD;
}

uint16_t CBhMriVolume::valueAt(int64_t x, int64_t y, int64_t z) const
{
	if (m_vRaw.empty() || !isInside(x, y, z)) return 0;
	return (uint16_t)(m_vRaw[(size_t)index(x, y, z)] & m_uValueMask);
}

void CBhMriVolume::voxelToHeadMm(double vx, double vy, double vz, double dMm[3]) const
{
	dMm[0] = vx * m_dSpaceX + m_dOriginMm[0];
	dMm[1] = vy * m_dSpaceY + m_dOriginMm[1];
	dMm[2] = vz * m_dSpaceZ + m_dOriginMm[2];
}

void CBhMriVolume::headMmToVoxel(const double dMm[3], double dVxl[3]) const
{
	dVxl[0] = (dMm[0] - m_dOriginMm[0]) / m_dSpaceX;
	dVxl[1] = (dMm[1] - m_dOriginMm[1]) / m_dSpaceY;
	dVxl[2] = (dMm[2] - m_dOriginMm[2]) / m_dSpaceZ;
}

uint16_t CBhMriVolume::maxValue() const
{
	uint16_t uMax = 0;
	for (uint16_t v : m_vRaw) uMax = (std::max)(uMax, (uint16_t)(v & m_uValueMask));
	return uMax;
}

bool CBhMriVolume::intensityPercentiles(double dLowPct, double dHighPct, double& dLow, double& dHigh) const
{
	dLow = 0.0;
	dHigh = 0.0;
	if (m_vRaw.empty()) return false;
	std::vector<int64_t> vHist((size_t)m_uValueMask + 1, 0);
	int64_t nCount = 0;
	for (uint16_t v : m_vRaw) {
		const uint16_t uVal = (uint16_t)(v & m_uValueMask);
		if (uVal == 0) continue;
		vHist[uVal]++;
		nCount++;
	}
	if (nCount == 0) return false;
	const double dLowCount = nCount * (std::max)(0.0, (std::min)(100.0, dLowPct)) / 100.0;
	const double dHighCount = nCount * (std::max)(0.0, (std::min)(100.0, dHighPct)) / 100.0;
	int64_t nCum = 0;
	bool bLow = false;
	for (size_t iVal = 0; iVal < vHist.size(); iVal++) {
		nCum += vHist[iVal];
		if (!bLow && nCum >= dLowCount) { dLow = (double)iVal; bLow = true; }
		if (nCum >= dHighCount) { dHigh = (double)iVal; break; }
	}
	if (dHigh <= dLow) dHigh = dLow + 1.0;
	return true;
}
