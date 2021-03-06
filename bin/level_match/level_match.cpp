#include "box/poly.h"

#include "combined/capsule_base.h"
#include "combined/engine.h"
#include "combined/waveguide_base.h"

#include "raytracer/simulation_parameters.h"

#include "waveguide/simulation_parameters.h"

#include "core/cl/common.h"
#include "core/dsp_vector_ops.h"
#include "core/environment.h"
#include "core/geo/box.h"
#include "core/scene_data_loader.h"

#include "utilities/string_builder.h"
#include "utilities/progress_bar.h"

#include "audio_file/audio_file.h"

#include <iostream>

auto load_scene(const std::string& name) {
    //  Attempt to load from file path.
    wayverb::core::scene_data_loader loader{name};

    const auto scene_data = loader.get_scene_data();
    if (!scene_data) {
        throw std::runtime_error{"Failed to load scene."};
    }

    return wayverb::core::make_scene_data(
            scene_data->get_triangles(),
            scene_data->get_vertices(),
            util::aligned::vector<
                    wayverb::core::surface<wayverb::core::simulation_bands>>(
                    scene_data->get_surfaces().size(),
                    wayverb::core::surface<wayverb::core::simulation_bands>{}));
}

int main(int argc, char** argv) {
    try {
        constexpr auto surface =
                wayverb::core::surface<wayverb::core::simulation_bands>{
                        {{0.07, 0.09, 0.11, 0.12, 0.13, 0.14, 0.16, 0.17}},
                        {{0.07, 0.09, 0.11, 0.12, 0.13, 0.14, 0.16, 0.17}}};

        if (argc != 2) {
            throw std::runtime_error{"Expected a scene file."};
        }

        auto scene_data = load_scene(argv[1]);
        scene_data.set_surfaces(surface);

        // const auto box = wayverb::core::geo::box{glm::vec3{0},
        //                                         glm::vec3{5.56, 3.97, 2.81}};
        // const auto scene_data =
        //        wayverb::core::geo::get_scene_data(box, surface);

        const auto aabb_centre = centre(
                wayverb::core::geo::compute_aabb(scene_data.get_vertices()));

        const auto source = aabb_centre + glm::vec3{0, 0, 0.2};
        const auto receiver = aabb_centre + glm::vec3{0, 0, -0.2};

        const auto rays = 1 << 16;
        const auto img_src_order = 4;

        const auto cutoff = 1000;
        const auto usable_portion = 0.6;

        const auto output_sample_rate = 44100.0;

        const auto params = wayverb::waveguide::single_band_parameters{cutoff, usable_portion};

        //  Set up simulation.
        wayverb::combined::engine engine{
                wayverb::core::compute_context{},
                scene_data,
                source,
                receiver,
                wayverb::core::environment{},
                wayverb::raytracer::simulation_parameters{rays, img_src_order},
                wayverb::combined::make_waveguide_ptr(params)};

        //  When the engine changes, print a nice progress bar.

        util::progress_bar pb{std::cout};
        engine.connect_engine_state_changed([&](auto state, auto progress) {
            set_progress(pb, progress);
        });

        //  Run simulation.
        const auto intermediate = engine.run(true);

        //  Do directional postprocessing.
        const wayverb::core::orientation receiver_orientation{};
        std::vector<util::named_value<
                std::unique_ptr<wayverb::combined::capsule_base>>>
                capsules;
        capsules.emplace_back(
                "microphone",
                wayverb::combined::make_capsule_ptr(
                        wayverb::core::attenuator::microphone{
                                wayverb::core::orientation{}, 0.5},
                        receiver_orientation));
        capsules.emplace_back(
                "hrtf",
                wayverb::combined::make_capsule_ptr(
                        wayverb::core::attenuator::hrtf{
                                wayverb::core::orientation{},
                                wayverb::core::attenuator::hrtf::channel::left},
                        receiver_orientation));

        auto output_channels = util::map_to_vector(
                begin(capsules), end(capsules), [&](const auto& i) {
                    return map(
                            [&](const auto& i) {
                                return i->postprocess(*intermediate,
                                                      output_sample_rate);
                            },
                            i);
                });

        //  Reduce volume to reasonable level.
        constexpr auto volume_factor = 0.05;
        for (auto& i : output_channels) {
            wayverb::core::mul(i.value, volume_factor);
        }

        //  Write out.
        for (const auto& i : output_channels) {
            write(util::build_string(i.name, ".wav").c_str(),
                  i.value,
                  output_sample_rate,
                  audio_file::format::wav,
                  audio_file::bit_depth::pcm16);
        }

    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
