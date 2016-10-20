#pragma once

#include "raytracer/cl/structs.h"
#include "raytracer/image_source/exact.h"
#include "raytracer/image_source/run.h"
#include "raytracer/postprocess.h"
#include "raytracer/raytracer.h"

#include "common/cl/iterator.h"
#include "common/geo/box.h"
#include "common/mixdown.h"
#include "common/model/parameters.h"
#include "common/pressure_intensity.h"

template <typename Absorption>
auto run_exact_img_src(const geo::box& box,
                       Absorption absorption,
                       const model::parameters& params,
                       float simulation_time,
                       bool flip_phase) {
    auto ret = raytracer::image_source::find_impulses(
            box,
            params.source,
            params.receiver,
            absorption,
            simulation_time * params.speed_of_sound,
            flip_phase);

    //  Correct for distance travelled.
    for (auto& it : ret) {
        it.volume *=
                pressure_for_distance(it.distance, params.acoustic_impedance);
    }
    return ret;
}

template <typename Absorption>
aligned::vector<impulse<8>> run_fast_img_src(const geo::box& box,
                                             Absorption absorption,
                                             const model::parameters& params,
                                             bool flip_phase) {
    const auto voxelised = make_voxelised_scene_data(
            geo::get_scene_data(box, absorption), 2, 0.1f);

    const auto directions = get_random_directions(1 << 13);
    return raytracer::image_source::run(begin(directions),
                                        end(directions),
                                        compute_context{},
                                        voxelised,
                                        params,
                                        flip_phase);
}
