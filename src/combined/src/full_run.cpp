#include "combined/full_run.h"
#include "combined/waveguide_base.h"

namespace wayverb {
namespace combined {

postprocessing_engine::postprocessing_engine(
        const core::compute_context& compute_context,
        const core::gpu_scene_data& scene_data,
        const glm::vec3& source,
        const glm::vec3& receiver,
        const core::environment& environment,
        const raytracer::simulation_parameters& raytracer,
        std::unique_ptr<waveguide_base> waveguide)
        : engine_{compute_context,
                  scene_data,
                  source,
                  receiver,
                  environment,
                  raytracer,
                  std::move(waveguide)} {}

postprocessing_engine::engine_state_changed::connection
postprocessing_engine::connect_engine_state_changed(
        engine_state_changed::callback_type callback) {
    return engine_state_changed_.connect(std::move(callback));
}

postprocessing_engine::waveguide_node_pressures_changed::connection
postprocessing_engine::connect_waveguide_node_pressures_changed(
        waveguide_node_pressures_changed::callback_type callback) {
    return waveguide_node_pressures_changed_.connect(std::move(callback));
}

postprocessing_engine::raytracer_reflections_generated::connection
postprocessing_engine::connect_raytracer_reflections_generated(
        raytracer_reflections_generated::callback_type callback) {
    return raytracer_reflections_generated_.connect(std::move(callback));
}

//  get contents

const waveguide::voxels_and_mesh& postprocessing_engine::get_voxels_and_mesh()
        const {
    return engine_.get_voxels_and_mesh();
}

}  // namespace combined
}  // namespace wayverb
