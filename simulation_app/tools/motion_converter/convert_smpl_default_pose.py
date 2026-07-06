import argparse
from pathlib import Path

import numpy as np
import torch

from motion_converter_common import (
    DEFAULT_A_POSE_ARM_ANGLE_DEG,
    DEFAULT_A_POSE_SHOULDER_AXIS,
    SMPL_BODY_POSE_OFFSET,
    SHOULDER_AXIS_TO_OFFSET,
    make_default_pose,
    set_smpl_compatibility,
    write_default_motion_labels,
    write_motion_header,
)

IS_DEFAULT_MOTION_ASSET = True
EPSILON = 1.0e-8
LOW_CONFIDENCE_THRESHOLD = 0.60
PART_GROUPS = (
    ("torso", (0, 3, 6, 9, 12, 13, 14)),
    ("head", (15,)),
    ("left_arm", (16, 18)),
    ("right_arm", (17, 19)),
    ("left_leg", (1, 4, 7, 10)),
    ("right_leg", (2, 5, 8, 11)),
    ("left_hand", (20, 22)),
    ("right_hand", (21, 23)),
)

def align_pose_to_ground(vertices, root_position, ground_clearance):
    aligned_vertices = np.asarray(vertices, dtype=np.float32).copy()
    aligned_root_position = np.asarray(root_position, dtype=np.float32).copy()
    y_offset = ground_clearance - aligned_vertices[:, 1].min()
    aligned_vertices[:, 1] += y_offset
    aligned_root_position[1] += y_offset
    return aligned_vertices, aligned_root_position


def make_triangle_part_labels(faces, lbs_weights):
    faces = np.asarray(faces)
    vertex_joint_weights = np.asarray(lbs_weights, dtype=np.float32)
    if vertex_joint_weights.ndim != 2 or vertex_joint_weights.shape[1] < 24:
        raise ValueError(f"Expected LBS weights shaped as [vertices, >=24], got {vertex_joint_weights.shape}")

    vertex_part_weights = np.stack([vertex_joint_weights[:, joint_indices].sum(axis=1) for _, joint_indices in PART_GROUPS], axis=1,)
    triangle_part_weights = vertex_part_weights[faces].sum(axis=1)
    triangle_part_labels = np.argmax(triangle_part_weights, axis=1).astype(np.uint8)
    confidence = triangle_part_weights.max(axis=1) / np.maximum(triangle_part_weights.sum(axis=1),EPSILON,)
    low_confidence_count = int(np.count_nonzero(confidence < LOW_CONFIDENCE_THRESHOLD))
    return triangle_part_labels, low_confidence_count


def write_default_pose_motion(output_path, fps, faces, vertices, root_position, triangle_part_labels):
    if fps <= 0.0:
        raise ValueError(f"Invalid FPS: {fps}")
    vertices = np.asarray(vertices, dtype=np.float32)
    if vertices.ndim != 2 or vertices.shape[1] != 3:
        raise ValueError(f"Expected vertices shaped as [vertices, 3], got {vertices.shape}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    root_position = np.asarray(root_position, dtype=np.float32).reshape(1, 3)

    temp_output_path = output_path.with_name(output_path.name + ".tmp")
    try:
        with temp_output_path.open("wb") as out_file:
            write_motion_header(out_file, fps, faces, 1, vertices.shape[0])
            root_position.tofile(out_file)
            vertices.tofile(out_file)
            write_default_motion_labels(out_file, faces, triangle_part_labels, IS_DEFAULT_MOTION_ASSET)

        temp_output_path.replace(output_path)
    except Exception:
        temp_output_path.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description="Convert a neutral SMPL default pose to a one-frame mesh motion asset.")
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--arm-angle-deg", default=DEFAULT_A_POSE_ARM_ANGLE_DEG, type=float)
    parser.add_argument("--shoulder-axis", default=DEFAULT_A_POSE_SHOULDER_AXIS, choices=tuple(SHOULDER_AXIS_TO_OFFSET))
    parser.add_argument("--fps", default=30.0, type=float)
    parser.add_argument("--ground-clearance", default=0.0, type=float)
    args = parser.parse_args()

    set_smpl_compatibility()

    from smplx.body_models import SMPL

    model = SMPL(str(args.model), gender="neutral", num_betas=10, batch_size=1)
    model.eval()
    body_pose = torch.from_numpy(make_default_pose(args.arm_angle_deg, args.shoulder_axis)[SMPL_BODY_POSE_OFFSET:]).reshape(1, -1)

    with torch.no_grad():
        output = model(
            betas=torch.zeros((1, 10), dtype=torch.float32),
            global_orient=torch.zeros((1, 3), dtype=torch.float32),
            body_pose=body_pose,
            transl=torch.zeros((1, 3), dtype=torch.float32),
            return_verts=True,
        )

    vertices = output.vertices[0].detach().cpu().numpy()
    root_position = output.joints[0, 0].detach().cpu().numpy()
    vertices, root_position = align_pose_to_ground(vertices, root_position, args.ground_clearance)
    triangle_part_labels, low_confidence_count = make_triangle_part_labels(
        model.faces,
        model.lbs_weights.detach().cpu().numpy(),
    )
    write_default_pose_motion(args.output, args.fps, model.faces, vertices, root_position, triangle_part_labels)

    print()
    print(f"Converted neutral SMPL A-pose: {args.model}")
    print(f"Output: {args.output}")
    print("Pose: a")
    print(f"Arm angle: {args.arm_angle_deg}")
    print(f"Shoulder axis: {args.shoulder_axis}")
    print(f"FPS: {args.fps}")
    print(f"Frames: 1")
    print(f"Vertices: {vertices.shape[0]}")
    print(f"Indices: {model.faces.size}")
    print(f"Triangle part labels: {triangle_part_labels.size}")
    print(f"Low-confidence labels (< {LOW_CONFIDENCE_THRESHOLD:.2f}): {low_confidence_count}")
    print(f"Min Y: {vertices[:, 1].min():.6f}")
    print()


if __name__ == "__main__":
    main()
