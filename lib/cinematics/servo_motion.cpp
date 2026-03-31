#include "servo_motion.h"
#include <math.h>

/// @brief  Numerically estimate the stopping distance with jerk and acceleration limits.
/// using analytic formulas with quantized time steps led to some big errors. This
/// function simulates the deceleration with time steps as close as possible to the control loop frequency.
float decel_distance_sim(float v_init, float acc_init, float a_max, float j_max, float v_max) {
    if (v_init <= 0.0f) return 0.0f;

    const float dt = 0.002f; 
    // starting speed and acceleration
    float v = v_init;
    float a = acc_init;
    // distance covered so far
    float x = 0.0f;
    // capping the number of iterations to avoid infinite loops
    const int max_iters = 20000;
    //interrupting the simulation if the velocity is very low
    for (int i = 0; i < max_iters && v > 1e-6f; ++i) {
        //target acceleration is the maximum allowed acceleration but is negative because we want to decelerate
        const float target_a = -a_max;
        // if the target acceleration is already reached, we don't need to apply jerk
        float j = 0.0f;
        if (a > target_a) j = -j_max;
        // updating the acceleration of the next step and clamping it to the target acceleration
        float a_next = a + j * dt;
        if (a_next < target_a) a_next = target_a;
        // we use the average of the acceleration because the acceleration changes linearly during the time step
        // so the average acceleration is the best estimate of the actual acceleration during the time step
        // because the area under the acceleration curve is the change in velocity,
        // because the area is a triangle with base dt and height a_next-a, the average acceleration is a + (a_next - a) / 2 = (a + a_next) / 2
        float a_avg = 0.5f * (a + a_next);
        float v_next = v + a_avg * dt;
        
        // clamping velocity to max
        if (v_next > v_max) v_next = v_max;
        // if the speed correctly goes to zero
        if (v_next <= 0.0f) {
            // calculating the exact time to stop with protection against division by 0
            float t_stop = (a_avg == 0.0f) ? dt : (-v / a_avg);
            // sanitizing t_stop
            if (t_stop < 0.0f) t_stop = dt;
            // adding the final bit of distance covered until full stop with linear accelerated motion
            x += v * t_stop + 0.5f * a_avg * t_stop * t_stop;
            return x;
        }
        // updating distance with linear accelerated motion
        x += v * dt + 0.5f * a_avg * dt * dt;
        v = v_next;
        a = a_next;
    }
    return x;
}

/// @brief Calculates the distance required to decelerate from a given velocity to zero
float decel_distance(float v, float a_max, float j_max, float v_max) {
    return decel_distance_sim(v, 0.0f, a_max, j_max, v_max);
}

/// @brief Calculates the distance required to decelerate from a given velocity and acceleration to zero
float decel_distance_with_acc(float v, float a, float a_max, float j_max, float v_max) {
    // if we're not currently accelerating (a <= 0) the fallback is the same
    // as decel_distance.
    if (a <= 0.0f) return decel_distance(v, a_max, j_max, v_max);
    return decel_distance_sim(v, a, a_max, j_max, v_max);
}
