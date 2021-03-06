#pragma once
#include "math/MathUtil.h"
#include <algorithm>
#include <vector>

class Distribution1D
{
    std::vector<float> _pdf;
    std::vector<float> _cdf;
public:
    Distribution1D(std::vector<float> weights)
    : _pdf(std::move(weights))
    {
        _cdf.resize(_pdf.size() + 1);
        _cdf[0] = 0.0f;
        for (size_t i = 0; i < _pdf.size(); ++i)
            _cdf[i + 1] = _cdf[i] + _pdf[i];
        float totalWeight = _cdf.back();
        for (float &p : _pdf)
            p /= totalWeight;
        for (float &c : _cdf)
            c /= totalWeight;
        _cdf.back() = 1.0f;
    }

    void warp(float &u, int &idx) const
    {
        idx = int(std::distance(_cdf.begin(), std::upper_bound(_cdf.begin(), _cdf.end(), u)) - 1);
        u = clamp(0.0f, (u - _cdf[idx])/_pdf[idx], 1.0f);
    }

    float pdf(int idx) const
    {
        return _pdf[idx];
    }
};
