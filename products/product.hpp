#pragma once

#include <memory>
#include <vector>
#include <string>
#include "../montecarlo/scenario.hpp"

namespace ActuaLib {

    template<class T>
    class LifeProduct {
        public:

            virtual const std::vector<double>& projectionTimeline() const = 0;
            virtual const std::vector<SampleDef>& projectionDefline() const = 0;

            virtual const std::vector<std::string>& outputLabels() const = 0;

            virtual void evaluateLiability(
                const Scenario<T>& path,
                std::vector<T>& outputs) const = 0;

            virtual std::unique_ptr<LifeProduct<T>> cloneLife() const = 0;

            virtual ~LifeProduct() = default;
    };

    template<class T>
    class Product : public LifeProduct<T> {
        public:

            virtual const std::vector<double>& timeline() const = 0;
            virtual const std::vector<SampleDef>& defline() const = 0;

            const std::vector<double>& projectionTimeline() const override {
                return timeline();
            }

            const std::vector<SampleDef>& projectionDefline() const override {
                return defline();
            }

            virtual const std::vector<std::string>& payoffLabels() const = 0; 

            const std::vector<std::string>& outputLabels() const override {
                return payoffLabels();
            }

            virtual void payoffs(const Scenario<T>& path,
                std::vector<T>& payoffs) const = 0;

            void evaluateLiability(
                const Scenario<T>& path,
                std::vector<T>& outputs) const override {
                payoffs(path, outputs);
            }

            virtual std::unique_ptr<Product<T>> clone() const = 0;

            std::unique_ptr<LifeProduct<T>> cloneLife() const override {
                return clone();
            }
            
            // Virtual destructor
            virtual ~Product() = default;
    };

} // namespace ActuaLib
