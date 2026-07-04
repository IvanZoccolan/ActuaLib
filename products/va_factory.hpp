#pragma once

/*
    VA product factory — makeRider<T>()

    Creates the appropriate VA product given a RiderType enum.
    All 9 rider types from Gan & Valdez (2017) are supported:

        GMDB        — Guaranteed Minimum Death Benefit
        GMMB        — Guaranteed Minimum Maturity Benefit
        GMAB        — Guaranteed Minimum Accumulation Benefit (renewable)
        GMWB        — Guaranteed Minimum Withdrawal Benefit
        GMIB        — Guaranteed Minimum Income Benefit
        GMDB_AB     — GMDB + GMAB combo
        GMDB_MB     — GMDB + GMMB combo
        GMDB_WB     — GMDB + GMWB combo
        GMDB_IB     — GMDB + GMIB combo

    Usage:
        auto product = makeRider<Number>(RiderType::GMAB, policy, mortality);
*/

#include "va_gmdb.hpp"
#include "va_gmmb.hpp"
#include "va_gmab.hpp"
#include "va_gmwb.hpp"
#include "va_gmib.hpp"
#include "va_combo.hpp"
#include "../demographics/mortality_model.hpp"
#include "product.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace ActuaLib {

    // ======================================================================
    //  RiderType enumeration
    // ======================================================================

    enum class RiderType {
        GMDB,
        GMMB,
        GMAB,
        GMWB,
        GMIB,
        GMDB_AB,
        GMDB_MB,
        GMDB_WB,
        GMDB_IB
    };

    /// Convert a RiderType to its string name.
    inline std::string riderTypeName(RiderType rt) {
        switch (rt) {
        case RiderType::GMDB:    return "GMDB";
        case RiderType::GMMB:    return "GMMB";
        case RiderType::GMAB:    return "GMAB";
        case RiderType::GMWB:    return "GMWB";
        case RiderType::GMIB:    return "GMIB";
        case RiderType::GMDB_AB: return "GMDB_AB";
        case RiderType::GMDB_MB: return "GMDB_MB";
        case RiderType::GMDB_WB: return "GMDB_WB";
        case RiderType::GMDB_IB: return "GMDB_IB";
        default: return "Unknown";
        }
    }

    // ======================================================================
    //  makeRider<T>()
    // ======================================================================

    template <class T>
    std::unique_ptr<Product<T>> makeRider(
        RiderType           riderType,
        const VAPolicy&     policy,
        const MortalityModel& mortality)
    {
        switch (riderType) {
        case RiderType::GMDB:
            return std::make_unique<GMDB<T>>(policy, mortality);
        case RiderType::GMMB:
            return std::make_unique<GMMB<T>>(policy, mortality);
        case RiderType::GMAB:
            return std::make_unique<GMAB<T>>(policy, mortality);
        case RiderType::GMWB:
            return std::make_unique<GMWB<T>>(policy, mortality);
        case RiderType::GMIB:
            return std::make_unique<GMIB<T>>(policy, mortality);
        case RiderType::GMDB_AB:
            return std::make_unique<GMDB_AB<T>>(policy, mortality);
        case RiderType::GMDB_MB:
            return std::make_unique<GMDB_MB<T>>(policy, mortality);
        case RiderType::GMDB_WB:
            return std::make_unique<GMDB_WB<T>>(policy, mortality);
        case RiderType::GMDB_IB:
            return std::make_unique<GMDB_IB<T>>(policy, mortality);
        default:
            throw std::runtime_error(
                "makeRider: unknown RiderType " +
                std::to_string(static_cast<int>(riderType)));
        }
    }

} // namespace ActuaLib
