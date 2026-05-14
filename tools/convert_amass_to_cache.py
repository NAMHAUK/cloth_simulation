import argparse
import inspect
import struct
from pathlib import Path

import numpy as np
import torch

CACHE_MAGIC = b"SMPLCACH"
CACHE_VERSION = 1
CACHE_BASE_HEADER_FORMAT = "<IfIII"
CACHE_BOUNDS_FORMAT = "<ffff"
CACHE_BOUNDS_OFFSET = len(CACHE_MAGIC) + struct.calcsize(CACHE_BASE_HEADER_FORMAT)
CACHE_HEADER_FORMAT = "<IfIIIffff"

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

def write_cache_header(out_file, fps, faces, frame_count, vertex_count):
    indices = np.asarray(faces, dtype=np.uint32).reshape(-1)
    out_file.write(CACHE_MAGIC)
    out_file.write(
        struct.pack(
            CACHE_HEADER_FORMAT,
            CACHE_VERSION,
            float(fps),
            frame_count,
            vertex_count,
            indices.size,
            0.0,
            0.0,
            0.0,
            1.0,
        )
    )
    indices.tofile(out_file)


def write_cache_bounds(out_file, bounds_center, bounds_radius):
    out_file.seek(CACHE_BOUNDS_OFFSET)
    out_file.write(
        struct.pack(
            CACHE_BOUNDS_FORMAT,
            float(bounds_center[0]),
            float(bounds_center[1]),
            float(bounds_center[2]),
            float(bounds_radius),
        )
    )

# motion을 Y-up 좌표계로 변환
def convert_vertices_to_project_y_up(vertices):
    converted = np.empty_like(vertices, dtype=np.float32)
    converted[..., 0] = vertices[..., 0]
    converted[..., 1] = vertices[..., 2]
    converted[..., 2] = -vertices[..., 1]
    return converted


def update_bounds(bounds_min, bounds_max, vertices):
    flattened = vertices.reshape(-1, 3)
    batch_min = flattened.min(axis=0)
    batch_max = flattened.max(axis=0)
    return np.minimum(bounds_min, batch_min), np.maximum(bounds_max, batch_max)


def compute_bounds_fit(bounds_min, bounds_max):
    bounds_center = ((bounds_min + bounds_max) * 0.5).astype(np.float32, copy=False)
    extents = np.maximum(bounds_max - bounds_min, 0.001)
    bounds_radius = float(np.max(extents) * 0.5)
    return bounds_center, bounds_radius

# 현재 batch의 vertices를 Y-up으로 변환해서 저장
def write_y_up_vertices(out_file, vertices):
    if vertices.ndim != 3 or vertices.shape[2] != 3:
        raise ValueError(f"Expected vertices shaped as [frames, vertices, 3], got {vertices.shape}")

    converted = convert_vertices_to_project_y_up(vertices)
    converted.tofile(out_file)
    return converted

# 각 pose에서 캐릭터 몸 형태의 vertex 계산
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
    return output.vertices.detach().cpu().numpy().astype(np.float32, copy=False)


def convert(input_path, model_dir, output_path, target_fps, batch_size):
    set_smpl_compatibility()

    from smplx.body_models import SMPL
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    # 1) AMASS motion 데이터 load
    data = np.load(input_path)
    poses = data["poses"].astype(np.float32)
    translations = data["trans"].astype(np.float32)
    betas = data["betas"].astype(np.float32)
    gender = str(data["gender"].item()).lower()
    source_fps = float(data["mocap_framerate"])

    if poses.shape[1] < 72:
        raise ValueError(f"Expected at least 72 pose values per frame, got {poses.shape[1]}")

    # 2) motion을 target FPS에 맞게 downsampling
    frame_indices, effective_fps = choose_frame_indices(poses.shape[0], source_fps, target_fps)
    if len(frame_indices) == 0:
        raise ValueError(f"No frames selected from input motion: {input_path}")

    # 3) gender에 맞는 SMPL model load
    model_path = resolve_smpl_model_path(model_dir, gender)
    model = SMPL(
        str(model_path),
        gender=gender,
        num_betas=min(10, betas.shape[0]),
        batch_size=1,
    ).to(device)
    model.eval()

    # 4) batch 단위로 캐릭터 애니메이션 계산 및 저장
    beta_values = betas[:10].reshape(1, -1)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_output_path = output_path.with_name(output_path.name + ".tmp")
    vertex_count = None
    bounds_min = np.full(3, np.inf, dtype=np.float32)
    bounds_max = np.full(3, -np.inf, dtype=np.float32)

    try:
        with temp_output_path.open("wb") as out_file:
            with torch.no_grad():
                for start in range(0, len(frame_indices), batch_size):
                    ids = frame_indices[start:start + batch_size]
                    
                    # 각 batch의 캐릭터 몸 vertex 계산
                    batch_vertices = compute_batch_vertices(model, poses, translations, beta_values, ids, device)

                    # 첫 batch면 header에 metadata 작성
                    if vertex_count is None:
                        vertex_count = batch_vertices.shape[1]
                        write_cache_header(
                            out_file,
                            effective_fps,
                            model.faces,
                            len(frame_indices),
                            vertex_count,
                        )
                    # vertex count가 일관되는지 확인
                    elif batch_vertices.shape[1] != vertex_count:
                        raise ValueError(
                            "SMPL vertex count changed within one conversion: "
                            f"{vertex_count} -> {batch_vertices.shape[1]}"
                        )

                    # batch의 vertices를 Y-up으로 변환해서 저장
                    converted_vertices = write_y_up_vertices(out_file, batch_vertices)
                    bounds_min, bounds_max = update_bounds(bounds_min, bounds_max, converted_vertices)

            bounds_center, bounds_radius = compute_bounds_fit(bounds_min, bounds_max)
            write_cache_bounds(out_file, bounds_center, bounds_radius)

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
    print(f"Gender: {gender}")
    print(f"Device: {device}")
    print(f"Source FPS: {source_fps}")
    print(f"Cache FPS: {effective_fps}")
    print(f"Frames: {poses.shape[0]} -> {len(frame_indices)}")
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
