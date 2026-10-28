#!/bin/bash
# Loads build/guitarpedal.elf into the Daisy's AXI SRAM through an SWD debugger (OpenOCD + gdb), lets it run, and reads the memory use:
# heap used (per effect, through a breakpoint on heapguard::SetStage) and the stack high-water mark (painted DTCM). Details: docs/HANDOVER_hardware_debugging.md.
#
#   tools/pedal_measure.sh [-t seconds] [-o outfile] [-s]    (-s: trace every _sbrk call with its call stack, to attribute the heap to the effects that allocate it; slow)      (arm-none-eabi-* must be on the PATH; the ELF must have been built with the heap guard)
#
# The pedal is reset, the Daisy bootloader runs and starts the app that is in QSPI; that is caught at its entry and replaced by the new image (so it needs one working
# app in QSPI: any build flashed once with make program-dfu).
# Nothing is written to flash. Output: key=value lines (and "STAGE name heap_used" lines, one per effect) on stdout, and into -o if given.
set -u
cd "$(dirname "$0")/.."

RUN_SECONDS=10
OUT=""
TRACE=0
while getopts "t:o:s" opt; do
    case $opt in
    t) RUN_SECONDS=$OPTARG ;;
    o) OUT=$OPTARG ;;
    s) TRACE=1 ;;
    *) exit 2 ;;
    esac
done

ELF=build/guitarpedal.elf
[ -f "$ELF" ] || { echo "no $ELF" >&2; exit 1; }
sym() { arm-none-eabi-nm -C "$ELF" | awk -v n="$1" '$3 == n { print "0x" $1 }'; }

S_BREAK=$(arm-none-eabi-nm -C "$ELF" | awk '/\(anonymous namespace\)::s_break/ { print "0x" $1 }')
HEAP_START=$(sym end)
HEAP_LIMIT=$(sym _ram_d2_limit)
ESTACK=$(sym _estack)
SBRK=$(arm-none-eabi-nm "$ELF" | awk '$3 == "_sbrk" { print "0x" $1 }')
SETSTAGE=$(arm-none-eabi-nm "$ELF" | awk '/_ZN9heapguard8SetStageEPKc/ { print "0x" $1 }')
[ -n "$S_BREAK" ] && [ -n "$SETSTAGE" ] || { echo "the ELF has no heap guard (s_break / SetStage missing)" >&2; exit 1; }
# first free DTCM address: the end of the highest segment in 0x2000xxxx; the stack's top is _estack
DTCM_FREE=0
while read -r _ _ virt _ _ memsz _; do
    if [[ $virt == 0x2000* ]] && ((virt + memsz > DTCM_FREE)); then DTCM_FREE=$((virt + memsz)); fi
done < <(arm-none-eabi-readelf -lW "$ELF" | grep "^  LOAD")
DTCM_FREE=$(printf "0x%x" $(((DTCM_FREE + 3) & ~3)))
PAINT_END=$(printf "0x%x" $((ESTACK - 512)))

TMP=$(mktemp -d)
# A bootloaded app relies on the Daisy bootloader's set-up (clocks, SDRAM, FPU, boot info in backup SRAM: libDaisy skips them when it finds a bootloader < v6). So let the
# bootloader run its normal course with the app that is in QSPI, and take over at that app's entry point: find the entry from its vector table in AXI SRAM.
ENTRY_WORD=$(openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "init; reset run; sleep 5000; halt; echo ENTRY=[read_memory 0x24000004 32 1]; resume; shutdown" 2>&1 | sed -n 's/.*ENTRY=\(0x[0-9a-fA-F]*\).*/\1/p')
if [ -z "$ENTRY_WORD" ] || (( ENTRY_WORD < 0x24000000 || ENTRY_WORD > 0x24080000 )); then
    echo "no app in QSPI to hand over from (entry vector: '${ENTRY_WORD}'): flash any build once with make program-dfu" >&2
    exit 1
