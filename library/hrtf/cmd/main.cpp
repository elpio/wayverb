/// A program to read a bunch of HRTF kernels and create a simple C++ header
/// file containing an array of hrtf::entry structs.
/// These can be processed at run-time to produce a fast lookup table (or a
/// slow lookup table or whatever).

#include "hrtf/entry.h"

#include "dir.h"

#include "utilities/map.h"

#include "frequency_domain/multiband_filter.h"

#include "audio_file/audio_file.h"

#include <array>
#include <experimental/optional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <regex>

class bracketer final {
public:
    bracketer(std::ostream& os, const char* enter, const char* exit)
            : os_{os}
            , exit_{exit} {
        os_ << enter;
    }

    ~bracketer() noexcept { os_ << exit_; }

private:
    std::ostream& os_;
    const char* exit_;
};

template <typename T,
          std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
std::ostream& write(std::ostream& os, const T& x) {
    return os << std::setprecision(std::numeric_limits<T>::max_digits10) << x;
}

template <typename T, size_t I>
std::ostream& write(std::ostream& os, const std::array<T, I>& arr) {
    bracketer b{os, "{{", "}}"};
    for (const auto& item : arr) {
        write(os, item);
        os << ",\n";
    }
    return os;
}

template <size_t I>
std::ostream& write(std::ostream& os, const hrtf::entry<I>& x) {
    bracketer b{os, "{", "}"};
    os << x.azimuth << ",\n" << x.elevation << ",\n";
    return write(os, x.energy) << ",\n";
}

template <typename T, typename Alloc>
std::ostream& write(std::ostream& os, const std::vector<T, Alloc>& t) {
    bracketer b{os, "{{", "}}"};
    for (const auto& item : t) {
        write(os, item);
        os << ",\n";
    }
    return os;
}

template <size_t bands>
void generate_data_file(std::ostream& os,
                        const std::vector<hrtf::entry<bands>>& entries) {
    os << R"(
//  Autogenerated file //
#pragma once
#include "hrtf/entry.h"
namespace hrtf {
const std::array<entry<)"
       << bands << ">, " << entries.size() << "> entries\n";
    write(os, entries) << R"(;
}  // namespace hrtf
)";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        throw std::runtime_error{"expected a directory name"};
    }

    const std::string base_path{argv[1]};

    constexpr auto bands{8};
    constexpr range<double> audible{20, 20000};

    std::vector<hrtf::entry<bands>> results;

    const auto entries{list_directory(base_path.c_str())};
    const std::regex name_regex{".*R([0-9]+)_T([0-9]+)_P([0-9]+).*"};
    for (const auto& entry : entries) {
        std::smatch match{};
        if (std::regex_match(entry, match, name_regex)) {
            const auto az{std::stoi(match[2].str())};
            const auto el{std::stoi(match[3].str())};

            const auto full_path{base_path + "/" + entry};

            const auto audio{audio_file::read(full_path)};

            if (audio.signal.size() != 2) {
                throw std::runtime_error{"hrtf data files must be stereo"};
            }

            if (audio.sample_rate < audible.get_max()) {
                throw std::runtime_error{"insufficiently high sampling rate"};
            }

            const range<double> normalised_range{
                    audible.get_min() / audio.sample_rate,
                    audible.get_max() / audio.sample_rate};

            const auto energy{
                    map(std::array<size_t, 2>{{0, 1}}, [&](auto channel) {
                        return frequency_domain::per_band_energy<bands>(
                                begin(audio.signal[channel]),
                                end(audio.signal[channel]),
                                normalised_range);
                    })};

            results.emplace_back(hrtf::entry<bands>{az, el, energy});
        }
    }

    generate_data_file(std::cout, results);

    return EXIT_SUCCESS;
}