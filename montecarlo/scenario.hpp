#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace ActuaLib {

struct SampleDef {
    bool numeraire = false;
    std::vector<double> forwardMats;
    std::vector<double> discountMats;

    struct RateDef {
        double start;
        double end;
        std::string curve;

        RateDef(const double s, const double e, const std::string& c)
            : start(s), end(e), curve(c) {}
    };

    std::vector<RateDef> liborDefs;
    std::size_t numAssets = 1;

    SampleDef() = default;
};

template <class T>
struct Sample {
    T numeraire;
    std::vector<T> forwards;
    std::vector<T> discounts;
    std::vector<T> libors;

    void allocate(const SampleDef& data) {
        forwards.resize(data.numAssets * data.forwardMats.size());
        discounts.resize(data.discountMats.size());
        libors.resize(data.liborDefs.size());
    }

    void initialize() {
        numeraire = T(1.0);
        std::fill(forwards.begin(), forwards.end(), T(100.0));
        std::fill(discounts.begin(), discounts.end(), T(1.0));
        std::fill(libors.begin(), libors.end(), T(0.0));
    }
};

template <class T>
using Scenario = std::vector<Sample<T>>;

template <class T>
inline void allocatePath(const std::vector<SampleDef>& defline, Scenario<T>& path) {
    const std::size_t nSamples = defline.size();
    path.resize(nSamples);
    for (std::size_t i = 0; i < nSamples; ++i) {
        path[i].allocate(defline[i]);
    }
}

template <class T>
inline void initializePath(Scenario<T>& path) {
    for (auto& sample : path) {
        sample.initialize();
    }
}

} // namespace ActuaLib
