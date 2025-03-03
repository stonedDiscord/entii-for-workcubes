#include "driver.h"
#include "runtime.h"
#include "../../fpvigxdrv/source/efb.h"

#define POINTER_SWAP32(ptr) (PULONG)(_Mmio_MungeAddressForBig(ptr, sizeof(ULONG)))

static ULONG CalculateTexOffset(ULONG x, ULONG y, ULONG width) {
	return (((y >> 2) << 4) * width) +
		((x >> 2) << 6) +
		(((y % 4 << 2) + x % 4) << 1);
}

static ULONG TexReadRgb(PUCHAR pTex, ULONG offset) {
	ULONG r = NativeReadBase8(pTex, offset + 1);
	ULONG gb = NativeReadBase16(pTex, offset + 32);
	return (r << 16) | gb;
}

static void TexWriteRgb(PUCHAR pTex, ULONG offset, ULONG rgb) {
	if ((offset & 3) != 0) {
		// offset must be at +2
		ULONG offset32 = offset & ~3;
		ULONG value = NativeReadBase32(pTex, offset32);
		value &= 0xFFFF0000;
		value |= 0xFF00 | ((rgb >> 16) & 0xFF);
		ULONG value2 = NativeReadBase32(pTex, offset32 + 0x20);
		value2 &= 0xFFFF0000;
		value2 |= (rgb & 0xFFFF);
		NativeWriteBase32(pTex, offset32, value);
		NativeWriteBase32(pTex, offset32 + 0x20, value2);
	} else {
		ULONG value = NativeReadBase32(pTex, offset);
		value &= 0x0000FFFF;
		value |= (0xFF00 | ((rgb >> 16) & 0xFF)) << 16;
		ULONG value2 = NativeReadBase32(pTex, offset + 0x20);
		value2 &= 0x0000FFFF;
		value2 |= (rgb & 0xFFFF) << 16;
		NativeWriteBase32(pTex, offset, value);
		NativeWriteBase32(pTex, offset + 0x20, value2);
	}
}

static void TexWriteRgb2Aligned(PUCHAR pTex, ULONG offset, ULONG rgb0, ULONG rgb1) {
	UCHAR r0 = (rgb0 >> 16) & 0xFF;
	UCHAR r1 = (rgb1 >> 16) & 0xFF;
	ULONG value = 0xFF00FF00 | ((rgb1 >> 16) & 0xFF) | (((rgb0 >> 16) & 0xFF) << 16);
	ULONG value2 = ((rgb0 & 0xFFFF) << 16) | (rgb1 & 0xFFFF);
	NativeWriteBase32(pTex, offset, value);
	NativeWriteBase32(pTex, offset + 0x20, value2);
}

