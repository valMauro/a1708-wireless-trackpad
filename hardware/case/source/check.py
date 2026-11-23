import numpy as np, scocca as C, tools as T
sh, pl, tp = C.shell(), C.plate(), T.trackpad()
pcb, parts = T.boxes()
def samp(shape, res=0.5):
    b = shape.bb
    xs, ys, zs = [np.arange(b[i], b[i + 3], res) for i in range(3)]
    X, Y, Z = np.meshgrid(xs, ys, zs, indexing="ij"); m = shape(X, Y, Z) < -0.05
    return X[m], Y[m], Z[m]
for name, a in (("trackpad", tp), ("scheda", pcb), ("componenti", parts)):
    X, Y, Z = samp(a, 0.4 if name == "trackpad" else 0.6)
    print("%-11s vs guscio min %+.2f   vs fondo min %+.2f" % (name, sh(X, Y, Z).min(), pl(X, Y, Z).min()))
X, Y, Z = samp(tp, 0.4); X2, Y2, Z2 = samp(pcb | parts, 0.6)
# trackpad vs board+parts
print("trackpad vs scheda/componenti min %+.2f" % (pcb | parts)(X, Y, Z).min())
# contact check: bracket underside just above the support top?
for sx, sy in C.STUDS[:4]:
    z = C.ztop(sy) - C.GLASS_DROP - C.BAR
    print("perno y=%.1f: fondo sotto la piastra %+.2f, sopra %+.2f" % (sy, pl(np.array([sx + 4.5]), np.array([sy]), np.array([z - 0.1]))[0], pl(np.array([sx + 4.5]), np.array([sy]), np.array([z + 0.1]))[0]))
