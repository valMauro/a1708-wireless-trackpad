"""Wedge case for the A1708 trackpad + all-in-one board v2.
Device frame: X = width (left->right), Y = depth from the BACK outer face (0) to the front, Z = up (0 = table)."""
import math, sys
import numpy as np
import sdf3 as S

# ---------------- measurements (mm)
TP_L, TP_W = 131.3, 81.6        # trackpad glass, from the photos (long side along X)
TP_R = 3.0                      # corner radius of the opening
GAP = 0.8                       # glass-to-opening clearance per side (generous: no test print)
T_WALL, T_TOP, T_FLOOR = 2.2, 2.0, 2.0
RIM = 3.0                       # top border beside the glass (sides and front)
W = TP_L + 2 * GAP + 2 * RIM    # outer width
Y_TP0 = 49.0                    # rear edge of the trackpad opening
Y_TPF = Y_TP0 + TP_W + 2 * GAP  # front edge of the opening
D = Y_TPF + RIM                 # outer depth
Z_FRONT = 9.0                   # top surface height at the front edge of the opening
SLOPE = 0.20                    # 11.3 degrees
R_OUT = 6.0

def ztop(y):
    return Z_FRONT + SLOPE * (Y_TPF - y)
COS = 1 / math.sqrt(1 + SLOPE ** 2)
def below(off=0.0):
    """solid below the top plane moved down by 'off' (measured vertically)"""
    return S.halfspace(0, Y_TPF, Z_FRONT - off, 0, SLOPE, 1)
def above(off=0.0):
    return S.halfspace(0, Y_TPF, Z_FRONT - off, 0, -SLOPE, -1)

# ---------------- board v2 placement in the case
PCB_X0 = (W - 124.0) / 2
PCB_Y0 = T_WALL + 0.9           # USB-C shell just inside the back wall
STANDOFF = 3.2
PCB_BOT = T_FLOOR + STANDOFF
PCB_TOP = PCB_BOT + 1.6
def P(x, y):
    """board coords (KiCad, seen from the top, y down) -> case coords (right-handed, Z up, Y towards the user).
    Looking from the user side towards the back, X grows to the LEFT, so the board x is mirrored."""
    return PCB_X0 + 124.0 - x, PCB_Y0 + y
KEYS = [P(kx, 37.0) for kx in (64.0, 77.0, 90.0)]
LEDS = [P(kx, 30.2) for kx in (64.0, 77.0, 90.0)]
HOLES_PCB = [P(3.5, 62.5), P(120.5, 62.5), P(120.5, 47.5), P(29.5, 4.0)]
USB = P(19.0, 0.0)[0], PCB_TOP + 1.63
SWX, SWZ = P(6.0, 0.0)[0], PCB_TOP + 3.2
KEY_TOP = PCB_TOP + 12.0        # TS6612CJ: 12 mm

# trackpad glass in case coords
GX0, GX1 = W / 2 - TP_L / 2, W / 2 + TP_L / 2
GY0, GY1 = Y_TP0 + GAP, Y_TPF - GAP
# support ribs under the two end brackets, studs (from the photos)
STUD_U = 18.9 - 0.75            # stud centres from each short end; rev 2: brackets 1.5 mm further apart (test print)
RIB_HALF = 6.0                  # supports 12 mm wide, centred on the studs
STUD_V = (22.7, 31.55, 52.65, 61.4)  # from the FRONT (haptic) long edge (photos)
GLASS_DROP = 0.4                # glass ~0.5 mm below the case surface, like the MacBook
BAR = 1.8                       # underside of the steel brackets below the glass top: the supports touch it
STUD_H = 2.9                    # studs stick out of the brackets (photo: tips 4.7 below the glass top)
STUD_HOLE = 3.0                 # round pocket the stud drops into (locates the trackpad)
SCREW_T = 1.4                   # plastic under the screw head (rev 3: was 2.4, the original T5 screws did not reach the threads)
SCREW_HOLE = 2.0
ACCESS = 8.0                    # screwdriver access from below
CHAMFER = 0.6                   # 45 deg chamfer on the top edge of the trackpad opening
SCREWED = STUD_V[:3]            # the rear row sits over the board edge: located by its pocket, no screw

STUDS = [(GX0 + STUD_U if i == 0 else GX1 - STUD_U, GY1 - v) for i in (0, 1) for v in STUD_V]

