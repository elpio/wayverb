#include "raytracer/postprocess.h"

#include "raytracer/attenuator.h"

#include "common/decibels.h"
#include "common/dsp_vector_ops.h"
#include "common/map_to_vector.h"

namespace raytracer {

/// Keep tracing until power has fallen by 60 decibels.
size_t compute_optimum_reflection_number(float absorption) {
    return -3 / std::log10(1 - absorption);
}

/// Find the index of the last sample with an amplitude of minVol or higher,
/// then resize the vectors down to this length.
void trimTail(aligned::vector<aligned::vector<float>>& audioChannels,
              float minVol) {
    using index_type = std::common_type_t<
            std::iterator_traits<
                    aligned::vector<float>::reverse_iterator>::difference_type,
            int>;

    // Find last index of required amplitude or greater.
    auto len = proc::accumulate(
            audioChannels, 0, [minVol](auto current, const auto& i) {
                return std::max(
                        index_type{current},
                        index_type{
                                distance(i.begin(),
                                         std::find_if(i.rbegin(),
                                                      i.rend(),
                                                      [minVol](auto j) {
                                                          return std::abs(j) >=
                                                                 minVol;
                                                      })
                                                 .base()) -
                                1});
            });

    // Resize.
    for (auto&& i : audioChannels)
        i.resize(len);
}

//----------------------------------------------------------------------------//

template <enum model::receiver_settings::mode mode>
aligned::vector<aligned::vector<float>> run_attenuation(
        const compute_context& cc,
        const model::receiver_settings& receiver,
        const aligned::vector<impulse>& input,
        double output_sample_rate,
        double speed_of_sound,
        double acoustic_impedance,
        double max_seconds);

template <>
aligned::vector<aligned::vector<float>>
run_attenuation<model::receiver_settings::mode::microphones>(
        const compute_context& cc,
        const model::receiver_settings& receiver,
        const aligned::vector<impulse>& input,
        double output_sample_rate,
        double speed_of_sound,
        double acoustic_impedance,
        double max_seconds) {
    raytracer::attenuator::microphone attenuator{cc, speed_of_sound};
    return map_to_vector(receiver.microphones, [&](const auto& i) {
        const auto processed{attenuator.process(
                input,
                get_pointing(i.orientable, receiver.position),
                i.shape,
                receiver.position)};
        return flatten_filter_and_mixdown(processed.begin(),
                                          processed.end(),
                                          output_sample_rate,
                                          acoustic_impedance,
                                          max_seconds);
    });
}

template <>
aligned::vector<aligned::vector<float>>
run_attenuation<model::receiver_settings::mode::hrtf>(
        const compute_context& cc,
        const model::receiver_settings& receiver,
        const aligned::vector<impulse>& input,
        double output_sample_rate,
        double speed_of_sound,
        double acoustic_impedance,
        double max_seconds) {
    raytracer::attenuator::hrtf attenuator{cc, speed_of_sound};
    const auto channels = {hrtf_channel::left, hrtf_channel::right};
    return map_to_vector(channels, [&](const auto& i) {
        const auto processed{attenuator.process(
                input,
                get_pointing(receiver.hrtf, receiver.position),
                glm::vec3(0, 1, 0),
                receiver.position,
                i)};
        return flatten_filter_and_mixdown(processed.begin(),
                                          processed.end(),
                                          output_sample_rate,
                                          acoustic_impedance,
                                          max_seconds);
    });
}

aligned::vector<aligned::vector<float>> run_attenuation(
        const compute_context& cc,
        const model::receiver_settings& receiver,
        const aligned::vector<impulse>& input,
        double output_sample_rate,
        double speed_of_sound,
        double acoustic_impedance,
        double max_seconds) {
    switch (receiver.mode) {
        case model::receiver_settings::mode::microphones:
            return run_attenuation<model::receiver_settings::mode::microphones>(
                    cc,
                    receiver,
                    input,
                    output_sample_rate,
                    speed_of_sound,
                    acoustic_impedance,
                    max_seconds);
        case model::receiver_settings::mode::hrtf:
            return run_attenuation<model::receiver_settings::mode::hrtf>(
                    cc,
                    receiver,
                    input,
                    output_sample_rate,
                    speed_of_sound,
                    acoustic_impedance,
                    max_seconds);
    }
}

}  // namespace raytracer
