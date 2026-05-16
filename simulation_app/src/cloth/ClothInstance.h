#pragma once

#include "assets/GarmentAsset.h"
#include "cloth/ClothGpuState.h"

struct ClothInstance {
    GarmentMesh mesh;
    ClothGpuState gpu_state;
    bool visible = true;
};
