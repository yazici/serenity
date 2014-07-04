#pragma once
#include "opencl.h"

/// Returns sum of square differences betweeen A and B
float SSE(const CLVolume& A, const CLVolume& B, const int3 origin=0, int3 size=0);
/// Returns sum of A multiplied by B
float dot(const CLVolume& A, const CLVolume& B, const int3 origin=0, int3 size=0);

// Element-wise operations
inline uint64 mul(const ImageArray& y, const ImageArray& a, const ImageArray& b) { CL(operators, mul); return emulateWriteTo3DImage(mul, y, noneNearestSampler, a, b); } // y = a * b [MLEM]
inline uint64 div(const ImageArray& y, const ImageArray& a, const ImageArray& b) { CL(operators, div) return emulateWriteTo3DImage(div, y, noneNearestSampler, a, b); } // y = a / b [MLEM]
inline uint64 divdiff(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) { CL(operators, divdiff) return emulateWriteTo3DImage(divdiff, y, noneNearestSampler, a, b, c); } // y = ( a - b ) / c [SART]
inline uint64 divmul(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) {CL(operators, divmul) return emulateWriteTo3DImage(divmul, y, noneNearestSampler, a, b, c); } // y = a / (b * c) [MLEM]
inline uint64 maxadd(const CLVolume& y, const CLVolume& a, const float alpha, const CLVolume& b) { CL(operators, maxadd) return emulateWriteTo3DImage(maxadd, y, noneNearestSampler, a, alpha, b); } // y = max(0, a + α b) [SART, CG]
inline uint64 add(const CLVolume& y, const float alpha, const CLVolume& a, const float beta, const CLVolume& b) { CL(operators, add) return emulateWriteTo3DImage(add, y, noneNearestSampler, alpha, a, beta, b); } // y = α a + β b [CG]
inline uint64 mulexp(const CLVolume& y, const CLVolume& a, const CLVolume& b) { CL(operators, mulexp) return emulateWriteTo3DImage(mulexp, y, noneNearestSampler, a, b); } // y = a · exp(-b) [MLTR]
inline uint64 diffexp(const CLVolume& y, const CLVolume& a, const CLVolume& b) { CL(operators, diffexp) return emulateWriteTo3DImage(diffexp, y, noneNearestSampler, a, b); } // y = exp(-a) - exp(-b) [MLTR]
inline uint64 adddiv(const CLVolume& y, const CLVolume& a, const CLVolume& b, const CLVolume& c) { CL(operators, adddiv) return emulateWriteTo3DImage(adddiv, y, noneNearestSampler, a, b, c); } // y = max(0, a + c ? b / c : 0) [MLTR]
inline uint64 muldiv(const CLVolume& y, const CLVolume& a, const CLVolume& b, const CLVolume& c) { CL(operators, muldiv) return emulateWriteTo3DImage(muldiv, y, noneNearestSampler, a, b, c); } // y = max(0, a + c ? a * b / c : 0) [MLTR, MLEM]
inline uint64 negln(const ImageArray& y, const ImageArray& a) { CL(operators, negln) return emulateWriteTo3DImage(negln, y, noneNearestSampler, a); } // y = ln(a) [SART, CG]
inline ImageArray negln(const ImageArray& a) { ImageArray y(a.size, 0, "ln "_+a.name); negln(y, a); return y; }
