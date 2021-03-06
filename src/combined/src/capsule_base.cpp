#include "combined/capsule_base.h"
#include "combined/engine.h"

#include "core/attenuator/hrtf.h"
#include "core/attenuator/microphone.h"
#include "core/az_el.h"
#include "core/orientation.h"

#include <iostream>

namespace wayverb {
namespace combined {

template <typename T>
class capsule final : public capsule_base {
public:
    explicit capsule(T attenuator, const core::orientation& orientation)
            : attenuator_{std::move(attenuator)} {
        attenuator_.orientation = combine(attenuator_.orientation, orientation);
    }

    std::unique_ptr<capsule_base> clone() const override {
        return std::make_unique<capsule>(*this);
    }

    util::aligned::vector<float> postprocess(
            const intermediate& intermediate,
            double sample_rate) const override {
        return intermediate.postprocess(attenuator_, sample_rate);
    }

private:
    T attenuator_;
};

std::unique_ptr<capsule_base> make_capsule_ptr(
        const core::attenuator::hrtf& attenuator,
        const core::orientation& orientation) {
    return std::make_unique<capsule<core::attenuator::hrtf>>(attenuator,
                                                             orientation);
}

std::unique_ptr<capsule_base> make_capsule_ptr(
        const core::attenuator::microphone& attenuator,
        const core::orientation& orientation) {
    return std::make_unique<capsule<core::attenuator::microphone>>(attenuator,
                                                                   orientation);
}

}  // namespace combined
}  // namespace wayverb
