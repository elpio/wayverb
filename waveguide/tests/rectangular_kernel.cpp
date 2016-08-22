#include "waveguide/log_nan.h"
#include "waveguide/program.h"
#include "waveguide/surface_filters.h"
#include "waveguide/waveguide.h"

#include "common/cl/common.h"
#include "common/timed_scope.h"
#include "common/write_audio_file.h"

#include "gtest/gtest.h"

#include <array>
#include <random>
#include <type_traits>

class InputGenerator {
public:
    virtual aligned::vector<aligned::vector<cl_float>> compute_input(
            int size) = 0;
};

class NoiseGenerator : public InputGenerator {
public:
    virtual aligned::vector<aligned::vector<cl_float>> compute_input(
            int size) override {
        static auto ret = generate(size);
        return ret;
    }

private:
    static aligned::vector<aligned::vector<cl_float>> generate(int size) {
        auto ret = aligned::vector<aligned::vector<cl_float>>{
                10000, aligned::vector<cl_float>(size, 0)};
        for (auto& i : ret)
            std::generate(i.begin(), i.end(), [] { return range(engine); });
        return ret;
    }

    static std::default_random_engine engine;
    static constexpr float r{0.25};
    static std::uniform_real_distribution<cl_float> range;
};

std::default_random_engine NoiseGenerator::engine{std::random_device()()};
std::uniform_real_distribution<cl_float> NoiseGenerator::range{-r, r};

class QuietNoiseGenerator : public InputGenerator {
public:
    virtual aligned::vector<aligned::vector<cl_float>> compute_input(
            int size) override {
        static auto ret = generate(size);
        return ret;
    }

private:
    static aligned::vector<aligned::vector<cl_float>> generate(int size) {
        auto ret = aligned::vector<aligned::vector<cl_float>>{
                40000, aligned::vector<cl_float>(size, 0)};
        for (auto& i : ret)
            proc::generate(i, [] { return range(engine); });
        return ret;
    }

    static std::default_random_engine engine;
    static constexpr float r{1e-35};
    static std::uniform_real_distribution<cl_float> range;
};

std::default_random_engine QuietNoiseGenerator::engine{std::random_device()()};
std::uniform_real_distribution<cl_float> QuietNoiseGenerator::range{-r, r};

template <size_t SAMPLES>
class ImpulseGenerator : public InputGenerator {
public:
    virtual aligned::vector<aligned::vector<cl_float>> compute_input(
            int size) override {
        auto ret = aligned::vector<aligned::vector<cl_float>>{
                SAMPLES, aligned::vector<cl_float>(size, 0)};
        for (auto& i : ret.front())
            i = 0.25;
        return ret;
    }
};