fi
ENTRY=$(printf "0x%x" $((ENTRY_WORD & ~1)))
trap 'kill $OCD_PID 2>/dev/null; [ -n "${KEEP:-}" ] && cp "$TMP/gdb.out" "$TMP/openocd.log" "$KEEP"/; rm -rf "$TMP"' EXIT
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "gdb_port 3333" -c "tcl_port disabled" -c "telnet_port disabled" >"$TMP/openocd.log" 2>&1 &
OCD_PID=$!
sleep 2
kill -0 $OCD_PID 2>/dev/null || { cat "$TMP/openocd.log" >&2; exit 1; }

TRACE_BLOCK=""
if [ $TRACE = 1 ]; then
    TRACE_BLOCK="hbreak *$SBRK
commands
  silent
  printf \"SBRK %d\\n\", \$r0
  bt 14
  continue
end"
fi

cat >"$TMP/gdb.cmd" <<EOF
set pagination off
set confirm off
set width 0
target extended-remote :3333
monitor reset halt
hbreak *$ENTRY
continue
delete
printf "AT_ENTRY pc=0x%x\n", \$pc
load
set *(unsigned*)0xE000ED08 = 0x24000000
# the bootloader leaves the FPU enabled (CPACR CP10/CP11 full access) and the app relies on it
set *(unsigned*)0xE000ED88 = 0xF << 20
set \$sp = *(unsigned*)0x24000000
set \$pc = *(unsigned*)0x24000004
printf "LOADED pc=0x%x sp=0x%x\n", \$pc, \$sp
# paint the free DTCM (the stack has not been used yet)
monitor mww $DTCM_FREE 0xDEADBEEF $(( (PAINT_END - DTCM_FREE) / 4 ))
set print frame-arguments none
set print address off
${TRACE_BLOCK}
hbreak *$SETSTAGE
commands
  silent
  printf "STAGE %s %d\n", (char*)\$r0, *(unsigned*)$S_BREAK == 0 ? 0 : *(unsigned*)$S_BREAK - $HEAP_START
  continue
end
continue
# interrupted by SIGINT after the run time
printf "HALTED pc=0x%x\n", \$pc
printf "S_BREAK 0x%x\n", *(unsigned*)$S_BREAK
printf "CFSR 0x%x HFSR 0x%x\n", *(unsigned*)0xE000ED28, *(unsigned*)0xE000ED2C
printf "SP 0x%x\n", \$sp
bt 6
dump binary memory $TMP/stack.bin $DTCM_FREE $PAINT_END
kill
EOF

arm-none-eabi-gdb -q -batch -x "$TMP/gdb.cmd" "$ELF" >"$TMP/gdb.out" 2>&1 &
GDB_PID=$!
# wait for the image to be loaded and running, then let it run, then interrupt
for _ in $(seq 1 120); do grep -q "^LOADED" "$TMP/gdb.out" 2>/dev/null && break; sleep 1; done
sleep "$RUN_SECONDS"
# with the trace on, booting takes longer: wait until the output has been quiet for 6 s
last=-1
while [ "$(stat -c %s "$TMP/gdb.out")" != "$last" ]; do last=$(stat -c %s "$TMP/gdb.out"); sleep 6; done
kill -INT $GDB_PID 2>/dev/null
wait $GDB_PID 2>/dev/null

