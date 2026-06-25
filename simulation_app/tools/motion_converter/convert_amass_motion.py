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

INTRO_FRAME_COUNT = 30
TARGET_GENDER = "neutral"
SMPL_JOINT_COUNT = 24
AMASS_TO_PROJECT_ROTATION = np.array(
    [
        [1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0],
        [0.0, -1.0, 0.0],
    ],
    dtype=np.float32,
)
AMASS_TO_PROJECT_QUATERNION = np.array(
    [np.sqrt(0.5), -np.sqrt(0.5), 0.0, 0.0],
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
    [np.sqrt(0.5), 0.0, -np.sqrt(0.5), 0.0],
    dtype=np.float32,
)
DEFAULT_BETAS = np.zeros((1, 10), dtype=np.float32)

"""
AMASS 모션 데이터를, SMPL-neutral 캐릭터 애니메이션으로 변환
"""

@dataclass
class AmassMotion:
    poses: np.ndarray
    translations: np.ndarray
    gender: str
    fps: float


@dataclass
class ConvertedMotion:
    poses: np.ndarray
    translations: np.ndarray
    frame_indices: np.ndarray
    effective_fps: float
    start_translation: np.ndarray


def load_smpl_model(model_path, device):
    from smplx.body_models import SMPL

    model = SMPL(str(model_path), gender=TARGET_GENDER, num_betas=10, batch_size=1).to(device)
    model.eval()
    return model


# convert motion
def load_amass_motion(input_path):
    data = np.load(input_path)
    poses = data["poses"].astype(np.float32)
    translations = data["trans"].astype(np.float32)

    if poses.shape[1] < SMPL_POSE_COMPONENT_COUNT:
        raise ValueError(f"Expected at least 72 pose values per frame, got {poses.shape[1]}")

    return AmassMotion(
        poses=poses,
        translations=translations,
        gender=str(data["gender"].item()).lower(),
        fps=float(data["mocap_framerate"]),
    )

def choose_frame_indices(frame_count, source_fps, target_fps):
    if source_fps <= 0.0:
        raise ValueError(f"Invalid source FPS: {source_fps}")
    if target_fps <= 0.0:
        raise ValueError(f"Invalid target FPS: {target_fps}")

    step = max(1, int(round(source_fps / target_fps)))
    frame_indices = np.arange(0, frame_count, step, dtype=np.int64)
    effective_fps = source_fps / step
    return frame_indices, effective_fps

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
    return poses, (translations @ START_FACING_CORRECTION_ROTATION.T).astype(np.float32, copy=False)

def interpolate_pose_rotations(start_pose, end_pose, weights):
    start_rotations = start_pose.reshape(SMPL_JOINT_COUNT, 3)
    end_rotations = end_pose.reshape(SMPL_JOINT_COUNT, 3)
    start_quaternions = axis_angle_to_quaternions(start_rotations)[None, :, :]
    end_quaternions = axis_angle_to_quaternions(end_rotations)[None, :, :]
    slerp_weights = weights[:, None, None]
    interpolated_quaternions = quaternion_slerp(start_quaternions, end_quaternions, slerp_weights)
    return quaternions_to_axis_angle(interpolated_quaternions).reshape(len(weights), SMPL_POSE_COMPONENT_COUNT)

def build_interpolated_intro_motion(start_pose, start_translation):
    if INTRO_FRAME_COUNT <= 0:
        weights = np.empty(0, dtype=np.float32)
    else:
        values = np.linspace(0.0, 1.0, INTRO_FRAME_COUNT, endpoint=False, dtype=np.float32)
        weights = values * values * (3.0 - 2.0 * values)

    init_pose = make_default_pose()
    intro_poses = interpolate_pose_rotations(init_pose, start_pose, weights)
    intro_translations = np.repeat(
        start_translation.reshape(1, 3),
        INTRO_FRAME_COUNT,
        axis=0,
    ).astype(np.float32, copy=False)
    return intro_poses, intro_translations

def build_converted_motion(motion, target_fps, input_path):
    # motion을 target FPS에 맞게 downsampling
    frame_indices, effective_fps = choose_frame_indices(motion.poses.shape[0], motion.fps, target_fps)
    if len(frame_indices) == 0:
        raise ValueError(f"No frames selected from input motion: {input_path}")

    # motion 시작 위치 원점으로 정규화
    start_translation = motion.translations[frame_indices[0]].copy()
    normalized_translations = motion.translations - start_translation

    # motion의 rotation/translation를 AMASS 좌표계에서 project 좌표계로 변환
    sampled_poses = motion.poses[frame_indices, :SMPL_POSE_COMPONENT_COUNT]
    sampled_poses[:, :3] = project_global_orient(sampled_poses[:, :3])
    sampled_translations = project_translations(normalized_translations[frame_indices])
    sampled_poses, sampled_translations = apply_start_facing_correction(sampled_poses, sampled_translations)
    
    # default pose에서 AMASS motion 시작 pose로 이어지는 보간 motion을 변환 motion 앞에 붙임
    intro_poses, intro_translations = build_interpolated_intro_motion(sampled_poses[0], sampled_translations[0])
    conversion_poses = np.concatenate([intro_poses, sampled_poses], axis=0)
    conversion_translations = np.concatenate([intro_translations, sampled_translations], axis=0)
    
    return ConvertedMotion(
        poses=conversion_poses,
        translations=conversion_translations,
        frame_indices=frame_indices,
        effective_fps=effective_fps,
        start_translation=start_translation,
    )