BOSS_R, BOSS_PILOT = 2.4, 1.1   # M2.5 self-tapping
BX = (4.5, W - 4.5)
BOSSES = [(BX[0], 6.0, None), (BX[1], 6.0, None), (BX[0], 60.0, ztop(60) - 3.0), (BX[1], 60.0, ztop(60) - 3.0),
          (BX[0], 118.0, ztop(118) - 3.0), (BX[1], 118.0, ztop(118) - 3.0)]


def rrect_xz(cx, cz, w, h, r, y0, y1):
    """rounded rectangle in the XZ plane, extruded along Y"""
    def f(X, Y, Z):
        qx = np.abs(X - cx) - (w / 2 - r)
        qz = np.abs(Z - cz) - (h / 2 - r)
        d2 = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qz, 0) ** 2) + np.minimum(np.maximum(qx, qz), 0) - r
        dy = np.abs(Y - (y0 + y1) / 2) - (y1 - y0) / 2
        return np.sqrt(np.maximum(d2, 0) ** 2 + np.maximum(dy, 0) ** 2) + np.minimum(np.maximum(d2, dy), 0)
    return S.S(f, (cx - w / 2, y0, cz - h / 2, cx + w / 2, y1, cz + h / 2))


def shell():
    zmax = ztop(0) + 1
    outer = S.box(0, 0, 0, W, D, zmax, R_OUT) & below()
    inner = S.box(T_WALL, T_WALL, -1, W - T_WALL, D - T_WALL, zmax, R_OUT - T_WALL) & below(T_TOP / COS)
    s = outer - inner
    # trackpad opening
    s = s - S.box(W / 2 - TP_L / 2 - GAP, Y_TP0, -1, W / 2 + TP_L / 2 + GAP, Y_TPF, zmax, TP_R)
    ox0, ox1, r = W / 2 - TP_L / 2 - GAP, W / 2 + TP_L / 2 + GAP, TP_R
    def ch(X, Y, Z, ox0=ox0, ox1=ox1, r=r):
        cx, cy = (ox0 + ox1) / 2, (Y_TP0 + Y_TPF) / 2
        qx = np.abs(X - cx) - ((ox1 - ox0) / 2 - r); qy = np.abs(Y - cy) - ((Y_TPF - Y_TP0) / 2 - r)
        d2 = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2) + np.minimum(np.maximum(qx, qy), 0) - r
        return d2 - (Z - (ztop(Y) - CHAMFER))
    s = s - S.S(ch, (ox0 - 2, Y_TP0 - 2, 0, ox1 + 2, Y_TPF + 2, zmax))
    # bosses (screws from below through the floor)
    for bx, by, zt in BOSSES:
        b = S.cyl(bx, by, T_FLOOR, zmax, BOSS_R) | S.box(bx - BOSS_R if bx < W / 2 else bx, by - BOSS_R, T_FLOOR,
                                                         bx if bx < W / 2 else bx + BOSS_R, by + BOSS_R, zmax)
        b = b & below(0.5)
        if zt is not None:      # under the glass: stop below it, 55 deg chamfer towards the wall (prints without support)
            k = math.tan(math.radians(55))
            if bx < W / 2:
                b = b & S.halfspace(T_WALL, 0, zt, k, 0, 1)
            else:
                b = b & S.halfspace(W - T_WALL, 0, zt, -k, 0, 1)
        s = s | b
    for bx, by, zt in BOSSES:
        s = s - S.cyl(bx, by, -1, T_FLOOR + 12, BOSS_PILOT)
    # flexure keys
    for kx, ky in KEYS:
        y_free, y_hinge, hw = ky - 4.5, ky + 7.5, 5.0
        slot = S.box(kx - hw - 1.0, y_free - 1.0, 0, kx + hw + 1.0, y_hinge, zmax, 0.5) \
            - S.box(kx - hw, y_free, -1, kx + hw, y_hinge + 1, zmax + 1)
        s = s - (slot & above(T_TOP / COS + 1.0))
        # thinner tongue (1.4 mm) so it flexes
        s = s - (S.box(kx - hw, y_free, 0, kx + hw, y_hinge, zmax) & above(T_TOP / COS + 0.5) & below(1.4 / COS))
        # nub pressing the switch (0.3 mm above the switch top)
        s = s | (S.cyl(kx, ky, KEY_TOP + 0.5, zmax, 2.0) & below(1.0))
        # finger mark: shallow dish
        s = s - (S.cyl(kx, ky, 0, zmax + 2, 3.2) & above(0.4))
    # LED light guides: tube down to 1 mm above the LED, 2 mm bore through the top
    for lx, ly in LEDS:
        s = s | (S.cyl(lx, ly, PCB_TOP + 1.2, zmax, 2.0) & below(0.5))
        s = s - S.cyl(lx, ly, PCB_TOP + 0.5, zmax + 2, 1.0)
    # back wall: USB-C plug opening, power push-button (slot open to the bottom so the shell can be lowered over it)
    s = s - rrect_xz(USB[0], USB[1], 12.6, 6.6, 3.0, -1, T_WALL + 0.5)
    s = s - S.box(SWX - 2.3, -1, -1, SWX + 2.3, T_WALL + 0.5, SWZ + 2.3, 0.0)
    return s


