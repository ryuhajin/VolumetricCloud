#pragma once

#include "OptimizationParameters.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>
#include <vector>

namespace stage9
{
struct TimingStatistics { double minimum=0, mean=0, p50=0, p95=0, maximum=0, standardDeviation=0; std::size_t validSamples=0; };

inline double Percentile(std::vector<double> values, double percentile)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !std::isfinite(v) || v < 0.0; }), values.end());
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(percentile, 0.0, 1.0) * (values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    return values[lower] + (values[upper] - values[lower]) * (position - lower);
}

inline TimingStatistics ComputeStatistics(std::vector<double> values)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !std::isfinite(v) || v < 0.0; }), values.end());
    TimingStatistics r; r.validSamples = values.size(); if (values.empty()) return r;
    r.minimum=*std::min_element(values.begin(),values.end()); r.maximum=*std::max_element(values.begin(),values.end());
    r.mean=std::accumulate(values.begin(),values.end(),0.0)/values.size(); r.p50=Percentile(values,.5); r.p95=Percentile(values,.95);
    for(double v:values) r.standardDeviation+=(v-r.mean)*(v-r.mean); r.standardDeviation=std::sqrt(r.standardDeviation/values.size()); return r;
}

inline bool PassesPerformanceGate(double baselineDense, double optimizedDense,
    const std::vector<std::pair<double,double>>& others)
{
    if (!(baselineDense>0) || !std::isfinite(optimizedDense) || (baselineDense-optimizedDense)/baselineDense<.15) return false;
    for (const auto& pair:others) if (!(pair.first>0) || !std::isfinite(pair.second) || (pair.second-pair.first)/pair.first>.03) return false;
    return true;
}

struct UniformMarchResult { double transmittance=1.0; std::size_t executedSteps=0; };
inline UniformMarchResult MarchUniform(double density,double extinction,double stepLength,std::size_t maximumSteps,bool earlyExit,double threshold)
{
    UniformMarchResult r; if(!std::isfinite(density)||!std::isfinite(extinction)||!std::isfinite(stepLength)||stepLength<=0) return r;
    threshold=std::clamp(std::isfinite(threshold)?threshold:.01,0.0,.1);
    const double stepT=std::exp(-std::max(density,0.0)*std::max(extinction,0.0)*stepLength);
    while(r.executedSteps<maximumSteps){r.transmittance*=stepT;++r.executedSteps;if(earlyExit&&threshold>0&&r.transmittance<=threshold)break;} return r;
}
}
