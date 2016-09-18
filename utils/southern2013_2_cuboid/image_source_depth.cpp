#include "image_source_depth.h"

#include "raytracer/image_source/run.h"
#include "raytracer/raytracer.h"

#include "common/azimuth_elevation.h"
#include "common/dsp_vector_ops.h"
#include "common/string_builder.h"
#include "common/write_audio_file.h"

image_source_depth_test::image_source_depth_test(const scene_data& sd,
                                                 float speed_of_sound,
                                                 float acoustic_impedance)
        : voxelised_{sd, 5, padded(sd.get_aabb(), glm::vec3{0.1})}
        , speed_of_sound_{speed_of_sound}
        , acoustic_impedance_{acoustic_impedance} {}

audio image_source_depth_test::operator()(
        const surface& surface,
        const glm::vec3& source,
        const model::receiver_settings& receiver) {
    voxelised_.set_surfaces(surface);

    const auto sample_rate{44100.0};

    const auto directions{get_random_directions(10000)};

    auto impulses{raytracer::image_source::run<
            raytracer::image_source::depth_counter_calculator<
                    raytracer::image_source::fast_pressure_calculator>>(
            directions.begin(),
            directions.end(),
            compute_context_,
            voxelised_,
            source,
            receiver.position,
            speed_of_sound_,
            acoustic_impedance_,
            sample_rate)};

    aligned::map<size_t, aligned::vector<impulse>> impulses_by_depth{};
    for (const auto& i : impulses) {
        impulses_by_depth[i.depth].emplace_back(i.value);
    }

    const auto mixdown_and_convert{[=](const auto& i) {
        return map_to_vector(
                mixdown(raytracer::convert_to_histogram(
                        i.begin(), i.end(), speed_of_sound_, sample_rate, 20)),
                [=](auto i) {
                    return intensity_to_pressure(i, acoustic_impedance_) * 0.1f;
                });
    }};

    static auto count{0};
    for (const auto& i : impulses_by_depth) {
        const auto img_src_results{mixdown_and_convert(i.second)};
        snd::write(build_string("img_src_depth_", i.first, "_", count, ".wav"),
                   {img_src_results},
                   sample_rate,
                   16);
    }
    count += 1;
    return {mixdown_and_convert(impulses_by_depth.begin()->second),
            sample_rate,
            "img_src_depth"};
}