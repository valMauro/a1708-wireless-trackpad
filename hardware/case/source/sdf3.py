"""Tiny SDF CAD: primitives + booleans evaluated on numpy grids, meshed with marching cubes, written as binary STL."""
import numpy as np
from skimage.measure import marching_cubes


class S:
    def __init__(self, f, bb):
        self.f, self.bb = f, bb          # bb = (xmin,ymin,zmin,xmax,ymax,zmax)

    def __call__(self, X, Y, Z):
        return self.f(X, Y, Z)

    def __or__(self, o):
        return S(lambda X, Y, Z: np.minimum(self(X, Y, Z), o(X, Y, Z)), _bbu(self.bb, o.bb))

    def __and__(self, o):
        return S(lambda X, Y, Z: np.maximum(self(X, Y, Z), o(X, Y, Z)), _bbi(self.bb, o.bb))

    def __sub__(self, o):
        return S(lambda X, Y, Z: np.maximum(self(X, Y, Z), -o(X, Y, Z)), self.bb)

    def move(self, dx=0, dy=0, dz=0):
        b = self.bb
        return S(lambda X, Y, Z: self(X - dx, Y - dy, Z - dz), (b[0] + dx, b[1] + dy, b[2] + dz, b[3] + dx, b[4] + dy, b[5] + dz))


def _bbu(a, b):
    return tuple(min(a[i], b[i]) for i in range(3)) + tuple(max(a[i], b[i]) for i in range(3, 6))


def _bbi(a, b):
    return tuple(max(a[i], b[i]) for i in range(3)) + tuple(min(a[i], b[i]) for i in range(3, 6))


BIG = (-1e4, -1e4, -1e4, 1e4, 1e4, 1e4)


def box(x0, y0, z0, x1, y1, z1, r=0.0):
    """axis-aligned box, vertical edges rounded with radius r"""
    cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
    hx, hy, hz = (x1 - x0) / 2, (y1 - y0) / 2, (z1 - z0) / 2

    def f(X, Y, Z):
        qx = np.abs(X - cx) - (hx - r)
        qy = np.abs(Y - cy) - (hy - r)
        d2 = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2) + np.minimum(np.maximum(qx, qy), 0) - r
        dz = np.abs(Z - cz) - hz
        return np.sqrt(np.maximum(d2, 0) ** 2 + np.maximum(dz, 0) ** 2) + np.minimum(np.maximum(d2, dz), 0)
    return S(f, (x0, y0, z0, x1, y1, z1))


def cyl(cx, cy, z0, z1, r):
    """vertical cylinder"""
    def f(X, Y, Z):
        d2 = np.sqrt((X - cx) ** 2 + (Y - cy) ** 2) - r
        dz = np.abs(Z - (z0 + z1) / 2) - (z1 - z0) / 2
        return np.sqrt(np.maximum(d2, 0) ** 2 + np.maximum(dz, 0) ** 2) + np.minimum(np.maximum(d2, dz), 0)
    return S(f, (cx - r, cy - r, z0, cx + r, cy + r, z1))


def cyl_y(cx, cz, y0, y1, r):
    """cylinder along Y"""
    def f(X, Y, Z):
        d2 = np.sqrt((X - cx) ** 2 + (Z - cz) ** 2) - r
        dy = np.abs(Y - (y0 + y1) / 2) - (y1 - y0) / 2
        return np.sqrt(np.maximum(d2, 0) ** 2 + np.maximum(dy, 0) ** 2) + np.minimum(np.maximum(d2, dy), 0)
    return S(f, (cx - r, y0, cz - r, cx + r, y1, cz + r))


def halfspace(px, py, pz, nx, ny, nz):
    """solid where (p - p0).n <= 0"""
    n = np.array([nx, ny, nz], float); n /= np.linalg.norm(n)
    return S(lambda X, Y, Z: (X - px) * n[0] + (Y - py) * n[1] + (Z - pz) * n[2], BIG)


def prism_yz(poly, x0, x1):
    """extrusion along X of a convex-or-not polygon given in the (y, z) plane"""
    P = np.asarray(poly, float)

    def f(X, Y, Z):
        d = np.full(Y.shape, np.inf)
        inside = np.zeros(Y.shape, bool)
        for (ay, az), (by, bz) in zip(P, np.roll(P, -1, axis=0)):
            ey, ez = by - ay, bz - az
            wy, wz = Y - ay, Z - az
            t = np.clip((wy * ey + wz * ez) / (ey * ey + ez * ez), 0, 1)
            d = np.minimum(d, np.hypot(wy - t * ey, wz - t * ez))
            c = ((az > Z) != (bz > Z)) & (Y < (by - ay) * (Z - az) / np.where(bz - az == 0, 1e-12, bz - az) + ay)
            inside ^= c
        d2 = np.where(inside, -d, d)
        dx = np.abs(X - (x0 + x1) / 2) - (x1 - x0) / 2
        return np.sqrt(np.maximum(d2, 0) ** 2 + np.maximum(dx, 0) ** 2) + np.minimum(np.maximum(d2, dx), 0)
    return S(f, (x0, P[:, 0].min(), P[:, 1].min(), x1, P[:, 0].max(), P[:, 1].max()))


def mesh(shape, res=0.25, bb=None, slab=40):
    """returns (verts, faces) via marching cubes, evaluated in slabs along Y"""
    b = bb or shape.bb
    pad = 2 * res
    xs = np.arange(b[0] - pad, b[3] + pad + res, res)
    ys = np.arange(b[1] - pad, b[4] + pad + res, res)
    zs = np.arange(b[2] - pad, b[5] + pad + res, res)
    V, F, n0 = [], [], 0
    for j0 in range(0, len(ys) - 1, slab):
        yy = ys[j0:j0 + slab + 1]
        if len(yy) < 2:
            break
        X, Y, Z = np.meshgrid(xs, yy, zs, indexing="ij")
        d = shape(X, Y, Z).astype(np.float32)
        if d.min() > 0 or d.max() < 0:
            continue
        v, f, _, _ = marching_cubes(d, 0.0, spacing=(res, res, res))
        v += np.array([xs[0], yy[0], zs[0]])
        V.append(v); F.append(f + n0); n0 += len(v)
    return np.vstack(V), np.vstack(F)


def write_stl(path, V, F):
    tri = V[F]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    data = np.zeros(len(F), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    data["n"] = n; data["v"] = tri
    with open(path, "wb") as fh:
        fh.write(b"trackpad case".ljust(80, b" "))
        fh.write(np.uint32(len(F)).tobytes())
        fh.write(data.tobytes())
