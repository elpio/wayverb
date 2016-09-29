#pragma once

#include "waveguide/attenuator.h"

#include "common/model/receiver_settings.h"

#include "common/attenuator/hrtf.h"
#include "common/attenuator/microphone.h"
#include "common/map_to_vector.h"

namespace waveguide {

template <typename It>
aligned::vector<aligned::vector<float>> attenuate_microphone(
        const model::receiver_settings& receiver,
        double sample_rate,
        It begin,
        It end) {
    return map_to_vector(receiver.microphones, [&](const auto& i) {
        return attenuate(
                microphone{get_pointing(i.orientable, receiver.position),
                           i.shape},
                begin,
                end);
    });
}

template <typename It>
aligned::vector<aligned::vector<float>> attenuate_hrtf(
        const model::receiver_settings& receiver,
        double sample_rate,
        It begin,
        It end) {
    const auto channels = {hrtf::channel::left, hrtf::channel::right};
    return map_to_vector(channels, [&](const auto& i) {
        return multiband_filter_and_mixdown(
                attenuate(hrtf{get_pointing(receiver.hrtf, receiver.position),
                               glm::vec3{0, 1, 0},
                               i},
                          begin,
                          end),
                sample_rate);
    });
}

//----------------------------------------------------------------------------//

template <typename It>
aligned::vector<aligned::vector<float>> run_attenuation(
        const model::receiver_settings& receiver,
        double sample_rate,
        It begin,
        It end) {
    switch (receiver.mode) {
        case model::receiver_settings::mode::microphones:
            return attenuate_microphone(receiver, sample_rate, begin, end);
        case model::receiver_settings::mode::hrtf:
            return attenuate_hrtf(receiver, sample_rate, begin, end);
    }
}

}  // namespace waveguide
