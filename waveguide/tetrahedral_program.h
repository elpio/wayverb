#pragma once

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

template <int PORTS>
class BasicDWMProgram final : public cl::Program {
public:
    BasicDWMProgram(const cl::Context& context, bool build_immediate = false)
            : Program(context, source, build_immediate) {
    }

    auto get_kernel() const {
        return cl::make_kernel<cl::Buffer,
                               cl::Buffer,
                               cl::Buffer,
                               cl::Buffer,
                               cl::Buffer,
                               cl_float,
                               cl_float,
                               cl_ulong,
                               cl::Buffer>(*this, "waveguide");
    };

private:
    static const std::string source;
};

template <int PORTS>
const std::string BasicDWMProgram<PORTS>::source{
#ifdef DIAGNOSTIC
    "#define DIAGNOSTIC\n"
#endif
    "#define PORTS (" +
    std::to_string(PORTS) +
    ")\n"
    R"(
typedef struct {
    int ports[PORTS];
    float3 position;
    bool inside;
    int bt;
} Node;

kernel void waveguide
(   const global float * current
,   global float * previous
,   const global Node * nodes
,   const global float * transform_matrix
,   global float3 * velocity_buffer
,   float spatial_sampling_period
,   float T
,   ulong read
,   global float * output
) {
    size_t index = get_global_id(0);
    const global Node * node = nodes + index;

    if (! node->inside) {
        return;
    }

    float temp = 0;

    //  waveguide logic goes here
    for (int i = 0; i != PORTS; ++i) {
        int port_index = node->ports[i];
        if (port_index >= 0 && nodes[port_index].inside)
            temp += current[port_index];
    }

    temp /= (PORTS / 2);
    temp -= previous[index];

    barrier(CLK_GLOBAL_MEM_FENCE);
    previous[index] = temp;
    barrier(CLK_GLOBAL_MEM_FENCE);

    if (index == read) {
        *output = previous[index];

        //
        //  instantaneous intensity for mic modelling
        //

        float differences[PORTS] = {0};
        for (int i = 0; i != PORTS; ++i) {
            int port_index = node->ports[i];
            if (port_index >= 0 && nodes[port_index].inside)
                differences[i] = (previous[port_index] - previous[index]) /
                    spatial_sampling_period;
        }

        //  the default for Eigen is column-major matrices
        //  so we'll assume that transform_matrix is column-major

        //  multiply differences by transformation matrix
        float3 multiplied = (float3)(0);
        for (int i = 0; i != PORTS; ++i) {
            multiplied += (float3)(
                transform_matrix[0 + i * 3],
                transform_matrix[1 + i * 3],
                transform_matrix[2 + i * 3]
            ) * differences[i];
        }

        //  muliply by -1/ambient_density
        float ambient_density = 1.225;
        multiplied /= -ambient_density;

        //  numerical integration
        //
        //  I thought integration meant just adding to the previous value like
        //  *velocity_buffer += multiplied;
        //  but apparently the integrator has the transfer function
        //  Hint(z) = Tz^-1 / (1 - z^-1)
        //  so hopefully this is right
        //
        //  Hint(z) = Y(z)/X(z) = Tz^-1/(1 - z^-1)
        //  y(n) = Tx(n - 1) + y(n - 1)

        *velocity_buffer += T * multiplied;
    }
}
)"};

using TetrahedralProgram = BasicDWMProgram<4>;