/** TRACCC library, part of the ACTS project (R&D line)
 *
 * (c) 2021-2022 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// Project include(s).
#include "traccc/edm/internal_spacepoint.hpp"
#include "traccc/edm/spacepoint.hpp"
#include "traccc/seeding/detail/seeding_config.hpp"
#include "traccc/seeding/detail/singlet.hpp"
#include "traccc/seeding/detail/spacepoint_grid.hpp"

// VecMem include(s).
#include <vecmem/memory/memory_resource.hpp>

namespace traccc {

inline std::pair<detray::axis::circular<>, detray::axis::regular<>> get_axes(
    const spacepoint_grid_config& grid_config, vecmem::memory_resource& mr) {

    detray::dindex phiBins;
    if (grid_config.bFieldInZ == 0) {
        phiBins = 100;
    } else {
        // calculate circle intersections of helix and max detector radius
        scalar minHelixRadius =
            grid_config.minPt / (300. * grid_config.bFieldInZ);  // in mm

        // sanity check: if yOuter takes the square root of a negative number
        if (minHelixRadius < grid_config.rMax / 2) {
            throw std::domain_error(
                "The value of minHelixRadius cannot be smaller than rMax / 2. "
                "Please "
                "check the configuration of bFieldInZ and minPt");
        }
        scalar maxR2 = grid_config.rMax * grid_config.rMax;
        scalar xOuter = maxR2 / (2 * minHelixRadius);
        scalar yOuter = std::sqrt(maxR2 - xOuter * xOuter);
        scalar outerAngle = std::atan(xOuter / yOuter);

        // intersection of helix and max detector radius minus maximum R
        // distance from middle SP to top SP
        scalar innerAngle = 0;
        scalar rMin = grid_config.rMax;
        if (grid_config.rMax > grid_config.deltaRMax) {
            rMin = grid_config.rMax - grid_config.deltaRMax;
            scalar innerCircleR2 = (grid_config.rMax - grid_config.deltaRMax) *
                                   (grid_config.rMax - grid_config.deltaRMax);
            scalar xInner = innerCircleR2 / (2 * minHelixRadius);
            scalar yInner = std::sqrt(innerCircleR2 - xInner * xInner);
            innerAngle = std::atan(xInner / yInner);
        }

        // evaluating the azimutal deflection including the maximum impact
        // parameter
        scalar deltaAngleWithMaxD0 =
            std::abs(std::asin(grid_config.impactMax / (rMin)) -
                     std::asin(grid_config.impactMax / grid_config.rMax));

        // evaluating delta Phi based on the inner and outer angle, and the
        // azimutal deflection including the maximum impact parameter Divide by
        // config.phiBinDeflectionCoverage since we combine
        // config.phiBinDeflectionCoverage number of consecutive phi bins in the
        // seed making step. So each individual bin should cover
        // 1/config.phiBinDeflectionCoverage of the maximum expected azimutal
        // deflection
        scalar deltaPhi = (outerAngle - innerAngle + deltaAngleWithMaxD0) /
                          grid_config.phiBinDeflectionCoverage;

        // sanity check: if the delta phi is equal to or less than zero, we'll
        // be creating an infinite or a negative number of bins, which would be
        // bad!
        if (deltaPhi <= 0.) {
            throw std::domain_error(
                "Delta phi value is equal to or less than zero, leading to an "
                "impossible number of bins (negative or infinite)");
        }

        // divide 2pi by angle delta to get number of phi-bins
        // size is always 2pi even for regions of interest
        phiBins = std::ceil(2 * M_PI / deltaPhi);
        // need to scale the number of phi bins accordingly to the number of
        // consecutive phi bins in the seed making step.
        // Each individual bin should be approximately a fraction (depending on
        // this number) of the maximum expected azimutal deflection.
    }

    detray::axis::circular m_phi_axis{phiBins, grid_config.phiMin,
                                      grid_config.phiMax, mr};

    // TODO: can probably be optimized using smaller z bins
    // and returning (multiple) neighbors only in one z-direction for forward
    // seeds
    // FIXME: zBinSize must include scattering

    scalar zBinSize = grid_config.cotThetaMax * grid_config.deltaRMax;
    detray::dindex zBins = std::max(
        1, (int)std::floor((grid_config.zMax - grid_config.zMin) / zBinSize));

    detray::axis::regular m_z_axis{zBins, grid_config.zMin, grid_config.zMax,
                                   mr};

    return {m_phi_axis, m_z_axis};
}

inline TRACCC_HOST_DEVICE size_t is_valid_sp(const seedfinder_config& config,
                                             const spacepoint& sp) {
    if (sp.z() > config.zMax || sp.z() < config.zMin) {
        return detray::detail::invalid_value<size_t>();
    }
    scalar spPhi = algebra::math::atan2(sp.y(), sp.x());
    if (spPhi > config.phiMax || spPhi < config.phiMin) {
        return detray::detail::invalid_value<size_t>();
    }
    size_t r_index = getter::perp(
        vector2{sp.x() - config.beamPos[0], sp.y() - config.beamPos[1]});

    if (r_index < config.get_num_rbins()) {
        return r_index;
    }

    return detray::detail::invalid_value<size_t>();
}

template <typename spacepoint_container_t,
          template <typename> class jagged_vector_type>
inline TRACCC_HOST_DEVICE void fill_radius_bins(
    const seedfinder_config& config, const spacepoint_container_t& sp_container,
    const sp_location sp_loc, jagged_vector_type<sp_location>& r_bins) {

    const spacepoint& sp =
        sp_container.get_items()[sp_loc.bin_idx][sp_loc.sp_idx];

    auto r_index = is_valid_sp(config, sp);

    if (r_index != detray::detail::invalid_value<size_t>()) {
        r_bins[r_index].push_back(sp_loc);
    }
}

}  // namespace traccc
