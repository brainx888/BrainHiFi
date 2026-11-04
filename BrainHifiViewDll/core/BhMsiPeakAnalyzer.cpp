#include "BhMsiPeakAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {

std::string fmt(const char* szFmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, szFmt);
	std::vsnprintf(buf, sizeof(buf), szFmt, args);
	va_end(args);
	return buf;
}

}

void CBhMsiPeakAnalyzer::fillRelative(const CBhMriVolume* pMri, SBhMsiPoint& pt)
{
	if (!pMri || pMri->isEmpty()) {
		for (int i = 0; i < 3; i++) { pt.dRelMm[i] = 0.0; pt.dBoxFrac[i] = 0.0; }
		pt.nMriValue = -1;
		return;
	}
	const int64_t nSize[3] = { pMri->m_nW, pMri->m_nH, pMri->m_nD };
	double dCentre[3], dLow[3], dHigh[3];
	pMri->voxelToHeadMm((pMri->m_nW - 1) * 0.5, (pMri->m_nH - 1) * 0.5, (pMri->m_nD - 1) * 0.5, dCentre);
	pMri->voxelToHeadMm(0.0, 0.0, 0.0, dLow);
	pMri->voxelToHeadMm((double)(pMri->m_nW - 1), (double)(pMri->m_nH - 1), (double)(pMri->m_nD - 1), dHigh);
	for (int i = 0; i < 3; i++) {
		pt.dRelMm[i] = pt.dHeadMm[i] - dCentre[i];
		const double dSpan = dHigh[i] - dLow[i];
		pt.dBoxFrac[i] = dSpan != 0.0 ? (pt.dHeadMm[i] - dLow[i]) / dSpan : 0.0;
	}
	double dVxl[3];
	pMri->headMmToVoxel(pt.dHeadMm, dVxl);
	int64_t n[3];
	for (int i = 0; i < 3; i++) n[i] = (int64_t)std::floor(dVxl[i] + 0.5);
	pt.nMriValue = (n[0] >= 0 && n[1] >= 0 && n[2] >= 0 && n[0] < nSize[0] && n[1] < nSize[1] && n[2] < nSize[2])
		? (int)pMri->valueAt(n[0], n[1], n[2]) : -1;
}

SBhMsiPoint CBhMsiPeakAnalyzer::makePoint(const CBhSourceVolume& src, const CBhMriVolume* pMri,
                                          int64_t nTime, int64_t nItem, const double dHeadMm[3],
                                          double dMaxAbs, const std::string& sLabel)
{
	SBhMsiPoint pt;
	pt.sLabel = sLabel;
	for (int i = 0; i < 3; i++) pt.dHeadMm[i] = dHeadMm[i];
	double dVal = 0.0;
	if (!src.isEmpty() && src.sampleHeadMm(nTime, nItem, dHeadMm, false, dVal)) pt.dValue = dVal;
	pt.dRelValue = dMaxAbs > 0.0 ? pt.dValue / dMaxAbs : 0.0;
	fillRelative(pMri, pt);
	return pt;
}