namespace testing {
constexpr auto sr{44100.0};
constexpr auto min_v{0.05};
constexpr auto max_v{0.95};

#if 0
TEST(stability, filters) {
    for (auto s : {
             Surface{{{0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9}},
                     {{0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9}}},
             Surface{{{0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1}},
                     {{0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1}}},
             Surface{{{0.4, 0.3, 0.5, 0.8, 0.9, 0.9, 0.9, 0.9}},
                     {{0.4, 0.3, 0.5, 0.8, 0.9, 0.9, 0.9, 0.9}}},
         }) {
        auto descriptors = RectangularWaveguide::to_filter_descriptors(s);
        auto individual_coeffs =
            program::get_peak_biquads_array(descriptors, sr);
        std::cout << individual_coeffs << std::endl;
        auto reflectance = program::convolve(individual_coeffs);
        //        ASSERT_TRUE(program::is_stable(reflectance)) << s;
        auto impedance =
            program::to_impedance_coefficients(reflectance);
        ASSERT_TRUE(program::is_stable(impedance)) << s;
    }
}
#endif

constexpr auto parallel_size{1 << 8};

std::default_random_engine engine{std::random_device()()};
std::uniform_real_distribution<float> range{min_v, max_v};

surface random_surface() {
    surface ret;
    proc::generate(ret.specular.s, [] { return range(engine); });
    proc::generate(ret.diffuse.s, [] { return range(engine); });
    return ret;
};

std::array<surface, parallel_size> compute_surfaces() {
    std::array<surface, parallel_size> ret;
    proc::generate(ret, [] { return random_surface(); });
    return ret;
}

static const auto surfaces = compute_surfaces();

std::array<std::array<waveguide::descriptor, biquad_sections>,
           parallel_size>
compute_descriptors() {
    std::array<std::array<waveguide::descriptor, biquad_sections>,
               parallel_size>
            ret;
    proc::transform(surfaces, ret.begin(), [](auto i) {
        return waveguide::to_filter_descriptors(i);
    });
    return ret;
}

static const auto descriptors = compute_descriptors();

enum class FilterType {
    biquad_cascade,
    single_reflectance,
    single_impedance,
};

template <FilterType FT>
struct CoefficientTypeTrait;

template <>
struct CoefficientTypeTrait<FilterType::biquad_cascade> {
    using type = biquad_coefficients_array;
};

template <>
struct CoefficientTypeTrait<FilterType::single_reflectance> {
    using type = coefficients<biquad_sections * coefficients_biquad::order>;
};

template <>
struct CoefficientTypeTrait<FilterType::single_impedance> {
    using type = coefficients<biquad_sections * coefficients_biquad::order>;
};

template <FilterType FT>
std::array<typename CoefficientTypeTrait<FT>::type, parallel_size>
compute_coeffs();

template <>
std::array<typename CoefficientTypeTrait<FilterType::biquad_cascade>::type,
           parallel_size>
compute_coeffs<FilterType::biquad_cascade>() {
    std::array<biquad_coefficients_array, parallel_size> ret;
    proc::transform(descriptors, ret.begin(), [](const auto& n) {
        return waveguide::get_peak_biquads_array(n, sr);
    });
    return ret;
}

template <>
std::array<typename CoefficientTypeTrait<FilterType::single_reflectance>::type,
           parallel_size>
compute_coeffs<FilterType::single_reflectance>() {
    std::array<
            typename CoefficientTypeTrait<FilterType::single_reflectance>::type,
            parallel_size>
            ret;
    proc::transform(descriptors, ret.begin(), [](const auto& n) {
        return waveguide::convolve(waveguide::get_peak_biquads_array(n, sr));
    });
    return ret;
}

template <>
std::array<typename CoefficientTypeTrait<FilterType::single_impedance>::type,
           parallel_size>
compute_coeffs<FilterType::single_impedance>() {
    std::array<
            typename CoefficientTypeTrait<FilterType::single_reflectance>::type,
            parallel_size>
            ret;
    proc::transform(descriptors, ret.begin(), [](const auto& n) {
        return waveguide::to_impedance_coefficients(
                waveguide::convolve(waveguide::get_peak_biquads_array(n, sr)));
    });
    return ret;
}
}

template <typename Memory, testing::FilterType FT, typename Generator>
class kernel : Generator {
public:
    virtual ~kernel() noexcept = default;

    template <typename Kernel>
    auto run_kernel(Kernel&& k) {
        auto kernel = std::move(k);

        {
            cl::CommandQueue queue(cc.get_context(), cc.get_device());
            TimedScope timer("filtering");
            for (auto i = 0u; i != input.size(); ++i) {
                cl::copy(queue, input[i].begin(), input[i].end(), cl_input);

                kernel(cl::EnqueueArgs(queue,
                                       cl::NDRange(testing::parallel_size)),
                       cl_input,
                       cl_output,
                       cl_memory,
                       cl_coeffs);

                cl::copy(queue, cl_output, output[i].begin(), output[i].end());
            }
        }
        auto buf = aligned::vector<cl_float>(output.size());
        proc::transform(
                output, buf.begin(), [](const auto& i) { return i.front(); });
        return buf;
    }

