#pragma once

/*
    Base class (pure virtual) for mortality models.

    Provides the interface that any mortality model must implement:
      - p(x, t)       →  _tp_x   survival probability
      - q(x, t)       →  _tq_x   death probability

    Derived classes may be:
      - Table-based  (MortalityTable: one-year q(x) + UDD)
      - Parametric   (Gompertz, Makeham, etc.)
      - Stochastic   (CIR-based intensity models, etc.)

    All types are plain double / Probability — not on the AAD tape.
*/

#include "../types.hpp"
#include <memory>

namespace ActuaLib {

    class MortalityModel {
    public:

        virtual ~MortalityModel() = default;

        // ==================================================================
        //  Core interface  (pure virtual)
        // ==================================================================

        /// _tp_x :  probability that a life aged x survives at least t years
        virtual Probability p(double x, double t) const = 0;

        /// _tq_x :  probability that a life aged x dies within t years
        virtual Probability q(double x, double t) const = 0;

        /// Minimum age supported by the model
        virtual int minAge() const = 0;

        /// Maximum age supported by the model
        virtual int maxAge() const = 0;

        /// Deep copy
        virtual std::unique_ptr<MortalityModel> clone() const = 0;

        // ==================================================================
        //  Convenience methods  (virtual with default implementations)
        // ==================================================================

        /// Deferred mortality:  _s|t_q_x  = _sp_x − _{s+t}p_x
        virtual Probability deferredQ(double x, double s, double t) const {
            return p(x, s) - p(x, s + t);
        }

        /// One-year survival probability for integer age:  p_x = 1 − q_x
        virtual Probability px(int age) const {
            return p(static_cast<double>(age), 1.0);
        }

        /// One-year death probability for integer age:  q_x
        virtual Probability qx(int age) const {
            return q(static_cast<double>(age), 1.0);
        }
    };

} // namespace ActuaLib
