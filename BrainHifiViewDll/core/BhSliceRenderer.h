#pragma once
/*
 * CBhSliceRenderer - builds the axial / coronal / sagittal 2D images of an
 * MRI volume with an optional MEG source overlay (MFC-free, BGRA 32-bit,
 * top-down rows).
 *
 * Orientation (voxel 0 = subject right-bottom-front, see CBhMriVolume):
 *   Axial    : slice y = const, columns = x, rows = z (anterior at top)
 *   Coronal  : slice z = const, columns = x, rows = y (superior at top)
 *   Sagittal : slice x = const, columns = z, rows = y (anterior at left, superior at top)
 * Axial / coronal use the radiological convention (subject right on the
 * screen left); bFlipLR switches to the neurological convention.
 */
#include "BhExport.h"
#include "BhMriVolume.h"
#include "BhSourceVolume.h"
#include <cstdint>
#include <vector>

enum class EBhView : int { Axial = 0, Coronal = 1, Sagittal = 2 };

struct BH_API SBhRenderParam
{
	double dGrayLow = 0.0;
	double dGrayHigh = 4095.0;

	bool bShowSource = true;
	int64_t nTime = 0;
	int64_t nItem = 0;
	double dSrcMaxAbs = 1.0;        // value mapped to the brightest colour
	double dThreshold = 0.5;        // fraction of dSrcMaxAbs below which nothing is drawn
	double dOpacity = 0.6;          // 0..1
	bool bShowNegative = true;      // draw negative values with the cool map
	bool bTrilinear = false;

	bool bFlipLR = false;
	bool bCrosshair = true;
	int64_t nCursor[3] = { 0, 0, 0 };   // MRI voxel x,y,z
};

class BH_API CBhSliceRenderer
{
public:
	static bool imageSize(const CBhMriVolume& mri, EBhView uView, int& nCol, int& nRow,
	                      double& dMmPerCol, double& dMmPerRow);
	/// Image column/row of the given view to MRI voxel (third axis from nSlice).
	static bool pixelToVoxel(const CBhMriVolume& mri, EBhView uView, bool bFlipLR,
	                         int nCol, int nRow, const int64_t nCursor[3], int64_t nVxl[3]);
	static bool voxelToPixel(const CBhMriVolume& mri, EBhView uView, bool bFlipLR,
	                         const int64_t nVxl[3], int& nCol, int& nRow);
	static int64_t sliceIndex(EBhView uView, const int64_t nCursor[3]);

	static bool render(const CBhMriVolume& mri, const CBhSourceVolume* pSrc, EBhView uView,
	                   const SBhRenderParam& param, std::vector<uint32_t>& vBgra, int& nCol, int& nRow);

	static uint32_t hotColor(double dFrac);
	static uint32_t coolColor(double dFrac);
	static uint32_t blend(uint32_t uBase, uint32_t uTop, double dAlpha);
};