    const compute_context cc;
    const waveguide::program program{cc.get_context(), cc.get_device()};
    aligned::vector<Memory> memory{testing::parallel_size, Memory{}};
    std::array<typename testing::CoefficientTypeTrait<FT>::type,
               testing::parallel_size>
            coeffs{testing::compute_coeffs<FT>()};
    cl::Buffer cl_memory{cc.get_context(), memory.begin(), memory.end(), false};
    cl::Buffer cl_coeffs{cc.get_context(), coeffs.begin(), coeffs.end(), false};
    aligned::vector<aligned::vector<cl_float>> input{
            Generator::compute_input(testing::parallel_size)};
    aligned::vector<aligned::vector<cl_float>> output{
            input.size(), aligned::vector<cl_float>(testing::parallel_size, 0)};
    cl::Buffer cl_input{cc.get_context(),
                        CL_MEM_READ_WRITE,
                        testing::parallel_size * sizeof(cl_float)};
    cl::Buffer cl_output{cc.get_context(),
                         CL_MEM_READ_WRITE,
                         testing::parallel_size * sizeof(cl_float)};
};

template <typename Generator>
using rk_biquad = kernel<biquad_memory_array,
                         testing::FilterType::biquad_cascade,
                         Generator>;

template <typename Generator>
using rk_filter = kernel<memory_canonical,
                         testing::FilterType::single_reflectance,
                         Generator>;

template <typename Generator>
using rk_impedance = kernel<memory_canonical,
                            testing::FilterType::single_impedance,
                            Generator>;

class testing_rk_biquad : public rk_biquad<NoiseGenerator>,
                          public ::testing::Test {};
class testing_rk_filter : public rk_filter<NoiseGenerator>,
                          public ::testing::Test {};

TEST_F(testing_rk_biquad, filtering) {
    auto results = run_kernel(program.get_filter_test_kernel());
    ASSERT_TRUE(waveguide::log_nan(results, "filter 1 results") ==
                results.end());
    snd::write("./filtered_noise.wav", {results}, testing::sr, 16);
}

TEST_F(testing_rk_filter, filtering_2) {
    auto results = run_kernel(program.get_filter_test_2_kernel());
    ASSERT_TRUE(waveguide::log_nan(results, "filter 2 results") ==
                results.end());
    snd::write("./filtered_noise_2.wav", {results}, testing::sr, 16);
}

class testing_rk_biquad_quiet : public rk_biquad<QuietNoiseGenerator>,
                                public ::testing::Test {};
class testing_rk_filter_quiet : public rk_filter<QuietNoiseGenerator>,
                                public ::testing::Test {};

TEST_F(testing_rk_biquad_quiet, filtering) {
    auto results = run_kernel(program.get_filter_test_kernel());
    ASSERT_TRUE(waveguide::log_nan(results, "filter 1 quiet results") ==
                results.end());
    snd::write("./filtered_noise_quiet.wav", {results}, testing::sr, 16);
}

TEST_F(testing_rk_filter_quiet, filtering_2) {
    auto results = run_kernel(program.get_filter_test_2_kernel());
    ASSERT_TRUE(waveguide::log_nan(results, "filter 2 quiet results") ==
                results.end());
    snd::write("./filtered_noise_2_quiet.wav", {results}, testing::sr, 16);
}

