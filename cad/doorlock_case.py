"""
도어락 3D 도면 생성 스크립트 (CadQuery)

실행:  pip install cadquery && python3 cad/doorlock_case.py
출력:
  cad/print/A_box_body.stl     보드 A 박스 본체 (앞판+벽)   ← 3D 프린터용
  cad/print/A_box_cover.stl    보드 A 박스 뒷판 (레일 일체)  ← 3D 프린터용
  cad/print/A_rail_bracket.stl 문에 붙이는 레일 브래킷        ← 3D 프린터용
  cad/assembly/doorlock_assembly.step  전체 완성 도면 (CAD용, 부품명·색 포함)
  cad/assembly/doorlock_assembly.glb   웹 뷰어용 (docs/case3d.html 에 내장)

좌표계 (단위 mm, 문 바깥에서 박스를 정면으로 본 기준)
  X: 오른쪽(+)  Y: 문 안쪽 방향(+)  Z: 위(+)
  박스 앞면 = y 0, 박스 중심 = x 0, z 0
"""
import os
import json
import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
PRINT_DIR = os.path.join(HERE, "print")
ASM_DIR = os.path.join(HERE, "assembly")

# ───────────────────────── 공통 치수 ─────────────────────────
CLR = 0.4          # Z-ABS 2 끼움 공차 (한쪽)
WALL = 2.5         # 측벽 두께
FRONT = 3.0        # 앞판 두께
W, H = 129.0, 185.0        # 박스 외곽 폭(x) / 높이(z)
BODY_D = 63.0              # 본체 깊이 (앞판 포함, y 0~63)
COVER_T = 7.0              # 뒷판 두께 (y 63~70, 바깥면에 도브테일 홈)
RAIL_H = 4.0               # 도브테일 레일 높이 (브래킷 쪽, y 70.4 → 66.4)
RAIL_BASE, RAIL_TIP = 8.0, 12.0   # 레일 폭: 브래킷 판 쪽 / 끝쪽
RAIL_X = 46.0              # 레일 중심 x (±)
IX, IZ = W / 2 - WALL, H / 2 - WALL   # 내부 반폭 62, 반높이 90

Y_COVER0 = BODY_D                  # 63
Y_COVER1 = BODY_D + COVER_T        # 70  뒷판 바깥면
Y_GROOVE = Y_COVER1 - RAIL_H       # 66  홈 바닥
Y_BR_PL0 = Y_COVER1 + CLR          # 70.4 브래킷 판 앞면
Y_RAIL_TIP = Y_BR_PL0 - RAIL_H     # 66.4 레일 끝
BR_PL_T = 4.0
Y_DOOR0 = Y_BR_PL0 + BR_PL_T       # 74.4 문 바깥면
DOOR_T = 9.0
Y_DOOR1 = Y_DOOR0 + DOOR_T         # 83.4 문 안쪽면

LIP_Z1 = -H / 2                    # -92.5 받침 윗면 = 박스 바닥
LIP_Z0 = LIP_Z1 - 6.0              # -98.5
LIP_Y0 = 45.0
LOCK_X, LOCK_Y = 2.3, 56.0         # 잠금나사 M3 위치
CABLE = (20.0, 30.0)               # 케이블 구멍 (x, z), 뒷판 Ø16 / 브래킷·문 Ø20
COVER_SCREWS = [(sx * 58.5, sz * 87.0) for sx in (-1, 1) for sz in (-1, 1)]

# 앞판 모듈 배치 (x, z 중심)
PIR_C = (0.0, 74.0)
LCD_C = (0.0, 38.0)
KEY_C = (-27.0, -26.5)     # 4x4 멤브레인 키패드 69x77
RFID_C = (37.0, -26.0)     # RC522 40x60 (세로)
BB = (-56.0, -1.4, -83.0, 82.1)   # 풀 브레드보드 x0,x1,z0,z1 (165.1x54.6)
UNO_X0, UNO_Z0 = 6.5, -81.5        # 우노 PCB: USB가 아래로


# ───────────────────────── 헬퍼 ─────────────────────────
def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def cbox(cx, cy, cz, dx, dy, dz):
    return box(cx - dx / 2, cx + dx / 2, cy - dy / 2, cy + dy / 2, cz - dz / 2, cz + dz / 2)


def cyl(r, h, pnt, d):
    return cq.Workplane("XY").add(cq.Solid.makeCylinder(r, h, cq.Vector(*pnt), cq.Vector(*d)))


def cone(r1, r2, h, pnt, d):
    return cq.Workplane("XY").add(cq.Solid.makeCone(r1, r2, h, cq.Vector(*pnt), cq.Vector(*d)))


def prism_z(pts_xy, z0, z1):
    """XY 다각형을 z0~z1 로 돌출"""
    return cq.Workplane("XY").polyline(pts_xy).close().extrude(z1 - z0).translate((0, 0, z0))


def l_guides(cx, cz, w, h, y0, height, arm=6.0, t=1.6):
    """PCB 네 모서리 L자 가이드 (앞판 뒷면 y0 에서 +y로 height)"""
    g = None
    hw, hh = w / 2, h / 2
    for sx in (-1, 1):
        for sz in (-1, 1):
            ex, ez = cx + sx * hw, cz + sz * hh       # PCB 모서리
            a = box(min(ex + sx * t, ex - sx * arm), max(ex + sx * t, ex - sx * arm), y0, y0 + height,
                    min(ez, ez + sz * t), max(ez, ez + sz * t))
            b = box(min(ex, ex + sx * t), max(ex, ex + sx * t), y0, y0 + height,
                    min(ez, ez - sz * arm), max(ez, ez - sz * arm))
            piece = a.union(b)
            g = piece if g is None else g.union(piece)
    return g


# Uno 기준 좌표 (u: 길이 0~68.6 USB쪽 0, v: 폭 0~53.3) → 박스 (x, z)
def uno_xz(u, v):
    return UNO_X0 + (53.3 - v), UNO_Z0 + u


