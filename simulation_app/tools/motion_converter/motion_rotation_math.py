import numpy as np

EPSILON = 1.0e-8
QUATERNION_DOT_THRESHOLD = 0.9995


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
