import argparse
import inspect
import struct
from pathlib import Path

import numpy as np
import torch

MOTION_SIGNATURE = b"SMPLMOTN"
MOTION_HEADER_FORMAT = "<fIII"
LEFT_SHOULDER_BODY_POSE_INDEX = (16 - 1) * 3
RIGHT_SHOULDER_BODY_POSE_INDEX = (17 - 1) * 3
DEFAULT_A_POSE_ARM_ANGLE_DEG = 80.0
SHOULDER_AXIS_CHOICES = ("x", "y", "z")
SHOULDER_AXIS_TO_OFFSET = {"x": 0, "y": 1, "z": 2}


def set_smpl_compatibility():
    np.int = np.int64
    np.float = np.float64
    np.complex = np.complex128
    np.object = np.object_
    np.unicode = np.str_
    np.str = np.str_
    if not hasattr(inspect, "getargspec"):
        inspect.getargspec = inspect.getfullargspec


def align_init_pose_to_ground(vertices, root_position, ground_clearance):
    aligned_vertices = np.asarray(vertices, dtype=np.float32).copy()
    aligned_root_position = np.asarray(root_position, dtype=np.float32).copy()
    min_y = float(aligned_vertices[:, 1].min())
    y_offset = float(ground_clearance) - min_y
    aligned_vertices[:, 1] += y_offset
    aligned_root_position[1] += y_offset
    return aligned_vertices, aligned_root_position


def make_body_pose(pose_name, arm_angle_deg, shoulder_axis):
    body_pose = torch.zeros((1, 69), dtype=torch.float32)
    if pose_name == "t":
        return body_pose

    angle = np.deg2rad(float(arm_angle_deg))
    axis_offset = SHOULDER_AXIS_TO_OFFSET[shoulder_axis]
    body_pose[0, LEFT_SHOULDER_BODY_POSE_INDEX + axis_offset] = -angle
    body_pose[0, RIGHT_SHOULDER_BODY_POSE_INDEX + axis_offset] = angle
    return body_pose


def write_default_pose_motion(output_path, fps, faces, vertices, root_position):
    if fps <= 0.0:
        raise ValueError(f"Invalid FPS: {fps}")
    if vertices.ndim != 2 or vertices.shape[1] != 3:
        raise ValueError(f"Expected vertices shaped as [vertices, 3], got {vertices.shape}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    indices = np.asarray(faces, dtype=np.uint32).reshape(-1)
    vertices = np.asarray(vertices, dtype=np.float32)
    root_position = np.asarray(root_position, dtype=np.float32).reshape(1, 3)

    temp_output_path = output_path.with_name(output_path.name + ".tmp")
    try:
        with temp_output_path.open("wb") as out_file:
            out_file.write(MOTION_SIGNATURE)
            out_file.write(
                struct.pack(
                    MOTION_HEADER_FORMAT,
                    float(fps),
                    1,
                    vertices.shape[0],
                    indices.size,
                )
            )
            indices.tofile(out_file)
            root_position.tofile(out_file)
            vertices.tofile(out_file)

        temp_output_path.replace(output_path)
    except Exception:
        try:
            temp_output_path.unlink()
        except FileNotFoundError:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description="Convert a neutral SMPL default pose to a one-frame mesh motion asset.")
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--pose", default="a", choices=("a", "t"))
    parser.add_argument("--arm-angle-deg", default=DEFAULT_A_POSE_ARM_ANGLE_DEG, type=float)
    parser.add_argument("--shoulder-axis", default="z", choices=SHOULDER_AXIS_CHOICES)
    parser.add_argument("--fps", default=30.0, type=float)
    parser.add_argument("--ground-clearance", default=0.0, type=float)
    args = parser.parse_args()

    set_smpl_compatibility()

    from smplx.body_models import SMPL

    model = SMPL(str(args.model), gender="neutral", num_betas=10, batch_size=1)
    model.eval()
    body_pose = make_body_pose(args.pose, args.arm_angle_deg, args.shoulder_axis)

    with torch.no_grad():
        output = model(
            betas=torch.zeros((1, 10), dtype=torch.float32),
            global_orient=torch.zeros((1, 3), dtype=torch.float32),
            body_pose=body_pose,
            transl=torch.zeros((1, 3), dtype=torch.float32),
            return_verts=True,
        )

    vertices = output.vertices.detach().cpu().numpy()[0]
    root_position = output.joints[:, 0, :].detach().cpu().numpy()[0]
    vertices, root_position = align_init_pose_to_ground(vertices, root_position, args.ground_clearance)
    write_default_pose_motion(args.output, args.fps, model.faces, vertices, root_position)

    print()
    print(f"Converted neutral SMPL {args.pose.upper()}-pose: {args.model}")
    print(f"Output: {args.output}")
    print(f"Pose: {args.pose}")
    if args.pose == "a":
        print(f"Arm angle: {args.arm_angle_deg}")
        print(f"Shoulder axis: {args.shoulder_axis}")
    print(f"FPS: {args.fps}")
    print(f"Frames: 1")
    print(f"Vertices: {vertices.shape[0]}")
    print(f"Indices: {model.faces.size}")
    print(f"Min Y: {vertices[:, 1].min():.6f}")
    print()


if __name__ == "__main__":
    main()