UNO_HOLES = [(14.0, 2.5), (15.3, 50.7), (66.1, 7.6), (66.1, 35.5)]
UNO_STANDOFF = 6.0
Y_UNO_BACK = Y_COVER0 - UNO_STANDOFF   # 57  PCB 뒷면
Y_UNO_FRONT = Y_UNO_BACK - 1.6         # 55.4


# ───────────────────────── 1) 박스 본체 ─────────────────────────
def make_body():
    b = box(-W / 2, W / 2, 0, BODY_D, -H / 2, H / 2)
    b = b.cut(box(-IX, IX, FRONT, BODY_D + 1, -IZ, IZ))

    # 뒷판 나사 보스 Ø7 (M3 셀프태핑, 파일럿 Ø2.5)
    for (x, z) in COVER_SCREWS:
        boss = cyl(3.5, BODY_D - FRONT, (x, FRONT, z), (0, 1, 0))
        boss = boss.union(box(x - 3.5 if x < 0 else x, x if x < 0 else x + 3.5, FRONT, BODY_D, z - 3.5, z + 3.5))
        boss = boss.union(box(x - 3.5, x + 3.5, FRONT, BODY_D, z - 3.5 if z < 0 else z, z if z < 0 else z + 3.5))
        boss = boss.intersect(box(-IX, IX, 0, BODY_D, -IZ, IZ))
        b = b.union(boss)
        b = b.cut(cyl(1.25, 25, (x, BODY_D - 24.9, z), (0, 1, 0)))

    # 잠금나사 보스 (바닥, M3 너트 트랩, 뒤에서 너트 삽입)
    lb = box(LOCK_X - 4, LOCK_X + 4, FRONT, BODY_D, -IZ, -83.5)   # 앞판까지 이어진 리브(출력 시 오버행 없음)
    b = b.union(lb)
    b = b.cut(cyl(1.7, 10, (LOCK_X, LOCK_Y, -H / 2 - 1), (0, 0, 1)))
    b = b.cut(box(LOCK_X - 2.95 - 0.1, LOCK_X + 2.95 + 0.1, LOCK_Y - 3.3, BODY_D + 1, -87.6, -84.8))

    # USB-B 노치 (바닥벽, 뒤쪽으로 열림)
    ux0, _ = uno_xz(0, 44.2)
    ux1, _ = uno_xz(0, 31.7)
    b = b.cut(box(ux0 - 1.8, ux1 + 1.8, Y_UNO_FRONT - 13.5, BODY_D + 1, -H / 2 - 1, -IZ + 0.01))

    # PIR 돔 구멍 Ø24 + PCB 가이드 (32.3x24.3)
    b = b.cut(cyl(12.0, FRONT + 2, (PIR_C[0], -1, PIR_C[1]), (0, 1, 0)))
    b = b.union(l_guides(PIR_C[0], PIR_C[1], 32.3 + 2 * CLR, 24.3 + 2 * CLR, FRONT, 6.0))

    # LCD 베젤 창 72x26.5 + 스탠드오프 4개 (75x31 간격, 높이 4)
    b = b.cut(cbox(LCD_C[0], FRONT / 2, LCD_C[1], 71.2 + 2 * CLR, FRONT + 2, 24.2 + 2 * CLR + 1.5))
    for sx in (-1, 1):
        for sz in (-1, 1):
            x, z = LCD_C[0] + sx * 37.5, LCD_C[1] + sz * 15.5
            b = b.union(cyl(3.0, 4.0, (x, FRONT, z), (0, 1, 0)))
            b = b.cut(cyl(1.25, 4.5, (x, FRONT, z), (0, 1, 0)))

    # 키패드 리본 슬롯 24x4 (키패드 하단 바로 아래)
    kz_bot = KEY_C[1] - 77 / 2
    b = b.cut(cbox(KEY_C[0], FRONT / 2, kz_bot - 3.0, 24.0, FRONT + 2, 4.0))

    # RFID 영역: 앞판을 안쪽에서 1.5mm만 남기고 얇게 + 가이드
    b = b.cut(cbox(RFID_C[0], FRONT, RFID_C[1], 40 + 2 * CLR + 3.2, 3.0, 60 + 2 * CLR + 3.2))
    b = b.union(l_guides(RFID_C[0], RFID_C[1], 40 + 2 * CLR, 60 + 2 * CLR, 1.5, 4.0))
    return b


# ───────────────────────── 2) 박스 뒷판 ─────────────────────────
def rail_profile(cx, y_base, y_tip, w_base, w_tip):
    return [(cx - w_base / 2, y_base), (cx + w_base / 2, y_base),
            (cx + w_tip / 2, y_tip), (cx - w_tip / 2, y_tip)]


def make_cover():
    c = box(-W / 2, W / 2, Y_COVER0, Y_COVER1, -H / 2, H / 2)
    # 도브테일 홈 (바깥면 입구가 좁고 안쪽이 넓음). 폭 = 레일 + 한쪽 0.4
    for sx in (-1, 1):
        cx = sx * RAIL_X
        def gw(y):   # 해당 y 에서 홈 폭
            rail = RAIL_BASE + (RAIL_TIP - RAIL_BASE) * (Y_BR_PL0 - y) / RAIL_H
            return rail + 2 * CLR
        yo, yb = Y_COVER1 + 1, Y_GROOVE
        c = c.cut(prism_z([(cx - gw(yo) / 2, yo), (cx + gw(yo) / 2, yo),
                           (cx + gw(yb) / 2, yb), (cx - gw(yb) / 2, yb)], -H / 2 - 1, H / 2 + 1))
    # 접시머리 M3 구멍 4개
    for (x, z) in COVER_SCREWS:
        c = c.cut(cyl(1.7, COVER_T + 2, (x, Y_COVER0 - 1, z), (0, 1, 0)))
        c = c.cut(cone(3.3, 1.7, 1.6, (x, Y_COVER1 + 0.001, z), (0, -1, 0)))
    # 케이블 구멍 Ø16
    c = c.cut(cyl(8.0, 20, (CABLE[0], Y_COVER0 - 1, CABLE[1]), (0, 1, 0)))
    # 우노 스탠드오프 (Ø6, 높이 6, M3 셀프태핑 파일럿 Ø2.5)
    for (u, v) in UNO_HOLES:
        x, z = uno_xz(u, v)
        c = c.union(cyl(3.0, UNO_STANDOFF, (x, Y_COVER0, z), (0, -1, 0)))
        c = c.cut(cyl(1.25, UNO_STANDOFF + 2.5, (x, Y_COVER0 + 2, z), (0, -1, 0)))
    # 브레드보드 위치 테두리 (높이 1.2, 폭 1.5)
    x0, x1, z0, z1 = BB
    rim = box(x0 - CLR - 1.5, x1 + CLR + 1.5, Y_COVER0 - 1.2, Y_COVER0, z0 - CLR - 1.5, z1 + CLR + 1.5)
    rim = rim.cut(box(x0 - CLR, x1 + CLR, Y_COVER0 - 2, Y_COVER0 + 1, z0 - CLR, z1 + CLR))
    rim = rim.cut(box(LOCK_X - 4 - CLR, LOCK_X + 4 + CLR, Y_COVER0 - 2, Y_COVER0 + 1, -IZ - 1, -83.5 + CLR))
    for (x, z) in COVER_SCREWS:   # 본체 모서리 보스 자리 비우기
        rim = rim.cut(cbox(x, Y_COVER0, z, 7 + 2 * CLR + 2, 4, 7 + 2 * CLR))
    c = c.union(rim)
    return c


