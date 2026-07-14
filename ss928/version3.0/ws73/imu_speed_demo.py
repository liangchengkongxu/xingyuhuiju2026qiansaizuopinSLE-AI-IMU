#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
智能羽毛球 — 拍柄 IMU 球速粗估 Demo（终端输出）

数据格式（与 paibing_imu Notify 一致）:
  @42483,A+56,+20,+85,G-145,+16,R-443,P+123,M94

换算:
  A*:  raw / 1000 -> g（广播 mg；旧 Notify 为 centi-g 则 * 0.01）
  G*:  raw        -> °/s
  R/P: raw / 10    -> °
  M:   raw / 100   -> 合加速度 g

用法:
  # 板端接 IMU 流
  /opt/widget_ui/ws73/sle_connect.sh 0 2>&1 | python3 imu_speed_demo.py

  # 或测本地样例
  python3 imu_speed_demo.py --demo
"""

from __future__ import annotations

import argparse
import math
import re
import sys
import time
from dataclasses import dataclass
from typing import Optional, TextIO

G0 = 9.80665
LINE_RE = re.compile(
    r"@(\d+),A([+-]?\d+),([+-]?\d+),([+-]?\d+),"
    r"G([+-]?\d+),([+-]?\d+),"
    r"R([+-]?\d+),P([+-]?\d+),"
    r"M(\d+)"
)

# paibing_imu Notify 间隔约 100ms（10Hz）
IMU_SAMPLE_MS = 100

SWING_ON_G = 0.14
SWING_OFF_G = 0.08
GYRO_ON_DPS = 110.0
GYRO_ASSIST_MIN_DYN_G = 0.05
BASELINE_UPDATE_MAX_DYN_G = 0.09
SWING_MIN_MS = IMU_SAMPLE_MS // 2
SWING_MAX_MS = 550
COOLDOWN_MS = IMU_SAMPLE_MS * 2 + 20
TRIGGER_CONFIRM_SAMPLES = 2
MIN_VALID_PEAK_DYN_G = 0.11
MIN_VALID_PEAK_GYRO_DPS = 75.0
BASELINE_ALPHA = 0.005
# 拍柄 -> 拍头速度放大（经验值，仅 demo，后续可用实测标定）
TIP_GAIN = 3.2
# 冲量等效时间 (s)
T_EFF = 0.18
# 角速度辅助 (m/s per °/s)
GYRO_SCALE = 0.0065
# 合加速度超出静止的简易标定 (km/h per g)，与上式取较大值
KM_PER_DYN_G = 95.0


@dataclass
class ImuSample:
    t_ms: int
    ax_g: float
    ay_g: float
    az_g: float
    gx_dps: float
    gy_dps: float
    roll_deg: float
    pitch_deg: float
    m_g: float

    @property
    def gyro_mag(self) -> float:
        return math.hypot(self.gx_dps, self.gy_dps)

    @property
    def accel_mag(self) -> float:
        return math.hypot(self.ax_g, self.ay_g, self.az_g)


def parse_line(line: str) -> Optional[ImuSample]:
    m = LINE_RE.search(line)
    if not m:
        return None
    t_ms = int(m.group(1))

    def a_raw(s: str) -> float:
        return int(s) / 1000.0

    return ImuSample(
        t_ms=t_ms,
        ax_g=a_raw(m.group(2)),
        ay_g=a_raw(m.group(3)),
        az_g=a_raw(m.group(4)),
        gx_dps=float(int(m.group(5))),
        gy_dps=float(int(m.group(6))),
        roll_deg=int(m.group(7)) / 10.0,
        pitch_deg=int(m.group(8)) / 10.0,
        m_g=int(m.group(9)) / 100.0,
    )


@dataclass
class SpeedEstimate:
    v_handle_mps: float
    v_shuttle_kmh: float
    peak_dyn_g: float
    peak_gyro: float
    duration_ms: int
    method: str


class SwingDetector:
    def __init__(self) -> None:
        self.baseline_m = 1.0
        self.state = "idle"
        self.swing_t0: int = 0
        self.last_t: int = 0
        self.peak_dyn = 0.0
        self.peak_gyro = 0.0
        self.peak_m = 0.0
        self.hit_count = 0
        self.armed = True
        self.on_confirm = 0

    def _dyn(self, s: ImuSample) -> float:
        return max(0.0, s.m_g - self.baseline_m)

    def _update_baseline(self, s: ImuSample, dyn: float) -> None:
        if dyn < BASELINE_UPDATE_MAX_DYN_G:
            self.baseline_m = (1.0 - BASELINE_ALPHA) * self.baseline_m + BASELINE_ALPHA * s.m_g

    def _try_start_swing(self, s: ImuSample, dyn: float, gyro: float) -> bool:
        if not self.armed:
            self.on_confirm = 0
            return False
        if not (dyn >= SWING_ON_G or (gyro >= GYRO_ON_DPS and dyn >= GYRO_ASSIST_MIN_DYN_G)):
            self.on_confirm = 0
            return False
        self.on_confirm += 1
        if self.on_confirm < TRIGGER_CONFIRM_SAMPLES:
            return False
        self.on_confirm = 0
        self.state = "swing"
        self.swing_t0 = s.t_ms
        self.peak_dyn = dyn
        self.peak_gyro = gyro
        self.peak_m = s.m_g
        self.armed = False
        print(
            f"[挥拍] 开始 t={s.t_ms}ms  M={s.m_g:.2f}g  基线={self.baseline_m:.2f}g  Δ={dyn:.2f}g",
            flush=True,
        )
        return True

    def feed(self, s: ImuSample) -> Optional[SpeedEstimate]:
        dyn = self._dyn(s)
        gyro = s.gyro_mag

        if dyn < SWING_OFF_G:
            self.armed = True

        if self.state == "idle":
            self._update_baseline(s, dyn)
            self._try_start_swing(s, dyn, gyro)
        elif self.state == "swing":
            self.peak_dyn = max(self.peak_dyn, dyn)
            self.peak_gyro = max(self.peak_gyro, gyro)
            self.peak_m = max(self.peak_m, s.m_g)
            dt = max(0, s.t_ms - self.swing_t0)
            if (dyn < SWING_OFF_G and dt >= SWING_MIN_MS) or dt >= SWING_MAX_MS:
                self.baseline_m = 0.75 * self.baseline_m + 0.25 * s.m_g
                est = self._finish(s.t_ms)
                self.state = "cooldown"
                self.last_t = s.t_ms
                self.armed = False
                self.on_confirm = 0
                return est
        elif self.state == "cooldown":
            self._update_baseline(s, dyn)
            since = s.t_ms - self.last_t
            if since < 0:
                since = COOLDOWN_MS + 1
            if since > COOLDOWN_MS:
                self.state = "idle"
                self.on_confirm = 0

        return None

    def _finish(self, t_end: int) -> Optional[SpeedEstimate]:
        if self.peak_dyn < MIN_VALID_PEAK_DYN_G and self.peak_gyro < MIN_VALID_PEAK_GYRO_DPS:
            return None
        dur = max(1, t_end - self.swing_t0)
        # 方法1：峰值动态加速度 × 等效冲量时间
        v_acc = self.peak_dyn * G0 * T_EFF
        v_gyro = self.peak_gyro * GYRO_SCALE
        v_handle = 0.55 * v_acc + 0.45 * v_gyro
        v_from_accel_kmh = v_handle * 3.6 * TIP_GAIN
        v_from_m_kmh = self.peak_dyn * KM_PER_DYN_G
        v_shuttle_kmh = max(v_from_accel_kmh, v_from_m_kmh)
        self.hit_count += 1
        return SpeedEstimate(
            v_handle_mps=v_handle,
            v_shuttle_kmh=v_shuttle_kmh,
            peak_dyn_g=self.peak_dyn,
            peak_gyro=self.peak_gyro,
            duration_ms=dur,
            method="peak_accel+gyro",
        )


def format_status(s: ImuSample, baseline: float) -> str:
    dyn = max(0.0, s.m_g - baseline)
    return (
        f"t={s.t_ms:5d}ms | M={s.m_g:.2f}g Δ={dyn:.2f}g | "
        f"A=({s.ax_g:+.2f},{s.ay_g:+.2f},{s.az_g:+.2f})g | "
        f"G=({s.gx_dps:+.0f},{s.gy_dps:+.0f})°/s | "
        f"R={s.roll_deg:+.1f}° P={s.pitch_deg:+.1f}°"
    )


def should_echo_line(line: str) -> bool:
    keys = (
        "[SLE_IMU]",
        "[sle_connect]",
        "[ws73]",
        "ERROR",
        "gle adapter",
        "sle enable",
        "扫描",
        "连接",
        "配对",
        "Notify",
        "CCCD",
    )
    low = line.lower()
    return any(k.lower() in low for k in keys)


def run_stream(fin: TextIO, verbose: bool, echo_sle: bool) -> None:
    det = SwingDetector()
    last_imu = time.time()
    last_hb = 0.0
    print("=" * 60, flush=True)
    print("羽毛球拍柄 IMU 球速粗估 Demo", flush=True)
    print("说明: 由加速度/角速度峰值估算，仅供演示，非精确测速仪", flush=True)
    print("下方应持续出现 [SLE_IMU] 连接日志；连接成功后才有 @... IMU 行", flush=True)
    print("=" * 60, flush=True)

    for line in fin:
        line = line.strip()
        if not line:
            continue

        now = time.time()
        if now - last_hb >= 8.0 and now - last_imu >= 5.0:
            print(
                "[speed] 仍在等待 IMU 数据…（需 paibing 已连接且正在发 Notify）",
                flush=True,
            )
            last_hb = now

        s = parse_line(line)
        if s is None:
            if echo_sle and should_echo_line(line):
                print(f"[speed] {line}", flush=True)
            continue

        last_imu = now
        if verbose:
            print("[speed] " + format_status(s, det.baseline_m), flush=True)
        est = det.feed(s)
        if est is None:
            continue
        print("-" * 60, flush=True)
        print(f"【击球 #{det.hit_count}】 持续 {est.duration_ms} ms", flush=True)
        print(f"  峰值动态加速度: {est.peak_dyn_g:.2f} g  (M_max≈{det.peak_m:.2f}g)", flush=True)
        print(f"  峰值角速度:     {est.peak_gyro:.0f} °/s", flush=True)
        print(f"  估算拍柄线速度: {est.v_handle_mps:.2f} m/s  ({est.v_handle_mps * 3.6:.1f} km/h)", flush=True)
        print(
            f"  >>> 粗估球速:   {est.v_shuttle_kmh:.1f} km/h  "
            f"(×{TIP_GAIN} 拍头放大, 方法={est.method})",
            flush=True,
        )
        print("-" * 60, flush=True)


DEMO_LINES = """
[SLE_IMU] @42483,A+56,+20,+85,G-145,+16,R-443,P+123,M94
[SLE_IMU] @42716,A+120,+45,+200,G-280,+80,R-520,P+200,M118
[SLE_IMU] @42940,A+30,+10,+40,G-50,+10,R-400,P+110,M102
[SLE_IMU] @43180,A+10,+5,+15,G-5,+2,R-390,P+105,M100
[SLE_IMU] @65000,A+5,+2,+8,G-3,+1,R-10,P+5,M99
[SLE_IMU] @65220,A+180,+60,+320,G-420,+120,R-600,P+250,M145
[SLE_IMU] @65450,A+25,+8,+35,G-40,+8,R-50,P+20,M103
""".strip()


def main() -> int:
    ap = argparse.ArgumentParser(description="拍柄 IMU 羽毛球速粗估")
    ap.add_argument("--demo", action="store_true", help="用内置样例数据跑一遍")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印每一帧解析")
    ap.add_argument(
        "--no-echo-sle",
        action="store_true",
        help="不重复打印 SLE 日志（run_imu_speed_demo.sh 已在终端显示）",
    )
    args = ap.parse_args()

    if args.demo:
        import io
        run_stream(io.StringIO(DEMO_LINES + "\n"), verbose=args.verbose, echo_sle=True)
    else:
        run_stream(sys.stdin, verbose=args.verbose, echo_sle=not args.no_echo_sle)
    return 0


if __name__ == "__main__":
    sys.exit(main())
