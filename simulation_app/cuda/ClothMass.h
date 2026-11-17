#pragma once

struct GarmentMesh;

void compute_vertex_masses(float* masses, const GarmentMesh& mesh, float surface_density);