# ───────────────────────── 3) 레일 브래킷 ─────────────────────────
def make_bracket():
    br = box(-W / 2, W / 2, Y_BR_PL0, Y_DOOR0, LIP_Z0, H / 2)
    # 도브테일 레일 (판에서 박스 쪽으로 돌출, 끝이 넓음). 위쪽 끝 3mm 모따기 → 끼우기 쉬움
    for sx in (-1, 1):
        rail = prism_z(rail_profile(sx * RAIL_X, Y_BR_PL0 + 0.01, Y_RAIL_TIP, RAIL_BASE, RAIL_TIP),
                       LIP_Z1, H / 2)
        rail = rail.cut(box(sx * RAIL_X - 8, sx * RAIL_X + 8, Y_RAIL_TIP - 1, Y_RAIL_TIP + 1.2,
                            H / 2 - 3, H / 2 + 1))
        br = br.union(rail)
    # 받침 턱 (박스 바닥을 받침) - USB 케이블 지나가는 x 5~35 는 비움
    for (x0, x1) in ((-W / 2, 5.0), (35.0, W / 2)):
        br = br.union(box(x0, x1, LIP_Y0, Y_BR_PL0 + 0.01, LIP_Z0, LIP_Z1))
    # 잠금나사 구멍 Ø3.4 + 아래 카운터보어 Ø6.5 깊이 3
    br = br.cut(cyl(1.7, 10, (LOCK_X, LOCK_Y, LIP_Z0 - 1), (0, 0, 1)))
    br = br.cut(cyl(3.25, 4, (LOCK_X, LOCK_Y, LIP_Z0 - 1), (0, 0, 1)))
    # 케이블 구멍 Ø20
    br = br.cut(cyl(10.0, 20, (CABLE[0], Y_BR_PL0 - 5, CABLE[1]), (0, 1, 0)))
    # 문 고정 접시나사 구멍 Ø4.2 (목재나사 3.5~4) x4
    for x in (-22, 22):
        for z in (-75, 75):
            br = br.cut(cyl(2.1, 10, (x, Y_BR_PL0 - 1, z), (0, 1, 0)))
            br = br.cut(cone(4.2, 2.1, 2.1, (x, Y_BR_PL0 - 0.001, z), (0, 1, 0)))
    return br


# ───────────────────────── 모듈 (단순화 모델) ─────────────────────────
def make_uno_A():
    # PCB
    xs = [uno_xz(0, 0)[0], uno_xz(0, 53.3)[0]]
    pcb = box(min(xs), max(xs), Y_UNO_FRONT, Y_UNO_BACK, UNO_Z0, UNO_Z0 + 68.6)
    for (u, v) in UNO_HOLES:
        x, z = uno_xz(u, v)
        pcb = pcb.cut(cyl(1.6, 5, (x, Y_UNO_FRONT - 1, z), (0, 1, 0)))
    parts = {"pcb": pcb}
    # USB-B (v 31.7~43.8, 높이 11, u -6.2~9.9)
    xa, za = uno_xz(-6.2, 43.8)
    xb, zb = uno_xz(9.9, 31.7)
    parts["usb"] = box(min(xa, xb), max(xa, xb), Y_UNO_FRONT - 10.9, Y_UNO_FRONT, min(za, zb), max(za, zb))
    xa, za = uno_xz(-1.8, 12.2)
    xb, zb = uno_xz(11.6, 3.3)
    parts["dc"] = box(min(xa, xb), max(xa, xb), Y_UNO_FRONT - 11, Y_UNO_FRONT, min(za, zb), max(za, zb))
    # 헤더 (디지털: v 50.5~53, u 17~64 / 전원·아날로그: v 0.3~2.8, u 27~64)
    for (u0, u1, v0, v1) in ((17.5, 64.0, 50.3, 52.8), (27.0, 64.0, 0.5, 3.0)):
        xa, za = uno_xz(u0, v1)
        xb, zb = uno_xz(u1, v0)
        parts["hdr%d" % int(u0)] = box(min(xa, xb), max(xa, xb), Y_UNO_FRONT - 8.5, Y_UNO_FRONT,
                                       min(za, zb), max(za, zb))
    xa, za = uno_xz(28, 38)
    xb, zb = uno_xz(63, 30)
    parts["mcu"] = box(min(xa, xb), max(xa, xb), Y_UNO_FRONT - 4, Y_UNO_FRONT, min(za, zb), max(za, zb))
    return parts


def make_breadboard_A():
    x0, x1, z0, z1 = BB
    bb = box(x0, x1, Y_COVER0 - 8.5, Y_COVER0, z0, z1)
    bb = bb.cut(box(x0 + 26.0, x1 - 26.0, Y_COVER0 - 9, Y_COVER0 - 7.5, z0 + 3, z1 - 3))  # 가운데 홈
    return bb


