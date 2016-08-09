#pragma once

#include "common/boundaries.h"

namespace waveguide {

geo::box compute_adjusted_boundary(const geo::box& min_boundary,
                                   const glm::vec3& anchor,
                                   float cube_side);

}  // namespace waveguide
