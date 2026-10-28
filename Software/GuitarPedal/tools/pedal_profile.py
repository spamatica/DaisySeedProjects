#!/usr/bin/env python3
"""Per-effect stack depth and CPU load, measured on the pedal through an SWD debugger (OpenOCD).

Loads build/guitarpedal.elf the way tools/pedal_measure.sh does (the Daisy bootloader starts the app that is in QSPI, which is replaced at its entry point), then selects
each effect in turn by writing the firmware's own variables (effectOn, pendingEffectIDFromAudioCallback), and reads:

  * the stack: (1) the depth of the audio callback alone (the effect's processing and everything under it): at the entry of the callback the stack below the current
    position is painted and the callback is run to its return address, so that the main loop cannot interleave; the lowest overwritten word is its depth; the deepest
    of several blocks is reported. (2) the whole stack's high-water mark (main loop with the menus + the callback), over the switch and over the steady state.
  * the CPU load: the average and the slowest block of the audio callback over a few seconds (libDaisy's CpuLoadMeter, 100% = a block takes as long as it lasts), and the
    heap break after the switch (the menus are rebuilt with new/delete).

The pedal gets no audio input: effects see silence, so anything that skips work without a signal (gates, detectors) can look cheaper than it is. The target is only ever halted
at the entry of the audio callback, so that no block is cut in the middle (the timer behind System::GetTick keeps running while halted, a cut block would read as an overrun).

Run from Software/GuitarPedal with arm-none-eabi-* on the PATH:   tools/pedal_profile.py [-o result.json] [--settle 1.5] [--window 4]
Settings in QSPI are changed by this (the active effect is saved); a different effect layout resets them anyway (see docs/HANDOVER_hardware_debugging.md).
"""
import argparse
import json
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time

ELF = "build/guitarpedal.elf"
PATTERN = 0xDEADBEEF