def make_lcd():
    cx, cz = LCD_C
    y_pcb0 = FRONT + 4.0          # 7.0
    pcb = cbox(cx, y_pcb0 + 0.8, cz, 80, 1.6, 36)
    for sx in (-1, 1):
        for sz in (-1, 1):
            pcb = pcb.cut(cyl(1.6, 3, (cx + sx * 37.5, y_pcb0 - 0.5, cz + sz * 15.5), (0, 1, 0)))
    bezel = cbox(cx, y_pcb0 - 3.5, cz, 71.2, 7.0, 24.2)
    glass = cbox(cx, 0.4, cz, 64.5, 0.6, 14.5)
    backpack = cbox(cx - 20, y_pcb0 + 1.6 + 6, cz, 41.5, 12, 19)
    return {"pcb": pcb, "bezel": bezel, "glass": glass, "i2c": backpack}


def make_keypad():
    cx, cz = KEY_C
    pad = cbox(cx, -0.4, cz, 69, 0.8, 77)
    keys = None
    for i in range(4):
        for j in range(4):
            k = cbox(cx - 25.5 + 17 * i, -1.0, cz + 23 - 15.5 * j + 3, 12, 0.6, 11)
            keys = k if keys is None else keys.union(k)
    zb = cz - 77 / 2
    tail = box(cx - 10, cx + 10, -0.3, 0.0, zb - 3.0, zb)
    tail = tail.union(box(cx - 10, cx + 10, 0.0, 14.0, zb - 3.0, zb - 2.7))
    conn = box(cx - 10.3, cx + 10.3, 14.0, 22.0, zb - 4.3, zb - 1.7)
    return {"pad": pad, "keys": keys, "tail": tail.union(conn)}


def make_rc522():
    cx, cz = RFID_C
    pcb = cbox(cx, 1.5 + 0.8, cz, 40, 1.6, 60)
    chip = cbox(cx, 1.5 + 1.6 + 0.8, cz - 12, 6, 1.6, 6)
    xtal = cbox(cx + 8, 1.5 + 1.6 + 2, cz - 12, 4, 4, 11)
    hdr = cbox(cx, 1.5 + 1.6 + 4.25, cz - 30 + 2, 20.3, 8.5, 2.5)
    return {"pcb": pcb, "parts": chip.union(xtal).union(hdr)}


def make_pir():
    cx, cz = PIR_C
    y_pcb = FRONT + 3.0    # 돔 플랜지 3mm
    pcb = cbox(cx, y_pcb + 0.8, cz, 32.3, 1.6, 24.3)
    flange = cbox(cx, FRONT + 1.5, cz, 24, 3.0, 24).cut(cyl(11.6, 5, (cx, FRONT - 1, cz), (0, 1, 0)))
    dome = cq.Workplane("XY").add(cq.Solid.makeSphere(11.5, cq.Vector(cx, FRONT, cz), cq.Vector(0, -1, 0), 0, 90))
    dome = dome.union(cyl(11.5, 3.0, (cx, FRONT, cz), (0, 1, 0)))
    back = cbox(cx, y_pcb + 1.6 + 5, cz, 28, 10, 18)
    return {"pcb": pcb, "lens": dome.union(flange), "parts": back}


def make_screws():
    s = None
    for (x, z) in COVER_SCREWS:
        p = cone(3.0, 1.5, 1.5, (x, Y_COVER1, z), (0, -1, 0)).union(cyl(1.5, 16, (x, Y_COVER1, z), (0, -1, 0)))
        s = p if s is None else s.union(p)
    lock = cyl(2.75, 3.0, (LOCK_X, LOCK_Y, LIP_Z0 + 0.01), (0, 0, 1)).union(
        cyl(1.5, 10, (LOCK_X, LOCK_Y, LIP_Z0 + 3), (0, 0, 1)))
    nut = cq.Workplane("XY").polygon(6, 6.35).extrude(2.4).translate((LOCK_X, LOCK_Y, -87.4))
    return s.union(lock), nut


# ───────────────────────── 큰 틀 (직접 제작 / 참고 모델) ─────────────────────────
DOOR_X0, DOOR_X1 = -170.0, 90.0
DOOR_Z0, DOOR_Z1 = -198.0, 170.0
POST = 30.0
FRAME_Y0, FRAME_Y1 = Y_DOOR0, Y_DOOR0 + POST          # 74.4 ~ 104.4
BASE_T = 12.0
BASE_Z1 = DOOR_Z0 - 2.0                               # -200
SERVO_SHAFT = (76.0, 0.0)                             # (x, z)
BB_B = (-155.0, -73.0, -60.0, -5.0)                   # 하프 브레드보드 82x55 (x0,x1,z0,z1)
UNO_B = (-148.0, 20.0)                                # 우노 B 좌하단 (가로, USB 왼쪽)
BTN_C = (20.0, -80.0)
HORN_Y0 = Y_DOOR1 + 1.0 + 26.7 + 1.5                  # 112.6 혼 앞면
STRIKE_Y1 = HORN_Y0 - 0.8                             # 걸쇠 받이 블록 (기둥 뒤, 혼과 0.8 간격)


