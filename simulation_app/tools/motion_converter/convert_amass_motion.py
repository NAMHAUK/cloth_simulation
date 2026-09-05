import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import torch

from motion_converter_common import (
    SMPL_POSE_COMPONENT_COUNT,
    make_default_pose,
    set_smpl_compatibility,
    write_motion_header,
)
from motion_rotation_math import (
    axis_angle_to_quaternions,
    multiply_quaternions,
    quaternion_slerp,
    quaternions_to_axis_angle,
)

SHAPE_TRANSITION_FRAME_COUNT = 60
POSE_INTRO_FRAME_COUNT = 30
SMPL_JOINT_COUNT = 24
SMPL_BETA_COUNT = 10
TORSO_JOINT_INDEX = 9
TORSO_JOINT_CHAIN = (0, 3, 6, 9)
NEUTRAL_GENDER = "neutral"
SUPPORTED_GENDERS = {NEUTRAL_GENDER, "male", "female"}
AMASS_TO_PROJECT_ROTATION = np.array(
    [
        [1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0],
        [0.0, -1.0, 0.0],
    ],
    dtype=np.float32,
)
AMASS_TO_PROJECT_QUATERNION = np.array(
    [-np.sqrt(0.5), 0.0, 0.0, np.sqrt(0.5)],
    dtype=np.float32,
)
START_FACING_CORRECTION_ROTATION = np.array(
    [
        [0.0, 0.0, -1.0],
        [0.0, 1.0, 0.0],
        [1.0, 0.0, 0.0],
    ],
    dtype=np.float32,
)
START_FACING_CORRECTION_QUATERNION = np.array(
    [0.0, -np.sqrt(0.5), 0.0, np.sqrt(0.5)],
    dtype=np.float32,
)
NEUTRAL_BETAS = np.zeros(SMPL_BETA_COUNT, dtype=np.float32)
IDENTITY_QUATERNION_XYZW = np.array([0.0, 0.0, 0.0, 1.0], dtype=np.float32)

@dataclass
class AmassMotion:
    poses: np.ndarray
    translations: np.ndarray
    betas: np.ndarray
    gender: str
    fps: float


@dataclass
class ConvertedMotion:
    poses: np.ndarray
    translations: np.ndarray
    frame_indices: np.ndarray
    effective_fps: float
    start_translation: np.ndarray


def load_smpl_model(model_path, gender, device):
    from smplx.body_models import SMPL

    model = SMPL(str(model_path), gender=gender, num_betas=SMPL_BETA_COUNT, batch_size=1).to(device)
    model.eval()
    return model


# convert motion
def load_amass_motion(input_path):
    with np.load(input_path) as data:
        required_fields = {"poses", "trans", "betas", "gender", "mocap_framerate"}
        missing_fields = required_fields.difference(data.files)
        if missing_fields:
            raise ValueError(f"AMASS motion is missing required fields: {sorted(missing_fields)}")

        poses = data["poses"].astype(np.float32)
        translations = data["trans"].astype(np.float32)
        betas = np.asarray(data["betas"], dtype=np.float32).reshape(-1)
        gender_value = data["gender"].item()
        fps = float(data["mocap_framerate"])

    if isinstance(gender_value, bytes):
        gender_value = gender_value.decode("utf-8")
    gender = str(gender_value).strip().lower()

    if poses.ndim != 2 or poses.shape[0] == 0:
        raise ValueError(f"Expected non-empty poses shaped as [frames, components], got {poses.shape}")
    if poses.shape[1] < SMPL_POSE_COMPONENT_COUNT:
        raise ValueError(f"Expected at least 72 pose values per frame, got {poses.shape[1]}")
    if translations.shape != (poses.shape[0], 3):
        raise ValueError(f"Expected translations shaped as ({poses.shape[0]}, 3), got {translations.shape}")
    if betas.size < SMPL_BETA_COUNT:
        raise ValueError(f"Expected at least {SMPL_BETA_COUNT} beta values, got {betas.size}")
    if gender not in SUPPORTED_GENDERS:
        raise ValueError(f"Unsupported AMASS gender: {gender}")
    if not np.isfinite(fps) or not np.isfinite(poses).all() or not np.isfinite(translations).all() or not np.isfinite(betas).all():
        raise ValueError(f"AMASS motion contains non-finite values: {input_path}")

    return AmassMotion(
        poses=poses,
        translations=translations,
        betas=betas[:SMPL_BETA_COUNT],
        gender=gender,
        fps=fps,
    )

def choose_frame_indices(frame_count, source_fps, target_fps):
    if source_fps <= 0.0:
        raise ValueError(f"Invalid source FPS: {source_fps}")
    if target_fps <= 0.0:
        raise ValueError(f"Invalid target FPS: {target_fps}")

    step = max(1, int(round(source_fps / target_fps)))
    return np.arange(0, frame_count, step, dtype=np.int64), source_fps / step