def plate():
    cl = 0.25
    p = S.box(T_WALL + cl, T_WALL + cl, 0, W - T_WALL - cl, D - T_WALL - cl, T_FLOOR, R_OUT - T_WALL - cl)
    for hx, hy in HOLES_PCB:
        p = p | S.cyl(hx, hy, 0, PCB_BOT, 3.0)
    top = GLASS_DROP + BAR                       # support top below the case surface
    y_pcb_end = PCB_Y0 + 66.0
    y0, y1 = y_pcb_end + 0.6, GY1 - 15.0
    xs = []
    for i in (0, 1):
        sx = GX0 + STUD_U if i == 0 else GX1 - STUD_U
        x0, x1 = sx - RIB_HALF, sx + RIB_HALF
        xs.append((x0, x1))
        rib = S.box(x0, y0, 0, x1, y1, ztop(0), 1.0)
        # above the board the support reaches further back (45 deg underside, prints without support)
        ext = S.box(x0, y_pcb_end - 2.6, 0, x1, y1, ztop(0), 1.0) & S.halfspace(0, y0, PCB_TOP + 3.6, 0, -1, -1)
        p = p | ((rib | ext) & below(top))
    # cross brace between the two supports (stiffens the floor under the trackpad)
    yb = GY1 - (STUD_V[1] + STUD_V[2]) / 2
    p = p | S.box(xs[0][1] - 1, yb - 1.5, 0, xs[1][0] + 1, yb + 1.5, T_FLOOR + 4.0)
    for sx, sy in STUDS:
        v = GY1 - sy
        p = p - (S.cyl(sx, sy, 0, ztop(0) + 5, STUD_HOLE / 2) & above(top + STUD_H + 0.4))        # stud pocket
        p = p - S.cyl(sx, sy, -1, ztop(0) + 5, SCREW_HOLE / 2)                                    # screw
        if any(abs(v - s) < 0.01 for s in SCREWED):
            p = p - (S.cyl(sx, sy, -1, ztop(0), ACCESS / 2) & below(top + STUD_H + 0.4 + SCREW_T))
    for hx, hy in HOLES_PCB:
        p = p - S.cyl(hx, hy, 0.8, PCB_BOT + 1, 0.85)          # M2 self-tapping
    for bx, by, _ in BOSSES:
        p = p - S.cyl(bx, by, -1, T_FLOOR + 1, 1.5)
        cone = S.S(lambda X, Y, Z, bx=bx, by=by: np.sqrt((X - bx) ** 2 + (Y - by) ** 2) - (2.9 - Z), (bx - 3, by - 3, -1, bx + 3, by + 3, 2))
        p = p - (cone & S.box(bx - 4, by - 4, -1, bx + 4, by + 4, 1.4))
    # rubber feet: one under the middle of each trackpad support + the four corners
    feet = [(GX0 + STUD_U, yb), (GX1 - STUD_U, yb), (14, 14), (W - 14, 14), (14, D - 14), (W - 14, D - 14)]
    for fx, fy in feet:
        p = p - S.cyl(fx, fy, -1, 0.8, 5.25)
    return p


def test_frame():
    """flat 2 mm frame with the exact trackpad opening, to check the fit before the big print"""
    ow, oh = TP_L + 2 * GAP + 12, TP_W + 2 * GAP + 12
    f = S.box(0, 0, 0, ow, oh, 2.0, 6.0) - S.box(6, 6, -1, ow - 6, oh - 6, 3, TP_R)
    return f


if __name__ == "__main__":
    print("W=%.1f D=%.1f  back=%.1f front=%.1f  angle=%.1f" % (W, D, ztop(0), ztop(D), math.degrees(math.atan(SLOPE))))