def make_frame():
    d = {}
    door = box(DOOR_X0, DOOR_X1, Y_DOOR0, Y_DOOR1, DOOR_Z0, DOOR_Z1)
    door = door.cut(cyl(10.0, 20, (CABLE[0], Y_DOOR0 - 5, CABLE[1]), (0, 1, 0)))
    door = door.cut(cyl(10.0, 20, (20.0, Y_DOOR0 - 5, -130.0), (0, 1, 0)))
    d["door"] = door
    d["post_hinge"] = box(DOOR_X0 - 2 - POST, DOOR_X0 - 2, FRAME_Y0, FRAME_Y1, BASE_Z1, DOOR_Z1 + 2 + POST)
    d["post_latch"] = box(DOOR_X1 + 2, DOOR_X1 + 2 + POST, FRAME_Y0, FRAME_Y1, BASE_Z1, DOOR_Z1 + 2 + POST)
    d["beam_top"] = box(DOOR_X0 - 2, DOOR_X1 + 2, FRAME_Y0, FRAME_Y1, DOOR_Z1 + 2, DOOR_Z1 + 2 + POST)
    d["base"] = box(DOOR_X0 - 2 - POST - 30, DOOR_X1 + 2 + POST + 30, -60, 200, BASE_Z1 - BASE_T, BASE_Z1)
    # 문 멈춤 턱 (문 안쪽 방향, 힌지쪽 기둥·윗보) : 문은 바깥(-y)으로 열림
    d["stop_hinge"] = box(DOOR_X0 - 2, DOOR_X0 + 10, Y_DOOR1, Y_DOOR1 + 10, DOOR_Z0, DOOR_Z1)
    d["stop_top"] = box(DOOR_X0 + 10, DOOR_X1 + 2, Y_DOOR1, Y_DOOR1 + 10, DOOR_Z1 - 12, DOOR_Z1 + 2)
    # 경첩 2개 (바깥면)
    hg = None
    for zc in (-120.0, 120.0):
        h_ = box(DOOR_X0 - 2 - 20, DOOR_X0 + 20, Y_DOOR0 - 1.5, Y_DOOR0, zc - 25, zc + 25)
        h_ = h_.union(cyl(2.5, 50, (DOOR_X0 - 1, Y_DOOR0 - 2.5, zc - 25), (0, 0, 1)))
        hg = h_ if hg is None else hg.union(h_)
    d["hinges"] = hg
    # 걸쇠 스트라이크 (기둥 안쪽면, 서보 혼이 닿는 마모 방지판)
    d["strike"] = box(DOOR_X1 + 2, DOOR_X1 + 2 + 20, FRAME_Y1, STRIKE_Y1, SERVO_SHAFT[1] - 15, SERVO_SHAFT[1] + 15)
    return d


def make_servo():
    """SG90: 몸체 22.8x12.2x22.7, 날개 32.2, 축은 몸체 끝에서 5.9. 축이 +y(문 안쪽 바깥)로 향함"""
    sx, sz = SERVO_SHAFT
    bx0 = sx + 5.9 - 22.8     # 축이 몸체 오른쪽 끝에서 5.9 안쪽
    y0 = Y_DOOR1 + 1.0        # 받침 블록 위에서 조금 띄움 (블록이 날개를 받침)
    body = box(bx0, bx0 + 22.8, y0, y0 + 22.7, sz - 6.1, sz + 6.1)
    tabs = box(bx0 - 4.7, bx0 + 22.8 + 4.7, y0 + 15.9, y0 + 18.4, sz - 6.1, sz + 6.1)
    top = cyl(6.0, 4.0, (sx, y0 + 22.7, sz), (0, 1, 0))
    shaft = cyl(2.4, 3.0, (sx, y0 + 26.7, sz), (0, 1, 0))
    yh = y0 + 26.7 + 1.5
    # 혼(일자형) : 잠금 0° = +x 방향, 문틀 기둥 뒤로 들어감
    horn = box(sx - 4, sx + 20.5, yh, yh + 1.8, sz - 3, sz + 3).union(cyl(3.6, 1.8, (sx, yh, sz), (0, 1, 0)))
    blocks = box(bx0 - 4.7, bx0 - 0.3, Y_DOOR1, y0 + 15.9, sz - 6.1, sz + 6.1).union(
        box(bx0 + 23.1, bx0 + 27.5, Y_DOOR1, y0 + 15.9, sz - 6.1, sz + 6.1))
    return {"body": body.union(tabs).union(top).union(shaft), "horn": horn, "blocks": blocks}, yh


def make_board_B():
    d = {}
    # 우노 B (가로, 문 안쪽 면에 스탠드오프 6mm)
    ux, uz = UNO_B
    y0 = Y_DOOR1 + 6
    pcb = box(ux, ux + 68.6, y0, y0 + 1.6, uz, uz + 53.3)
    d["uno_pcb"] = pcb
    d["uno_parts"] = box(ux - 6.2, ux + 9.9, y0 + 1.6, y0 + 12.5, uz + 31.7, uz + 43.8).union(
        box(ux + 17.5, ux + 64, y0 + 1.6, y0 + 10.1, uz + 50.3, uz + 52.8)).union(
        box(ux + 27, ux + 64, y0 + 1.6, y0 + 10.1, uz + 0.5, uz + 3.0)).union(
        box(ux - 1.8, ux + 11.6, y0 + 1.6, y0 + 12.6, uz + 3.3, uz + 12.2))
    so = None
    for (u, v) in UNO_HOLES:
        c = cyl(2.5, 6, (ux + u, Y_DOOR1, uz + v), (0, 1, 0))
        so = c if so is None else so.union(c)
    d["uno_standoffs"] = so
    x0, x1, z0, z1 = BB_B
    d["breadboard_B"] = box(x0, x1, Y_DOOR1, Y_DOOR1 + 8.5, z0, z1)
    # LED 2개 (초록·빨강, 5mm)
    leds = {}
    for name, (lx, lz) in (("led_green", (-120.0, -20.0)), ("led_red", (-105.0, -20.0))):
        leds[name] = cyl(2.5, 6.0, (lx, Y_DOOR1 + 8.5, lz), (0, 1, 0)).union(
            cq.Workplane("XY").add(cq.Solid.makeSphere(2.5, cq.Vector(lx, Y_DOOR1 + 14.5, lz),
                                                       cq.Vector(0, 1, 0), 0, 90)))
    d.update(leds)
    cap = cyl(5.0, 16, (-85.0, Y_DOOR1 + 8.5, -45.0), (0, 1, 0))
    d["cap_470uF"] = cap
    # 실내 버튼 (12x12 택트 + 받침)
    bx, bz = BTN_C
    d["button_base"] = cbox(bx, Y_DOOR1 + 4, bz, 20, 8, 20)
    d["button"] = cbox(bx, Y_DOOR1 + 8 + 1.75, bz, 12, 3.5, 12).union(
        cyl(3.0, 3.0, (bx, Y_DOOR1 + 11.5, bz), (0, 1, 0)))
    return d


