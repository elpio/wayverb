#include "FrequencyAxis.hpp"

FrequencyAxisObject::FrequencyAxisObject(ShaderProgram& shader)
        : BasicDrawableObject(
                  shader,
                  {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 0}},
                  {{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}},
                  {0, 1, 2, 3},
                  GL_LINES) {
}
