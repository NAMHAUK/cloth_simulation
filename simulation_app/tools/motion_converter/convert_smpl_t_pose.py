import argparse
import inspect
import struct
from pathlib import Path

import numpy as np
import torch

CACHE_MAGIC = b"SMPLCACH"
CACHE_VERSION = 1
CACHE_HEADER_FORMAT = "<IfIIIffff"


def set_smpl_compatibility():
    np.int = np.int64
    np.float = np.float64
    np.complex = np.complex128
    np.object = np.object_
    np.unicode = np.str_
    np.str = np.str_
    if not hasattr(inspect, "getargspec"):
        inspect.getargspec = inspect.getfullargspec


def compute_bounds_fit(vertices):
    bounds_min = vertices.min(axis=0)
    bounds_max = vertices.max(axis=0)
    bounds_center = ((bounds_min + bounds_max) * 0.5).astype(np.float32, copy=False)
    extents = np.maximum(bounds_max - bounds_min, 0.001)
    bounds_radius = float(np.max(extents) * 0.5)
    return bounds_center, bounds_radius


def align_vertices_min_y_to_ground(vertices, ground_clearance):
    aligned_vertices = np.asarray(vertices, dtype=np.float32).copy()
    min_y = float(aligned_vertices[:, 1].min())
    aligned_vertices[:, 1] += float(ground_clearance) - min_y
    return aligned_vertices


def write_t_pose_cache(output_path, fps, faces, vertices):
    if fps <= 0.0:
        raise ValueError(f"Invalid FPS: {fps}")
    if vertices.ndim != 2 or vertices.shape[1] != 3:
        raise ValueError(f"Expected vertices shaped as [vertices, 3], got {vertices.shape}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    indices = np.asarray(faces, dtype=np.uint32).reshape(-1)
    vertices = np.asarray(vertices, dtype=np.float32)
    bounds_center, bounds_radius = compute_bounds_fit(vertices)

    temp_output_path = output_path.with_name(output_path.name + ".tmp")
    try:
        with temp_output_path.open("wb") as out_file:
            out_file.write(CACHE_MAGIC)
            out_file.write(
                struct.pack(
                    CACHE_HEADER_FORMAT,
                    CACHE_VERSION,
                    float(fps),
                    1,
                    vertices.shape[0],
                    indices.size,
                    float(bounds_center[0]),
                    float(bounds_center[1]),
                    float(bounds_center[2]),
                    bounds_radius,
                )
            )
            indices.tofile(out_file)
            vertices.tofile(out_file)

        temp_output_path.replace(output_path)
    except Exception:
        try:
            temp_output_path.unlink()
        except FileNotFoundError:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description="Convert a neutral SMPL zero pose to a one-frame mesh cache.")
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--fps", default=30.0, type=float)
    parser.add_argument("--ground-clearance", default=0.0, type=float)
    args = parser.parse_args()

    set_smpl_compatibility()

    from smplx.body_models import SMPL

    model = SMPL(str(args.model), gender="neutral", num_betas=10, batch_size=1)
    model.eval()

    with torch.no_grad():
        output = model(
            betas=torch.zeros((1, 10), dtype=torch.float32),
            global_orient=torch.zeros((1, 3), dtype=torch.float32),
            body_pose=torch.zeros((1, 69), dtype=torch.float32),
            transl=torch.zeros((1, 3), dtype=torch.float32),
            return_verts=True,
        )

    vertices = output.vertices.detach().cpu().numpy()[0]
    vertices = align_vertices_min_y_to_ground(vertices, args.ground_clearance)
    write_t_pose_cache(args.output, args.fps, model.faces, vertices)

    print()
    print(f"Converted neutral SMPL T-pose: {args.model}")
    print(f"Output: {args.output}")
    print(f"FPS: {args.fps}")
    print(f"Frames: 1")
    print(f"Vertices: {vertices.shape[0]}")
    print(f"Indices: {model.faces.size}")
    print(f"Min Y: {vertices[:, 1].min():.6f}")
    print()


if __name__ == "__main__":
    main()
