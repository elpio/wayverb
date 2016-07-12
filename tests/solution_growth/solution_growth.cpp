#include "waveguide/config.h"
#include "waveguide/make_transparent.h"
#include "waveguide/microphone.h"
#include "waveguide/rectangular_waveguide.h"

#include "common/azimuth_elevation.h"
#include "common/cl_common.h"
#include "common/conversions.h"
#include "common/kernel.h"
#include "common/scene_data.h"
#include "common/dc_blocker.h"

#include "common/serialize/boundaries.h"
#include "common/serialize/surface.h"

#include "common/filters_common.h"
#include "common/sinc.h"
#include "common/write_audio_file.h"

#include "samplerate.h"
#include "sndfile.hh"

#include <gflags/gflags.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>

namespace {
auto uniform_surface(float r) {
    return Surface{VolumeType{{r, r, r, r, r, r, r, r}},
                   VolumeType{{r, r, r, r, r, r, r, r}}};
}
}  // namespace

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    try {
        glm::vec3 source{0, 0, 0};
        glm::vec3 receiver{0.5, 0, 0};
        auto sampling_frequency = 20000.0;

        ComputeContext compute_context;
        CuboidBoundary cuboid_boundary(glm::vec3(-1), glm::vec3(1));

        RectangularProgram waveguide_program(compute_context.context,
                                             compute_context.device);

        auto scene_data = cuboid_boundary.get_scene_data();
        scene_data.set_surfaces(uniform_surface(0.999));

        RectangularWaveguide<BufferType::cl> waveguide(waveguide_program,
                                                       MeshBoundary(scene_data),
                                                       receiver,
                                                       sampling_frequency);

        auto receiver_index = waveguide.get_index_for_coordinate(receiver);
        auto source_index = waveguide.get_index_for_coordinate(source);

        auto corrected_source =
                waveguide.get_coordinate_for_index(source_index);

        struct signal {
            std::string name;
            std::vector<float> kernel;
        };

        std::vector<signal> signals{
                signal{"dirac", std::vector<float>{1.0}},
                signal{"gauss", kernels::gaussian_kernel(sampling_frequency)},
                signal{"sinmod_gauss",
                       kernels::sin_modulated_gaussian_kernel(
                               sampling_frequency)},
                signal{"ricker", kernels::ricker_kernel(sampling_frequency)},
                signal{"hi_sinc",
                       hipass_sinc_kernel(sampling_frequency, 100, 401)},
                signal{"band_sinc",
                       bandpass_sinc_kernel(sampling_frequency,
                                            100,
                                            sampling_frequency * 0.25,
                                            401)},
        };

        for (const auto& i : signals) {
            auto steps = 10000;

            auto kernel = make_transparent(i.kernel);

            std::atomic_bool keep_going{true};
            ProgressBar pb(std::cout, steps);
            auto results = waveguide.init_and_run(corrected_source,
                                                  kernel,
                                                  receiver_index,
                                                  steps,
                                                  keep_going,
                                                  [&pb] { pb += 1; });

            auto output = std::vector<float>(results.size());
            proc::transform(results, output.begin(), [](const auto& i) {
                return i.pressure;
            });

            {
                auto fname = build_string(
                        "solution_growth.", i.name, ".transparent.wav");

                snd::write(fname, {output}, sampling_frequency, 16);
            }

            filter::ZeroPhaseDCBlocker blocker(32);
            auto dc_removed = blocker.filter(output.begin(), output.end());

            {
                auto fname = build_string(
                        "solution_growth.", i.name, ".dc_blocked.wav");
                snd::write(fname, {dc_removed}, sampling_frequency, 16);
            }
        }
    } catch (const cl::Error& e) {
        LOG(INFO) << "critical cl error: " << e.what();
        return EXIT_FAILURE;
    } catch (const std::runtime_error& e) {
        LOG(INFO) << "critical runtime error: " << e.what();
        return EXIT_FAILURE;
    } catch (...) {
        LOG(INFO) << "unknown error";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
