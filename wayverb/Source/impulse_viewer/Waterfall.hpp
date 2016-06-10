#pragma once

#include "FadeShader.hpp"
#include "FrequencyAxis.hpp"
#include "LoadContext.hpp"
#include "WaterfallShader.hpp"
#include "WorkQueue.hpp"

#include "modern_gl_utils/buffer_object.h"
#include "modern_gl_utils/drawable.h"
#include "modern_gl_utils/updatable.h"
#include "modern_gl_utils/vao.h"

class Waterfall : public ::Updatable,
                  public ::Drawable,
                  public GLAudioThumbnailBase {
public:
    enum class Mode { linear, log };

    Waterfall(WaterfallShader& waterfall_shader,
              FadeShader& fade_shader,
              TexturedQuadShader& quad_shader);

    void update(float dt) override;
    void draw() const override;

    void set_mode(Mode u);

    void set_position(const glm::vec3& p);

    void clear() override;
    void load_from(AudioFormatManager& manager, const File& file) override;
    void reset(int num_channels,
               double sample_rate,
               int64 total_samples) override;
    void addBlock(int64 sample_number_in_source,
                  const AudioSampleBuffer& new_data,
                  int start_offset,
                  int num_samples) override;

    static float z_to_frequency(Mode mode, float z);
    static float frequency_to_z(Mode mode, float frequency);

    void set_amplitude_scale(float f) override;
    void set_time_scale(float f) override;
    float get_length_in_seconds() const;

    void set_visible_range(const Range<float>& range) override;

    static const float width;

private:
    void load_from(std::unique_ptr<AudioFormatReader>&& reader);
    void clear_impl();

    glm::vec3 get_scale() const;
    glm::vec3 get_position() const;

    class HeightMapStrip : public ::Drawable {
    public:
        HeightMapStrip(WaterfallShader& shader,
                       const std::vector<float>& left,
                       const std::vector<float>& right,
                       Mode mode,
                       float x,
                       float x_spacing,
                       float min_frequency,
                       float max_frequency,
                       float sample_rate);

        void draw() const override;

    private:
        static std::vector<glm::vec3> compute_geometry(
                const std::vector<float>& left,
                const std::vector<float>& right,
                Mode mode,
                float x,
                float x_spacing,
                float min_frequency,
                float max_frequency,
                float sample_rate);

        WaterfallShader* shader;

        VAO vao;
        StaticVBO geometry;
        StaticIBO ibo;
        GLuint size;
    };

    static std::vector<HeightMapStrip> compute_strips(
            WaterfallShader& shader,
            const std::vector<std::vector<float>>& input,
            Mode mode,
            float x_spacing,
            float sample_rate);

    static const int per_buffer{4};
    static const int axes{6};

    static const float min_frequency;
    static const float max_frequency;

    WaterfallShader* waterfall_shader;
    FadeShader* fade_shader;
    TexturedQuadShader* quad_shader;

    glm::vec2 viewport;
    glm::vec3 position{0};

    Mode mode{Mode::log};
    std::vector<std::vector<float>> spectrum;
    std::vector<HeightMapStrip> strips;

    std::unique_ptr<LoadContext> load_context;
    float x_spacing;

    float amplitude_scale{1};
    float time_scale{1};
    Range<float> visible_range;

    WorkQueue incoming_work_queue;
    mutable std::mutex mut;
};
