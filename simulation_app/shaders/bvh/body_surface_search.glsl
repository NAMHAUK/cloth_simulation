#ifndef BODY_MESH_SEARCH_GLSL
#define BODY_MESH_SEARCH_GLSL

const uint max_bvh_stack_depth = 32u;
const uint bvh_root_node = 0u;

uniform uvec4 uArmTriangleRanges;

bool is_body_surface_candidate(uint triangle_index)
{
    return !((triangle_index >= uArmTriangleRanges.x && triangle_index < uArmTriangleRanges.y) ||
             (triangle_index >= uArmTriangleRanges.z && triangle_index < uArmTriangleRanges.w));
}

struct NearestBodySurface {
    uint triangle_index;
    vec3 point;
    vec3 normal;
};

float length_squared(vec3 value)
{
    return dot(value, value);
}

float squared_distance_to_bounds(vec3 point, Aabb bounds)
{
    vec3 clamped_point = clamp(point, bounds.min_bounds.xyz, bounds.max_bounds.xyz);
    return length_squared(point - clamped_point);
}

void push_child_nodes(vec3 point,
                      BvhNode current_node,
                      float best_distance_sq,
                      inout uint node_index_stack[max_bvh_stack_depth],
                      inout float node_distance_stack[max_bvh_stack_depth],
                      inout uint stack_count)
{
    uint left_child_index = current_node.left_child_index;
    uint right_child_index = current_node.right_child_index;
    float distance_to_left_node_sq = squared_distance_to_bounds(point, bvh_nodes[left_child_index].bounds);
    float distance_to_right_node_sq = squared_distance_to_bounds(point, bvh_nodes[right_child_index].bounds);

    if (distance_to_right_node_sq < distance_to_left_node_sq) {
        if (distance_to_left_node_sq <= best_distance_sq) {
            node_index_stack[stack_count] = left_child_index;
            node_distance_stack[stack_count] = distance_to_left_node_sq;
            ++stack_count;
        }
        if (distance_to_right_node_sq <= best_distance_sq) {
            node_index_stack[stack_count] = right_child_index;
            node_distance_stack[stack_count] = distance_to_right_node_sq;
            ++stack_count;
        }
    } else {
        if (distance_to_right_node_sq <= best_distance_sq) {
            node_index_stack[stack_count] = right_child_index;
            node_distance_stack[stack_count] = distance_to_right_node_sq;
            ++stack_count;
        }
        if (distance_to_left_node_sq <= best_distance_sq) {
            node_index_stack[stack_count] = left_child_index;
            node_distance_stack[stack_count] = distance_to_left_node_sq;
            ++stack_count;
        }
    }
}

vec3 closest_point_on_triangle(vec3 point, vec3 a, vec3 b, vec3 c)
{
    vec3 ab = b - a;
    vec3 ac = c - a;
    vec3 ap = point - a;
    float d1 = dot(ab, ap);
    float d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return a;
    }

    vec3 bp = point - b;
    float d3 = dot(ab, bp);
    float d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) {
        return b;
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        float v = d1 / (d1 - d3);
        return a + v * ab;
    }

    vec3 cp = point - c;
    float d5 = dot(ab, cp);
    float d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) {
        return c;
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        float w = d2 / (d2 - d6);
        return a + w * ac;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (ac - ab);
    }

    float inverse_denominator = 1.0 / (va + vb + vc);
    float v = vb * inverse_denominator;
    float w = vc * inverse_denominator;
    return a + ab * v + ac * w;
}

bool update_nearest_body_surface(uint triangle_index,
                                 vec3 point,
                                 inout float best_distance_sq,
                                 inout NearestBodySurface nearest_surface)
{
    vec4 normal = body_triangle_normals[triangle_index];
    if (normal.w == 0.0) {
        return false;
    }

    vec3 a = body_triangle_positions[triangle_index].a;
    vec3 b = body_triangle_positions[triangle_index].b;
    vec3 c = body_triangle_positions[triangle_index].c;
    vec3 unit_normal = normal.xyz;

    float plane_distance = dot(point - a, unit_normal);
    if (plane_distance * plane_distance > best_distance_sq) {
        return false;
    }

    vec3 closest_point = closest_point_on_triangle(point, a, b, c);
    float distance_sq = length_squared(point - closest_point);
    if (distance_sq > best_distance_sq) {
        return false;
    }

    best_distance_sq = distance_sq;
    nearest_surface.triangle_index = triangle_index;
    nearest_surface.point = closest_point;
    nearest_surface.normal = unit_normal;
    return true;
}

bool find_nearest_body_surface(vec3 point,
                               float max_distance_sq,
                               out NearestBodySurface nearest_surface)
{
    nearest_surface.triangle_index = 0u;
    nearest_surface.point = vec3(0.0);
    nearest_surface.normal = vec3(0.0);

    float best_distance_sq = max_distance_sq;
    bool has_surface = false;
    uint node_index_stack[max_bvh_stack_depth];
    float node_distance_stack[max_bvh_stack_depth];
    uint stack_count = 1u;
    node_index_stack[0] = bvh_root_node;
    node_distance_stack[0] = squared_distance_to_bounds(point, bvh_nodes[bvh_root_node].bounds);

    while (stack_count > 0u) {
        --stack_count;
        uint current_node_index = node_index_stack[stack_count];
        float distance_to_node_sq = node_distance_stack[stack_count];
        if (distance_to_node_sq > best_distance_sq) {
            continue;
        }

        BvhNode current_node = bvh_nodes[current_node_index];
        if (is_leaf_node(current_node)) {
            if (!is_body_surface_candidate(current_node.first_element_index)) {
                continue;
            }
            for (uint triangle_offset = 0u; triangle_offset < current_node.element_count; ++triangle_offset) {
                if (update_nearest_body_surface(current_node.first_element_index + triangle_offset,
                                                point,
                                                best_distance_sq,
                                                nearest_surface)) {
                    has_surface = true;
                }
            }
        } else {
            push_child_nodes(point,
                             current_node,
                             best_distance_sq,
                             node_index_stack,
                             node_distance_stack,
                             stack_count);
        }
    }

    return has_surface;
}

#endif
