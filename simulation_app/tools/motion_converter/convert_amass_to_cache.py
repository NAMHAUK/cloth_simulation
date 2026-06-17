import argparse
import inspect
import struct
from pathlib import Path

import numpy as np
import torch

CACHE_SIGNATURE = b"SMPLCACH"
CACHE_HEADER_FORMAT = "<fIII"
PRELUDE_FRAME_COUNT = 30
SMPL_POSE_COMPONENT_COUNT = 72
SMPL_JOINT_COUNT = 24
LEFT_SHOULDER_BODY_POSE_INDEX = (16 - 1) * 3
RIGHT_SHOULDER_BODY_POSE_INDEX = (17 - 1) * 3
DEFAULT_A_POSE_ARM_ANGLE_DEG = 80.0
DEFAULT_A_POSE_SHOULDER_AXIS = "z"
SHOULDER_AXIS_TO_OFFSET = {"x": 0, "y": 1, "z": 2}
QUATERNION_DOT_THRESHOLD = 0.9995
EPSILON = 1.0e-8
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

"""
AMASS 모션 데이터를, SMPL 캐릭터 애니메이션으로 변환
"""

# SMPL 모델 호환성 처리
def set_smpl_compatibility():
    np.int = np.int64
    np.float = np.float64
    np.complex = np.complex128
    np.object = np.object_
    np.unicode = np.str_
    np.str = np.str_
    if not hasattr(inspect, "getargspec"):
        inspect.getargspec = inspect.getfullargspec


