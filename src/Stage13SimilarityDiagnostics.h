#pragma once

#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>

namespace stage13diagnostics
{
enum class ComparisonKind
{
    CircularRgb,
    Scalar,
    Rgb,
};

struct MetricSummary
{
    double mae = 0.0;
    double rmse = 0.0;
    double p99 = 0.0;
    double maximum = 0.0;
    double overThresholdRatio = 0.0;
    double densityMaskMismatchRatio = 0.0;
    std::size_t comparedPixels = 0;
    std::size_t largestComponentArea = 0;
    int largestComponentWidth = 0;
    int largestComponentHeight = 0;
};

inline double CircularDistance(double a, double b)
{
    const double difference = std::abs(a - b);
    return std::min(difference, std::abs(1.0 - difference));
}

inline double PixelError(const DirectX::XMFLOAT4& reference,
                         const DirectX::XMFLOAT4& current,
                         ComparisonKind kind)
{
    if (kind == ComparisonKind::CircularRgb)
    {
        return (CircularDistance(reference.x, current.x) +
                CircularDistance(reference.y, current.y) +
                CircularDistance(reference.z, current.z)) / 3.0;
    }
    if (kind == ComparisonKind::Rgb)
    {
        return (std::abs(static_cast<double>(reference.x) - current.x) +
                std::abs(static_cast<double>(reference.y) - current.y) +
                std::abs(static_cast<double>(reference.z) - current.z)) / 3.0;
    }
    return std::abs(static_cast<double>(reference.x) - current.x);
}

inline MetricSummary CompareFrames(
    const CloudDiagnosticFrame& reference,
    const CloudDiagnosticFrame& current,
    const CloudDiagnosticFrame* referenceHitMask,
    const CloudDiagnosticFrame* currentHitMask,
    ComparisonKind kind, double mismatchThreshold,
    bool compareDensityMask)
{
    MetricSummary result;
    if (reference.width != current.width ||
        reference.height != current.height ||
        reference.pixels.size() != current.pixels.size() ||
        reference.pixels.empty())
        return result;

    const int width = reference.width;
    const int height = reference.height;
    const std::size_t pixelCount = reference.pixels.size();
    std::vector<double> errors;
    errors.reserve(pixelCount);
    std::vector<unsigned char> mismatch(pixelCount, 0);
    std::size_t overThreshold = 0;
    std::size_t densityMaskMismatch = 0;
    double squaredSum = 0.0;

    for (std::size_t index = 0; index < pixelCount; ++index)
    {
        const bool referenceHit = !referenceHitMask ||
            referenceHitMask->pixels[index].x > 0.5f;
        const bool currentHit = !currentHitMask ||
            currentHitMask->pixels[index].x > 0.5f;
        if (!referenceHit && !currentHit)
            continue;

        const double error = PixelError(
            reference.pixels[index], current.pixels[index], kind);
        errors.push_back(error);
        result.mae += error;
        squaredSum += error * error;
        result.maximum = std::max(result.maximum, error);
        const bool over = error > mismatchThreshold;
        if (over)
            ++overThreshold;

        bool maskDifferent = false;
        if (compareDensityMask)
        {
            maskDifferent = (reference.pixels[index].x > 0.01f) !=
                (current.pixels[index].x > 0.01f);
            if (maskDifferent)
                ++densityMaskMismatch;
        }
        mismatch[index] = (over || maskDifferent) ? 1 : 0;
    }

    result.comparedPixels = errors.size();
    if (errors.empty())
        return result;
    result.mae /= static_cast<double>(errors.size());
    result.rmse = std::sqrt(squaredSum / static_cast<double>(errors.size()));
    std::sort(errors.begin(), errors.end());
    const std::size_t p99Index = std::min(
        errors.size() - 1,
        static_cast<std::size_t>(std::ceil(errors.size() * 0.99)) - 1);
    result.p99 = errors[p99Index];
    result.overThresholdRatio =
        static_cast<double>(overThreshold) / errors.size();
    result.densityMaskMismatchRatio =
        static_cast<double>(densityMaskMismatch) / errors.size();

    std::vector<unsigned char> visited(pixelCount, 0);
    for (int startY = 0; startY < height; ++startY)
    {
        for (int startX = 0; startX < width; ++startX)
        {
            const std::size_t start = static_cast<std::size_t>(startY) * width + startX;
            if (!mismatch[start] || visited[start])
                continue;
            std::queue<std::pair<int, int>> pending;
            pending.push({ startX, startY });
            visited[start] = 1;
            std::size_t area = 0;
            int minX = startX;
            int maxX = startX;
            int minY = startY;
            int maxY = startY;
            while (!pending.empty())
            {
                const auto [x, y] = pending.front();
                pending.pop();
                ++area;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
                const int neighbors[4][2] = {
                    { x - 1, y }, { x + 1, y },
                    { x, y - 1 }, { x, y + 1 }
                };
                for (const auto& neighbor : neighbors)
                {
                    const int nx = neighbor[0];
                    const int ny = neighbor[1];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                        continue;
                    const std::size_t next =
                        static_cast<std::size_t>(ny) * width + nx;
                    if (mismatch[next] && !visited[next])
                    {
                        visited[next] = 1;
                        pending.push({ nx, ny });
                    }
                }
            }
            if (area > result.largestComponentArea)
            {
                result.largestComponentArea = area;
                result.largestComponentWidth = maxX - minX + 1;
                result.largestComponentHeight = maxY - minY + 1;
            }
        }
    }
    return result;
}
}
