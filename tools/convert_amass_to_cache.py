import argparse
import inspect
import struct
from pathlib import Path

import numpy as np


def apply_legacy_smpl_pickle_compatibility():
    # The official SMPL v1.1.0 pickle files reference chumpy-era NumPy aliases.
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


def choose_frame_ids(frame_count, source_fps, target_fps):
    if source_fps <= 0.0:
        raise ValueError(f"Invalid source FPS: {source_fps}")
    if target_fps <= 0.0:
        raise ValueError(f"Invalid target FPS: {target_fps}")

    step = max(1, int(round(source_fps / target_fps)))
    frame_ids = np.arange(0, frame_count, step, dtype=np.int64)
    effective_fps = source_fps / step
    return frame_ids, effective_fps


def write_cache(output_path, fps, faces, vertices):
    output_path.parent.mkdir(parents=True, exist_ok=True)

    faces = np.asarray(faces, dtype=np.uint32)
    vertices = np.asarray(vertices, dtype=np.float32)

    frame_count, vertex_count, xyz_count = vertices.shape
    if xyz_count != 3:
        raise ValueError(f"Expected vertices shaped as [frames, vertices, 3], got {vertices.shape}")

    indices = faces.reshape(-1)
    with output_path.open("wb") as out_file:
        out_file.write(b"SMPLCACH")
        out_file.write(struct.pack("<IfIII", 1, float(fps), frame_count, vertex_count, indices.size))
        out_file.write(indices.tobytes(order="C"))
        out_file.write(vertices.tobytes(order="C"))


def convert_vertices_to_project_y_up(vertices):
    converted = np.empty_like(vertices, dtype=np.float32)
    converted[..., 0] = vertices[..., 0]
    converted[..., 1] = vertices[..., 2]
    converted[..., 2] = -vertices[..., 1]
    return converted


def convert(input_path, model_dir, output_path, target_fps, batch_size):
    apply_legacy_smpl_pickle_compatibility()

    import torch
    from smplx.body_models import SMPL

    data = np.load(input_path)
    poses = data["poses"].astype(np.float32)
    translations = data["trans"].astype(np.float32)
    betas = data["betas"].astype(np.float32)
    gender = str(data["gender"].item()).lower()
    source_fps = float(data["mocap_framerate"])

    if poses.shape[1] < 72:
        raise ValueError(f"Expected at least 72 pose values per frame, got {poses.shape[1]}")

    frame_ids, effective_fps = choose_frame_ids(poses.shape[0], source_fps, target_fps)
    model_path = resolve_smpl_model_path(model_dir, gender)

    model = SMPL(
        str(model_path),
        gender=gender,
        num_betas=min(10, betas.shape[0]),
        batch_size=1,
    )
    model.eval()

    beta_values = betas[:10].reshape(1, -1)
    converted_batches = []

    with torch.no_grad():
        for start in range(0, len(frame_ids), batch_size):
            ids = frame_ids[start:start + batch_size]
            batch_poses = poses[ids]

            global_orient = torch.from_numpy(batch_poses[:, :3])
            body_pose = torch.from_numpy(batch_poses[:, 3:72])
            transl = torch.from_numpy(translations[ids])
            batch_betas = torch.from_numpy(np.repeat(beta_values, len(ids), axis=0))

            output = model(
                betas=batch_betas,
                global_orient=global_orient,
                body_pose=body_pose,
                transl=transl,
                return_verts=True,
            )
            converted_batches.append(output.vertices.detach().cpu().numpy().astype(np.float32))

    vertices = np.concatenate(converted_batches, axis=0)
    vertices = convert_vertices_to_project_y_up(vertices)
    write_cache(output_path, effective_fps, model.faces, vertices)

    print(f"Converted: {input_path}")
    print(f"Gender: {gender}")
    print(f"Source FPS: {source_fps}")
    print(f"Cache FPS: {effective_fps}")
    print(f"Frames: {poses.shape[0]} -> {vertices.shape[0]}")
    print(f"Vertices: {vertices.shape[1]}")
    print(f"Faces: {model.faces.shape[0]}")
    print(f"Output: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Convert AMASS SMPL motion to a binary OpenGL mesh cache.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--model-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target-fps", default=30.0, type=float)
    parser.add_argument("--batch-size", default=64, type=int)
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
