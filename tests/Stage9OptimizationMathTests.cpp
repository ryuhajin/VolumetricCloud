#include "Stage9OptimizationMath.h"
#include <cstdlib>
#include <iostream>
#include <limits>
namespace { void Require(bool v,const char* m){if(!v){std::cerr<<m<<'\n';std::exit(1);}} }
int main()
{
    Require(sizeof(OptimizationParameters)==32u,"OptimizationCB size");
    OptimizationParameters p; p.baseDensityEpsilon=std::numeric_limits<float>::quiet_NaN(); p.coarseStepMultiplier=100; p.emptySamplesBeforeCoarse=0; p=stage9optimization::Sanitize(p);
    Require(std::isfinite(p.baseDensityEpsilon)&&p.coarseStepMultiplier==8&&p.emptySamplesBeforeCoarse==1,"sanitize");
    const auto full=stage9::MarchUniform(1,1,.1,128,false,.01), early=stage9::MarchUniform(1,1,.1,128,true,.01), empty=stage9::MarchUniform(0,1,.1,128,true,.01);
    Require(early.executedSteps<full.executedSteps&&early.transmittance<=.01,"early exit"); Require(empty.executedSteps==128&&empty.transmittance==1,"transparent");
    const auto s=stage9::ComputeStatistics({1,2,3,4,5,std::numeric_limits<double>::quiet_NaN()}); Require(s.validSamples==5&&s.p50==3&&s.p95>4&&s.p95<=5,"statistics");
    Require(stage9::PassesPerformanceGate(10,8.4,{{10,10.2},{5,5.1}}),"gate pass"); Require(!stage9::PassesPerformanceGate(10,8.6,{{10,10}}),"dense gate"); Require(!stage9::PassesPerformanceGate(10,8,{{10,10.4}}),"regression gate");
    std::cout<<"Stage9OptimizationMath passed\n";
}
