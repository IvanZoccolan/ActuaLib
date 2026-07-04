#pragma once

#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include "../montecarlo/scenario.hpp"
#include "../products/product.hpp"


namespace ActuaLib {

    // Type trait: does T have a putOnTape() method?
    template<typename U, typename = void>
    struct has_putOnTape : std::false_type {};

    template<typename U>
    struct has_putOnTape<U, std::void_t<decltype(std::declval<U>().putOnTape())>> : std::true_type {};

    template<class T>
    class LifeModel {
    public:

        virtual void allocateProjection(
            const std::vector<double>& projectionTimeline,
            const std::vector<SampleDef>& projectionDefline) = 0;

        virtual void initProjection(
            const std::vector<double>& projectionTimeline,
            const std::vector<SampleDef>& projectionDefline) = 0;

        virtual size_t simDim() const = 0;

        virtual void generatePath(
            const std::vector<double>& gaussVec,
            Scenario<T>& path) const = 0;

        virtual std::unique_ptr<LifeModel<T>> cloneLife() const = 0;

        virtual const std::vector<T*>& stateParameters() const = 0;
        virtual const std::vector<std::string>& stateParameterLabels() const = 0;

        size_t numParameters() const {
            return const_cast<LifeModel*>(this)->stateParameters().size();
        }

        void putParametersOnTape() {
            if constexpr (has_putOnTape<T>::value) {
                for (auto* param : stateParameters()) {
                    param->putOnTape();
                }
            }
        }

        virtual ~LifeModel() = default;
    };
    
    template<class T>
    class Model : public LifeModel<T> {
        public:

            virtual void allocate(
                const std::vector<double>& prdTimeline,
                const std::vector<SampleDef>& prdDefline) = 0;

            void allocateProjection(
                const std::vector<double>& projectionTimeline,
                const std::vector<SampleDef>& projectionDefline) override {
                allocate(projectionTimeline, projectionDefline);
            }
            
            virtual void init(
                const std::vector<double>& prdTimeline,
                const std::vector<SampleDef>& prdDefline) = 0;

            void initProjection(
                const std::vector<double>& projectionTimeline,
                const std::vector<SampleDef>& projectionDefline) override {
                init(projectionTimeline, projectionDefline);
            }
            
            virtual size_t simDim() const override = 0;

            virtual void generatePath(
                const std::vector<double>& gaussVec,
                Scenario<T>& path) const override = 0;
            
            // Virtual destructor
            virtual ~Model() = default;

            virtual std::unique_ptr<Model<T>> clone() const = 0;

            std::unique_ptr<LifeModel<T>> cloneLife() const override {
                return clone();
            }

            virtual const std::vector<T*>& parameters() const = 0;
            virtual const std::vector<std::string>& parameterLabels() const = 0;

            const std::vector<T*>& stateParameters() const override {
                return parameters();
            }

            const std::vector<std::string>& stateParameterLabels() const override {
                return parameterLabels();
            }

            size_t numParameters() const {
                return const_cast<Model*>(this)->parameters().size();
            }

            void putParametersOnTape(){
                if constexpr (has_putOnTape<T>::value) {
                    for (auto* param : parameters()) {
                        param->putOnTape();
                    }
                }
            }
    };

}