BOOL CopyBitsSwap32(
SURFOBJ  *psoDest,
SURFOBJ  *psoSrc,
CLIPOBJ  *pco,
XLATEOBJ *pxlo,
RECTL    *prclDest,
POINTL   *pptlSrc,
BOOL copyFromFb)
{
/*
	Copy 32bpp bitmap data endian swapped
*/
	
	LONG lDeltaSrc = psoSrc->lDelta;
	LONG lDeltaDst = psoDest->lDelta;
	
	if (pco != NULL &&  pco->iDComplexity != DC_TRIVIAL) {
		return FALSE;
	}
	
	if (prclDest->top >= prclDest->bottom) return TRUE;
	if (prclDest->left >= prclDest->right) return TRUE;
	
	
	LONG xDst = prclDest->left;
	LONG yDst = prclDest->top;
	LONG xSrc = pptlSrc->x + xDst - prclDest->left;
	LONG ySrc = pptlSrc->y + yDst - prclDest->top;
	
	LONG cx = prclDest->right - xDst;
	LONG cy = prclDest->bottom - yDst;
	
	LONG xSrcStart = xSrc;
	LONG xSrcEnd = xSrcStart + cx;
	LONG xDstStart = xDst;
	LONG yDstStart = prclDest->top;
	PBYTE pjSrc = ((PBYTE) psoSrc->pvScan0) + (ySrc * psoSrc->lDelta);
	PBYTE pjDst = ((PBYTE) psoDest->pvScan0) + (yDst * psoDest->lDelta);
	
	PULONG pulSrc = (PULONG) (pjSrc + (4 * xSrcStart));
	PULONG pulDst = (PULONG) (pjDst + (4 * xDstStart));
	PULONG pstartSrc = (PULONG) psoSrc->pvBits;
	PULONG pendSrc = (PULONG) ((PUCHAR) pstartSrc + psoSrc->cjBits);
	PULONG pstartDst = (PULONG) psoDest->pvBits;
	PULONG pendDst = (PULONG) ((PUCHAR) pstartDst + psoDest->cjBits);
	ULONG copyX = cx;
	ULONG copyY = cy;
	
	ULONG srcHeight = psoSrc->sizlBitmap.cy;
	ULONG srcWidth = psoSrc->sizlBitmap.cx;
	PBYTE pSrcFbStart = ((PBYTE) psoSrc->pvScan0);
	if (psoSrc->lDelta < 0) pSrcFbStart += (srcHeight * psoSrc->lDelta);
	PBYTE pDestFbStart = ((PBYTE) psoDest->pvScan0);
	ULONG destHeight = psoDest->sizlBitmap.cy;
	ULONG destWidth = psoDest->sizlBitmap.cx;
	if (psoDest->lDelta < 0) pDestFbStart += (destHeight * psoDest->lDelta);
	
	ULONG cyIdx = 0;
	while (1) {
		PULONG pulSrcTemp = pulSrc;
		PULONG pulDstTemp = pulDst;
		// Bounds check the pointers, we could be in here when drawing off the screen
		if (pulSrc >= pstartSrc && pulDst >= pstartDst) {
			ULONG TexOffset = 0;
			
			ULONG cxTemp = cx;
			ULONG cxIdx = 0;
			BOOLEAN validAccess;
			if (copyFromFb) {
				// Read swapped from source, write normally to dest.
				while (cxTemp--) {
					validAccess = pulSrcTemp >= pstartSrc && pulDstTemp >= pstartDst &&
						pulSrcTemp < pendSrc && pulDstTemp < pendDst &&
						(xSrc + cxIdx) < srcWidth &&
						(ySrc + cyIdx) < srcHeight;
					if (validAccess) {
						TexOffset = CalculateTexOffset(xSrc + cxIdx, ySrc + cyIdx, srcWidth);
						//pulSrcTemp++;
						*pulDstTemp = TexReadRgb(pSrcFbStart, TexOffset);
					}
					pulDstTemp++;
					cxIdx++;
				}
			} else {
				// Read normally from source, write swapped to dest.
				while (cxTemp--) {
					validAccess = pulSrcTemp >= pstartSrc && pulDstTemp >= pstartDst &&
						pulSrcTemp < pendSrc && pulDstTemp < pendDst &&
						(xDst + cxIdx) < destWidth &&
						(yDst + cyIdx) < destHeight;
					if (validAccess) {
						ULONG sourceVal = LoadToRegister32(*pulSrcTemp);
						pulSrcTemp++;
						//EfbWrite32(pulDstTemp, sourceVal);
						//pulDstTemp++;
						TexOffset = CalculateTexOffset(xDst + cxIdx, yDstStart + cyIdx, destWidth);
						ULONG TexOffset2 = CalculateTexOffset(xDst + cxIdx + 1, yDstStart + cyIdx, destWidth);
						if (((TexOffset & 3) == 0) && (TexOffset2 == (TexOffset + 2)) && (cxTemp > 1)) {
							// Will be writing the next pixel too, we can optimise this to two writes from four reads and four writes.
							ULONG sourceVal2 = LoadToRegister32(*pulSrcTemp);
							pulSrcTemp++;
							TexWriteRgb2Aligned(pDestFbStart, TexOffset, sourceVal, sourceVal2);
							cxIdx++;
							cxTemp--;
						} else {
							TexWriteRgb(pDestFbStart, TexOffset, sourceVal);
						}
					} else {
						pulSrcTemp++;
					}
					cxIdx++;
				}
			}
		}
		
		cy--;
		cyIdx++;
		if (cy == 0) break;
		pulSrc = (PULONG) (((PBYTE)pulSrc) + lDeltaSrc);
		pulDst = (PULONG) (((PBYTE)pulDst) + lDeltaDst);
	}
	
	return TRUE;
}

