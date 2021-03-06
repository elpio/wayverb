#pragma once

#include "core/cl/scene_structs.h"
#include "core/geo/triangle_vec.h"

namespace wayverb {
namespace core {
namespace geo {
namespace detail {
constexpr glm::vec2 perp(const glm::vec2& a) { return glm::vec2(-a.y, a.x); }

inline glm::vec2 normal_2d(const glm::vec2& a, const glm::vec2& b) {
    return perp(b - a);
}

template <typename It, typename Out>
void normals_2d(It begin, It end, Out o) {
    const auto dist = std::distance(begin, end);
    if (dist == 0) {
        throw std::runtime_error("Empty range passed to normal-finder.");
    }
    if (dist == 1) {
        throw std::runtime_error("Single point passed to normal-finder.");
    }
    *o++ = normal_2d(*(end - 1), *begin++);
    for (; begin != end; ++begin) {
        *o++ = normal_2d(*(begin - 1), *begin);
    }
}

template <size_t n>
std::array<glm::vec2, n> normals_2d(const std::array<glm::vec2, n>& u) {
    std::array<glm::vec2, n> ret;
    detail::normals_2d(u.begin(), u.end(), ret.begin());
    return ret;
}

struct projection_2d final {
    float min;
    float max;
};

constexpr bool operator<(const projection_2d& a, const projection_2d& b) {
    return a.min < b.min;
}

constexpr bool operator==(const projection_2d& a, const projection_2d& b) {
    return std::tie(a.min, a.max) == std::tie(b.min, b.max);
}

template <typename It>
projection_2d project(It begin, It end, const glm::vec2& axis) {
    if (begin == end) {
        throw std::runtime_error("Null shape passed to projection function.");
    }

    const auto first = glm::dot(axis, *begin++);
    projection_2d ret{first, first};
    for (; begin != end; ++begin) {
        const auto p = glm::dot(axis, *begin);
        ret.min = std::min(ret.min, p);
        ret.max = std::max(ret.max, p);
    }
    return ret;
}

constexpr bool overlaps(const projection_2d& a, const projection_2d& b) {
    const auto min_max = std::minmax(a, b);
    return min_max.second.min <= min_max.first.max;
}

template <typename It, typename Jt>
bool overlaps(
        It i_begin, It i_end, Jt j_begin, Jt j_end, const glm::vec2& axis) {
    return overlaps(project(i_begin, i_end, axis),
                    project(j_begin, j_end, axis));
}

template <typename It, typename Jt, typename Kt>
bool overlaps(It i_begin,
              It i_end,
              Jt j_begin,
              Jt j_end,
              Kt axes_begin,
              Kt axes_end) {
    return std::all_of(axes_begin, axes_end, [=](const auto& i) {
        return overlaps(i_begin, i_end, j_begin, j_end, i);
    });
}
}  // namespace detail

/// Implements test for overlapping primitives using the separating-axis
/// theorem.
/// Shouldn't do any allocation, hopefully very fast as a result!
template <size_t n, size_t m>
bool overlaps_2d(const std::array<glm::vec2, n>& a,
                 const std::array<glm::vec2, m>& b) {
    const auto a_axes = detail::normals_2d(a);
    const auto b_axes = detail::normals_2d(b);
    return detail::overlaps(a.begin(),
                            a.end(),
                            b.begin(),
                            b.end(),
                            a_axes.begin(),
                            a_axes.end()) &&
           detail::overlaps(a.begin(),
                            a.end(),
                            b.begin(),
                            b.end(),
                            b_axes.begin(),
                            b_axes.end());
}

}  // namespace geo
}  // namespace core
}  // namespace wayverb