TEST(compare_filters, compare_filters) {
    /*
    Logger::log_err("cpu: sizeof(CanonicalMemory): ",
                    sizeof(CanonicalMemory));
    Logger::log_err("cpu: sizeof(BiquadMemoryArray): ",
                    sizeof(BiquadMemoryArray));
    Logger::log_err("cpu: sizeof(CanonicalCoefficients): ",
                    sizeof(CanonicalCoefficients));
    Logger::log_err("cpu: sizeof(BiquadCoefficientsArray): ",
                    sizeof(BiquadCoefficientsArray));
    */

    auto test = [](auto&& biquad, auto&& filter) {
        for (auto i = 0; i != biquad.input.size(); ++i) {
            for (auto j = 0; j != biquad.input[i].size(); ++j) {
                ASSERT_EQ(biquad.input[i][j], filter.input[i][j]);
            }
        }

        auto buf_1 = biquad.run_kernel(biquad.program.get_filter_test_kernel());
        auto buf_2 =
                filter.run_kernel(filter.program.get_filter_test_2_kernel());

        auto diff = buf_1;
        proc::transform(buf_1, buf_2.begin(), diff.begin(), [](auto i, auto j) {
            return std::abs(i - j);
        });

        auto div = buf_1;
        proc::transform(buf_1, buf_2.begin(), div.begin(), [](auto i, auto j) {
            if (i == 0 || j == 0)
                return 0.0;
            return std::abs(decibels::a2db(std::abs(i / j)));
        });

        /*
        std::for_each(
            diff.begin(), diff.end(), [](auto i) { ASSERT_NEAR(i, 0, 0.001); });
        */

        // auto min_diff = *std::min_element(diff.begin(), diff.end());
        auto max_diff = *proc::max_element(diff);

        ASSERT_TRUE(max_diff < 0.001) << max_diff;

        /*
        auto min_div = *std::min_element(div.begin(), div.end());
        auto max_div = *std::max_element(div.begin(), div.end());

        Logger::log_err("min diff: ", min_diff);
        Logger::log_err("max diff: ", max_diff);
        Logger::log_err("min div / dB: ", min_div);
        Logger::log_err("max div / dB: ", max_div);
        */

        snd::write("./buf_1.wav", {buf_1}, testing::sr, 16);
        snd::write("./buf_2.wav", {buf_2}, testing::sr, 16);
    };

    using Imp = ImpulseGenerator<200>;

    test(rk_biquad<Imp>{}, rk_filter<Imp>{});
    test(rk_biquad<NoiseGenerator>{}, rk_filter<NoiseGenerator>{});
}

/*
TEST(filter_stability, filter_stability) {
    std::default_random_engine engine{std::random_device()()};
    std::uniform_real_distribution<float> range{-1, 1};
    constexpr auto order = 200;
    constexpr auto tries = 20;
    for (auto i = 0; i != tries; ++i) {
        std::array<float, order> poly;
        std::generate(poly.begin(),
                      poly.end(),
                      [&engine, &range] { return range(engine); });

        auto stable = program::is_stable(poly);
    }
}
*/

template <typename T>
struct PrintType;

template <typename T>
aligned::vector<aligned::vector<T>> transpose(
        const aligned::vector<aligned::vector<T>>& t) {
    aligned::vector<aligned::vector<T>> ret(t.front().size(),
                                            aligned::vector<T>(t.size()));
    for (auto i = 0u; i != ret.size(); ++i) {
        for (auto j = 0u; j != ret.front().size(); ++j) {
            ret[i][j] = t[j][i];
        }
    }
    return ret;
}