# ───────────────────────── 조립 / 출력 ─────────────────────────
COL = {
    "print": (0.93, 0.55, 0.16),   # 출력물: 주황
    "cover": (0.98, 0.70, 0.30),
    "bracket": (0.75, 0.42, 0.10),
    "pcb_blue": (0.05, 0.35, 0.62),
    "pcb_green": (0.10, 0.45, 0.22),
    "black": (0.12, 0.12, 0.13),
    "white": (0.94, 0.94, 0.92),
    "metal": (0.72, 0.73, 0.75),
    "lcd": (0.35, 0.65, 0.95),
    "wood": (0.80, 0.66, 0.47),
    "wood2": (0.70, 0.55, 0.37),
    "servo": (0.20, 0.35, 0.85),
    "red": (0.90, 0.12, 0.12),
    "green": (0.10, 0.80, 0.25),
    "yellow": (0.95, 0.80, 0.20),
}


def to_print_pose(wp, kind):
    """출력 방향으로 눕히고 베드 중앙(0,0)에 배치"""
    if kind == "body":      # 앞면이 베드
        s = wp.rotate((0, 0, 0), (1, 0, 0), 90)
    else:                   # 뒷판: 바깥면(홈 입구)이 베드 / 브래킷: 문쪽 면이 베드
        s = wp.rotate((0, 0, 0), (1, 0, 0), -90)
    bb = s.val().BoundingBox()
    return s.translate((-(bb.xmin + bb.xmax) / 2, -(bb.ymin + bb.ymax) / 2, -bb.zmin))


def main():
    os.makedirs(PRINT_DIR, exist_ok=True)
    os.makedirs(ASM_DIR, exist_ok=True)

    body, cover, bracket = make_body(), make_cover(), make_bracket()
    printed = {"A_box_body": (body, "body"), "A_box_cover": (cover, "cover"), "A_rail_bracket": (bracket, "bracket")}
    info = {}
    for name, (wp, kind) in printed.items():
        p = to_print_pose(wp, kind)
        bb = p.val().BoundingBox()
        assert bb.xlen <= 200 and bb.ylen <= 200 and bb.zlen <= 180, (name, bb.xlen, bb.ylen, bb.zlen)
        cq.exporters.export(p, os.path.join(PRINT_DIR, name + ".stl"), tolerance=0.02, angularTolerance=0.1)
        info[name] = {"x": round(bb.xlen, 1), "y": round(bb.ylen, 1), "z": round(bb.zlen, 1),
                      "volume_cm3": round(p.val().Volume() / 1000, 1)}
        print(f"{name}: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm, {p.val().Volume()/1000:.1f} cm3")

    # 전체 조립
    asm = cq.Assembly(name="doorlock")
    pa = cq.Assembly(name="A_box_printed")
    pa.add(body, name="A_box_body", color=cq.Color(*COL["print"]))
    pa.add(cover, name="A_box_cover", color=cq.Color(*COL["cover"]))
    pa.add(bracket, name="A_rail_bracket", color=cq.Color(*COL["bracket"]))
    asm.add(pa)

    ma = cq.Assembly(name="board_A_modules")
    uno = make_uno_A()
    ma.add(uno["pcb"], name="uno_A_pcb", color=cq.Color(*COL["pcb_blue"]))
    ma.add(uno["usb"], name="uno_A_usb", color=cq.Color(*COL["metal"]))
    ma.add(uno["dc"], name="uno_A_dcjack", color=cq.Color(*COL["black"]))
    for k in uno:
        if k.startswith("hdr") or k == "mcu":
            ma.add(uno[k], name="uno_A_" + k, color=cq.Color(*COL["black"]))
    ma.add(make_breadboard_A(), name="breadboard_full_A", color=cq.Color(*COL["white"]))
    lcd = make_lcd()
    ma.add(lcd["pcb"], name="lcd1602_pcb", color=cq.Color(*COL["pcb_green"]))
    ma.add(lcd["bezel"], name="lcd1602_bezel", color=cq.Color(*COL["black"]))
    ma.add(lcd["glass"], name="lcd1602_glass", color=cq.Color(*COL["lcd"]))
    ma.add(lcd["i2c"], name="lcd1602_i2c", color=cq.Color(*COL["black"]))
    kp = make_keypad()
    ma.add(kp["pad"], name="keypad_4x4", color=cq.Color(*COL["white"]))
    ma.add(kp["keys"], name="keypad_keys", color=cq.Color(*COL["black"]))
    ma.add(kp["tail"], name="keypad_ribbon", color=cq.Color(*COL["black"]))
    rf = make_rc522()
    ma.add(rf["pcb"], name="rc522_pcb", color=cq.Color(*COL["pcb_blue"]))
    ma.add(rf["parts"], name="rc522_parts", color=cq.Color(*COL["black"]))
    pir = make_pir()
    ma.add(pir["pcb"], name="pir_pcb", color=cq.Color(*COL["pcb_green"]))
    ma.add(pir["lens"], name="pir_lens", color=cq.Color(*COL["white"]))
    ma.add(pir["parts"], name="pir_parts", color=cq.Color(*COL["black"]))
    screws, nut = make_screws()
    ma.add(screws, name="screws_M3", color=cq.Color(*COL["metal"]))
    ma.add(nut, name="nut_M3", color=cq.Color(*COL["metal"]))
    asm.add(ma)

    fa = cq.Assembly(name="frame_handmade")
    fr = make_frame()
    for k, v in fr.items():
        col = COL["metal"] if k in ("hinges", "strike") else (COL["wood"] if k == "door" else COL["wood2"])
        fa.add(v, name=k, color=cq.Color(*col))
    asm.add(fa)

    ba = cq.Assembly(name="board_B_side")
    sv, _ = make_servo()
    ba.add(sv["body"], name="servo_SG90", color=cq.Color(*COL["servo"]))
    ba.add(sv["horn"], name="servo_horn", color=cq.Color(*COL["white"]))
    ba.add(sv["blocks"], name="servo_blocks", color=cq.Color(*COL["wood2"]))
    bdict = make_board_B()
    cmap = {"uno_pcb": "pcb_blue", "uno_parts": "black", "uno_standoffs": "metal", "breadboard_B": "white",
            "led_green": "green", "led_red": "red", "cap_470uF": "black", "button_base": "wood2", "button": "black"}
    for k, v in bdict.items():
        ba.add(v, name=k, color=cq.Color(*COL[cmap[k]]))
    asm.add(ba)

    asm.save(os.path.join(ASM_DIR, "doorlock_assembly.step"))
    asm.save(os.path.join(ASM_DIR, "doorlock_assembly.glb"))
    with open(os.path.join(ASM_DIR, "print_info.json"), "w") as f:
        json.dump(info, f, indent=2)
    return asm