def project_global_orient(global_orient):
    amass_quaternions = axis_angle_to_quaternions(global_orient)
    project_quaternions = multiply_quaternions(AMASS_TO_PROJECT_QUATERNION, amass_quaternions)
    return quaternions_to_axis_angle(project_quaternions)

def project_translations(translations):
    return (translations @ AMASS_TO_PROJECT_ROTATION.T).astype(np.float32, copy=False)

def apply_start_facing_correction(poses, translations):
    root_quaternions = axis_angle_to_quaternions(poses[:, :3])
    poses[:, :3] = quaternions_to_axis_angle(
        multiply_quaternions(START_FACING_CORRECTION_QUATERNION, root_quaternions)
    )
    return (translations @ START_FACING_CORRECTION_ROTATION.T).astype(np.float32, copy=False)

def interpolate_pose_rotations(start_pose, end_pose, weights):
    start_rotations = start_pose.reshape(SMPL_JOINT_COUNT, 3)
    end_rotations = end_pose.reshape(SMPL_JOINT_COUNT, 3)
    start_quaternions = axis_angle_to_quaternions(start_rotations)[None, :, :]
    end_quaternions = axis_angle_to_quaternions(end_rotations)[None, :, :]
    slerp_weights = weights[:, None, None]
    interpolated_quaternions = quaternion_slerp(start_quaternions, end_quaternions, slerp_weights)
    return quaternions_to_axis_angle(interpolated_quaternions).reshape(len(weights), SMPL_POSE_COMPONENT_COUNT)

def compute_reference_orientations(poses):
    joint_axis_angles = poses.reshape(-1, SMPL_JOINT_COUNT, 3)
    joint_orientations = axis_angle_to_quaternions(joint_axis_angles)
    pelvis_orientations = joint_orientations[:, 0]
    torso_orientations = pelvis_orientations
    for joint_index in TORSO_JOINT_CHAIN[1:]:
        torso_orientations = multiply_quaternions(torso_orientations, joint_orientations[:, joint_index])
    return pelvis_orientations, torso_orientations

def make_smoothstep_weights(frame_count):
    values = np.linspace(0.0, 1.0, frame_count, endpoint=False, dtype=np.float32)
    return values * values * (3.0 - 2.0 * values)

def build_interpolated_intro_motion(start_pose):
    weights = make_smoothstep_weights(POSE_INTRO_FRAME_COUNT)

    intro_poses = interpolate_pose_rotations(make_default_pose(), start_pose, weights)
    intro_translations = np.zeros((POSE_INTRO_FRAME_COUNT, 3), dtype=np.float32)
    return intro_poses, intro_translations

def build_converted_motion(motion, target_fps):
    frame_indices, effective_fps = choose_frame_indices(motion.poses.shape[0], motion.fps, target_fps)

    start_translation = motion.translations[0]
    normalized_translations = motion.translations - start_translation

    sampled_poses = motion.poses[frame_indices, :SMPL_POSE_COMPONENT_COUNT]
    sampled_poses[:, :3] = project_global_orient(sampled_poses[:, :3])
    sampled_translations = project_translations(normalized_translations[frame_indices])
    sampled_translations = apply_start_facing_correction(sampled_poses, sampled_translations)

    intro_poses, intro_translations = build_interpolated_intro_motion(sampled_poses[0])
    conversion_poses = np.concatenate([intro_poses, sampled_poses], axis=0)
    conversion_translations = np.concatenate([intro_translations, sampled_translations], axis=0)

    return ConvertedMotion(
        poses=conversion_poses,
        translations=conversion_translations,
        frame_indices=frame_indices,
        effective_fps=effective_fps,
        start_translation=start_translation,
    )

def compute_grounded_default_pose(model, betas, device):
    with torch.no_grad():
        pose = make_default_pose().reshape(1, SMPL_POSE_COMPONENT_COUNT)
        global_orient = torch.from_numpy(pose[:, :3]).to(device)
        body_pose = torch.from_numpy(pose[:, 3:72]).to(device)
        model_betas = torch.from_numpy(betas.reshape(1, SMPL_BETA_COUNT)).to(device)
        transl = torch.zeros((1, 3), dtype=torch.float32, device=device)

        output = model(
            betas=model_betas,
            global_orient=global_orient,
            body_pose=body_pose,
            transl=transl,
            return_verts=True,
        )
        vertices = output.vertices[0].cpu().numpy().astype(np.float32, copy=True)
        pelvis_position = output.joints[0, 0].cpu().numpy().astype(np.float32, copy=True)
        torso_position = output.joints[0, TORSO_JOINT_INDEX].cpu().numpy().astype(np.float32, copy=True)

    ground_offset = -float(vertices[:, 1].min())
    vertices[:, 1] += ground_offset
    pelvis_position[1] += ground_offset
    torso_position[1] += ground_offset
    return vertices, pelvis_position, torso_position, ground_offset