#if 0
TEST(impulse_response, filters) {
    {
        //  try an analytical method to test filter stability
        proc::for_each(
            testing::compute_coeffs<testing::FilterType::single_reflectance>(),
            [](const auto& i) {
                ASSERT_TRUE(program::is_stable(i));
            });
        //  test that the reflectance filters are stable(ish)
        rk_filter<ImpulseGenerator<10000>> filter;
        auto buf = filter.run_kernel(filter.program.get_filter_test_2_kernel());
        auto full_output = transpose(filter.output);
        ASSERT_EQ(full_output.front(), buf);

        proc::for_each(
            proc::zip(full_output, testing::surfaces, testing::descriptors),
            [](const auto& i) {
                auto samples = std::get<0>(i);
                proc::transform(samples, samples.begin(), [](auto x) {
                    return std::abs(x);
                });
                ASSERT_TRUE(samples.back() < 1e-10) << std::get<1>(i) << " "
                                                    << samples;
            });
    }
    {
        //  try an analytical method to test filter stability
        proc::for_each(
            testing::compute_coeffs<testing::FilterType::single_impedance>(),
            [](const auto& i) {
                ASSERT_TRUE(program::is_stable(i));
            });
        //  TODO need a reliable way of making impedance filters stable if they
        //  are not already
        rk_impedance<ImpulseGenerator<10000>> filter;
        auto buf = filter.run_kernel(filter.program.get_filter_test_2_kernel());
        auto full_output = transpose(filter.output);
        ASSERT_EQ(full_output.front(), buf);

        proc::for_each(
            proc::zip(full_output, testing::surfaces, testing::descriptors),
            [](const auto& i) {
                auto samples = std::get<0>(i);
                proc::transform(samples, samples.begin(), [](auto x) {
                    return std::abs(x);
                });
            });
    }
}
#endif

// TEST(ghost_point, filters) {
//    ComputeContext compute_context;
//    program program{get_program<program>(
//        compute_context.context, compute_context.device)};
//
//    constexpr auto v = 0.9;
//    constexpr Surface surface{{{v, v, v, v, v, v, v, v}},
//                              {{v, v, v, v, v, v, v, v}}};
//
//    auto c = RectangularWaveguide::to_filter_coefficients(surface,
//    testing::sr);
//
//    LOG(INFO) << c;
//
//    std::array<program::CanonicalCoefficients,
//               testing::parallel_size>
//#if 0
//        coefficients{{program::CanonicalCoefficients{
//            {2, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}}};
//
//#else
//        coefficients{{c}};
//#endif
//
//        cl::Buffer cl_coefficients{compute_context.context,
//                                   coefficients.begin(),
//                                   coefficients.end(),
//                                   false};
//    std::array<program::BoundaryData, testing::parallel_size>
//        boundary_data{};
//    cl::Buffer cl_boundary_data{compute_context.context,
//                                boundary_data.begin(),
//                                boundary_data.end(),
//                                false};
//
//    aligned::vector<cl_float> debug(testing::parallel_size, 0);
//    cl::Buffer cl_debug_buffer{
//        compute_context.context, debug.begin(), debug.end(), false};
//
//    //    aligned::vector<aligned::vector<cl_float>> input{
//    // ImpulseGenerator<10000>().compute_input(testing::parallel_size)};
//    aligned::vector<aligned::vector<cl_float>> input{
//        NoiseGenerator().compute_input(testing::parallel_size)};
//    aligned::vector<aligned::vector<cl_float>> output{
//        input.size(), aligned::vector<cl_float>(testing::parallel_size, 0)};
//    cl::Buffer cl_input{compute_context.context,
//                        CL_MEM_READ_WRITE,
//                        testing::parallel_size * sizeof(cl_float)};
//
//    auto kernel = program.get_ghost_point_test_kernel();
//
//    for (auto i = 0u; i != input.size(); ++i) {
//        cl::copy(
//            compute_context.queue, input[i].begin(), input[i].end(),
//            cl_input);
//        cl::copy(
//            compute_context.queue, debug.begin(), debug.end(),
//            cl_debug_buffer);
//
//        kernel(cl::EnqueueArgs(compute_context.queue,
//                               cl::NDRange(testing::parallel_size)),
//               cl_input,
//               cl_boundary_data,
//               cl_coefficients);
//
//        cl::copy(compute_context.queue,
//                 cl_boundary_data,
//                 boundary_data.begin(),
//                 boundary_data.end());
//
//        proc::transform(boundary_data, output[i].begin(), [](auto i) {
//            return i.filter_memory.array[0];
//        });
//    }
//    auto buf = aligned::vector<cl_float>(output.size());
//    proc::transform(
//        output, buf.begin(), [](const auto& i) { return i.front(); });
//}
