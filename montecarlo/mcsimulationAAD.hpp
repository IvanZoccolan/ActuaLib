#pragma once

#include "AAD.hpp"
#include "scenario.hpp"
#include "../esg/model.hpp"
#include "../products/product.hpp"
#include "../math/randomnumbers/genericrng.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace ActuaLib {

    using std::vector;

    struct AADSimulResults {

        AADSimulResults(
            const size_t nPaths,
            const size_t nPay,
            const size_t nParam) :
            payoffs(nPaths, std::vector<double>(nPay)),
            aggregated(nPaths),
            risks(nParam)
        {}
        
        // matrix of payoffs: nPaths x nPayoffs
        vector<vector<double>> payoffs;
        // vector of aggregated payoffs: nPaths
        vector<double> aggregated;
        // vector of risks sensitivities of aggregated payoffs: nParameters
        vector<double> risks;
    };

    // Default aggregator for payoffs = first payoff 
    const auto defaultAggregator = [](const vector<Number>& payoffs) {
        return payoffs[0];
    };

    struct LifeAADResults {
        vector<vector<double>> pathOutputs;
        vector<double> pathLiability;
        vector<double> parameterSensitivities;
        vector<std::string> outputLabels;
        vector<std::string> parameterLabels;
    };

    template<class F = decltype(defaultAggregator)>
    inline LifeAADResults lifeSimulationAAD(
        const LifeProduct<Number>& prd,
        const LifeModel<Number>& mdl,
        const RNG& rng,
        const size_t nPaths,
        const F& aggFun = defaultAggregator) {

        auto cMdl = mdl.cloneLife();
        auto cRng = rng.clone();

        // Allocate path and model
        Scenario<Number> path;
        allocatePath(prd.projectionDefline(), path);
        cMdl -> allocateProjection(prd.projectionTimeline(), prd.projectionDefline());

        const size_t nPay = prd.outputLabels().size();
        const vector<Number*>& params = cMdl -> stateParameters();
        const size_t nParam = params.size();

        Tape& tape = *Number::tape;
        tape.clear();

        cMdl -> putParametersOnTape();
        cMdl -> initProjection(prd.projectionTimeline(), prd.projectionDefline());
        initializePath(path);
        tape.mark();

        cRng -> init(cMdl -> simDim());
        vector<Number> nPayoffs(nPay);
        vector<double> gaussVec(cMdl -> simDim());

        LifeAADResults results;
        results.pathOutputs.assign(nPaths, vector<double>(nPay));
        results.pathLiability.assign(nPaths, 0.0);
        results.parameterSensitivities.assign(nParam, 0.0);
        results.outputLabels = prd.outputLabels();
        results.parameterLabels = cMdl->stateParameterLabels();

        for (size_t i = 0; i < nPaths; ++i) {
            tape.rewindToMark();
            cRng -> nextG(gaussVec);
            cMdl -> generatePath(gaussVec, path);
            prd.evaluateLiability(path, nPayoffs);
            Number result = aggFun(nPayoffs);

            result.propagateToMark();
            results.pathLiability[i] = double(result);
            convertCollection(nPayoffs.begin(), nPayoffs.end(), results.pathOutputs[i].begin());
        }

        Number::propagateMarkToStart();

        std::transform(params.begin(), params.end(), results.parameterSensitivities.begin(),
            [nPaths](const Number* param) { return param->adjoint() / nPaths; });

        tape.clear();
        return results;
    }


    template<class F = decltype(defaultAggregator)>
    inline AADSimulResults mcSimulationAAD(
        const LifeProduct<Number>& prd,
        const LifeModel<Number>& mdl,
        const RNG& rng,
        const size_t nPaths,
        const F& aggFun = defaultAggregator) {
        const auto life = lifeSimulationAAD(prd, mdl, rng, nPaths, aggFun);
        AADSimulResults legacy(
            life.pathOutputs.size(),
            life.pathOutputs.empty() ? 0 : life.pathOutputs.front().size(),
            life.parameterSensitivities.size());
        legacy.payoffs = life.pathOutputs;
        legacy.aggregated = life.pathLiability;
        legacy.risks = life.parameterSensitivities;
        return legacy;
    };


} // namespace ActuaLib