# ───────────────────────── 웹 뷰어 (docs/case3d.html) ─────────────────────────
PART_NAMES = {
    "A_box_body": ["박스 본체 (출력)", "앞판 + 벽, 129×185×63. 앞면이 프린터 바닥"],
    "A_box_cover": ["박스 뒷판 (출력)", "7mm 판, 바깥면에 도브테일 홈 2줄 · Uno/브레드보드 고정"],
    "A_rail_bracket": ["레일 브래킷 (출력)", "문에 나사 4개로 고정, 박스를 위에서 밀어 넣음"],
    "uno_A_pcb": ["Arduino Uno A", "68.6×53.3, USB가 아래(바닥 노치)"], "uno_A_usb": ["Uno A USB-B", ""],
    "uno_A_dcjack": ["Uno A DC 잭", "사용 안 함"], "uno_A_hdr17": ["Uno A 디지털 헤더", ""],
    "uno_A_hdr27": ["Uno A 전원·아날로그 헤더", ""], "uno_A_mcu": ["ATmega328P", ""],
    "breadboard_full_A": ["풀 브레드보드 (830홀)", "165.1×54.6×8.5"],
    "lcd1602_pcb": ["LCD 1602 PCB", "80×36, 스탠드오프 4개"], "lcd1602_bezel": ["LCD 베젤", "71.2×24.2, 앞판 창에 맞물림"],
    "lcd1602_glass": ["LCD 화면", ""], "lcd1602_i2c": ["I2C 모듈 (PCF8574)", ""],
    "keypad_4x4": ["4x4 멤브레인 키패드", "69×77, 앞면에 부착"], "keypad_keys": ["키패드 버튼", ""],
    "keypad_ribbon": ["키패드 리본 + 8핀", "슬롯 24×4로 안쪽에"],
    "rc522_pcb": ["RC522 RFID", "40×60, 얇게 판 앞판(1.5mm) 뒤"], "rc522_parts": ["RC522 부품·헤더", ""],
    "pir_pcb": ["PIR (HC-SR501)", "32.3×24.3"], "pir_lens": ["PIR 렌즈 돔", "Ø23, 앞판 Ø24 구멍"],
    "pir_parts": ["PIR 뒷면 부품", ""], "screws_M3": ["M3 나사", "뒷판 M3×16 접시 4 + 잠금 M3×10"],
    "nut_M3": ["M3 너트 (잠금용)", "본체 바닥 보스 홈"],
    "door": ["문 (직접 제작)", "260×368×9, Ø20 구멍 2개"], "post_hinge": ["경첩쪽 기둥", "30×30×402"],
    "post_latch": ["걸쇠쪽 기둥", "30×30×402"], "beam_top": ["윗보", "264×30×30"],
    "base": ["바닥판", "384×260×12"], "stop_hinge": ["문 멈춤 턱", ""], "stop_top": ["문 멈춤 턱 (위)", ""],
    "hinges": ["경첩 2개", ""], "strike": ["걸쇠 받이 블록", "20×30×7.4, 서보 혼이 여기 걸림"],
    "servo_SG90": ["SG90 서보", "22.8×12.2×22.7, 축이 문 안쪽 바깥을 향함"],
    "servo_horn": ["서보 혼 (걸쇠)", "잠금 0° = 기둥 뒤로 / 열림 90° = 위로"],
    "servo_blocks": ["서보 받침 블록", "4.4×12.2×16.9 ×2"],
    "uno_pcb": ["Arduino Uno B", "문 안쪽 면"], "uno_parts": ["Uno B 커넥터·헤더", ""],
    "uno_standoffs": ["Uno B 스탠드오프", "높이 6"], "breadboard_B": ["하프 브레드보드 (B)", "82×55"],
    "led_green": ["초록 LED (열림)", "D10"], "led_red": ["빨강 LED (잠김)", "D12"],
    "cap_470uF": ["470µF 커패시터", "서보 전원"], "button_base": ["버튼 받침", ""],
    "button": ["실내 버튼", "D2 ↔ GND, INT0"],
}