def build_shape_transition(neutral_vertices,
                           neutral_pelvis_position,
                           neutral_torso_position,
                           target_vertices,
                           target_pelvis_position,
                           target_torso_position):
    weights = make_smoothstep_weights(SHAPE_TRANSITION_FRAME_COUNT)
    vertex_weights = weights.reshape(-1, 1, 1)
    position_weights = weights.reshape(-1, 1)
    vertices = neutral_vertices[None, :, :] + (target_vertices - neutral_vertices)[None, :, :] * vertex_weights
    pelvis_positions = neutral_pelvis_position[None, :] + (target_pelvis_position - neutral_pelvis_position)[None, :] * position_weights
    torso_positions = neutral_torso_position[None, :] + (target_torso_position - neutral_torso_position)[None, :] * position_weights
    return vertices, pelvis_positions, torso_positions

# write motion file
def initialize_motion_file(out_file, motion, faces, frame_count, vertex_count):
    write_motion_header(out_file, motion.effective_fps, faces, frame_count, vertex_count)

    pelvis_positions_file_position = out_file.tell()
    np.zeros((frame_count, 3), dtype=np.float32).tofile(out_file)
    pelvis_orientations_file_position = out_file.tell()
    np.zeros((frame_count, 4), dtype=np.float32).tofile(out_file)
    torso_positions_file_position = out_file.tell()
    np.zeros((frame_count, 3), dtype=np.float32).tofile(out_file)
    torso_orientations_file_position = out_file.tell()
    np.zeros((frame_count, 4), dtype=np.float32).tofile(out_file)
    return (
        pelvis_positions_file_position,
        pelvis_orientations_file_position,
        torso_positions_file_position,
        torso_orientations_file_position,
    )

def compute_batch_vertices(model, motion, betas, batch_slice, device):
    batch_poses = motion.poses[batch_slice]

    global_orient = torch.from_numpy(batch_poses[:, :3]).to(device)
    body_pose = torch.from_numpy(batch_poses[:, 3:72]).to(device)
    transl = torch.from_numpy(motion.translations[batch_slice]).to(device)
    batch_betas = torch.from_numpy(np.repeat(betas.reshape(1, SMPL_BETA_COUNT), len(batch_poses), axis=0)).to(device)

    output = model(
        betas=batch_betas,
        global_orient=global_orient,
        body_pose=body_pose,
        transl=transl,
        return_verts=True,
    )

    vertices = output.vertices.cpu().numpy().astype(np.float32, copy=False)
    pelvis_positions = output.joints[:, 0, :].cpu().numpy().astype(np.float32, copy=False)
    torso_positions = output.joints[:, TORSO_JOINT_INDEX, :].cpu().numpy().astype(np.float32, copy=False)
    return vertices, pelvis_positions, torso_positions

def write_motion_file(output_path,
                      model,
                      motion,
                      betas,
                      shape_transition_vertices,
                      shape_transition_pelvis_positions,
                      shape_transition_torso_positions,
                      batch_size,
                      device):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_output_path = output_path.with_name(output_path.name + ".tmp")

    motion_frame_count = len(motion.poses)
    frame_count = len(shape_transition_vertices) + motion_frame_count
    vertex_count = model.v_template.shape[0]
    converted_pelvis_positions = [shape_transition_pelvis_positions]
    converted_torso_positions = [shape_transition_torso_positions]
    shape_orientations = np.tile(IDENTITY_QUATERNION_XYZW, (len(shape_transition_vertices), 1))
    pelvis_orientations, torso_orientations = compute_reference_orientations(motion.poses)
    converted_pelvis_orientations = np.concatenate([shape_orientations, pelvis_orientations], axis=0)
    converted_torso_orientations = np.concatenate([shape_orientations, torso_orientations], axis=0)

    try:
        with temp_output_path.open("wb") as out_file:
            transform_file_positions = initialize_motion_file(out_file, motion, model.faces, frame_count, vertex_count)
            shape_transition_vertices.tofile(out_file)
            
            with torch.no_grad():
                for start in range(0, motion_frame_count, batch_size):
                    batch_slice = slice(start, start + batch_size)

                    batch_vertices, batch_pelvis_positions, batch_torso_positions = compute_batch_vertices(
                        model,
                        motion,
                        betas,
                        batch_slice,
                        device,
                    )

                    converted_pelvis_positions.append(batch_pelvis_positions)
                    converted_torso_positions.append(batch_torso_positions)
                    batch_vertices.tofile(out_file)

            pelvis_positions = np.concatenate(converted_pelvis_positions, axis=0)
            torso_positions = np.concatenate(converted_torso_positions, axis=0)
            (
                pelvis_positions_file_position,
                pelvis_orientations_file_position,
                torso_positions_file_position,
                torso_orientations_file_position,
            ) = transform_file_positions
            out_file.seek(pelvis_positions_file_position)
            pelvis_positions.tofile(out_file)
            out_file.seek(pelvis_orientations_file_position)
            converted_pelvis_orientations.tofile(out_file)
            out_file.seek(torso_positions_file_position)
            torso_positions.tofile(out_file)
            out_file.seek(torso_orientations_file_position)
            converted_torso_orientations.tofile(out_file)

        temp_output_path.replace(output_path)
    except Exception:
        temp_output_path.unlink(missing_ok=True)
        raise