def default_pose_ground_offset(model, device):
    with torch.no_grad():
        pose = make_default_pose().reshape(1, SMPL_POSE_COMPONENT_COUNT)
        global_orient = torch.from_numpy(pose[:, :3]).to(device)
        body_pose = torch.from_numpy(pose[:, 3:72]).to(device)
        betas = torch.from_numpy(DEFAULT_BETAS).to(device)
        transl = torch.zeros((1, 3), dtype=torch.float32, device=device)

        output = model(
            betas=betas,
            global_orient=global_orient,
            body_pose=body_pose,
            transl=transl,
            return_verts=True,
        )
        min_y = float(output.vertices[0, :, 1].detach().cpu().min().item())
    return -min_y

# write motion file
def initialize_motion_file(out_file, motion, faces, frame_count, vertex_count):
    write_motion_header(out_file, motion.effective_fps, faces, frame_count, vertex_count)

    # 빈 값으로 write하고, 나중에 실행중 저장된 값을 rewrite
    root_positions_file_position = out_file.tell()
    np.zeros((frame_count, 3), dtype=np.float32).tofile(out_file)
    return root_positions_file_position

def compute_batch_vertices(model, motion, batch_slice, device):
    batch_poses = motion.poses[batch_slice]

    global_orient = torch.from_numpy(batch_poses[:, :3]).to(device)
    body_pose = torch.from_numpy(batch_poses[:, 3:72]).to(device)
    transl = torch.from_numpy(motion.translations[batch_slice]).to(device)
    batch_betas = torch.from_numpy(np.repeat(DEFAULT_BETAS, len(batch_poses), axis=0)).to(device)

    output = model(
        betas=batch_betas,
        global_orient=global_orient,
        body_pose=body_pose,
        transl=transl,
        return_verts=True,
    )

    vertices = output.vertices.detach().cpu().numpy().astype(np.float32, copy=False)
    root_positions = output.joints[:, 0, :].detach().cpu().numpy().astype(np.float32, copy=False)
    return vertices, root_positions

def write_motion_file(output_path, model, motion, batch_size, device):
    # batch 단위로 캐릭터 motion 계산 및 저장
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_output_path = output_path.with_name(output_path.name + ".tmp")

    frame_count = len(motion.poses)
    vertex_count = int(model.v_template.shape[0])
    converted_root_positions = []

    try:
        with temp_output_path.open("wb") as out_file:
            root_positions_file_position = initialize_motion_file(out_file, motion, model.faces, frame_count, vertex_count)
            
            with torch.no_grad():
                for start in range(0, frame_count, batch_size):
                    batch_slice = slice(start, min(start + batch_size, frame_count))

                    # 각 batch의 캐릭터 몸 vertex 계산
                    batch_vertices, batch_root_positions = compute_batch_vertices(model, motion, batch_slice, device)

                    converted_root_positions.append(np.asarray(batch_root_positions, dtype=np.float32))
                    np.asarray(batch_vertices, dtype=np.float32).tofile(out_file)

            root_positions = np.concatenate(converted_root_positions, axis=0).astype(np.float32, copy=False)
            out_file.seek(root_positions_file_position)
            root_positions.tofile(out_file)

        # 모든 batch가 성공하면 최종 motion 파일로 교체
        temp_output_path.replace(output_path)
    except Exception:
        temp_output_path.unlink(missing_ok=True)
        raise


def convert(input_path, model_path, output_path, target_fps, batch_size):
    set_smpl_compatibility()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    source_motion = load_amass_motion(input_path)
    converted_motion = build_converted_motion(source_motion, target_fps, input_path)
    model = load_smpl_model(model_path, device)
    converted_motion.translations[:, 1] += default_pose_ground_offset(model, device)
    
    write_motion_file(output_path, model, converted_motion, batch_size, device)

    print()
    print(f"Converted: {input_path}")
    print(f"Source gender: {source_motion.gender}")
    print(f"Target gender: {TARGET_GENDER}")
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
        f"{source_motion.poses.shape[0]} -> {len(converted_motion.frame_indices)} "
        f"+ {INTRO_FRAME_COUNT} intro = {len(converted_motion.poses)}"
    )
    print(f"Output: {output_path}")
    print()


def main():
    parser = argparse.ArgumentParser(description="Convert AMASS SMPL motion to a binary OpenGL mesh motion asset.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target-fps", default=30.0, type=float)
    parser.add_argument("--batch-size", default=128, type=int)
    args = parser.parse_args()

    convert(
        input_path=args.input,
        model_path=args.model,
        output_path=args.output,
        target_fps=args.target_fps,
        batch_size=max(1, args.batch_size),
    )


if __name__ == "__main__":
    main()
