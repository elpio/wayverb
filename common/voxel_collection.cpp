#include "voxel_collection.h"

#include <cmath>
#include <iostream>

VoxelCollection::Voxel::Voxel(const CuboidBoundary& aabb,
                              const std::vector<int>& triangles)
        : aabb(aabb)
        , triangles(triangles) {
}

VoxelCollection::Voxel::Voxel(const Octree& o)
        : Voxel(o.get_aabb(), o.get_triangles()) {
}

CuboidBoundary VoxelCollection::Voxel::get_aabb() const {
    return aabb;
}
const std::vector<int>& VoxelCollection::Voxel::Voxel::get_triangles() const {
    return triangles;
}

VoxelCollection::VoxelCollection(const Octree& o)
        : aabb(o.get_aabb())
        , data(o.get_side(), YAxis(o.get_side(), ZAxis(o.get_side()))) {
    init(o);
}

VoxelCollection::VoxelCollection(const SceneData& scene_data,
                                 int depth,
                                 float padding)
        : VoxelCollection(Octree(scene_data, depth, padding)) {
}

void VoxelCollection::init(const Octree& o, const Vec3i& d) {
    if (!o.has_nodes()) {
        data[d.x][d.y][d.z] = Voxel(o);
    } else {
        auto count = 0u;
        for (const auto& i : o.get_nodes()) {
            auto x = (count & 1u) ? 1u : 0u;
            auto y = (count & 2u) ? 1u : 0u;
            auto z = (count & 4u) ? 1u : 0u;
            init(i, d + Vec3i(x, y, z) * o.get_side() / 2);
            count += 1;
        }
    }
}

CuboidBoundary VoxelCollection::get_aabb() const {
    return aabb;
}

CuboidBoundary VoxelCollection::get_voxel_aabb() const {
    return data.front().front().front().get_aabb();
}

const VoxelCollection::XAxis& VoxelCollection::get_data() const {
    return data;
}

Vec3i VoxelCollection::get_starting_index(const Vec3f& position) const {
    return ((position - get_aabb().c0) / get_voxel_aabb().get_dimensions())
        .map([](auto i) { return floor(i); });
}

Vec3i VoxelCollection::get_step(const Vec3f& d) {
    return d.map([](auto i) { return i > 0 ? 1 : -1; });
}

const VoxelCollection::Voxel& VoxelCollection::get_voxel(const Vec3i& i) const {
    return data[i.x][i.y][i.z];
}

void min_max_ind(const Vec3f& v, int& min_i, int& max_i) {
    min_i = 0;
    max_i = 0;
    for (auto i = 1u; i != 3; ++i) {
        if (v[i] < v[min_i]) {
            min_i = i;
        }
        if (v[i] > v[max_i]) {
            max_i = i;
        }
    }
}

geo::Intersection VoxelCollection::traverse(const geo::Ray& ray,
                                            TraversalCallback& fun) {
    //  from http://www.cse.chalmers.se/edu/year/2010/course/TDA361/grid.pdf
    auto ind = get_starting_index(ray.position);
    auto voxel_bounds = get_voxel(ind).get_aabb();
    auto step = get_step(ray.direction);

    auto just_out =
        step.map([this](auto i) -> int { return i > 0 ? data.size() : -1; });

    auto boundary =
        step.apply(voxel_bounds.c0,
                   voxel_bounds.c1,
                   [](auto i, auto j, auto k) { return i < 0 ? j : k; });

    auto t_max =
        boundary.apply(ray.position,
                       ray.direction,
                       [](auto i, auto j, auto k) { return (i - j) / k; })
            .abs();
    auto t_delta = (get_voxel_aabb().get_dimensions() / ray.direction).abs();

    for (;;) {
        auto min_i = 0;
        for (auto i = 1u; i != 3; ++i) {
            if (t_max[i] < t_max[min_i]) {
                min_i = i;
            }
        }

        const auto& tri = get_voxel(ind).get_triangles();
        if (!tri.empty()) {
            auto ret = fun(ray, tri);

            if (ret.intersects && ret.distance < t_max[min_i]) {
                return ret;
            }
        }

        ind[min_i] += step[min_i];
        if (ind[min_i] == just_out[min_i])
            return geo::Intersection();
        t_max[min_i] += t_delta[min_i];
    }
}

VoxelCollection::TriangleTraversalCallback::TriangleTraversalCallback(
    const SceneData& scene_data)
        : tri(scene_data.get_triangles())
        , vertices(scene_data.get_converted_vertices()) {
}

geo::Intersection VoxelCollection::TriangleTraversalCallback::operator()(
    const geo::Ray& ray, const std::vector<int>& triangles) {
    return geo::ray_triangle_intersection(ray, triangles, tri, vertices);
}