def resolve_smpl_model_path(model_dir, gender):
    normalized_gender = gender.lower()
    gender_suffix = {
        "male": "m",
        "female": "f",
        "neutral": "neutral",
    }.get(normalized_gender, "neutral")

    standard_name = f"SMPL_{normalized_gender.upper()}.pkl"
    candidates = [
        model_dir / standard_name,
        model_dir / f"basicmodel_{gender_suffix}_lbs_10_207_0_v1.1.0.pkl",
        model_dir / "basicmodel_neutral_lbs_10_207_0_v1.1.0.pkl",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    raise FileNotFoundError(
        f"Could not find an SMPL model for gender '{gender}' in {model_dir}"
    )

# motion을 target FPS에 맞게 downsampling해서 frame index 선택
# Ex) source_fps=120, target_fps=30 -> step=4 -> 0, 4, 8, ... frame 선택
def choose_frame_indices(frame_count, source_fps, target_fps):
    if source_fps <= 0.0:
        raise ValueError(f"Invalid source FPS: {source_fps}")
    if target_fps <= 0.0:
        raise ValueError(f"Invalid target FPS: {target_fps}")

    step = max(1, int(round(source_fps / target_fps)))
    frame_indices = np.arange(0, frame_count, step, dtype=np.int64)
    effective_fps = source_fps / step
    return frame_indices, effective_fps


def make_a_pose():
    pose = np.zeros(SMPL_POSE_COMPONENT_COUNT, dtype=np.float32)
    angle = np.deg2rad(float(DEFAULT_A_POSE_ARM_ANGLE_DEG))
    axis_offset = SHOULDER_AXIS_TO_OFFSET[DEFAULT_A_POSE_SHOULDER_AXIS]
    body_pose_offset = 3
    pose[body_pose_offset + LEFT_SHOULDER_BODY_POSE_INDEX + axis_offset] = -angle
    pose[body_pose_offset + RIGHT_SHOULDER_BODY_POSE_INDEX + axis_offset] = angle
    return pose


def make_smoothstep_weights(frame_count):
    if frame_count <= 0:
        return np.empty(0, dtype=np.float32)

    values = np.linspace(0.0, 1.0, frame_count, endpoint=False, dtype=np.float32)
    return values * values * (3.0 - 2.0 * values)


def normalize_quaternions(quaternions):
    lengths = np.linalg.norm(quaternions, axis=-1, keepdims=True)
    return quaternions / np.maximum(lengths, EPSILON)


def axis_angle_to_quaternions(axis_angles):
    axis_angles = np.asarray(axis_angles, dtype=np.float32)
    angles = np.linalg.norm(axis_angles, axis=-1, keepdims=True)
    half_angles = angles * 0.5
    scales = np.full_like(angles, 0.5)
    np.divide(np.sin(half_angles), angles, out=scales, where=angles > EPSILON)

    quaternions = np.empty(axis_angles.shape[:-1] + (4,), dtype=np.float32)
    quaternions[..., 0:1] = np.cos(half_angles)
    quaternions[..., 1:4] = axis_angles * scales
    return normalize_quaternions(quaternions)


def quaternions_to_axis_angle(quaternions):
    quaternions = normalize_quaternions(np.asarray(quaternions, dtype=np.float32))
    vector = quaternions[..., 1:4]
    vector_lengths = np.linalg.norm(vector, axis=-1, keepdims=True)
    angles = 2.0 * np.arctan2(vector_lengths, quaternions[..., 0:1])
    scales = np.full_like(vector_lengths, 2.0)
    np.divide(angles, vector_lengths, out=scales, where=vector_lengths > EPSILON)
    return (vector * scales).astype(np.float32, copy=False)


def multiply_quaternions(lhs, rhs):
    lhs = np.asarray(lhs, dtype=np.float32)
    rhs = np.asarray(rhs, dtype=np.float32)
    w1, x1, y1, z1 = np.moveaxis(lhs, -1, 0)
    w2, x2, y2, z2 = np.moveaxis(rhs, -1, 0)

    return np.stack(
        [
            w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
            w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        ],
        axis=-1,
    ).astype(np.float32, copy=False)


def convert_amass_global_orient_to_project(global_orient):
    amass_quaternions = axis_angle_to_quaternions(global_orient)
    project_quaternions = multiply_quaternions(AMASS_TO_PROJECT_QUATERNION, amass_quaternions)
    return quaternions_to_axis_angle(project_quaternions)


def convert_amass_translations_to_project(translations):
    return (translations @ AMASS_TO_PROJECT_ROTATION.T).astype(np.float32, copy=False)


def quaternion_slerp(start_quaternions, end_quaternions, weights):
    start_quaternions = normalize_quaternions(start_quaternions)
    end_quaternions = normalize_quaternions(end_quaternions)
    weights = np.asarray(weights, dtype=np.float32)

    dot = np.sum(start_quaternions * end_quaternions, axis=-1, keepdims=True)
    end_quaternions = np.where(dot < 0.0, -end_quaternions, end_quaternions)
    dot = np.abs(dot)
    dot = np.clip(dot, -1.0, 1.0)

    linear_result = normalize_quaternions(
        start_quaternions + weights * (end_quaternions - start_quaternions)
    )

    theta_0 = np.arccos(dot)
    sin_theta_0 = np.sin(theta_0)
    theta = theta_0 * weights
    sin_theta = np.sin(theta)
    scale_start = np.cos(theta) - dot * sin_theta / np.maximum(sin_theta_0, EPSILON)
    scale_end = sin_theta / np.maximum(sin_theta_0, EPSILON)
    slerp_result = scale_start * start_quaternions + scale_end * end_quaternions

    return np.where(dot > QUATERNION_DOT_THRESHOLD, linear_result, slerp_result).astype(np.float32, copy=False)


def interpolate_pose_rotations(start_pose, end_pose, weights):
    start_rotations = start_pose.reshape(SMPL_JOINT_COUNT, 3)
    end_rotations = end_pose.reshape(SMPL_JOINT_COUNT, 3)
    start_quaternions = axis_angle_to_quaternions(start_rotations)[None, :, :]
    end_quaternions = axis_angle_to_quaternions(end_rotations)[None, :, :]
    slerp_weights = weights[:, None, None]
    interpolated_quaternions = quaternion_slerp(start_quaternions, end_quaternions, slerp_weights)
    return quaternions_to_axis_angle(interpolated_quaternions).reshape(len(weights), SMPL_POSE_COMPONENT_COUNT)


def build_prelude_motion(start_pose, start_translation):
    weights = make_smoothstep_weights(PRELUDE_FRAME_COUNT)
    init_pose = make_a_pose()
    prelude_poses = interpolate_pose_rotations(init_pose, start_pose, weights)
    prelude_translations = np.repeat(
        start_translation.reshape(1, 3),
        PRELUDE_FRAME_COUNT,
        axis=0,
    ).astype(np.float32, copy=False)
    return prelude_poses, prelude_translations


def compute_pose_ground_y_offset(model, pose, beta_values, device):
    pose = pose.reshape(1, SMPL_POSE_COMPONENT_COUNT)
    global_orient = torch.from_numpy(pose[:, :3]).to(device)
    body_pose = torch.from_numpy(pose[:, 3:72]).to(device)
    betas = torch.from_numpy(beta_values).to(device)
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


def write_cache_header(out_file, fps, faces, frame_count, vertex_count):
    indices = np.asarray(faces, dtype=np.uint32).reshape(-1)
    out_file.write(CACHE_SIGNATURE)
    out_file.write(
        struct.pack(
            CACHE_HEADER_FORMAT,
            float(fps),
            frame_count,
            vertex_count,
            indices.size,
        )
    )
    indices.tofile(out_file)
    root_positions_offset = out_file.tell()
    np.zeros((frame_count, 3), dtype=np.float32).tofile(out_file)
    return root_positions_offset


# motion을 Y-up 좌표계로 변환
def convert_vertices_to_project_y_up(vertices):
    return np.asarray(vertices, dtype=np.float32)


# 현재 batch의 vertices를 Y-up으로 변환해서 저장
def write_y_up_vertices(out_file, vertices):
    if vertices.ndim != 3 or vertices.shape[2] != 3:
        raise ValueError(f"Expected vertices shaped as [frames, vertices, 3], got {vertices.shape}")

    converted = convert_vertices_to_project_y_up(vertices)
    converted.tofile(out_file)
    return converted

# 각 pose에서 캐릭터 몸 형태의 vertex 계산
def write_root_positions(out_file, root_positions_offset, root_positions):
    out_file.seek(root_positions_offset)
    root_positions.tofile(out_file)


def compute_batch_vertices(model, poses, translations, beta_values, ids, device):
    batch_poses = poses[ids]

    global_orient = torch.from_numpy(batch_poses[:, :3]).to(device)
    body_pose = torch.from_numpy(batch_poses[:, 3:72]).to(device)
    transl = torch.from_numpy(translations[ids]).to(device)
    batch_betas = torch.from_numpy(np.repeat(beta_values, len(ids), axis=0)).to(device)

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


def convert(input_path, model_dir, output_path, target_fps, batch_size):
    set_smpl_compatibility()

    from smplx.body_models import SMPL
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    # 1) AMASS motion 데이터 load
    data = np.load(input_path)
    poses = data["poses"].astype(np.float32)
    translations = data["trans"].astype(np.float32).copy()
    source_gender = str(data["gender"].item()).lower()
    target_gender = "neutral"
    source_fps = float(data["mocap_framerate"])

    if poses.shape[1] < SMPL_POSE_COMPONENT_COUNT:
        raise ValueError(f"Expected at least 72 pose values per frame, got {poses.shape[1]}")

    # 2) motion을 target FPS에 맞게 downsampling
    frame_indices, effective_fps = choose_frame_indices(poses.shape[0], source_fps, target_fps)
    if len(frame_indices) == 0:
        raise ValueError(f"No frames selected from input motion: {input_path}")

    start_translation = translations[frame_indices[0]].copy()
    translations -= start_translation
    sampled_poses = poses[frame_indices, :SMPL_POSE_COMPONENT_COUNT].copy()
    sampled_poses[:, :3] = convert_amass_global_orient_to_project(sampled_poses[:, :3])
    sampled_translations = convert_amass_translations_to_project(translations[frame_indices])
    prelude_poses, prelude_translations = build_prelude_motion(
        sampled_poses[0],
        sampled_translations[0],
    )
    conversion_poses = np.concatenate([prelude_poses, sampled_poses], axis=0)
    conversion_translations = np.concatenate([prelude_translations, sampled_translations], axis=0)

    # 3) gender에 맞는 SMPL model load
    model_path = resolve_smpl_model_path(model_dir, target_gender)
    model = SMPL(
        str(model_path),
        gender=target_gender,
        num_betas=10,
        batch_size=1,
    ).to(device)
    model.eval()

    # 4) batch 단위로 캐릭터 애니메이션 계산 및 저장
    beta_values = np.zeros((1, 10), dtype=np.float32)
    with torch.no_grad():
        init_ground_y_offset = compute_pose_ground_y_offset(model, make_a_pose(), beta_values, device)
    conversion_translations[:, 1] += init_ground_y_offset
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_output_path = output_path.with_name(output_path.name + ".tmp")
    vertex_count = None
    root_positions_offset = None
    converted_root_positions = []

    try:
        with temp_output_path.open("wb") as out_file:
            with torch.no_grad():
                for start in range(0, len(conversion_poses), batch_size):
                    ids = np.arange(start, min(start + batch_size, len(conversion_poses)), dtype=np.int64)
                    
                    # 각 batch의 캐릭터 몸 vertex 계산
                    batch_vertices, batch_root_positions = compute_batch_vertices(
                        model,
                        conversion_poses,
                        conversion_translations,
                        beta_values,
                        ids,
                        device,
                    )

                    # 첫 batch면 header에 metadata 작성
                    if vertex_count is None:
                        vertex_count = batch_vertices.shape[1]
                        root_positions_offset = write_cache_header(
                            out_file,
                            effective_fps,
                            model.faces,
                            len(conversion_poses),
                            vertex_count,
                        )
                    # vertex count가 일관되는지 확인
                    elif batch_vertices.shape[1] != vertex_count:
                        raise ValueError(
                            "SMPL vertex count changed within one conversion: "
                            f"{vertex_count} -> {batch_vertices.shape[1]}"
                        )

                    # batch의 vertices를 Y-up으로 변환해서 저장
                    converted_roots = convert_vertices_to_project_y_up(batch_root_positions[:, None, :])[:, 0, :]
                    converted_root_positions.append(converted_roots)
                    write_y_up_vertices(out_file, batch_vertices)

            root_positions = np.concatenate(converted_root_positions, axis=0).astype(np.float32, copy=False)
            write_root_positions(out_file, root_positions_offset, root_positions)

        # 모든 batch가 성공하면 최종 cache 파일로 교체
        temp_output_path.replace(output_path)
    except Exception:
        try:
            temp_output_path.unlink()
        except FileNotFoundError:
            pass
        raise

    print()
    print(f"Converted: {input_path}")
    print(f"Source gender: {source_gender}")
    print(f"Target gender: {target_gender}")
    print(f"Start translation offset: x={start_translation[0]}, y={start_translation[1]}, z={start_translation[2]}")
    print(f"Init ground Y offset: {init_ground_y_offset}")
    print(f"Device: {device}")
    print(f"Source FPS: {source_fps}")
    print(f"Cache FPS: {effective_fps}")
    print(f"Frames: {poses.shape[0]} -> {len(frame_indices)} + {PRELUDE_FRAME_COUNT} prelude = {len(conversion_poses)}")
    print(f"Output: {output_path}")
    print()


def main():
    parser = argparse.ArgumentParser(description="Convert AMASS SMPL motion to a binary OpenGL mesh cache.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target-fps", default=30.0, type=float)
    parser.add_argument("--batch-size", default=128, type=int)
    args = parser.parse_args()

    convert(
        input_path=args.input,
        model_dir=args.model_dir,
        output_path=args.output,
        target_fps=args.target_fps,
        batch_size=max(1, args.batch_size),
    )


if __name__ == "__main__":
    main()