RESULT=$(
    grep -E "^(LOADED|STAGE|HALTED|S_BREAK|CFSR|SP) " "$TMP/gdb.out"
    python3 - "$TMP/stack.bin" "$DTCM_FREE" "$PAINT_END" "$S_BREAK" "$HEAP_START" "$HEAP_LIMIT" "$TMP/gdb.out" <<'PY'
import re, struct, sys
stack, free, end, _, hs, hl, out = sys.argv[1:8]
free, end, hs, hl = int(free, 0), int(end, 0), int(hs, 0), int(hl, 0)
try:
    data = open(stack, "rb").read()
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    # the stack grows down from the top: the deepest point is the lowest address in the painted area that was overwritten
    lowest = next((k for k, w in enumerate(words) if w != 0xDEADBEEF), None)
    deepest = (free + 4 * lowest) if lowest is not None else None
    print("STACK_FREE_START 0x%x" % free)
    print("STACK_PAINT_END 0x%x" % end)
    print("STACK_LOWEST_TOUCHED %s" % ("none" if deepest is None else "0x%x" % deepest))
    if deepest is not None:
        print("STACK_USED_BYTES %d" % (end + 512 - deepest))
except Exception as e:
    print("STACK_ERROR %s" % e)
text = open(out).read()
m = re.findall(r"^S_BREAK 0x([0-9a-f]+)", text, re.M)
stages = [(n, int(v)) for n, v in re.findall(r"^STAGE (.*) (\d+)$", text, re.M)]
if m and len(stages) > 1:
    last = int(m[-1], 16) - hs
    print("LOAD_EFFECTS_BYTES %d" % (stages[1][1] - stages[0][1]))  # constructing all the effect objects (and chains)
    for k in range(1, len(stages)):
        nxt = stages[k + 1][1] if k + 1 < len(stages) else last
        print("INIT %s %d" % (stages[k][0], nxt - stages[k][1]))  # the heap one effect's Init() took (the last also includes what the firmware did after)
if "SBRK " in text:
    owners = {}
    inc_lines = open("generated/effects_instances.inc").read().split("\n")  # `new XModule(),` on line N (the constructors are inlined into load_effects)
    for blk in re.split(r"^SBRK ", text, flags=re.M)[1:]:
        lines = blk.split("\n")
        inc = int(lines[0])
        frames = [l for l in lines[1:] if l.startswith("#")]
        who = None
        for f in frames:
            mm = re.search(r"in (?:bkshepherd::)?(\w+Module)(?:<[^>]*>)?::", f) or re.search(r"(?:bkshepherd::)?(\w+Module)(?:<[^>]*>)?::", f)
            if mm:
                who = mm.group(1) + (" (Init)" if re.search(r"::Init\b", f) else " (constructor)" if re.search(r"::(\w+)Module\s*\(", f) else "")
                break
        if who is None:
            for f in frames:
                mm = re.search(r"effects_instances\.inc:(\d+)", f)
                if mm and int(mm.group(1)) <= len(inc_lines):
                    c = re.search(r"new (\w+)", inc_lines[int(mm.group(1)) - 1])
                    who = (c.group(1) if c else "instances.inc:" + mm.group(1)) + " (constructor)"
                    break
        if who is None:
            for f in frames:
                if "_GLOBAL__sub_I" in f or "__static_initialization" in f:
                    mm = re.search(r"at (\S+:\d+)", f)  # the global variable's definition (file:line)
                    who = "static init: " + (mm.group(1) if mm else f.strip())
                    break
        if who is None:
            names = [re.sub(r"^#\d+\s+(0x[0-9a-f]+ )?(in )?", "", f).split(" (")[0] for f in frames]
            names = [n for n in names if not re.match(r"(_sbrk|sbrk_aligned|_malloc_r|malloc|operator new|_calloc_r|calloc|_realloc_r|realloc|__wrap)", n)]
            who = "other: " + (names[0] if names else "?")
        owners[who] = owners.get(who, 0) + inc
    for who, b in sorted(owners.items(), key=lambda kv: -kv[1]):
        print("ALLOC %s %d" % (who, b))
if m:
    used = int(m[-1], 16) - hs
    print("HEAP_USED_BYTES %d" % used)
    print("HEAP_FREE_BYTES %d" % (hl - int(m[-1], 16)))
PY
)
echo "$RESULT"
[ -n "$OUT" ] && echo "$RESULT" >"$OUT"
exit 0