class Ocd:
    """OpenOCD's Tcl RPC port: a command, terminated by 0x1a; the reply ends with 0x1a too."""

    def __init__(self, port=6666):
        self.proc = subprocess.Popen(
            ["openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32h7x.cfg", "-c", "tcl_port %d" % port, "-c", "gdb_port disabled", "-c", "telnet_port disabled"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.sock = None
        for _ in range(50):
            time.sleep(0.2)
            if self.proc.poll() is not None:
                sys.exit("openocd exited: is the debugger connected and the udev rule installed?")
            try:
                self.sock = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                continue
        if self.sock is None:
            sys.exit("could not connect to openocd")

    def cmd(self, c):
        self.sock.sendall(c.encode() + b"\x1a")
        data = b""
        while not data.endswith(b"\x1a"):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("openocd closed the connection")
            data += chunk
        return data[:-1].decode(errors="replace").strip()

    def words(self, addr, count=1):
        out = self.cmd("read_memory 0x%x 32 %d" % (addr, count)).split()
        return [int(x, 0) for x in out]

    def word(self, addr):
        return self.words(addr)[0]

    def close(self):
        try:
            self.cmd("shutdown")
        except Exception:
            pass
        self.proc.terminate()


def nm(name_regex):
    out = subprocess.run(["arm-none-eabi-nm", "-C", ELF], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and re.fullmatch(name_regex, parts[2]):
            return int(parts[0], 16)
    sys.exit("symbol not found in %s: %s" % (ELF, name_regex))


def gdb_addr(expr):
    out = subprocess.run(["arm-none-eabi-gdb", "-q", "-batch", "-ex", "p/x (unsigned long)&" + expr, ELF], capture_output=True, text=True).stdout
    m = re.search(r"= (0x[0-9a-f]+)", out)
    if not m:
        sys.exit("cannot resolve %s" % expr)
    return int(m.group(1), 16)


def entry_names():
    """What each entry of the effect list is, in order (generated/effects_heap.inc has a comment per entry)."""
    names = []
    for line in open("generated/effects_heap.inc"):
        m = re.match(r"\d+, // (.*)", line.strip())
        if m:
            names.append(m.group(1))
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", help="write the result as JSON")
    ap.add_argument("--settle", type=float, default=1.5, help="seconds after selecting an effect before reading (the switch phase)")
    ap.add_argument("--window", type=float, default=4.0, help="seconds of steady-state processing that the CPU load is read over")
    ap.add_argument("--samples", type=int, default=12, help="blocks the audio callback's own stack depth is sampled over")
    ap.add_argument("--only", help="comma separated indexes of the effect list to measure (default all)")
    args = ap.parse_args()

    a = {
        "effectOn": gdb_addr("effectOn"),
        "pending": nm(r"pendingEffectIDFromAudioCallback"),
        "activeID": nm(r"activeEffectID"),
        "count": nm(r"availableEffectsCount"),
        "avg": gdb_addr("cpuLoadMeter.avg_"),
        "max": gdb_addr("cpuLoadMeter.max_"),
        "s_break": nm(r"\(anonymous namespace\)::s_break"),
        "end": nm(r"end"),
        "estack": nm(r"_estack"),
        "callback": nm(r"AudioCallback\(.*\)"),
    }
    # the end of what is static in DTCM: the highest segment end in 0x2000xxxx
    dtcm_free = 0
    for line in subprocess.run(["arm-none-eabi-readelf", "-lW", ELF], capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if f and f[0] == "LOAD" and f[2].startswith("0x2000"):
            dtcm_free = max(dtcm_free, int(f[2], 16) + int(f[5], 16))
    dtcm_free = (dtcm_free + 3) & ~3
    names = entry_names()

    ocd = Ocd()
    tmp = tempfile.mkdtemp()
    try:
        # 1. where the app in QSPI starts: let the bootloader run, read the reset vector of what it loaded into AXI SRAM
        ocd.cmd("reset run")
        time.sleep(5)
        ocd.cmd("halt")
        old_entry = ocd.word(0x24000004) & ~1
        if not 0x24000000 <= old_entry < 0x24080000:
            sys.exit("no app in QSPI to hand over from (entry vector 0x%x): flash any build once with make program-dfu" % old_entry)
        # 2. reset, let the bootloader run to that entry, take over there
        ocd.cmd("reset halt")
        ocd.cmd("bp 0x%x 2 hw" % old_entry)
        ocd.cmd("resume")
        ocd.cmd("wait_halt 30000")
        ocd.cmd("rbp 0x%x" % old_entry)
        if "halted" not in ocd.cmd("[target current] curstate"):
            sys.exit("the bootloader did not reach the app's entry point 0x%x" % old_entry)
        ocd.cmd("load_image %s" % os.path.abspath(ELF))
        vec = ocd.words(0x24000000, 2)
        ocd.cmd("mww 0xE000ED08 0x24000000")  # VTOR
        ocd.cmd("mww 0xE000ED88 0x%x" % (0xF << 20))  # FPU on, as the bootloader leaves it
        ocd.cmd("reg msp 0x%x" % vec[0])
        ocd.cmd("reg pc 0x%x" % (vec[1] & ~1))

        def paint(below):
            top = (below - 64) & ~3
            ocd.cmd("mww 0x%x 0x%x %d" % (dtcm_free, PATTERN, (top - dtcm_free) // 4))
            return top

        def deepest(top):
            """The lowest address in the painted area that the stack has overwritten (None: it did not get that deep)."""
            path = os.path.join(tmp, "stack.bin")
            ocd.cmd("dump_image %s 0x%x %d" % (path, dtcm_free, top - dtcm_free))
            data = open(path, "rb").read()
            words = struct.unpack("<%dI" % (len(data) // 4), data)
            for i, w in enumerate(words):
                if w != PATTERN:
                    return dtcm_free + 4 * i
            return None

        def depth(top):
            d = deepest(top)
            return (a["estack"] - d) if d is not None else None  # None: less than estack - top

        def clean_halt(timeout_ms=30000):
            """Halt at the entry of the audio callback: no block is cut in the middle."""
            ocd.cmd("bp 0x%x 2 hw" % a["callback"])
            ocd.cmd("resume")
            ocd.cmd("wait_halt %d" % timeout_ms)
            halted = "halted" in ocd.cmd("[target current] curstate")
            ocd.cmd("rbp 0x%x" % a["callback"])
            if not halted:
                raise TimeoutError("the audio callback was not reached")

        def callback_depth(samples):
            """Deepest stack use of the audio callback over `samples` blocks (call at the entry of the callback). None: the painted window was not deep enough."""
            window_words = 2048  # 8 KB below the entry
            worst = 0
            for n in range(samples):
                sp = msp()
                lr = int(re.search(r"0x([0-9a-f]+)", ocd.cmd("reg lr")).group(1), 16) & ~1
                hi = (sp - 64) & ~3
                lo = hi - 4 * window_words
                ocd.cmd("mww 0x%x 0x%x %d" % (lo, PATTERN, window_words))
                ocd.cmd("bp 0x%x 2 hw" % lr)
                ocd.cmd("resume")
                ocd.cmd("wait_halt 2000")
                halted = "halted" in ocd.cmd("[target current] curstate")
                ocd.cmd("rbp 0x%x" % lr)
                if not halted:
                    raise TimeoutError("the audio callback did not return")
                path = os.path.join(tmp, "cb.bin")
                ocd.cmd("dump_image %s 0x%x %d" % (path, lo, 4 * window_words))
                words = struct.unpack("<%dI" % window_words, open(path, "rb").read())
                first = next((i for i, w in enumerate(words) if w != PATTERN), None)
                if first == 0:
                    return None
                worst = max(worst, (sp - (lo + 4 * first)) if first is not None else 0)  # from the entry's sp down to the lowest word written
                if n + 1 < samples:
                    clean_halt()  # the next block's entry
            return worst

        def msp():
            m = re.search(r"0x([0-9a-f]+)", ocd.cmd("reg msp"))
            return int(m.group(1), 16)

        top = paint(a["estack"])
        ocd.cmd("resume")
        # 3. wait for the firmware to start the audio (effects are created and initialised first: seconds)
        booted = False
        for _ in range(20):
            time.sleep(2)
            ocd.cmd("halt")
            try:
                clean_halt(3000)
                booted = True
                break
            except Exception:
                ocd.cmd("resume")
        if not booted:
            sys.exit("the firmware did not start the audio callback within 40 s")
        count = ocd.word(a["count"])
        boot_depth = depth(top)
        result = {"boot_stack_bytes": boot_depth, "effects": [], "heap_at_boot": ocd.word(a["s_break"]) - a["end"], "count": count}
        print("booted: %d effects/chains in the list, stack %s bytes (boot)" % (count, boot_depth), flush=True)

        def window(seconds):
            """CPU over a window: reset the max, run, halt cleanly, read."""
            ocd.cmd("mww 0x%x 0" % a["max"])
            ocd.cmd("resume")
            time.sleep(seconds)
            clean_halt()
            f = lambda w: struct.unpack("<f", struct.pack("<I", w))[0]
            return f(ocd.word(a["avg"])) * 100.0, f(ocd.word(a["max"])) * 100.0

        # 4. the baseline: the effect off (the callback without an effect: pots, UI events, level meters, the mixing)
        ocd.cmd("mwb 0x%x 0" % a["effectOn"])
        ocd.cmd("resume")
        time.sleep(args.settle)
        clean_halt()
        avg0, max0 = window(args.window)
        result["bypass_cpu_avg"], result["bypass_cpu_max"] = avg0, max0
        print("baseline (effect off): CPU avg %.1f%% peak %.1f%%" % (avg0, max0), flush=True)

        only = set(int(x) for x in args.only.split(",")) if args.only else None
        for k in range(count):
            if only is not None and k not in only:
                continue
            name = names[k] if k < len(names) else "entry %d" % k
            ocd.cmd("mwb 0x%x 1" % a["effectOn"])
            top = paint(msp())
            ocd.cmd("mww 0x%x %d" % (a["pending"], k))
            ocd.cmd("resume")
            time.sleep(args.settle)
            clean_halt()
            row = {"index": k, "name": name, "selected": ocd.word(a["activeID"]) == k, "heap_after_switch": ocd.word(a["s_break"]) - a["end"], "stack_switch_bytes": depth(top)}
            top = paint(msp())
            row["cpu_avg"], row["cpu_max"] = window(args.window)
            row["stack_run_bytes"] = depth(top)
            row["stack_callback_bytes"] = callback_depth(args.samples)
            result["effects"].append(row)
            print("%2d %-28s %s stack callback %6s  total switch %6s run %6s  CPU avg %5.1f%% peak %5.1f%%  heap %d" % (
                k, name, "ok " if row["selected"] else "NOT SELECTED", row["stack_callback_bytes"], row["stack_switch_bytes"], row["stack_run_bytes"], row["cpu_avg"], row["cpu_max"], row["heap_after_switch"]), flush=True)
        result["heap_at_end"] = ocd.word(a["s_break"]) - a["end"]
        ocd.cmd("mwb 0x%x 0" % a["effectOn"])
        ocd.cmd("resume")
        if args.output:
            json.dump(result, open(args.output, "w"), indent=1)
    finally:
        ocd.close()


if __name__ == "__main__":
    main()