def convert(input_path, model_paths, output_path, target_fps, batch_size):
    set_smpl_compatibility()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    source_motion = load_amass_motion(input_path)
    converted_motion = build_converted_motion(source_motion, target_fps)

    required_model_paths = {NEUTRAL_GENDER: model_paths[NEUTRAL_GENDER]}
    required_model_paths[source_motion.gender] = model_paths[source_motion.gender]
    for gender, model_path in required_model_paths.items():
        if not model_path.is_file():
            raise FileNotFoundError(f"Missing {gender} SMPL model: {model_path}")

    neutral_model = load_smpl_model(model_paths[NEUTRAL_GENDER], NEUTRAL_GENDER, device)
    neutral_vertices, neutral_pelvis_position, neutral_torso_position, _ = compute_grounded_default_pose(
        neutral_model,
        NEUTRAL_BETAS,
        device,
    )
    canonical_faces = np.asarray(neutral_model.faces, dtype=np.uint32)

    if source_motion.gender == NEUTRAL_GENDER:
        target_model = neutral_model
    else:
        del neutral_model
        if device.type == "cuda":
            torch.cuda.empty_cache()
        target_model = load_smpl_model(model_paths[source_motion.gender], source_motion.gender, device)

    target_faces = np.asarray(target_model.faces, dtype=np.uint32)
    if int(target_model.v_template.shape[0]) != len(neutral_vertices) or not np.array_equal(target_faces, canonical_faces):
        raise ValueError(f"SMPL topology does not match the canonical neutral model: {source_motion.gender}")

    target_vertices, target_pelvis_position, target_torso_position, target_ground_offset = compute_grounded_default_pose(
        target_model,
        source_motion.betas,
        device,
    )
    shape_transition_vertices, shape_transition_pelvis_positions, shape_transition_torso_positions = build_shape_transition(
        neutral_vertices,
        neutral_pelvis_position,
        neutral_torso_position,
        target_vertices,
        target_pelvis_position,
        target_torso_position,
    )
    converted_motion.translations[:, 1] += target_ground_offset

    write_motion_file(
        output_path,
        target_model,
        converted_motion,
        source_motion.betas,
        shape_transition_vertices,
        shape_transition_pelvis_positions,
        shape_transition_torso_positions,
        batch_size,
        device,
    )

    print()
    print(f"Converted: {input_path}")
    print(f"Source gender: {source_motion.gender}")
    print(
        "Start translation offset: "
        f"x={converted_motion.start_translation[0]}, "
        f"y={converted_motion.start_translation[1]}, "
        f"z={converted_motion.start_translation[2]}"
    )
    print(f"Device: {device}")
    print(f"Source FPS: {source_motion.fps}")
    print(f"Motion FPS: {converted_motion.effective_fps}")
    print(
        "Frames: "
        f"{SHAPE_TRANSITION_FRAME_COUNT} shape transition "
        f"+ {POSE_INTRO_FRAME_COUNT} pose transition "
        f"+ {len(converted_motion.frame_indices)} motion = {SHAPE_TRANSITION_FRAME_COUNT + len(converted_motion.poses)} total"
    )
    print(f"Output: {output_path}")
    print()


def main():
    parser = argparse.ArgumentParser(description="Convert AMASS SMPL motion to a binary OpenGL mesh motion asset.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--neutral-model", required=True, type=Path)
    parser.add_argument("--male-model", required=True, type=Path)
    parser.add_argument("--female-model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target-fps", default=30.0, type=float)
    parser.add_argument("--batch-size", default=128, type=int)
    args = parser.parse_args()

    convert(
        input_path=args.input,
        model_paths={
            "neutral": args.neutral_model,
            "male": args.male_model,
            "female": args.female_model,
        },
        output_path=args.output,
        target_fps=args.target_fps,
        batch_size=max(1, args.batch_size),
    )


if __name__ == "__main__":
    main()
