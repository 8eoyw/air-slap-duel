"""Laptop stand-in for the referee's BLE side: connect to one or both wearables,
send clock-sync beacons, print every event, and log to CSV. Useful in weeks 3-5
before the XiaoZhi referee exists, and for experiments 3 and 5.

    python tools/ble_monitor.py                      # both nodes, events only
    python tools/ble_monitor.py --only SLAP-ATK --log results/atk_events.csv
    python tools/ble_monitor.py --raw --only SLAP-ATK --seconds 60   # experiment 3: raw stream rate

Requires: pip install bleak
"""
import argparse
import asyncio
import csv
import struct
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

SERVICE = "5a1a0000-8c3b-4f7e-9d2a-000000000001"
EVENT = "5a1a0001-8c3b-4f7e-9d2a-000000000001"
SYNC = "5a1a0002-8c3b-4f7e-9d2a-000000000001"
RAW = "5a1a0004-8c3b-4f7e-9d2a-000000000001"
NAMES = ["SLAP-ATK", "SLAP-DEF"]
CLS = {0: "background", 1: "forehand", 2: "backhand", 3: "feint",
       10: "dodge_left", 11: "dodge_right", 12: "block"}

T0 = time.monotonic()


def now_ms() -> int:
    return int((time.monotonic() - T0) * 1000) & 0xFFFFFFFF


class Node:
    def __init__(self, name, writer, raw):
        self.name, self.writer, self.raw = name, writer, raw
        self.raw_count = 0
        self.raw_bytes = 0
        self.last_seq = None
        self.lost = 0

    def on_event(self, _, data: bytearray):
        rx = now_ms()
        t, cls, conf, peak = struct.unpack("<IBBH", data[:8])
        lat = rx - t
        print(f"{self.name:9s} {CLS.get(cls, cls):12s} conf={conf:3d} peak={peak:4d}dps "
              f"t={t} rx={rx} rx-t={lat}ms")
        if self.writer:
            self.writer.writerow([self.name, t, rx, CLS.get(cls, cls), conf, peak])

    def on_raw(self, _, data: bytearray):
        seq = struct.unpack("<H", data[:2])[0]
        if self.last_seq is not None:
            self.lost += (seq - self.last_seq - 1) & 0xFFFF
        self.last_seq = seq
        self.raw_count += 1
        self.raw_bytes += len(data)


async def run_node(name, args, writer, stop: asyncio.Event):
    while not stop.is_set():
        print(f"scanning for {name} …")
        dev = await BleakScanner.find_device_by_name(name, timeout=10)
        if not dev:
            continue
        node = Node(name, writer, args.raw)
        try:
            async with BleakClient(dev) as c:
                print(f"{name} connected ({dev.address})")
                if args.raw:
                    await c.start_notify(RAW, node.on_raw)
                else:
                    await c.start_notify(EVENT, node.on_event)
                t_start = time.monotonic()
                while c.is_connected and not stop.is_set():
                    await c.write_gatt_char(SYNC, struct.pack("<I", now_ms()), response=False)
                    await asyncio.sleep(1.0)
                    if args.raw:
                        dt = time.monotonic() - t_start
                        print(f"{name} raw: {node.raw_count / dt:6.1f} samples/s "
                              f"{node.raw_bytes / dt:7.1f} B/s  lost={node.lost}")
        except Exception as e:  # noqa: BLE001 — keep monitoring through BLE hiccups
            print(f"{name}: {e}")
        await asyncio.sleep(1)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", choices=NAMES)
    ap.add_argument("--log", type=Path, help="CSV of events: node,t_onset,rx,cls,conf,peak")
    ap.add_argument("--raw", action="store_true", help="subscribe to the raw stream (build node with -DSLAP_STREAM_RAW)")
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = until Ctrl+C)")
    args = ap.parse_args()

    writer, f = None, None
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        f = open(args.log, "w", newline="")
        writer = csv.writer(f)
        writer.writerow(["node", "t_onset", "rx", "cls", "conf", "peak"])

    stop = asyncio.Event()
    names = [args.only] if args.only else NAMES
    tasks = [asyncio.create_task(run_node(n, args, writer, stop)) for n in names]
    try:
        if args.seconds:
            await asyncio.sleep(args.seconds)
            stop.set()
        await asyncio.gather(*tasks)
    finally:
        if f:
            f.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