bool CBhMsiPeakAnalyzer::segment(const CBhSourceVolume& src, const CBhMriVolume* pMri,
                                 const SBhMsiSegmentParam& param, SBhMsiSegmentResult& result,
                                 std::string* pError)
{
	result = SBhMsiSegmentResult();
	result.param = param;
	auto fail = [&](const char* sz) { if (pError) *pError = sz; return false; };
	if (src.isEmpty()) return fail("no source volume");
	if (param.nTime < 0 || param.nTime >= src.m_nTime) return fail("time slice out of range");
	if (param.nItem < 0 || param.nItem >= src.m_nItems) return fail("item out of range");
	if (!(param.dThresholdFrac >= 0.0 && param.dThresholdFrac <= 1.0)) return fail("threshold must be 0..1");

	const int64_t nW = src.m_nW, nH = src.m_nH, nD = src.m_nD;
	const int64_t nVxl = nW * nH * nD;
	std::vector<double> vVal((size_t)nVxl);
	double dMin = 0.0, dMax = 0.0;
	for (int64_t x = 0; x < nW; x++)
		for (int64_t y = 0; y < nH; y++)
			for (int64_t z = 0; z < nD; z++) {
				const double v = src.valueAt(param.nTime, x, y, z, param.nItem);
				const int64_t i = (x * nH + y) * nD + z;
				vVal[(size_t)i] = std::isfinite(v) ? v : 0.0;
				if (i == 0 || vVal[(size_t)i] < dMin) dMin = vVal[(size_t)i];
				if (i == 0 || vVal[(size_t)i] > dMax) dMax = vVal[(size_t)i];
			}
	result.dMin = dMin;
	result.dMax = dMax;
	result.dMaxAbs = param.bIncludeNegative ? (std::max)(std::fabs(dMin), std::fabs(dMax)) : (std::max)(0.0, dMax);
	result.bValid = true;
	if (!(result.dMaxAbs > 0.0)) return true;   // flat map: valid, no clusters
	result.dThreshold = param.dThresholdFrac * result.dMaxAbs;

	// sign of each supra-threshold node: +1 / -1, 0 = below threshold
	std::vector<int8_t> vSign((size_t)nVxl, 0);
	for (int64_t i = 0; i < nVxl; i++) {
		const double v = vVal[(size_t)i];
		if (v > 0.0 && v >= result.dThreshold) vSign[(size_t)i] = 1;
		else if (param.bIncludeNegative && v < 0.0 && -v >= result.dThreshold) vSign[(size_t)i] = -1;
		if (vSign[(size_t)i]) result.nSupraVoxels++;
	}

	const double dCellMm3 = std::fabs(src.m_dStepX * src.m_dStepY * src.m_dStepZ);
	std::vector<int32_t> vLabel((size_t)nVxl, 0);
	std::vector<int64_t> vStack;
	std::vector<SBhMsiCluster> vClusters;
	int32_t nLabel = 0;
	for (int64_t iSeed = 0; iSeed < nVxl; iSeed++) {
		if (!vSign[(size_t)iSeed] || vLabel[(size_t)iSeed]) continue;
		const int8_t nSign = vSign[(size_t)iSeed];
		++nLabel;
		SBhMsiCluster cl;
		cl.bPositive = nSign > 0;
		double dSumW = 0.0, dSumMm[3] = { 0, 0, 0 };
		double dPeakAbs = -1.0;
		bool bFirst = true;
		vStack.assign(1, iSeed);
		vLabel[(size_t)iSeed] = nLabel;
		while (!vStack.empty()) {
			const int64_t i = vStack.back();
			vStack.pop_back();
			const int64_t x = i / (nH * nD), y = (i / nD) % nH, z = i % nD;
			const double v = vVal[(size_t)i];
			double dMm[3];
			src.voxelToHeadMm(x + 0.5, y + 0.5, z + 0.5, dMm);
			cl.nVoxels++;
			const double w = std::fabs(v);
			dSumW += w;
			for (int k = 0; k < 3; k++) {
				dSumMm[k] += w * dMm[k];
				if (bFirst || dMm[k] < cl.dExtentMinMm[k]) cl.dExtentMinMm[k] = dMm[k];
				if (bFirst || dMm[k] > cl.dExtentMaxMm[k]) cl.dExtentMaxMm[k] = dMm[k];
			}
			bFirst = false;
			if (w > dPeakAbs) {
				dPeakAbs = w;
				cl.dPeakValue = v;
				cl.nPeakVxl[0] = x; cl.nPeakVxl[1] = y; cl.nPeakVxl[2] = z;
				for (int k = 0; k < 3; k++) cl.dPeakMm[k] = dMm[k];
			}
			for (int64_t dx = -1; dx <= 1; dx++)
				for (int64_t dy = -1; dy <= 1; dy++)
					for (int64_t dz = -1; dz <= 1; dz++) {
						const int64_t xn = x + dx, yn = y + dy, zn = z + dz;
						if (xn < 0 || yn < 0 || zn < 0 || xn >= nW || yn >= nH || zn >= nD) continue;
						const int64_t j = (xn * nH + yn) * nD + zn;
						if (vSign[(size_t)j] != nSign || vLabel[(size_t)j]) continue;
						vLabel[(size_t)j] = nLabel;
						vStack.push_back(j);
					}
		}
		if (cl.nVoxels < (std::max)((int64_t)1, param.nMinVoxels)) continue;
		cl.dVolumeMm3 = cl.nVoxels * dCellMm3;
		for (int k = 0; k < 3; k++) cl.dCentroidMm[k] = dSumW > 0.0 ? dSumMm[k] / dSumW : cl.dPeakMm[k];
		vClusters.push_back(cl);
	}

	std::stable_sort(vClusters.begin(), vClusters.end(), [](const SBhMsiCluster& a, const SBhMsiCluster& b) {
		return std::fabs(a.dPeakValue) > std::fabs(b.dPeakValue);
	});
	result.nClustersFound = (int)vClusters.size();
	if (param.nMaxClusters > 0 && (int)vClusters.size() > param.nMaxClusters) vClusters.resize((size_t)param.nMaxClusters);
	for (size_t i = 0; i < vClusters.size(); i++) {
		SBhMsiCluster& cl = vClusters[i];
		cl.iRank = (int)i + 1;
		SBhMsiPoint& pt = cl.peak;
		pt.sLabel = fmt("Peak %d", cl.iRank);
		for (int k = 0; k < 3; k++) pt.dHeadMm[k] = cl.dPeakMm[k];
		pt.dValue = cl.dPeakValue;
		pt.dRelValue = cl.dPeakValue / result.dMaxAbs;
		pt.nClusterVoxels = cl.nVoxels;
		pt.dClusterVolumeMm3 = cl.dVolumeMm3;
		fillRelative(pMri, pt);
	}
	result.vClusters.swap(vClusters);
	return true;
}

