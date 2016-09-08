#pragma once

#include "common/cl/scene_structs.h"

//  for both methods:
//      define absorption coefficients per-band
//      convert to absolute pressure-reflectance values 
//          should really be complex *shrug*
//      
//  waveguide:
//      generate reflectance filter from pressure-reflectance per-band values
//      convert to impedance filter
//
//  ray tracer:
//      ????

template <typename T>
T absorption_to_energy_reflectance(T t) {
    return 1 - t;
}

template <typename T>
T absorption_to_pressure_reflectance(T t) {
    using std::sqrt;
    return sqrt(absorption_to_energy_reflectance(t));
}