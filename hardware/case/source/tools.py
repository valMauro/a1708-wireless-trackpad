import sys, os, math, json, base64
import numpy as np
import sdf3 as S, scocca as C

def orient_up(V, n):
    n = np.asarray(n, float); n /= np.linalg.norm(n); t = np.array([0, 0, -1.0])
    v = np.cross(n, t); c = np.dot(n, t)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    R = np.eye(3) + vx + vx @ vx * (1 / (1 + c))
    V = V @ R.T
    return V - V.min(axis=0)

def weld_check(V, F, name):
    key = np.round(V[F].reshape(-1, 3) * 1e4).astype(np.int64)
    u, inv = np.unique(key, axis=0, return_inverse=True)
    G = inv.reshape(-1, 3); G = G[(G[:, 0] != G[:, 1]) & (G[:, 1] != G[:, 2]) & (G[:, 0] != G[:, 2])]
    e = np.sort(np.vstack([G[:, [0, 1]], G[:, [1, 2]], G[:, [2, 0]]]), axis=1)
    _, cnt = np.unique(e, axis=0, return_counts=True)
    W = u / 1e4; tri = W[G]
    vol = np.einsum("ij,ij->i", tri[:, 0], np.cross(tri[:, 1], tri[:, 2])).sum() / 6
    print(name, "triangoli", len(G), "spigoli non a coppie", int((cnt != 2).sum()), "vol cm3 %.1f" % (vol / 1000), (W.max(0) - W.min(0)).round(1), flush=True)
    return int((cnt != 2).sum())

def export(which, res):
    if which == "fondo":
        V, F = S.mesh(C.plate(), res)
    else:
        V, F = S.mesh(C.shell(), res); V = orient_up(V, (0, C.SLOPE, 1))
    bad = weld_check(V, F, which)
    os.makedirs("out", exist_ok=True)
    S.write_stl("out/%s.stl" % which, V, F)
    return bad

def boxes():
    P, T = C.P, C.PCB_TOP
    def b(xa, ya, xb, yb, h, z0=None):
        (x0, y0), (x1, y1) = P(xa, ya), P(xb, yb)
        return S.box(min(x0, x1), min(y0, y1), T if z0 is None else z0, max(x0, x1), max(y0, y1), T + h)
    pcb = b(0, 0, 124, 66, 0.0, z0=C.PCB_BOT)
    xc = P(78, 0)[0]
    bat = S.S(lambda X, Y, Z: np.maximum(np.hypot(Y - P(0, 11.5)[1], Z - (T + 11.25)) - 9.25, np.abs(X - xc) - 38.5), (xc - 39, 0, T, xc + 39, 30, T + 21))
    parts = (bat | b(1.6, 23.9, 53.9, 44.9, 15.0) | b(27.5, 9.9, 30.5, 20.1, 20.4) | b(99.3, 24.8, 109.7, 35.2, 10.5) | b(99.3, 36.0, 109.7, 46.4, 10.5)
             | b(60.4, 33.4, 67.6, 40.6, 12) | b(73.4, 33.4, 80.6, 40.6, 12) | b(86.4, 33.4, 93.6, 40.6, 12) | b(14.6, -0.8, 23.4, 7.3, 3.3)
             | b(3.2, -0.15, 8.8, 5.3, 5.8) | b(34.2, 1.2, 122.8, 21.8, 2.0))
    return pcb, parts

def trackpad():
    """glass slab + brackets + studs, at their designed height"""
    d = C.GLASS_DROP
    g = S.box(C.GX0, C.GY0, -10, C.GX1, C.GY1, 60, 3.0) & C.below(d) & C.above(d + 1.0)
    for i in (0, 1):
        sx = C.GX0 + C.STUD_U if i == 0 else C.GX1 - C.STUD_U
        g = g | (S.box(sx - 6.3, C.GY1 - 75.7, -10, sx + 6.3, C.GY1 - 5.3, 60) & C.below(d) & C.above(d + C.BAR))
    for sx, sy in C.STUDS:
        g = g | (S.cyl(sx, sy, -10, 60, 1.2) & C.below(d) & C.above(d + C.BAR + C.STUD_H))
    return g

def viewer(res=1.0):
    def pack(shape, name, col, r=res):
        V, F = S.mesh(shape, r)
        return dict(name=name, col=col, a=1.0, on=True, v=base64.b64encode(V.astype(np.float32).tobytes()).decode(),
                    f=base64.b64encode(F.astype(np.uint32).tobytes()).decode())
    pcb, parts = boxes()
    P = [pack(C.shell(), "Guscio", [0.86, 0.86, 0.89]), pack(C.plate(), "Fondo", [0.52, 0.6, 0.74]),
         pack(trackpad(), "Trackpad", [0.12, 0.12, 0.14], 0.6), pack(pcb, "Scheda", [0.45, 0.16, 0.52], 0.6),
         pack(parts, "Componenti", [0.25, 0.25, 0.27], 0.6)]
    h = open("viewer_template.html").read()
    h = h.replace("__PARTS__", json.dumps(P)).replace("__CX__", "%.1f" % (C.W / 2)).replace("__CY__", "%.1f" % (C.D / 2)).replace("__CZ__", "14")
    h = h.replace("__SIZE__", "%.0f × %.0f mm, alta %.0f mm dietro e %.0f davanti" % (C.W, C.D, C.ztop(0), C.ztop(C.D)))
    h = h.replace("__ANGLE__", "%.1f° (come il Magic Trackpad 1, ma più alta)" % math.degrees(math.atan(C.SLOPE)))
    os.makedirs("out", exist_ok=True)
    open("out/scocca-3d.html", "w").write(h)

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "export":
        for w in sys.argv[3:]: export(w, float(sys.argv[2]))
    elif cmd == "viewer":
        viewer()
