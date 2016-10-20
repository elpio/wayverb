#pragma once

#include "raytracer/cl/structs.h"
#include "raytracer/image_source/tree.h"

#include "common/callback_accumulator.h"
#include "common/spatial_division/voxelised_scene_data.h"

#include "utilities/aligned/vector.h"

#include <future>

namespace raytracer {
namespace image_source {

aligned::vector<impulse<8>> postprocess_branches(
        const multitree<path_element>& tree,
        const glm::vec3& source,
        const glm::vec3& receiver,
        const voxelised_scene_data<cl_float3, surface>& voxelised,
        bool flip_phase);

template <typename It>
auto postprocess_branches(
        It b_branches,
        It e_branches,
        const glm::vec3& source,
        const glm::vec3& receiver,
        const voxelised_scene_data<cl_float3, surface>& voxelised,
        bool flip_phase) {
    auto futures = map_to_vector(
            b_branches, e_branches, [&](const auto& branch) {
                return std::async(std::launch::async, [&] {
                    return postprocess_branches(
                            branch, source, receiver, voxelised, flip_phase);
                });
            });

    using value_type = decltype(
            std::declval<typename decltype(futures)::value_type>().get());

    //  Collect futures.
    value_type ret;
    for (auto& fut : futures) {
        const auto thread_results = fut.get();
        ret.insert(ret.end(), thread_results.begin(), thread_results.end());
    }

    return ret;
}

}  // namespace image_source
}  // namespace raytracer