BOOL DrvCopyBits(
SURFOBJ  *psoDest,
SURFOBJ  *psoSrc,
CLIPOBJ  *pco,
XLATEOBJ *pxlo,
RECTL    *prclDest,
POINTL   *pptlSrc)
{

/*
	DrvCopyBits translates between device-managed raster surfaces and
	GDI standard-format bitmaps. This function is required for a device driver
	that has device-managed bitmaps or raster surfaces.
	The implementation in the driver must translate driver surfaces to and from
	any standard-format bitmap.
	
	Standard-format bitmaps are single-plane, packed-pixel format. Each scan line is
	aligned on a four-byte boundary. These bitmaps have
	1, 4, 8, 16, 24, 32, or 64 bits per pixel.
	
	This function should ideally be able to deal with RLE and device-dependent
	bitmaps. (See the Windows NT SDK.) The device-dependent format is optional;
	only a few specialized drivers need to support it. These bitmaps may be sent
	to this function as a result of the following GDI functions:
	SetDIBits, SetDIBitsToDevice, GetDIBits, SetBitmapBits, and GetBitmapBits.
*/

	// If both surfaces are not device managed, just call original func:
	if (psoDest->iType != STYPE_DEVBITMAP && psoSrc->iType != STYPE_DEVBITMAP) {
		return EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);
	}
	POINTL point = {0};
	point.x = prclDest->left;
	point.y = prclDest->top;
	
	// Get the pdev. At least one of dest or src must be devbitmap.
	PPDEV ppDev = NULL;
	if (psoDest->iType == STYPE_DEVBITMAP) ppDev = (PPDEV)psoDest->dhpdev;
	else if (psoSrc->iType == STYPE_DEVBITMAP) ppDev = (PPDEV)psoSrc->dhpdev;
	else {
		// Should never get here.
		return FALSE;
	}
	
	// Both surfaces are device mapped
	if (psoDest->iType == STYPE_DEVBITMAP && psoSrc->iType == STYPE_DEVBITMAP) {
		// Proxy through the double buffer.
		psoDest = ppDev->psurfDouble;
		psoSrc = ppDev->psurfDouble;
		if (!EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc)) {
			return FALSE;
		}
		psoDest = ppDev->psurfBigFb;
		return CopyBitsSwap32(psoDest, psoSrc, NULL, NULL, prclDest, &point, FALSE);
	}
	
	// Copying to framebuffer
	if (psoDest->iType == STYPE_DEVBITMAP) {
		// Source is always going to be a 32bpp bitmap
		// Proxy through the double buffer.
		psoDest = ppDev->psurfDouble;
		if (!EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc)) {
			return FALSE;
		}
		psoSrc = psoDest;
		psoDest = ppDev->psurfBigFb;
		return CopyBitsSwap32(psoDest, psoSrc, NULL, NULL, prclDest, &point, FALSE);
	}
	
	// Copying from framebuffer
	if (psoSrc->iType == STYPE_DEVBITMAP) {
		// Dest is always going to be a 32bpp bitmap
		// Proxy through the double buffer.
		psoSrc = ppDev->psurfDouble;
		return EngCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);
	}
	
	// Should never get here.
	return FALSE;
}

BOOL DrvStrokePath(
SURFOBJ*   pso,
PATHOBJ*   ppo,
CLIPOBJ*   pco,
XFORMOBJ*  pxo,
BRUSHOBJ*  pbo,
POINTL*    pptlBrush,
LINEATTRS* pla,
MIX        mix)
{
	/*
		DrvStrokePath strokes a path when called by GDI.
		If the driver has hooked the function,
		and if the appropriate GCAPs are set,
		GDI calls DrvStrokePath when GDI draws a line or curve
		with any set of attributes.
	*/
	
	if (pso->iType != STYPE_DEVBITMAP) {
		// Not device managed, call original function
		return EngStrokePath(pso, ppo, pco, pxo, pbo, pptlBrush, pla, mix);
	}
	
	PPDEV ppdev = (PPDEV)pso->dhpdev;
	
	// Get the path bounds, convert it into a rect
	RECTFX PathBounds;
	RECTL destRect;
	PATHOBJ_vGetBounds(ppo, &PathBounds);
	destRect.left = (PathBounds.xLeft >> 4);
	destRect.top = (PathBounds.yTop >> 4);
	destRect.right = (PathBounds.xRight >> 4) + 2;
	destRect.bottom = (PathBounds.yBottom >> 4) + 2;
	
	POINTL point = {0};
	point.x = destRect.left;
	point.y = destRect.top;
	
	// Call via the double buffer
	if (!EngStrokePath(ppdev->psurfDouble, ppo, pco, pxo, pbo, pptlBrush, pla, mix)) {
		return FALSE;
	}
	
	// Copy back to the framebuffer
	return CopyBitsSwap32(ppdev->psurfBigFb, ppdev->psurfDouble, NULL, NULL, &destRect, &point, FALSE);
}

BOOL DrvTextOut(
SURFOBJ*  pso,
STROBJ*   pstro,
FONTOBJ*  pfo,
CLIPOBJ*  pco,
RECTL*    prclExtra,
RECTL*    prclOpaque,
BRUSHOBJ* pboFore,
BRUSHOBJ* pboOpaque,
POINTL*   pptlOrg,
MIX       mix)
{
	/*
		DrvTextOut is the entry point from GDI that calls for the driver to
		render a set of glyphs at specified positions.
	*/
	
	if (pso->iType != STYPE_DEVBITMAP) {
		// Not device managed, call original function
		return EngTextOut(pso, pstro, pfo, pco, prclExtra, prclOpaque, pboFore, pboOpaque, pptlOrg, mix);
	}
	
	// Copy to the double buffer
	RECTL* prclDest = (prclOpaque != NULL) ? prclOpaque : &pstro->rclBkGround;
	PPDEV ppdev = (PPDEV)pso->dhpdev;
	POINTL point = {0};
	
	RECTL rclDest;
	memcpy(&rclDest, prclDest, sizeof(rclDest));
	point.x = prclDest->left;
	point.y = prclDest->top;
	
	// Call via the double buffer
	if (!EngTextOut(ppdev->psurfDouble, pstro, pfo, pco, prclExtra, prclOpaque, pboFore, pboOpaque, pptlOrg, mix)) {
		return FALSE;
	}
	
	// Copy back to the framebuffer
	return CopyBitsSwap32(ppdev->psurfBigFb, ppdev->psurfDouble, NULL, NULL, &rclDest, &point, FALSE);
}