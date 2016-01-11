#include "rayverb_program.h"
#include "test_flag.h"

RayverbProgram::RayverbProgram(const cl::Context& context, bool build_immediate)
        : Program(context, source, build_immediate) {
}

const std::string RayverbProgram::source(
#ifdef TESTING
    "#define TESTING\n"
#endif
    "#define NUM_IMAGE_SOURCE " +
    std::to_string(NUM_IMAGE_SOURCE) +
    "\n"
    "#define SPEED_OF_SOUND " +
    std::to_string(SPEED_OF_SOUND) +
    "\n"
    R"(

#define EPSILON (0.0001f)
#define NULL (0)

constant float SECONDS_PER_METER = 1.0f / SPEED_OF_SOUND;
typedef float8 VolumeType;

typedef struct {
    float3 position;
    float3 direction;
} Ray;

typedef struct {
    float3 v0;
    float3 v1;
    float3 v2;
} TriangleVerts;

typedef struct {
    TriangleVerts prev_primitives [NUM_IMAGE_SOURCE - 1];
    Ray ray;
    VolumeType volume;
    float3 mic_reflection;
    float distance;
    unsigned int cont;
} RayInfo;

typedef struct {
    VolumeType specular;
    VolumeType diffuse;
} Surface;

typedef struct {
    unsigned long surface;
    unsigned long v0;
    unsigned long v1;
    unsigned long v2;
} Triangle;

typedef struct {
    unsigned long primitive;
    float distance;
    unsigned int intersects;
} Intersection;

typedef struct {
    VolumeType volume;
    float3 position;
    float time;
} Impulse;

typedef struct {
    VolumeType volume;
    float time;
} AttenuatedImpulse;

typedef struct {
    float3 direction;
    float coefficient;
} Speaker;

float triangle_vert_intersection (TriangleVerts v, Ray ray);
float triangle_vert_intersection (TriangleVerts v, Ray ray)
{
    float3 e0 = v.v1 - v.v0;
    float3 e1 = v.v2 - v.v0;

    float3 pvec = cross (ray.direction, e1);
    float det = dot (e0, pvec);

    if (-EPSILON < det && det < EPSILON)
        return 0.0f;

    float invdet = 1.0f / det;
    float3 tvec = ray.position - v.v0;
    float ucomp = invdet * dot (tvec, pvec);

    if (ucomp < 0.0f || 1.0f < ucomp)
        return 0.0f;

    float3 qvec = cross (tvec, e0);
    float vcomp = invdet * dot (ray.direction, qvec);

    if (vcomp < 0.0f || 1.0f < vcomp + ucomp)
        return 0.0f;

    return invdet * dot (e1, qvec);
}

float triangle_intersection
(   global Triangle * triangle
,   global float3 * vertices
,   Ray ray
);
float triangle_intersection
(   global Triangle * triangle
,   global float3 * vertices
,   Ray ray
)
{
    TriangleVerts v =
    {   vertices [triangle->v0]
    ,   vertices [triangle->v1]
    ,   vertices [triangle->v2]
    };
    return triangle_vert_intersection (v, ray);
}

float3 triangle_verts_normal (TriangleVerts t);
float3 triangle_verts_normal (TriangleVerts t)
{
    float3 e0 = t.v1 - t.v0;
    float3 e1 = t.v2 - t.v0;

    return normalize (cross (e0, e1));
}

float3 triangle_normal (global Triangle * triangle, global float3 * vertices);
float3 triangle_normal (global Triangle * triangle, global float3 * vertices)
{
    TriangleVerts t =
    {   vertices [triangle->v0]
    ,   vertices [triangle->v1]
    ,   vertices [triangle->v2]
    };
    return triangle_verts_normal (t);
}

float3 reflect (float3 normal, float3 direction);
float3 reflect (float3 normal, float3 direction)
{
    return direction - (normal * 2 * dot (direction, normal));
}

Ray ray_reflect (Ray ray, float3 normal, float3 intersection);
Ray ray_reflect (Ray ray, float3 normal, float3 intersection)
{
    return (Ray) {intersection, reflect (normal, ray.direction)};
}

Ray triangle_reflectAt
(   global Triangle * triangle
,   global float3 * vertices
,   Ray ray
,   float3 intersection
);
Ray triangle_reflectAt
(   global Triangle * triangle
,   global float3 * vertices
,   Ray ray
,   float3 intersection
)
{
    return ray_reflect
    (   ray
    ,   triangle_normal (triangle, vertices)
    ,   intersection
    );
}

Intersection ray_triangle_intersection
(   Ray ray
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
);
Intersection ray_triangle_intersection
(   Ray ray
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
)
{
    Intersection ret = {0, 0, false};

    for (unsigned long i = 0; i != numtriangles; ++i)
    {
        global Triangle * thisTriangle = triangles + i;
        float distance = triangle_intersection (thisTriangle, vertices, ray);
        if
        (   distance > EPSILON
        &&  (   !ret.intersects
            ||  (ret.intersects && distance < ret.distance)
            )
        )
        {
            ret = (Intersection) {i, distance, true};
        }
    }

    return ret;
}

VolumeType air_attenuation_for_distance (float distance, VolumeType AIR_COEFFICIENT);
VolumeType air_attenuation_for_distance (float distance, VolumeType AIR_COEFFICIENT)
{
    return pow (M_E, distance * AIR_COEFFICIENT);
}

float power_attenuation_for_distance (float distance);
float power_attenuation_for_distance (float distance)
{
    //return 1 / (distance * distance);
    return 1;
}

VolumeType attenuation_for_distance (float distance, VolumeType AIR_COEFFICIENT);
VolumeType attenuation_for_distance (float distance, VolumeType AIR_COEFFICIENT)
{
    return
    (   air_attenuation_for_distance (distance, AIR_COEFFICIENT)
    *   power_attenuation_for_distance (distance)
    );
}

float3 mirror_point (float3 p, TriangleVerts t);
float3 mirror_point (float3 p, TriangleVerts t)
{
    float3 n = triangle_verts_normal (t);
    return p - n * dot (n, p - t.v0) * 2;
}


TriangleVerts mirror_verts (TriangleVerts in, TriangleVerts t);
TriangleVerts mirror_verts (TriangleVerts in, TriangleVerts t)
{
    return (TriangleVerts)
    {   mirror_point (in.v0, t)
    ,   mirror_point (in.v1, t)
    ,   mirror_point (in.v2, t)
    };
}

void add_image_improved
(   float3 mic_position
,   float3 mic_reflection
,   float3 source
,   global Impulse * image_source
,   global unsigned long * image_source_index
,   VolumeType volume
,   unsigned long object_index
,   VolumeType AIR_COEFFICIENT
);
void add_image_improved
(   float3 mic_position
,   float3 mic_reflection
,   float3 source
,   global Impulse * image_source
,   global unsigned long * image_source_index
,   VolumeType volume
,   unsigned long object_index
,   VolumeType AIR_COEFFICIENT
)
{
    const float3 init_diff = source - mic_reflection;
    const float init_dist = length(init_diff);
    *image_source = (Impulse)
    {   volume * attenuation_for_distance(init_dist, AIR_COEFFICIENT)
    ,   mic_position + init_diff
    ,   SECONDS_PER_METER * init_dist
    };
    *image_source_index = object_index;
}

void add_image
(   float3 mic_position
,   float3 mic_reflection
,   float3 source
,   global Impulse * image_source
,   global unsigned long * image_source_index
,   size_t thread_index
,   size_t thread_offset_index
,   VolumeType volume
,   unsigned long object_index
,   VolumeType AIR_COEFFICIENT
);
void add_image
(   float3 mic_position
,   float3 mic_reflection
,   float3 source
,   global Impulse * image_source
,   global unsigned long * image_source_index
,   size_t thread_index
,   size_t thread_offset_index
,   VolumeType volume
,   unsigned long object_index
,   VolumeType AIR_COEFFICIENT
)
{
    const size_t OFFSET = thread_index * NUM_IMAGE_SOURCE + thread_offset_index;
    add_image_improved
    (   mic_position
    ,   mic_reflection
    ,   source
    ,   image_source + OFFSET
    ,   image_source_index + OFFSET
    ,   volume
    ,   object_index
    ,   AIR_COEFFICIENT
    );
}

bool point_intersection
(   float3 begin
,   float3 point
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
);
bool point_intersection
(   float3 begin
,   float3 point
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
)
{
    const float3 begin_to_point = point - begin;
    const float mag = length (begin_to_point);
    const float3 direction = normalize (begin_to_point);

    Ray to_point = {begin, direction};

    Intersection inter = ray_triangle_intersection
    (   to_point
    ,   triangles
    ,   numtriangles
    ,   vertices
    );

    return (!inter.intersects) || inter.distance > mag;
}

float3 get_direction (float3 from, float3 to);
float3 get_direction (float3 from, float3 to)
{
    return normalize (to - from);
}

kernel void raytrace_improved
(   global RayInfo * ray_info
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
,   global Surface * surfaces
,   float3 source
,   float3 mic
,   global Impulse * impulses
,   global Impulse * image_source
,   global unsigned long * image_source_index
,   VolumeType AIR_COEFFICIENT
,   unsigned long index
)
{
    size_t thread = get_global_id(0);
    global RayInfo * info = ray_info + thread;

    if (! info->cont) {
        return;
    }

    Ray ray = info->ray;

    Intersection closest = ray_triangle_intersection
    (   ray
    ,   triangles
    ,   numtriangles
    ,   vertices
    );

    if (! closest.intersects)
    {
        info->cont = false;
        return;
    }

    global Triangle * triangle = triangles + closest.primitive;

    if (index < NUM_IMAGE_SOURCE)
    {
        TriangleVerts current =
        {   vertices [triangle->v0]
        ,   vertices [triangle->v1]
        ,   vertices [triangle->v2]
        };

        for (unsigned int k = 0; k != index; ++k)
        {
            current = mirror_verts (current, info->prev_primitives[k]);
        }

        info->prev_primitives [index] = current;
        info->mic_reflection = mirror_point (info->mic_reflection, current);
        const float3 DIR = get_direction (source, info->mic_reflection);

        Ray to_mic = {source, DIR};
        bool intersects = true;
        float3 prev_intersection = source;
        for (unsigned long k = 0; k != index + 1 && intersects; ++k)
        {

            TriangleVerts to_test = info->prev_primitives[k];

            const float TO_INTERSECTION = triangle_vert_intersection(to_test, to_mic);


            if (TO_INTERSECTION <= EPSILON)
            {
                intersects = false;
                break;
            }

            float3 intersection_point = source + DIR * TO_INTERSECTION;
            for (long l = k - 1; l != -1; --l)
            {
                intersection_point = mirror_point
                (   intersection_point
                ,   info->prev_primitives[l]
                );
            }

            Ray intermediate =
            {   prev_intersection
            ,   get_direction (prev_intersection, intersection_point)
            };
            Intersection inter = ray_triangle_intersection
            (   intermediate
            ,   triangles
            ,   numtriangles
            ,   vertices
            );

            float3 new_intersection_point =
            (   intermediate.position
            +   intermediate.direction
            *   inter.distance
            );
            intersects =
            (   inter.intersects
            &&  all (new_intersection_point - EPSILON < intersection_point)
            &&  all (intersection_point < new_intersection_point + EPSILON)
            );

            prev_intersection = intersection_point;
        }

        if (intersects)
        {
            intersects = point_intersection
            (   prev_intersection
            ,   mic
            ,   triangles
            ,   numtriangles
            ,   vertices
            );
        }

        if (intersects)
        {
            add_image_improved
            (   mic
            ,   info->mic_reflection
            ,   source
            ,   image_source + thread
            ,   image_source_index + thread
            ,   info->volume
            ,   closest.primitive + 1
            ,   AIR_COEFFICIENT
            );
        }
    }

    float3 intersection = ray.position + ray.direction * closest.distance;
    float new_dist = info->distance + closest.distance;
    VolumeType new_vol = -info->volume * surfaces[triangle->surface].specular;

    const bool is_intersection = point_intersection
    (   intersection
    ,   mic
    ,   triangles
    ,   numtriangles
    ,   vertices
    );

    const float dist = is_intersection ? new_dist + length(mic - intersection) : 0;
    const float diff = fabs(dot(triangle_normal(triangle, vertices), ray.direction));
    impulses[thread] = (Impulse)
    {   (   is_intersection
        ?   (   new_vol
            *   attenuation_for_distance(dist, AIR_COEFFICIENT)
            *   surfaces[triangle->surface].diffuse
            *   diff
            )
        :   0
        )
    ,   intersection
    ,   SECONDS_PER_METER * dist
    };

    Ray new_ray = triangle_reflectAt
    (   triangle
    ,   vertices
    ,   ray
    ,   intersection
    );

    info->ray = new_ray;
    info->volume = new_vol;
    info->distance = new_dist;
}

kernel void raytrace
(   global float3 * directions
,   float3 position
,   global Triangle * triangles
,   unsigned long numtriangles
,   global float3 * vertices
,   float3 source
,   global Surface * surfaces
,   global Impulse * impulses
,   global Impulse * image_source
,   global unsigned long * image_source_index
,   unsigned long outputOffset
,   VolumeType AIR_COEFFICIENT
)
{
    size_t thread = get_global_id (0);

    if (all(directions[thread] == (float3)(0, 0, 0))) {
        return;
    }

    RayInfo ray_info;
    ray_info.ray = (Ray){source, directions[thread]};
    ray_info.distance = 0;
    ray_info.volume = 1;
    ray_info.mic_reflection = position;

    RayInfo * info = &ray_info;

    for (unsigned long index = 0; index != outputOffset; ++index)
    {
        //  Check for an intersection between the current ray and all the
        //  scene geometry.
        Ray ray = info->ray;

        Intersection closest = ray_triangle_intersection
        (   ray
        ,   triangles
        ,   numtriangles
        ,   vertices
        );

        if (! closest.intersects)
        {
            break;
        }

        global Triangle * triangle = triangles + closest.primitive;

        if (index < NUM_IMAGE_SOURCE)
        {

            TriangleVerts current =
            {   vertices [triangle->v0]
            ,   vertices [triangle->v1]
            ,   vertices [triangle->v2]
            };

            for (unsigned int k = 0; k != index; ++k)
            {
                current = mirror_verts (current, info->prev_primitives[k]);
            }

            info->prev_primitives [index] = current;
            info->mic_reflection = mirror_point (info->mic_reflection, current);
            const float3 DIR = get_direction (source, info->mic_reflection);

            Ray to_mic = {source, DIR};
            bool intersects = true;
            float3 prev_intersection = source;
            for (unsigned long k = 0; k != index + 1 && intersects; ++k)
            {

                TriangleVerts to_test = info->prev_primitives[k];

                const float TO_INTERSECTION = triangle_vert_intersection(to_test, to_mic);


                if (TO_INTERSECTION <= EPSILON)
                {
                    intersects = false;
                    break;
                }

                float3 intersection_point = source + DIR * TO_INTERSECTION;
                for (long l = k - 1; l != -1; --l)
                {
                    intersection_point = mirror_point
                    (   intersection_point
                    ,   info->prev_primitives[l]
                    );
                }

                Ray intermediate =
                {   prev_intersection
                ,   get_direction (prev_intersection, intersection_point)
                };
                Intersection inter = ray_triangle_intersection
                (   intermediate
                ,   triangles
                ,   numtriangles
                ,   vertices
                );

                float3 new_intersection_point =
                (   intermediate.position
                +   intermediate.direction
                *   inter.distance
                );
                intersects =
                (   inter.intersects
                &&  all (new_intersection_point - EPSILON < intersection_point)
                &&  all (intersection_point < new_intersection_point + EPSILON)
                );

                prev_intersection = intersection_point;
            }

            if (intersects)
            {
                intersects = point_intersection
                (   prev_intersection
                ,   position
                ,   triangles
                ,   numtriangles
                ,   vertices
                );
            }

            if (intersects)
            {
                add_image
                (   position
                ,   info->mic_reflection
                ,   source
                ,   image_source
                ,   image_source_index
                ,   thread
                ,   index
                ,   info->volume
                ,   closest.primitive + 1
                ,   AIR_COEFFICIENT
                );
            }
        }

        float3 intersection = ray.position + ray.direction * closest.distance;
        float new_dist = info->distance + closest.distance;
        VolumeType new_vol = -info->volume * surfaces [triangle->surface].specular;

        const bool IS_INTERSECTION = point_intersection
        (   intersection
        ,   position
        ,   triangles
        ,   numtriangles
        ,   vertices
        );

        const float DIST = IS_INTERSECTION ? new_dist + length (position - intersection) : 0;
        //const float DIFF = fabs (dot (triangle_normal (triangle, vertices), normalize (position - intersection)));

        //  The reflected luminous intensity in any direction from a perfectly
        //  diffusing surface varies as the cosine of the angle between the
        //  direction of incident light and the normal vector of the surface.
        //  http://www.cs.rit.edu/~jmg/courses/procshade/20073/slides/3-1-brdf.pdf
        const float DIFF = fabs (dot (triangle_normal (triangle, vertices), ray.direction));
        impulses [thread * outputOffset + index] = (Impulse)
        {   (   IS_INTERSECTION
            ?   (   new_vol
                *   attenuation_for_distance (DIST, AIR_COEFFICIENT)
                *   surfaces [triangle->surface].diffuse
                *   DIFF
                )
            :   0
            )
        ,   intersection
        ,   SECONDS_PER_METER * DIST
        };

        Ray new_ray = triangle_reflectAt
        (   triangle
        ,   vertices
        ,   ray
        ,   intersection
        );

        info->ray = new_ray;
        info->volume = new_vol;
        info->distance = new_dist;
    }
}

float speaker_attenuation (Speaker * speaker, float3 direction);
float speaker_attenuation (Speaker * speaker, float3 direction)
{
    return
    (   (1 - speaker->coefficient)
    +   speaker->coefficient
    *   dot (normalize (direction), normalize (speaker->direction))
    );
}

kernel void attenuate
(   float3 mic_pos
,   global Impulse * impulsesIn
,   global AttenuatedImpulse * impulsesOut
,   Speaker speaker
)
{
    size_t i = get_global_id (0);
    global Impulse * thisImpulse = impulsesIn + i;
    if (any (thisImpulse->volume != 0))
    {
        const float ATTENUATION = speaker_attenuation
        (   &speaker
        ,   get_direction (mic_pos, thisImpulse->position)
        );
        impulsesOut [i] = (AttenuatedImpulse)
        {   thisImpulse->volume * ATTENUATION
        ,   thisImpulse->time
        };
    }
}

float3 transform (float3 pointing, float3 up, float3 d);
float3 transform (float3 pointing, float3 up, float3 d)
{
    float3 x = normalize(cross(up, pointing));
    float3 y = cross(pointing, x);
    float3 z = pointing;

    return (float3)
    (   dot(x, d)
    ,   dot(y, d)
    ,   dot(z, d)
    );
}

float azimuth (float3 d);
float azimuth (float3 d)
{
    return atan2(d.x, d.z);
}

float elevation (float3 d);
float elevation (float3 d)
{
    return atan2(d.y, length(d.xz));
}

VolumeType hrtf_attenuation
(   global VolumeType * hrtfData
,   float3 pointing
,   float3 up
,   float3 impulseDirection
);
VolumeType hrtf_attenuation
(   global VolumeType * hrtfData
,   float3 pointing
,   float3 up
,   float3 impulseDirection
)
{
    float3 transformed = transform(pointing, up, impulseDirection);

    long a = degrees(azimuth(transformed)) + 180;
    a %= 360;
    long e = degrees(elevation(transformed));
    e = 90 - e;

    return hrtfData[a * 180 + e];
}

kernel void hrtf
(   float3 mic_pos
,   global Impulse * impulsesIn
,   global AttenuatedImpulse * impulsesOut
,   global VolumeType * hrtfData
,   float3 pointing
,   float3 up
,   unsigned long channel
)
{
    size_t i = get_global_id (0);
    const float WIDTH = 0.1;

    float3 ear_pos = transform
    (   pointing
    ,   up
    ,   (float3) {channel == 0 ? -WIDTH : WIDTH, 0, 0}
    ) + mic_pos;

    global Impulse * thisImpulse = impulsesIn + i;

    if (any (thisImpulse->volume != 0))
    {
        const VolumeType ATTENUATION = hrtf_attenuation
        (   hrtfData
        ,   pointing
        ,   up
        ,   get_direction (mic_pos, impulsesIn [i].position)
        );

        const float dist0 = distance (thisImpulse->position, mic_pos);
        const float dist1 = distance (thisImpulse->position, ear_pos);
        const float diff = dist1 - dist0;

        impulsesOut [i] = (AttenuatedImpulse)
        {   thisImpulse->volume * ATTENUATION
        ,   thisImpulse->time + diff * SECONDS_PER_METER
        };
    }
}

)");
