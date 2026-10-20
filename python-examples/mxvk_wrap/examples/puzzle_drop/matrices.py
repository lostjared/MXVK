"""Row-major matrices accepted by MXVK's Python ModelUniforms bindings."""
import math
import numpy as np


def translation(x, y, z):
    matrix = np.eye(4, dtype=np.float32)
    matrix[:3, 3] = (x, y, z)
    return matrix


def scale(x, y, z):
    return np.diag((x, y, z, 1)).astype(np.float32)


def rotation_x(angle):
    c, s = math.cos(angle), math.sin(angle)
    return np.array(((1, 0, 0, 0), (0, c, -s, 0), (0, s, c, 0), (0, 0, 0, 1)), dtype=np.float32)


def rotation_y(angle):
    c, s = math.cos(angle), math.sin(angle)
    return np.array(((c, 0, s, 0), (0, 1, 0, 0), (-s, 0, c, 0), (0, 0, 0, 1)), dtype=np.float32)


def look_at(eye, center):
    forward = np.array(center, dtype=np.float32) - eye
    forward /= np.linalg.norm(forward)
    side = np.cross(forward, (0, 1, 0))
    side /= np.linalg.norm(side)
    up = np.cross(side, forward)
    matrix = np.eye(4, dtype=np.float32)
    matrix[:3, :3] = (side, up, -forward)
    matrix[:3, 3] = -matrix[:3, :3] @ eye
    return matrix


def perspective(aspect):
    tangent = math.tan(math.radians(45) / 2)
    near, far = 0.1, 100
    matrix = np.zeros((4, 4), dtype=np.float32)
    matrix[0, 0], matrix[1, 1] = 1 / (aspect * tangent), -1 / tangent
    matrix[2, 2], matrix[2, 3], matrix[3, 2] = far / (near - far), far * near / (near - far), -1
    return matrix
