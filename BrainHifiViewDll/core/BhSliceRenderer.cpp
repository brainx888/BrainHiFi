#include "BhSliceRenderer.h"
#include <algorithm>
#include <cmath>

namespace {
inline uint32_t makeBgra(int r, int g, int b)
{
	r = (std::max)(0, (std::min)(255, r));
	g = (std::max)(0, (std::min)(255, g));
	b = (std::max)(0, (std::min)(255, b));
	return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
}

bool CBhSliceRenderer::imageSize(const CBhMriVolume& mri, EBhView uView, int& nCol, int& nRow,
                                 double& dMmPerCol, double& dMmPerRow)
{
	nCol = nRow = 0;
	dMmPerCol = dMmPerRow = 1.0;
	if (mri.isEmpty()) return false;
	switch (uView) {
	case EBhView::Axial:
		nCol = (int)mri.m_nW; nRow = (int)mri.m_nD;
		dMmPerCol = mri.m_dSpaceX; dMmPerRow = mri.m_dSpaceZ;
		break;
	case EBhView::Coronal:
		nCol = (int)mri.m_nW; nRow = (int)mri.m_nH;
		dMmPerCol = mri.m_dSpaceX; dMmPerRow = mri.m_dSpaceY;
		break;
	case EBhView::Sagittal:
	default:
		nCol = (int)mri.m_nD; nRow = (int)mri.m_nH;
		dMmPerCol = mri.m_dSpaceZ; dMmPerRow = mri.m_dSpaceY;
		break;
	}
	return nCol > 0 && nRow > 0;
}

int64_t CBhSliceRenderer::sliceIndex(EBhView uView, const int64_t nCursor[3])
{
	switch (uView) {
	case EBhView::Axial:   return nCursor[1];
	case EBhView::Coronal: return nCursor[2];
	default:               return nCursor[0];
	}
}

bool CBhSliceRenderer::pixelToVoxel(const CBhMriVolume& mri, EBhView uView, bool bFlipLR,
                                    int nCol, int nRow, const int64_t nCursor[3], int64_t nVxl[3])
{
	int nCols = 0, nRows = 0;
	double dC = 1, dR = 1;
	if (!imageSize(mri, uView, nCols, nRows, dC, dR)) return false;
	if (nCol < 0 || nRow < 0 || nCol >= nCols || nRow >= nRows) return false;
	nVxl[0] = nCursor[0]; nVxl[1] = nCursor[1]; nVxl[2] = nCursor[2];
	const int64_t x = bFlipLR ? (mri.m_nW - 1 - nCol) : nCol;
	switch (uView) {
	case EBhView::Axial:
		nVxl[0] = x;
		nVxl[2] = nRow;
		break;
	case EBhView::Coronal:
		nVxl[0] = x;
		nVxl[1] = mri.m_nH - 1 - nRow;
		break;
	case EBhView::Sagittal:
	default:
		nVxl[2] = nCol;
		nVxl[1] = mri.m_nH - 1 - nRow;
		break;
	}
	return true;
}

bool CBhSliceRenderer::voxelToPixel(const CBhMriVolume& mri, EBhView uView, bool bFlipLR,
                                    const int64_t nVxl[3], int& nCol, int& nRow)
{
	nCol = nRow = 0;
	if (mri.isEmpty()) return false;
	const int64_t nColX = bFlipLR ? (mri.m_nW - 1 - nVxl[0]) : nVxl[0];
	switch (uView) {
	case EBhView::Axial:
		nCol = (int)nColX; nRow = (int)nVxl[2];
		break;
	case EBhView::Coronal:
		nCol = (int)nColX; nRow = (int)(mri.m_nH - 1 - nVxl[1]);
		break;
	case EBhView::Sagittal:
	default:
		nCol = (int)nVxl[2]; nRow = (int)(mri.m_nH - 1 - nVxl[1]);
		break;
	}
	return true;
}

uint32_t CBhSliceRenderer::hotColor(double dFrac)
{
	dFrac = (std::max)(0.0, (std::min)(1.0, dFrac));
	const double r = (std::min)(1.0, 0.5 + dFrac * 1.5);
	const double g = (std::max)(0.0, (std::min)(1.0, (dFrac - 0.2) * 1.6));
	const double b = (std::max)(0.0, (std::min)(1.0, (dFrac - 0.8) * 5.0));
	return makeBgra((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

uint32_t CBhSliceRenderer::coolColor(double dFrac)
{
	dFrac = (std::max)(0.0, (std::min)(1.0, dFrac));
	const double b = (std::min)(1.0, 0.5 + dFrac * 1.5);
	const double g = (std::max)(0.0, (std::min)(1.0, (dFrac - 0.2) * 1.6));
	const double r = (std::max)(0.0, (std::min)(1.0, (dFrac - 0.8) * 5.0));
	return makeBgra((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

uint32_t CBhSliceRenderer::blend(uint32_t uBase, uint32_t uTop, double dAlpha)
{
	dAlpha = (std::max)(0.0, (std::min)(1.0, dAlpha));
	const int r = (int)(((uBase >> 16) & 0xFF) * (1 - dAlpha) + ((uTop >> 16) & 0xFF) * dAlpha + 0.5);
	const int g = (int)(((uBase >> 8) & 0xFF) * (1 - dAlpha) + ((uTop >> 8) & 0xFF) * dAlpha + 0.5);
	const int b = (int)((uBase & 0xFF) * (1 - dAlpha) + (uTop & 0xFF) * dAlpha + 0.5);
	return makeBgra(r, g, b);
}

bool CBhSliceRenderer::render(const CBhMriVolume& mri, const CBhSourceVolume* pSrc, EBhView uView,
                              const SBhRenderParam& param, std::vector<uint32_t>& vBgra, int& nCol, int& nRow)
{
	double dMmC = 1, dMmR = 1;
	if (!imageSize(mri, uView, nCol, nRow, dMmC, dMmR)) {
		vBgra.clear();
		return false;
	}
	vBgra.assign((size_t)nCol * (size_t)nRow, 0xFF000000u);

	const double dGrayRange = (std::max)(1e-9, param.dGrayHigh - param.dGrayLow);
	const bool bSrc = param.bShowSource && pSrc && !pSrc->isEmpty()
		&& param.nTime >= 0 && param.nTime < pSrc->m_nTime
		&& param.nItem >= 0 && param.nItem < pSrc->m_nItems;
	const double dMaxAbs = (param.dSrcMaxAbs > 0.0) ? param.dSrcMaxAbs : 1.0;
	const double dCut = (std::max)(0.0, (std::min)(1.0, param.dThreshold)) * dMaxAbs;
	const double dColorSpan = (std::max)(1e-12, dMaxAbs - dCut);

	for (int iRow = 0; iRow < nRow; iRow++) {
		for (int iCol = 0; iCol < nCol; iCol++) {
			int64_t nVxl[3];
			if (!pixelToVoxel(mri, uView, param.bFlipLR, iCol, iRow, param.nCursor, nVxl)) continue;
			const double dGray = (mri.valueAt(nVxl[0], nVxl[1], nVxl[2]) - param.dGrayLow) / dGrayRange;
			const int nGray = (int)((std::max)(0.0, (std::min)(1.0, dGray)) * 255.0 + 0.5);
			uint32_t uPix = makeBgra(nGray, nGray, nGray);
			if (bSrc) {
				double dMm[3];
				mri.voxelToHeadMm((double)nVxl[0], (double)nVxl[1], (double)nVxl[2], dMm);
				double dVal = 0.0;
				if (pSrc->sampleHeadMm(param.nTime, param.nItem, dMm, param.bTrilinear, dVal) && std::isfinite(dVal)) {
					if (dVal > 0.0 && dVal >= dCut) {
						uPix = blend(uPix, hotColor((dVal - dCut) / dColorSpan), param.dOpacity);
					} else if (param.bShowNegative && dVal < 0.0 && -dVal >= dCut) {
						uPix = blend(uPix, coolColor((-dVal - dCut) / dColorSpan), param.dOpacity);
					}
				}
			}
			vBgra[(size_t)iRow * nCol + iCol] = uPix;
		}
	}

	if (param.bCrosshair) {
		int nCx = 0, nCy = 0;
		voxelToPixel(mri, uView, param.bFlipLR, param.nCursor, nCx, nCy);
		const uint32_t uLine = makeBgra(0, 255, 0);
		if (nCx >= 0 && nCx < nCol)
			for (int iRow = 0; iRow < nRow; iRow++)
				vBgra[(size_t)iRow * nCol + nCx] = blend(vBgra[(size_t)iRow * nCol + nCx], uLine, 0.6);
		if (nCy >= 0 && nCy < nRow)
			for (int iCol = 0; iCol < nCol; iCol++)
				vBgra[(size_t)nCy * nCol + iCol] = blend(vBgra[(size_t)nCy * nCol + iCol], uLine, 0.6);
	}
	return true;
}