std::vector<SBhMsiPoint> CBhMsiPeakAnalyzer::peakPoints(const SBhMsiSegmentResult& result)
{
	std::vector<SBhMsiPoint> v;
	for (const SBhMsiCluster& cl : result.vClusters) v.push_back(cl.peak);
	return v;
}

std::string CBhMsiPeakAnalyzer::formatPoints(const std::vector<SBhMsiPoint>& vPoints)
{
	std::string s;
	for (size_t i = 0; i < vPoints.size(); i++) {
		const SBhMsiPoint& p = vPoints[i];
		s += fmt("%zu. %s: head (%.1f, %.1f, %.1f) mm; rel. MRI centre (%+.1f, %+.1f, %+.1f) mm; "
		         "box fraction (%.2f, %.2f, %.2f); value %.5g (%.0f%% of max |value|)",
		         i + 1, p.sLabel.c_str(), p.dHeadMm[0], p.dHeadMm[1], p.dHeadMm[2],
		         p.dRelMm[0], p.dRelMm[1], p.dRelMm[2], p.dBoxFrac[0], p.dBoxFrac[1], p.dBoxFrac[2],
		         p.dValue, p.dRelValue * 100.0);
		if (p.nClusterVoxels > 0) s += fmt("; cluster %lld nodes, %.0f mm^3", (long long)p.nClusterVoxels, p.dClusterVolumeMm3);
		if (p.nMriValue >= 0) s += fmt("; MRI intensity %d", p.nMriValue);
		s += "\n";
	}
	return s;
}

std::string CBhMsiPeakAnalyzer::formatReport(const SBhMsiSegmentResult& r)
{
	if (!r.bValid) return "MSI segmentation: no valid source volume.\n";
	std::string s = fmt("MSI segmentation (time slice %lld, item %lld): value range %.5g .. %.5g, "
	                    "threshold %.0f%% of max |value| = %.5g, %lld supra-threshold nodes, %d cluster(s)",
	                    (long long)r.param.nTime, (long long)r.param.nItem, r.dMin, r.dMax,
	                    r.param.dThresholdFrac * 100.0, r.dThreshold, (long long)r.nSupraVoxels, r.nClustersFound);
	if ((int)r.vClusters.size() < r.nClustersFound) s += fmt(", strongest %zu shown", r.vClusters.size());
	s += ".\n";
	for (const SBhMsiCluster& c : r.vClusters) {
		s += fmt("#%d %s: peak %.5g at head (%.1f, %.1f, %.1f) mm [grid %lld,%lld,%lld]; centroid (%.1f, %.1f, %.1f) mm; "
		         "%lld nodes = %.0f mm^3; extent x %.1f..%.1f, y %.1f..%.1f, z %.1f..%.1f mm\n",
		         c.iRank, c.bPositive ? "positive" : "negative", c.dPeakValue,
		         c.dPeakMm[0], c.dPeakMm[1], c.dPeakMm[2],
		         (long long)c.nPeakVxl[0], (long long)c.nPeakVxl[1], (long long)c.nPeakVxl[2],
		         c.dCentroidMm[0], c.dCentroidMm[1], c.dCentroidMm[2], (long long)c.nVoxels, c.dVolumeMm3,
		         c.dExtentMinMm[0], c.dExtentMaxMm[0], c.dExtentMinMm[1], c.dExtentMaxMm[1],
		         c.dExtentMinMm[2], c.dExtentMaxMm[2]);
	}
	return s;
}