def front_svg():
    """박스 정면 배치도 (SVG, 1 mm = 2.4 px)"""
    k = 2.4
    pad = 70
    Wp, Hp = W * k + pad * 2, H * k + pad * 2
    X = lambda x: pad + (x + W / 2) * k
    Z = lambda z: pad + (H / 2 - z) * k
    out = [f'<svg viewBox="0 0 {Wp:.0f} {Hp:.0f}" style="width:100%;max-width:520px;display:block;margin:0 auto;'
           f'background:var(--panel);border:1px solid var(--line);border-radius:12px" role="img" '
           f'aria-label="박스 앞판 배치도">']

    def rect(cx, cz, w, h, fill, stroke, dash=False, label=None, sub=None):
        x0, y0 = X(cx - w / 2), Z(cz + h / 2)
        d = ' stroke-dasharray="5 4"' if dash else ''
        out.append(f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{w*k:.1f}" height="{h*k:.1f}" rx="3" fill="{fill}" '
                   f'stroke="{stroke}" stroke-width="1.5"{d}/>')
        if label:
            out.append(f'<text x="{X(cx):.1f}" y="{Z(cz)+4:.1f}" text-anchor="middle" font-size="13" '
                       f'fill="var(--ink)" font-weight="600">{label}</text>')
        if sub:
            out.append(f'<text x="{X(cx):.1f}" y="{Z(cz)+20:.1f}" text-anchor="middle" font-size="11" '
                       f'fill="var(--muted)">{sub}</text>')

    rect(0, 0, W, H, "#ed8c2922", "#ed8c29")
    # 모듈
    rect(*RFID_C, 40, 60, "none", "#0d5a9e", True, "RFID", "뒤 40×60")
    rect(*KEY_C, 69, 77, "#ffffff10", "var(--muted)", False, "키패드", "69×77")
    rect(KEY_C[0], KEY_C[1] - 77 / 2 - 3, 24, 4, "var(--ink)", "var(--ink)")
    rect(*LCD_C, 80, 36, "none", "#1d7a3e", True)
    rect(*LCD_C, 72, 26.5, "#1e293b", "#1e293b", False)
    out.append(f'<text x="{X(0):.1f}" y="{Z(LCD_C[1])+5:.1f}" text-anchor="middle" font-size="13" fill="#7dd3fc" font-weight="600">LCD 창 72×26.5</text>')
    out.append(f'<circle cx="{X(PIR_C[0]):.1f}" cy="{Z(PIR_C[1]):.1f}" r="{12*k:.1f}" fill="#f8fafc" stroke="var(--muted)" stroke-width="1.5"/>')
    out.append(f'<text x="{X(PIR_C[0]):.1f}" y="{Z(PIR_C[1])+4:.1f}" text-anchor="middle" font-size="12" fill="#334155" font-weight="600">PIR Ø24</text>')

    # 치수선
    def hdim(x0, x1, z, text, off):
        y = Z(z) + off
        out.append(f'<line x1="{X(x0):.1f}" y1="{y:.1f}" x2="{X(x1):.1f}" y2="{y:.1f}" stroke="var(--dim)" stroke-width="1.2"/>')
        for xx in (x0, x1):
            out.append(f'<line x1="{X(xx):.1f}" y1="{Z(z):.1f}" x2="{X(xx):.1f}" y2="{y:.1f}" stroke="var(--dim)" stroke-width=".8" stroke-dasharray="2 2"/>')
        out.append(f'<text x="{(X(x0)+X(x1))/2:.1f}" y="{y-5:.1f}" text-anchor="middle" font-size="12" fill="var(--dim)" font-weight="600">{text}</text>')

    def vdim(z0, z1, x, text, off):
        xx = X(x) + off
        out.append(f'<line x1="{xx:.1f}" y1="{Z(z0):.1f}" x2="{xx:.1f}" y2="{Z(z1):.1f}" stroke="var(--dim)" stroke-width="1.2"/>')
        for zz in (z0, z1):
            out.append(f'<line x1="{X(x):.1f}" y1="{Z(zz):.1f}" x2="{xx:.1f}" y2="{Z(zz):.1f}" stroke="var(--dim)" stroke-width=".8" stroke-dasharray="2 2"/>')
        ym = (Z(z0) + Z(z1)) / 2
        out.append(f'<text x="{xx-6:.1f}" y="{ym:.1f}" text-anchor="middle" font-size="12" fill="var(--dim)" font-weight="600" '
                   f'transform="rotate(-90 {xx-6:.1f} {ym:.1f})">{text}</text>')

    hdim(-W / 2, W / 2, H / 2, "129", -28)
    vdim(-H / 2, H / 2, -W / 2, "185", -30)
    vdim(PIR_C[1], H / 2, W / 2, "18.5", 26)
    vdim(LCD_C[1], PIR_C[1], W / 2, "36", 26)
    vdim(KEY_C[1] + 77 / 2, LCD_C[1], W / 2, "26", 26)
    vdim(-H / 2, KEY_C[1] - 77 / 2, W / 2, "27.5", 26)
    hdim(-W / 2, KEY_C[0] - 69 / 2, -H / 2, "3", 26)
    hdim(KEY_C[0] + 69 / 2, RFID_C[0] - 20, -H / 2, "9.5", 26)
    out.append(f'<text x="{X(KEY_C[0]):.1f}" y="{Z(KEY_C[1]-77/2-3)+18:.1f}" text-anchor="middle" font-size="11" fill="var(--muted)">리본 슬롯 24×4</text>')
    out.append('</svg>')
    return "\n".join(out)


def build_viewer():
    import base64
    with open(os.path.join(ASM_DIR, "doorlock_assembly.glb"), "rb") as f:
        b64 = base64.b64encode(f.read()).decode()
    with open(os.path.join(ASM_DIR, "print_info.json")) as f:
        info = json.load(f)
    desc = {
        "A_box_body": ("박스 본체", "앞면을 바닥에 놓고 출력. 앞판 3 / 벽 2.5"),
        "A_box_cover": ("박스 뒷판", "바깥면(도브테일 홈 입구)을 바닥에 놓고 출력"),
        "A_rail_bracket": ("레일 브래킷", "문에 닿는 면을 바닥에 놓고 출력"),
    }
    cards = []
    for k, (t, d) in desc.items():
        i = info[k]
        g = i["volume_cm3"] * 1.04
        cards.append(f'<div class="card"><span class="tag mono">cad/print/{k}.stl</span><b>{t}</b>'
                     f'<span class="big">{i["x"]:.0f} × {i["y"]:.0f} × {i["z"]:.1f}</span>'
                     f'<span>{d}</span><span class="mono" style="color:var(--muted)">솔리드 부피 {i["volume_cm3"]} cm³ '
                     f'(100% 채움 기준 약 {g:.0f} g)</span></div>')
    with open(os.path.join(HERE, "viewer_template.html"), encoding="utf-8") as f:
        html = f.read()
    html = (html.replace("__PRINT_CARDS__", "\n    ".join(cards))
                .replace("__FRONT_SVG__", front_svg())
                .replace("__NAMES__", json.dumps(PART_NAMES, ensure_ascii=False))
                .replace("__GLB_B64__", b64))
    out = os.path.join(os.path.dirname(HERE), "docs", "case3d.html")
    with open(out, "w", encoding="utf-8") as f:
        f.write(html)
    print("viewer:", out, f"{len(html)/1e6:.1f} MB")


if __name__ == "__main__":
    main()
    build_viewer()
