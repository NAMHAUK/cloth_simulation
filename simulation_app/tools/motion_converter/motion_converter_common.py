import inspect
import struct

import numpy as np

MOTION_SIGNATURE = b"SMPLMOTN"
MOTION_HEADER_FORMAT = "<fIII"
SMPL_POSE_COMPONENT_COUNT = 72
SMPL_BODY_POSE_OFFSET = 3
LEFT_SHOULDER_BODY_POSE_INDEX = (16 - 1) * 3
RIGHT_SHOULDER_BODY_POSE_INDEX = (17 - 1) * 3
DEFAULT_A_POSE_ARM_ANGLE_DEG = 80.0
DEFAULT_A_POSE_SHOULDER_AXIS = "z"
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


def make_default_pose(arm_angle_deg=DEFAULT_A_POSE_ARM_ANGLE_DEG, shoulder_axis=DEFAULT_A_POSE_SHOULDER_AXIS):
    pose = np.zeros(SMPL_POSE_COMPONENT_COUNT, dtype=np.float32)
    angle = np.deg2rad(arm_angle_deg)
    axis_offset = SHOULDER_AXIS_TO_OFFSET[shoulder_axis]
    body_pose = pose[SMPL_BODY_POSE_OFFSET:]
    body_pose[LEFT_SHOULDER_BODY_POSE_INDEX + axis_offset] = -angle
    body_pose[RIGHT_SHOULDER_BODY_POSE_INDEX + axis_offset] = angle
    return pose


def write_motion_header(out_file, fps, faces, frame_count, vertex_count):
    indices = np.asarray(faces, dtype=np.uint32).reshape(-1)
    out_file.write(MOTION_SIGNATURE)
    out_file.write(struct.pack(MOTION_HEADER_FORMAT, float(fps), frame_count, vertex_count, indices.size))
    indices.tofile(out_file)


def write_default_motion_labels(out_file, faces, triangle_part_labels, is_default_motion_asset):
    if not is_default_motion_asset:
        return
    if triangle_part_labels is None:
        raise ValueError("Default motion asset requires triangle part labels")

    labels = np.asarray(triangle_part_labels, dtype=np.uint8).reshape(-1)
    triangle_count = np.asarray(faces).reshape(-1).size // 3
    if labels.size != triangle_count:
        raise ValueError(
            "Triangle part label count does not match triangle count: "
            f"{labels.size} != {triangle_count}"
        )

    out_file.write(struct.pack("<I", int(labels.size)))
    labels.tofile(out_file)
