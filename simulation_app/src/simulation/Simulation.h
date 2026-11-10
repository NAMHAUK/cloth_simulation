#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <filesystem>
#include <vector>

class QOpenGLFunctions_4_5_Core;
struct GarmentObject;

class Simulation
{
public:
    virtual ~Simulation() = default;

    virtual void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void set_character_motion(QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void update_character_pose(float frame_alpha, QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void rebuild_garment_resources(GarmentLayer changed_layer, QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void create_simulation_buffers(QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void prepare_garments(const std::vector<const GarmentObject*>& placement_garments,
                                  QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void capture_garment_base_positions(QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void restore_garment_base_positions(QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void clear_garment_base_positions(QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void step(std::uint32_t motion_step_index, QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual bool is_initialized() const = 0;
    virtual void release_garment_resources(QOpenGLFunctions_4_5_Core& gl) = 0;
    virtual void release(QOpenGLFunctions_4_5_Core& gl) = 0;
};
