#pragma once
/*
 * CBhMsiPeakAnalyzer - segments the MEG source volume of an MSI
 * (MRI + MEG source overlay) into activation clusters and reports their
 * peaks and locations as plain text / structured records.
 *
 * Segmentation, per time slice and item:
 *   1. threshold = dThresholdFrac * max|value| (or max value when positive only)
 *   2. supra-threshold grid nodes grouped by 26-connectivity, positive and
 *      negative lobes kept apart
 *   3. clusters smaller than nMinVoxels dropped, the rest sorted by |peak|
 *
 * Coordinates are head mm (same frame as CBhMriVolume / CBhSourceVolume,
 * axes +x subject left, +y superior, +z posterior).  A grid node (x,y,z)
 * is reported at its cell centre (x+0.5, y+0.5, z+0.5), as OnBnClickedGotoPeak.
 * Positions are also given relative to the MRI volume centre and as a
 * 0..1 fraction of the MRI box, which helps an LLM reason about anatomy.
 * The core is MFC-free so it can be unit tested outside Visual Studio.
 */
#include "BhExport.h"
#include "BhMriVolume.h"
#include "BhSourceVolume.h"
#include <cstdint>
#include <string>
#include <vector>

struct SBhMsiSegmentParam
{
	int64_t nTime = 0;
	int64_t nItem = 0;
	double dThresholdFrac = 0.5;   // fraction of max |value|, 0..1
	bool bIncludeNegative = true;  // segment negative lobes too
	int64_t nMinVoxels = 1;        // drop smaller clusters
	int nMaxClusters = 10;         // keep the strongest N (<=0: all)
};

/// One 3D point sent to / described by the AI model.
struct SBhMsiPoint
{
	std::string sLabel;            // e.g. "Peak 1", "Cursor"
	double dHeadMm[3] = { 0, 0, 0 };
	double dRelMm[3] = { 0, 0, 0 };   // head mm minus MRI centre (+x left, +y sup, +z post)
	double dBoxFrac[3] = { 0, 0, 0 }; // 0..1 position inside the MRI box (R->L, I->S, A->P)
	double dValue = 0.0;           // source value
	double dRelValue = 0.0;        // value / max|value| of the slice
	int nMriValue = -1;            // MRI intensity at the point, -1 if no MRI
	int64_t nClusterVoxels = 0;    // 0 for single points
	double dClusterVolumeMm3 = 0.0;
};

struct SBhMsiCluster
{
	int iRank = 0;                 // 1 = strongest
	bool bPositive = true;
	int64_t nVoxels = 0;
	double dVolumeMm3 = 0.0;
	double dPeakValue = 0.0;
	int64_t nPeakVxl[3] = { 0, 0, 0 };    // source grid index
	double dPeakMm[3] = { 0, 0, 0 };      // head mm of the peak
	double dCentroidMm[3] = { 0, 0, 0 };  // |value|-weighted centroid, head mm
	double dExtentMinMm[3] = { 0, 0, 0 }; // bounding box, head mm
	double dExtentMaxMm[3] = { 0, 0, 0 };
	SBhMsiPoint peak;                     // peak as an AI query point
};

struct SBhMsiSegmentResult
{
	bool bValid = false;
	SBhMsiSegmentParam param;
	double dMaxAbs = 0.0;
	double dMin = 0.0, dMax = 0.0;
	double dThreshold = 0.0;
	int64_t nSupraVoxels = 0;
	int nClustersFound = 0;        // before nMaxClusters truncation
	std::vector<SBhMsiCluster> vClusters;
};

class BH_API CBhMsiPeakAnalyzer
{
public:
	/// pMri may be null (then no relative / MRI fields are filled).
	static bool segment(const CBhSourceVolume& src, const CBhMriVolume* pMri,
	                    const SBhMsiSegmentParam& param, SBhMsiSegmentResult& result,
	                    std::string* pError = nullptr);

	/// Builds a query point at a head-mm position (e.g. the viewer cursor).
	static SBhMsiPoint makePoint(const CBhSourceVolume& src, const CBhMriVolume* pMri,
	                             int64_t nTime, int64_t nItem, const double dHeadMm[3],
	                             double dMaxAbs, const std::string& sLabel);

	/// Plain-text table of the clusters (what the local segmentation found).
	static std::string formatReport(const SBhMsiSegmentResult& result);
	/// One line per point, used in the AI prompts.
	static std::string formatPoints(const std::vector<SBhMsiPoint>& vPoints);
	static std::vector<SBhMsiPoint> peakPoints(const SBhMsiSegmentResult& result);

	static void fillRelative(const CBhMriVolume* pMri, SBhMsiPoint& pt);
};
