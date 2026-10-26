"""The interactive, full-screen tool of tools/effects.py (curses; Python's standard library only). It starts with a menu: Configure effects (the effect selection below),
NAM setup (which NAM amp models are built in, see NamSelector) and Options (a placeholder for now). q or Esc leaves a menu.

  Up/Down (or k/j), PgUp/PgDn, Home/End   move in the list of effects
  Space                                   select / deselect the effect under the cursor
  s                                       save (writes effects.config and generates the build files; asks first if the selection does not fit)
  p                                       start from a preset
  a                                       allow / forbid the GPL-3.0 effects (ALLOW_GPL)
  c                                       effect chains: several effects combined into one menu entry
  x                                       clear everything: no effects, no chains, GPL forbidden (asks first; nothing is written until saved)
  i                                       about the effect under the cursor: origin, copyright lines, who committed it
  r                                       the memory budget in detail
  h                                       help
  q or Esc                                back to the start menu (asks about unsaved changes)

The state and what it computes (`Selector`) do not touch the terminal, so it is tested without one (tests/test_effects_tui.py).
"""
import copy
import curses
import os
import re
import subprocess

REGION_SHORT = {"dtcm": "DTCM", "ram_d2": "RAM_D2", "sram": "Code", "sdram": "SDRAM"}

HELP = [
    "Choose the effects that are built into the firmware.",
    "",
    "  Up / Down, k / j     move          PgUp / PgDn, Home / End   jump",
    "  Space                select or deselect the effect (a GPL effect asks first to allow the GPL effects)",
    "  s                    save: writes effects.config and the generated build files (asks first if it does not fit)",
    "  p                    start from a preset (configs/*.config, everything)",
    "  a                    allow / forbid the GPL-3.0 effects (ALLOW_GPL)",
    "  c                    effect chains: several effects combined into one menu entry",
    "                       ([c] in the list is an effect that is only built for a chain)",
    "  x                    clear everything (no effects, no chains, GPL forbidden), to start from scratch",
    "  i                    about the effect: origin, copyright lines, who committed it",
    "  r                    the memory budget in detail",
    "  h                    this help               q or Esc   back to the start menu",
    "",
    "The status bar is an estimate of what the selection needs:",
    "  DTCM and RAM_D2 are where the effects' variables and the heap are, Code is the",
    "  program (SRAM), SDRAM is the external memory. The percentages are of each memory's",
    "  size. 'fits', 'tight' or 'does NOT fit' is the verdict (RAM_D2 includes the heap,",
    "  which is only an estimate). Run `tools/effects.py measure` after a build to update",
    "  the figures. A '*' means unsaved changes.",
    "",
    "A selection that cannot be built (conflicting effects, GPL effects without",
    "ALLOW_GPL) is shown in red and cannot be saved.",
    "",
    "Press any key to close.",
]


class Selector:
    """The selection and what follows from it: no curses in here."""

    def __init__(self, fx):
        self.fx = fx
        self.m = fx.manifest()
        self.mem = fx.load_json(fx.MEMORY)
        self.enabled, self.options = fx.read_config()
        self.chains = copy.deepcopy(fx.read_chains())
        self.effects = self.m["effects"]
        self.cursor = next((i for i, e in enumerate(self.effects) if e["id"] in self.enabled), 0)  # starts on the first selected effect
        self.top = 0
        self.dirty = False
        self.message = ""

    # -- selection
    def current(self):
        return self.effects[self.cursor]

    def move(self, delta, page=1):
        self.cursor = max(0, min(len(self.effects) - 1, self.cursor + delta * page))

    def toggle(self, effect_id=None):
        i = effect_id or self.current()["id"]
        if i in self.enabled:
            self.enabled.discard(i)
        else:
            self.enabled.add(i)
        self.dirty = True
        self.message = ""

    def needs_gpl(self, effect_id):
        """Whether the effect (or a component it needs) is GPL-3.0-or-later."""
        e = self.m["by_id"][effect_id]
        return e.get("license") == "gpl" or any(self.m["components"][c].get("license") == "gpl" for c in e.get("requires", []))

    def toggle_gpl(self):
        self.options["ALLOW_GPL"] = not self.options.get("ALLOW_GPL")
        self.dirty = True

    def presets(self):
        names = []
        if os.path.isdir(self.fx.PRESETS):
            names = sorted(f[:-7] for f in os.listdir(self.fx.PRESETS) if f.endswith(".config"))
        return names + ["everything"]

    def load_preset(self, name):
        if name == "everything":
            self.enabled = {e["id"] for e in self.effects} - {"reverb_delay"}
            self.options["ALLOW_GPL"] = True
        else:
            self.enabled, options = self.fx.read_config(os.path.join(self.fx.PRESETS, name + ".config"))
            # (the chains stay.) A preset can allow the GPL effects but does not forbid them again: "ALLOW_GPL=n" in it means only that it has none,
            # and it used to switch off what was allowed with "a" a moment before.
            if options.get("ALLOW_GPL"):
                self.options["ALLOW_GPL"] = True
        self.dirty = True
        self.message = "preset %s loaded (not saved)" % name

    def clear(self):
        """Everything off: no effects, no chains, the GPL effects forbidden again (not saved until save())."""
        self.enabled = set()
        self.chains = []
        self.options["ALLOW_GPL"] = False
        self.cursor = 0
        self.top = 0
        self.dirty = True
        self.message = "cleared (not saved): select the effects, then s to save"

    # -- what the selection needs
    def built(self):
        """What is compiled: the standalone effects and every effect that is a slot of a chain."""
        return self.fx.effective_enabled(self.enabled, self.chains)

    def problems(self):
        if not self.built():
            return ["nothing is selected (the firmware needs at least one effect)"]
        return self.fx.selection_problems(self.built(), self.options, self.m) + self.fx.all_chain_problems(self.enabled, self.chains, self.m, self.mem)

    def ids(self):
        built = self.built()
        return [e["id"] for e in self.effects if e["id"] in built]

    def in_chain(self, effect_id):
        return effect_id in self.fx.chain_members(self.chains)

    def usage(self):
        """(used per region, heap, verdict, reasons)"""
        used, heap, notes = self.fx.budget(self.ids(), self.m, self.mem)
        verdict, why = self.fx.verdict(used, self.mem)
        return used, heap, verdict, why

    def percent(self, used, region):
        return 100 * used[region] // self.mem["regions"][region]

    def status(self):
        """The text of the status bar and its severity (0 fine, 1 tight, 2 does not fit or cannot be built)."""
        used, heap, verdict, why = self.usage()
        parts = ["%s %d%%" % (REGION_SHORT[r], self.percent(used, r)) for r in self.fx.REGIONS]
        sev = {"fits": 0, "tight": 1}.get(verdict, 2)
        text = "  ".join(parts) + "  | " + {"fits": "fits", "tight": "tight"}.get(verdict, "does NOT fit")
        if self.problems():
            text += "  | cannot be built"
            sev = 2
        if self.options.get("ALLOW_GPL"):
            text += "  | GPL allowed"
        count = "%d effects" % len(self.ids()) + (", %d chains" % len(self.chains) if self.chains else "")
        text = ("* " if self.dirty else "") + count + "  " + text
        return text, sev

    def effect_cost(self, e):
        f = self.mem["effects"].get(e["id"], {})
        return {"code": f.get("sram", 0), "dtcm": f.get("dtcm", 0), "ram_d2": f.get("ram_d2", 0), "sdram": f.get("sdram", 0), "heap": f.get("heap")}

    def detail(self, e):
        """The line under the list: what the effect under the cursor is and needs."""
        bits = []
        if e.get("note"):
            bits.append(e["note"])
        if e.get("license") == "gpl":
            bits.append("GPL-3.0-or-later")
        if e.get("origin"):
            bits.append("from " + e["origin"])
        else:
            authors = self.fx.git_authors(e, self.m, 2)
            if authors:
                bits.append("committed by " + ", ".join(a[0] for a in authors))
        if e.get("requires"):
            bits.append("needs " + ", ".join(e["requires"]))
        if e.get("conflicts"):
            bits.append("not with " + ", ".join(e["conflicts"]))
        return "; ".join(bits)

    def info_lines(self, width=76):
        """All that is known about the effect under the cursor, wrapped to the width of the popup."""
        import textwrap
        out = []
        for label, text in self.fx.effect_info(self.current(), self.m, self.mem):
            wrapped = textwrap.wrap(text, width - 15) or [""]
            for k, w in enumerate(wrapped):
                out.append("%-14s %s" % (label if k == 0 else "", w))
        out += ["", "Press any key to close."]
        return out

    def report_lines(self):
        used, heap, verdict, why = self.usage()
        lines = ["%-24s %10s %10s %6s" % ("region", "used", "size", "%")]
        for r in self.fx.REGIONS:
            size = self.mem["regions"][r]
            lines.append("%-24s %9d K %9d K %5d%%" % (self.fx.REGION_NAMES[r], used[r] // 1024, size // 1024, 100 * used[r] // size))
        static_d2 = used["ram_d2"] - heap
        lines += ["", "RAM_D2: %d KB static data, %d KB heap (estimate), %d KB to spare" % (static_d2 // 1024, heap // 1024, (self.mem["regions"]["ram_d2"] - used["ram_d2"]) // 1024),
                  "", "verdict: %s%s" % (verdict, (" (" + "; ".join(why) + ")") if why else "")]
        lines += ["problem: " + p for p in self.problems()]
        lines += ["", "Press any key to close."]
        return lines

    # -- saving
    def misfit(self):
        """Why the tool thinks the selection will not work on the pedal (the memory regions it overflows); empty if it fits or is only tight.
        It is a warning, not an error: the selection can still be saved and tried."""
        if self.problems():
            return []
        verdict, why = self.usage()[2:]
        return why if verdict == "does not fit" else []

    def save(self):
        problems = self.problems()
        if problems:
            self.message = "cannot save: " + "; ".join(problems) + ("  (a: allow the GPL effects)" if any("ALLOW_GPL" in x for x in problems) else "")
            return False
        self.fx.write_config(self.enabled, self.options, self.m)
        self.fx.write_chains(self.chains)
        self.fx.generate(quiet=True)
        self.dirty = False
        self.message = "saved: effects.config, chains.json and the generated build files"
        if self.misfit():
            self.message += " (it may not fit on the pedal)"
        return True


class NamSelector:
    """Which NAM amp models are built into the firmware: no curses in here.

    The models are the files in Effect-Modules/Nam/models/ (one header per model, made from a .nam file by nam_to_cpp_array.py), plus the .nam and
    .zip files (as delivered by TONE3000) in Effect-Modules/Nam/models_nam/ that have not been converted yet: selecting one of those converts it when
    saved, and the display name is the name in the .nam file's metadata. The choice is kept in models.config (name=y or name=n) and passed to
    update_models.sh, which writes models_list.inc."""

    MAX_NAME = 14  # characters: the display is small (the MODEL choices are drawn with an auto-fit font)

    def __init__(self, fx):
        import json
        import zipfile
        self.fx = fx
        self.dir = fx.NAM_DIR
        self.models = []  # dicts: stem, name, weights (or None), state ("installed", "new" or "unsupported"), note, nam (the .nam file's bytes, for "new")
        models_dir = os.path.join(self.dir, "models")
        sources = set()
        for f in sorted(os.listdir(models_dir)) if os.path.isdir(models_dir) else []:
            if not f.endswith(".h"):
                continue
            text = open(os.path.join(models_dir, f)).read()
            name = re.search(r'kName\s*=\s*"([^"]*)"', text)
            weights = re.search(r"\((\d+) weights", text)
            source = re.search(r"source: (.*)", text)
            if source:
                sources.add(source.group(1).strip())
            self.models.append({"stem": f[:-2], "name": name.group(1) if name else f[:-2], "weights": int(weights.group(1)) if weights else None,
                                "state": "installed", "note": "", "nam": None, "source": source.group(1).strip() if source else ""})
        self.expected_weights = None
        runtime = os.path.join(self.dir, "nam_a2_runtime.h")
        if os.path.exists(runtime):
            m = re.search(r"kA2WeightCount\s*=\s*(\d+)", open(runtime).read())
            self.expected_weights = int(m.group(1)) if m else None
        self.converter = None
        # the .nam and .zip files not converted yet
        src_dir = os.path.join(self.dir, "models_nam")
        found = []  # (shown file name, the .nam file's own name, its bytes)
        for f in sorted(os.listdir(src_dir)) if os.path.isdir(src_dir) else []:
            path = os.path.join(src_dir, f)
            if f.lower().endswith(".nam"):
                found.append((f, f, open(path, "rb").read()))
            elif f.lower().endswith(".zip"):
                try:
                    with zipfile.ZipFile(path) as z:
                        for member in sorted(z.namelist()):
                            if member.lower().endswith(".nam"):
                                found.append((f, os.path.basename(member), z.read(member)))
                except (zipfile.BadZipFile, OSError):
                    self.models.append({"stem": "", "name": f, "weights": None, "state": "unsupported", "note": "not a readable zip file", "nam": None, "source": ""})
        used = {m["stem"] for m in self.models}
        for shown, nam_name, data in found:
            if any(src == nam_name or src.startswith(nam_name + " (") for src in sources):  # (the comment can add what was picked, "... .nam (SlimmableContainer submodel 0...")
                continue  # converted already
            entry = {"stem": "", "name": os.path.splitext(nam_name)[0], "weights": None, "state": "unsupported", "note": "", "nam": data, "source": nam_name, "from": shown}
            try:
                d = json.loads(data)
                meta = d.get("metadata") or {}
                entry["name"] = self.clean(meta.get("name") or entry["name"])
                entry["gear_model"] = self.clean(meta.get("gear_model") or "")
                entry["meta_name"] = entry["name"]
                entry["weights"] = len(self.weights_of(d))
                if self.expected_weights and entry["weights"] != self.expected_weights:
                    entry["note"] = "needs %d weights (an A2 Nano model), this has %d" % (self.expected_weights, entry["weights"])
                else:
                    entry["state"] = "new"
            except (ValueError, KeyError, TypeError) as e:
                entry["note"] = "not a supported .nam file (%s)" % e
            base = re.sub(r"[^A-Za-z0-9]+", "_", entry["name"]).strip("_").lower()[:40] or "model"
            stem, n = base, 2
            while stem in used:
                stem = "%s_%d" % (base, n)
                n += 1
            used.add(stem)
            entry["stem"] = stem
            self.models.append(entry)
        saved = self.read_config()
        self.selected = {m["stem"] for m in self.models if m["state"] == "installed" and saved.get(m["stem"], True)}  # a model that is not in the file is included
        self.cursor = 0
        self.top = 0
        self.dirty = False
        self.message = "" if self.models else "no models: put .nam or .zip files in %s" % os.path.join(self.dir, "models_nam")

    @staticmethod
    def clean(text):
        """A name for the C string in the model file: no quotes or backslashes, one line."""
        return " ".join(str(text).replace('"', "'").replace("\\", "/").split())

    def suggested_name(self, m):
        """What the name dialog starts with: the gear model and the name from the .nam file together, to be edited down to MAX_NAME characters."""
        gear, name = m.get("gear_model", ""), m["name"]
        return name if not gear or gear == name or gear in name else (gear if name in gear else "%s - %s" % (gear, name))

    def name_problem(self, name):
        if not name.strip():
            return "the name cannot be empty"
        if len(self.clean(name)) > self.MAX_NAME:
            return "%d characters, at most %d fit the display" % (len(self.clean(name)), self.MAX_NAME)
        return ""

    def rename(self, m, name):
        """Sets the display name of a new model; returns the problem (empty if fine, then the name is set)."""
        problem = self.name_problem(name)
        if not problem:
            m["name"] = self.clean(name)
            m["named"] = True
        return problem

    @staticmethod
    def weights_of(d):
        """The weights of a .nam file: for a SlimmableContainer the smallest submodel (what nam_to_cpp_array.py picks)."""
        arch = d.get("architecture", "")
        if arch == "SlimmableContainer":
            sub = min(d["config"]["submodels"], key=lambda s: abs(s["max_value"] - 0.5))
            weights = sub["model"].get("weights")
        elif arch == "WaveNet":
            weights = d.get("weights")
        else:
            raise ValueError("architecture %r" % arch)
        if not isinstance(weights, list):
            raise ValueError("no weights")
        return weights

    def config_path(self):
        return os.path.join(self.dir, "models.config")

    def read_config(self):
        saved = {}
        if os.path.exists(self.config_path()):
            for line in open(self.config_path()):
                m = re.match(r"\s*([A-Za-z0-9_]+)=([yn])\s*$", line)
                if m:
                    saved[m.group(1)] = m.group(2) == "y"
        return saved

    def current(self):
        return self.models[self.cursor]

    def move(self, delta, page=1):
        self.cursor = max(0, min(len(self.models) - 1, self.cursor + delta * page))

    def toggle(self):
        if not self.models:
            return
        m = self.current()
        if m["state"] == "unsupported":
            self.message = "cannot be used: " + m["note"]
            return
        self.selected.symmetric_difference_update({m["stem"]})
        self.dirty = True
        self.message = ""

    def set_all(self, on):
        self.selected = {m["stem"] for m in self.models if m["state"] != "unsupported"} if on else set()
        self.dirty = True
        self.message = ""

    def kilobytes(self, weights):
        return None if weights is None else weights * 4 / 1024.0  # the weights are 4-byte floats in the program memory

    def total_kb(self):
        return sum(self.kilobytes(m["weights"]) or 0 for m in self.models if m["stem"] in self.selected)

    def new_selected(self):
        return [m for m in self.models if m["state"] == "new" and m["stem"] in self.selected]

    def status(self):
        """The text of the status bar and its severity (0 fine, 2 cannot be saved)."""
        text = "%s%d of %d models, %.0f KB of weights" % ("* " if self.dirty else "", len(self.selected), len(self.models), self.total_kb())
        if self.new_selected():
            text += ", %d to convert" % len(self.new_selected())
        if not self.selected:
            return text + "  | at least one is needed", 2
        return text, 0

    def convert(self, m):
        """Writes models/<stem>.h from the .nam file of a new model (the display name is the name in its metadata)."""
        import importlib.util
        import json
        if self.converter is None:
            spec = importlib.util.spec_from_file_location("nam_to_cpp_array", os.path.join(self.dir, "nam_to_cpp_array.py"))
            self.converter = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.converter)
        weights = self.weights_of(json.loads(m["nam"]))
        os.makedirs(os.path.join(self.dir, "models"), exist_ok=True)
        with open(os.path.join(self.dir, "models", m["stem"] + ".h"), "w") as f:
            f.write(self.converter.to_model_file(m["name"], weights, m["source"], m["stem"]))
        m["state"], m["nam"] = "installed", None

    def problem(self):
        """What stops the choice from being saved (empty if nothing)."""
        if not self.selected:
            return "the firmware needs at least one model"
        for m in self.new_selected():
            problem = self.name_problem(m["name"])
            if problem:
                return "the name of %s: %s (e: edit it)" % (m["stem"], problem)
        return ""

    def save(self, calibrate=False):
        """Converts the selected new models, writes models.config and runs update_models.sh with the selected models (it writes models_list.inc).
        With calibrate it also levels the output gains of the models (update_models.sh --calibrate: the default target, about a minute)."""
        problem = self.problem()
        if problem:
            self.message = "cannot save: " + problem
            return False
        converted = 0
        for m in self.new_selected():
            try:
                self.convert(m)
                converted += 1
            except Exception as e:  # a model that cannot be converted must not stop the others, but nothing is built from a half-done choice
                self.message = "could not convert %s: %s" % (m["name"], e)
                return False
        names = [m["stem"] for m in self.models if m["stem"] in self.selected]
        with open(self.config_path(), "w") as f:
            f.write("# Which NAM models are built in, edited with pedal_setup (NAM setup). Read by update_models.sh; a model that is not here is included.\n")
            for m in self.models:
                if m["state"] == "installed":
                    f.write("%s=%s\n" % (m["stem"], "y" if m["stem"] in self.selected else "n"))
        try:
            result = subprocess.run(["sh", os.path.join(self.dir, "update_models.sh"), "--calibrate" if calibrate else "--no-calibrate", "--models", ",".join(names)],
                                    capture_output=True, text=True, timeout=900 if calibrate else 120)
        except (OSError, subprocess.SubprocessError) as e:
            self.message = "update_models.sh could not be run: %s" % e
            return False
        if result.returncode != 0:
            self.message = "update_models.sh failed: " + (result.stderr.strip() or result.stdout.strip() or "exit %d" % result.returncode).splitlines()[-1]
            return False
        self.dirty = False
        extra = ""
        if calibrate:
            extra = ", output levels calibrated"
        elif converted:
            extra = ", %d converted (level 1.0: run update_models.sh --calibrate)" % converted
        self.message = "saved: models.config and models_list.inc (%d models%s); rebuild to use them" % (len(names), extra)
        return True

class ChainEditor:
    """One chain being edited. Slots and mappings refer to each other by index, so every change that moves or removes a slot fixes the references: no curses in here."""

    def __init__(self, sel, chain):
        self.sel = sel
        self.fx = sel.fx
        self.chain = chain
        self.cursor = 0
        self.message = ""

    # -- the rows of the editor: (kind, index, text)
    def slot_text(self, i):
        sl = self.chain["slots"][i]
        return "%d  [%s]  %s" % (i + 1, sl["tag"], sl["effect"])

    def mapping_text(self, i):
        mp = self.chain["mappings"][i]
        sl = self.chain["slots"][mp["slot"]] if 0 <= mp["slot"] < len(self.chain["slots"]) else {"tag": "??"}
        knob = "-" if mp.get("knob") is None else ("%d" % (mp["knob"] + 1) if mp["knob"] < self.fx.KNOB_COUNT else "%d (shifted knob %d)" % (mp["knob"] + 1, mp["knob"] + 1 - self.fx.KNOB_COUNT))
        cc = "-" if mp.get("cc") is None else str(mp["cc"])
        return "%s %-14s knob %-18s CC %s" % (sl["tag"], "On" if mp["param"] == self.fx.SLOT_ENABLE else mp["param"], knob, cc)

    def rows(self):
        c = self.chain
        rows = [("name", 0, "Name:             %s" % c["name"]),
                ("primary", 0, "Primary slot:     %s  (owns the LED and the footswitch)" % (self.slot_label(c.get("primary", 0)) if c["slots"] else "-")),
                ("toggle", 0, "Footswitch:       %s" % ("toggles " + ", ".join(self.slot_label(t) for t in c.get("toggle", [])) if c.get("toggle") else "forwarded to the primary slot (tap tempo ...)")),
                ("header", 0, "Effects, in the order the audio goes through them"),]
        rows += [("slot", i, "  " + self.slot_text(i)) for i in range(len(c["slots"]))]
        rows += [("add_slot", 0, "  + add an effect"), ("header", 0, "Knob and MIDI mappings (knobs 1-6; 7-12 are the same knobs after holding the footswitch for a second)")]
        rows += [("map", i, "  " + self.mapping_text(i)) for i in range(len(c["mappings"]))]
        rows += [("add_map", 0, "  + add a mapping")]
        return rows

    def slot_label(self, i):
        return "%d [%s]" % (i + 1, self.chain["slots"][i]["tag"]) if 0 <= i < len(self.chain["slots"]) else "?"

    def current(self):
        rows = self.rows()
        self.cursor = max(0, min(len(rows) - 1, self.cursor))
        return rows[self.cursor]

    def move_cursor(self, delta):
        rows = self.rows()
        pos = self.cursor
        while True:
            pos += delta
            if pos < 0 or pos >= len(rows):
                return
            if rows[pos][0] != "header":
                self.cursor = pos
                return

    def problems(self):
        return self.fx.chain_problems(self.chain, self.sel.enabled, self.sel.chains, self.sel.m, self.sel.mem)

    # -- changes
    def touch(self):
        self.sel.dirty = True

    def set_name(self, name):
        self.chain["name"] = name.strip()
        self.touch()

    def default_tag(self, effect_id):
        """A suggested tag of up to three characters: the first letters of the effect's name ("auto_filter" gives Afi, "booster" gives Boo)."""
        used = {sl["tag"] for sl in self.chain["slots"]}
        words = [w for w in effect_id.replace("-", "_").split("_") if w]
        if len(words) > 1 and words[0] == "funbox":
            words = words[1:]  # the FunBox ports: the name of the port (funbox-uranus gives Ura)
        candidates = [(words[0][0] + words[1][:2]).title() if len(words) > 1 else "", words[0][:3].title(), (effect_id[0] + effect_id[-2:]).title()]
        for c in candidates:
            if c and c not in used:
                return c
        for n in range(100, 1000):
            if str(n) not in used:
                return str(n)
        return "xxx"

    def chainable(self):
        return [e for e in self.sel.effects if self.fx.chain_blocked(e, self.sel.m) is None]

    def add_slot(self, effect_id, tag=None):
        self.chain["slots"].append({"tag": tag or self.default_tag(effect_id), "effect": effect_id})
        self.touch()

    def set_tag(self, i, tag):
        self.chain["slots"][i]["tag"] = tag
        self.touch()

    def set_effect(self, i, effect_id):
        """Another effect in a slot: the mappings to parameters it does not have are dropped."""
        self.chain["slots"][i]["effect"] = effect_id
        params = set(self.fx.effect_params(self.sel.m["by_id"][effect_id])) | {self.fx.SLOT_ENABLE}
        self.chain["mappings"] = [mp for mp in self.chain["mappings"] if mp["slot"] != i or mp["param"] in params]
        self.touch()

    def _renumber(self, mapping):
        """Applies mapping (old slot index -> new index or None) to everything that refers to slots."""
        c = self.chain
        c["mappings"] = [dict(mp, slot=mapping[mp["slot"]]) for mp in c["mappings"] if mapping.get(mp["slot"]) is not None]
        c["toggle"] = sorted(mapping[t] for t in c.get("toggle", []) if mapping.get(t) is not None)
        primary = mapping.get(c.get("primary", 0))
        c["primary"] = primary if primary is not None else 0

    def delete_slot(self, i):
        n = len(self.chain["slots"])
        del self.chain["slots"][i]
        self._renumber({k: (k if k < i else (None if k == i else k - 1)) for k in range(n)})
        self.touch()

    def move_slot(self, i, delta):
        j = i + delta
        n = len(self.chain["slots"])
        if not (0 <= j < n):
            return i
        sl = self.chain["slots"]
        sl[i], sl[j] = sl[j], sl[i]
        swap = {k: k for k in range(n)}
        swap[i], swap[j] = j, i
        self._renumber(swap)
        self.touch()
        return j

    def toggle_footswitch(self, i):
        t = set(self.chain.get("toggle", []))
        t.symmetric_difference_update({i})
        self.chain["toggle"] = sorted(t)
        self.touch()

    def params_of_slot(self, i):
        return [self.fx.SLOT_ENABLE] + self.fx.effect_params(self.sel.m["by_id"][self.chain["slots"][i]["effect"]])

    def next_free_knob(self):
        used = {mp.get("knob") for mp in self.chain["mappings"]}
        return next((k for k in range(self.fx.MAX_KNOB) if k not in used), None)

    def next_free_cc(self):
        used = {mp.get("cc") for mp in self.chain["mappings"]}
        return next((c for c in range(20, 128) if c not in used), None)

    def add_mapping(self, slot, param, knob, cc):
        self.chain["mappings"].append({"slot": slot, "param": param, "knob": knob, "cc": cc})
        self.touch()

    def edit_mapping(self, i, knob, cc):
        self.chain["mappings"][i]["knob"] = knob
        self.chain["mappings"][i]["cc"] = cc
        self.touch()

    def delete_mapping(self, i):
        del self.chain["mappings"][i]
        self.touch()


# ------------------------------------------------------------------ curses

def _init_colors():
    pairs = {}
    if curses.has_colors():
        curses.start_color()
        try:
            curses.use_default_colors()
            bg = -1
        except curses.error:
            bg = curses.COLOR_BLACK
        for n, color in ((1, curses.COLOR_GREEN), (2, curses.COLOR_YELLOW), (3, curses.COLOR_RED), (4, curses.COLOR_CYAN), (5, curses.COLOR_MAGENTA), (6, curses.COLOR_BLUE)):
            curses.init_pair(n, color, bg)
            pairs[n] = curses.color_pair(n)
        curses.init_pair(7, curses.COLOR_WHITE, curses.COLOR_BLUE)  # the field of a text prompt: light text on a dark background
        curses.init_pair(8, curses.COLOR_WHITE, 22 if curses.COLORS >= 256 else curses.COLOR_GREEN)  # the same while the text is acceptable (dark green)
    return pairs


def _put(win, y, x, text, attr=0):
    h, w = win.getmaxyx()
    if 0 <= y < h and x < w:
        try:
            win.addnstr(y, x, text, max(0, w - x - (1 if y == h - 1 else 0)), attr)
        except curses.error:
            pass


def _popup(stdscr, lines, title="", attr_title=0):
    h, w = stdscr.getmaxyx()
    width = min(w - 2, max(len(l) for l in lines + [title]) + 4)
    height = min(h - 2, len(lines) + 2)
    win = curses.newwin(height, width, max(0, (h - height) // 2), max(0, (w - width) // 2))
    win.keypad(True)
    win.box()
    if title:
        _put(win, 0, 2, " %s " % title, attr_title | curses.A_BOLD)
    for i, l in enumerate(lines[:height - 2]):
        _put(win, i + 1, 2, l)
    win.refresh()
    return win.getch()


def _choose(stdscr, title, items):
    """A small list to pick from; returns the item or None."""
    pos = 0
    while True:
        h, w = stdscr.getmaxyx()
        width = min(w - 2, max(len(i) for i in items + [title]) + 6)
        height = min(h - 2, len(items) + 2)
        win = curses.newwin(height, width, max(0, (h - height) // 2), max(0, (w - width) // 2))
        win.keypad(True)
        win.box()
        _put(win, 0, 2, " %s " % title, curses.A_BOLD)
        first = max(0, min(pos - (height - 3), len(items) - (height - 2)))
        for row, item in enumerate(items[first:first + height - 2]):
            _put(win, row + 1, 2, ("> " if first + row == pos else "  ") + item, curses.A_REVERSE if first + row == pos else 0)
        win.refresh()
        k = win.getch()
        if k in (curses.KEY_UP, ord("k")):
            pos = max(0, pos - 1)
        elif k in (curses.KEY_DOWN, ord("j")):
            pos = min(len(items) - 1, pos + 1)
        elif k in (10, 13, curses.KEY_ENTER, ord(" ")):
            return items[pos]
        elif k in (27, ord("q")):
            return None


def draw(stdscr, sel, colors):
    stdscr.erase()
    h, w = stdscr.getmaxyx()
    if h < 6 or w < 40:
        _put(stdscr, 0, 0, "The terminal is too small (needs 40x6).")
        stdscr.refresh()
        return
    _put(stdscr, 0, 0, " Effects in the firmware", curses.A_BOLD)
    cols = " %-20s %6s %6s %7s %7s %6s" % ("effect", "code", "DTCM", "RAM_D2", "SDRAM", "heap")
    _put(stdscr, 1, 0, "      " + cols[1:], curses.A_UNDERLINE)
    rows = h - 5
    if sel.cursor < sel.top:
        sel.top = sel.cursor
    if sel.cursor >= sel.top + rows:
        sel.top = sel.cursor - rows + 1
    for row in range(rows):
        i = sel.top + row
        if i >= len(sel.effects):
            break
        e = sel.effects[i]
        c = sel.effect_cost(e)
        on = e["id"] in sel.enabled
        heap = "%d K" % (c["heap"] // 1024) if c["heap"] is not None else "?"
        sdram = "%.1f M" % (c["sdram"] / 1048576) if c["sdram"] else "-"
        k = lambda v: ("%d K" % (v // 1024)) if v else "-"
        line = " %s %-20s %6s %6s %7s %7s %6s%s" % ("[x]" if on else ("[c]" if sel.in_chain(e["id"]) else "[ ]"), e["id"], k(c["code"]), k(c["dtcm"]), k(c["ram_d2"]), sdram, heap, "  GPL" if e.get("license") == "gpl" else "")
        attr = curses.A_REVERSE if i == sel.cursor else 0
        if on or sel.in_chain(e["id"]):
            attr |= curses.A_BOLD | colors.get(1, 0)
        _put(stdscr, row + 2, 0, line.ljust(w - 1), attr)
    if sel.top > 0:
        _put(stdscr, 1, w - 3, "^")
    if sel.top + rows < len(sel.effects):
        _put(stdscr, h - 4, w - 3, "v")
    info = sel.message or sel.detail(sel.current())
    _put(stdscr, h - 3, 1, info, colors.get(4, 0))
    draw_status(stdscr, sel, colors)
    stdscr.refresh()


def draw_status(stdscr, sel, colors, hint="h: help "):
    h, w = stdscr.getmaxyx()
    text, sev = sel.status()
    bar = (" " + text).ljust(w - 1 - len(hint)) + hint
    attr = curses.A_REVERSE | {0: colors.get(1, 0), 1: colors.get(2, 0), 2: colors.get(3, 0)}[sev]
    _put(stdscr, h - 1, 0, bar.ljust(w - 1), attr)


def _prompt(stdscr, title, initial="", hint="", info=(), valid=None):
    """A line of text to edit (with optional lines of information above it); returns the text, or None if cancelled with Esc.
    The field is a bar of its own colour with the terminal's cursor at the end of the text; with `valid` (text -> bool) it is dark green while the
    text is acceptable and blue otherwise, and Enter does nothing until it is acceptable."""
    text = initial
    try:
        previous = curses.curs_set(1)  # a real cursor at the edit point
    except curses.error:
        previous = None
    try:
        while True:
            h, w = stdscr.getmaxyx()
            width = min(w - 2, max(46, len(title) + 6, len(hint) + 6, *[len(l) + 6 for l in info]))
            height = 5 + len(info)
            win = curses.newwin(height, width, max(0, (h - height) // 2), max(0, (w - width) // 2))
            win.keypad(True)
            win.box()
            _put(win, 0, 2, " %s " % title, curses.A_BOLD)
            for n, line in enumerate(info):
                _put(win, 1 + n, 2, line)
            row = 2 + len(info)
            shown = text[-(width - 7):]  # the end of the text, so the cursor is always in view
            _put(win, row, 2, (" " + shown).ljust(width - 4), (curses.color_pair(8 if valid and valid(text) else 7) if curses.has_colors() else curses.A_REVERSE) | curses.A_BOLD)
            if previous is None:
                _put(win, row, 3 + len(shown), "_", curses.A_BOLD)  # no cursor on this terminal: draw one
            if hint:
                _put(win, 3 + len(info), 2, hint)
            win.move(row, min(3 + len(shown), width - 3))
            win.refresh()
            k = win.getch()
            if k in (10, 13, curses.KEY_ENTER):
                if valid is None or valid(text):
                    return text  # (with `valid`, Enter does nothing until the text is acceptable)
            elif k == 27:
                return None
            elif k in (curses.KEY_BACKSPACE, 127, 8):
                text = text[:-1]
            elif 32 <= k < 127:
                text += chr(k)
    finally:
        if previous is not None:
            curses.curs_set(previous)


def _number(stdscr, title, initial, low, high, blank_ok=True, hint=""):
    """Asks for a whole number (blank = none when blank_ok). Returns (ok, value): ok False if cancelled."""
    text = "" if initial is None else str(initial)
    while True:
        answer = _prompt(stdscr, title, text, hint or ("%d to %d%s" % (low, high, ", empty for none" if blank_ok else "")))
        if answer is None:
            return False, None
        if answer.strip() == "" and blank_ok:
            return True, None
        if answer.strip().lstrip("-").isdigit() and low <= int(answer) <= high:
            return True, int(answer)
        text = answer


def _checklist(stdscr, title, items, checked):
    """Items to switch on and off with Space; Enter when done. Returns the set of checked indexes, or None if cancelled."""
    checked = set(checked)
    pos = 0
    while True:
        h, w = stdscr.getmaxyx()
        width = min(w - 2, max(len(i) for i in items + [title]) + 10)
        height = min(h - 2, len(items) + 3)
        win = curses.newwin(height, width, max(0, (h - height) // 2), max(0, (w - width) // 2))
        win.keypad(True)
        win.box()
        _put(win, 0, 2, " %s " % title, curses.A_BOLD)
        for row, item in enumerate(items[:height - 3]):
            _put(win, row + 1, 2, ("[x] " if row in checked else "[ ] ") + item, curses.A_REVERSE if row == pos else 0)
        _put(win, height - 2, 2, "Space: switch   Enter: done   Esc: cancel")
        win.refresh()
        k = win.getch()
        if k in (curses.KEY_UP, ord("k")):
            pos = max(0, pos - 1)
        elif k in (curses.KEY_DOWN, ord("j")):
            pos = min(len(items) - 1, pos + 1)
        elif k == ord(" "):
            checked.symmetric_difference_update({pos})
        elif k in (10, 13, curses.KEY_ENTER):
            return checked
        elif k == 27:
            return None


CHAIN_HELP = [
    "A chain combines two or more effects into one menu entry: the audio goes through",
    "them in order, all their parameters are in one menu (tagged with the slot's three",
    "character tag, like \"Tre Depth\"), and the six knobs and MIDI CCs are wired as you say.",
    "",
    "  Up / Down     move                   Enter   edit the row under the cursor",
    "  a             add an effect or a mapping (by where the cursor is)",
    "  x             remove the effect or mapping under the cursor",
    "  < / >         move the effect up / down in the chain",
    "  Esc, q        back (the chain is kept; s on the main screen saves everything)",
    "",
    "Primary slot: the effect that owns the LED and the footswitch (tap tempo, ...).",
    "Footswitch toggles: instead of forwarding the footswitch, it switches these effects on",
    "and off. Mappings: knobs 1-6 are the six knobs; 7-12 are the same knobs after holding",
    "the footswitch for a second (the shift bank). An effect that keeps big buffers can only",
    "exist once: in a chain it cannot also be enabled on its own.",
    "",
    "On the pedal, a chain of two or more effects also has a Switches parameter in its menu:",
    "a choice like 3+1 puts two effects on the left and right footswitches, named left to right",
    "(3+1: effect 3 on the left footswitch, effect 1 on the right; there is a choice for every",
    "pair of effects in the chain, however many it has; each switches",
    "its effect on and off, and its LED shows it), overriding the footswitch settings above;",
    "Normal (the default) keeps them. See the user manual (Footswitches and LEDs).",
    "",
    "Press any key to close.",
]


def edit_chain(stdscr, sel, chain, colors):
    ed = ChainEditor(sel, chain)
    while True:
        stdscr.erase()
        h, w = stdscr.getmaxyx()
        _put(stdscr, 0, 0, " Chain: %s" % (chain["name"] or "(no name)"), curses.A_BOLD)
        rows = ed.rows()
        ed.current()
        height = h - 4
        top = max(0, min(ed.cursor - height // 2, len(rows) - height))
        for r in range(height):
            i = top + r
            if i >= len(rows):
                break
            kind, _, text = rows[i]
            attr = curses.A_BOLD | colors.get(4, 0) if kind == "header" else (curses.A_REVERSE if i == ed.cursor else 0)
            if kind in ("add_slot", "add_map") and i != ed.cursor:
                attr = colors.get(1, 0)
            _put(stdscr, r + 1, 0, text.ljust(w - 1), attr)
        problems = ed.problems()
        _put(stdscr, h - 3, 1, ("! " + problems[0]) if problems else (ed.message or "Enter: edit   a: add   x: remove   < >: move   h: help   Esc: back"), colors.get(3, 0) if problems else colors.get(4, 0))
        draw_status(stdscr, sel, colors, hint="Esc: back ")
        stdscr.refresh()
        key = stdscr.getch()
        kind, idx, _ = ed.current()
        ed.message = ""
        if key in (curses.KEY_UP, ord("k")):
            ed.move_cursor(-1)
        elif key in (curses.KEY_DOWN, ord("j")):
            ed.move_cursor(1)
        elif key in (27, ord("q")):
            return
        elif key in (ord("h"), ord("?")):
            _popup(stdscr, CHAIN_HELP, "Chains")
        elif key in (ord("a"),) or (key in (10, 13, curses.KEY_ENTER) and kind in ("add_slot", "add_map")):
            if kind in ("map", "add_map"):
                _add_mapping(stdscr, ed)
            else:
                _add_slot(stdscr, ed)
        elif key in (ord("x"), curses.KEY_DC):
            if kind == "slot":
                ed.delete_slot(idx)
            elif kind == "map":
                ed.delete_mapping(idx)
        elif key == ord("<") and kind == "slot":
            ed.move_slot(idx, -1)
            ed.cursor = max(0, ed.cursor - 1)
        elif key == ord(">") and kind == "slot":
            ed.move_slot(idx, 1)
            ed.cursor = min(len(ed.rows()) - 1, ed.cursor + (1 if idx + 1 < len(chain["slots"]) else 0))
        elif key in (10, 13, curses.KEY_ENTER):
            _edit_row(stdscr, ed, kind, idx)


def _add_slot(stdscr, ed):
    items = ["%-18s %s" % (e["id"], e.get("note", "")) for e in ed.chainable()]
    pick = _choose(stdscr, "Add an effect to the chain", items)
    if pick is None:
        return
    effect_id = pick.split()[0]
    tag = _prompt(stdscr, "Tag (3 characters, in front of its parameter names)", ed.default_tag(effect_id))
    if tag is not None:
        ed.add_slot(effect_id, tag.strip()[:3])


def _add_mapping(stdscr, ed):
    if not ed.chain["slots"]:
        ed.message = "add effects first"
        return
    slot = 0
    if len(ed.chain["slots"]) > 1:
        pick = _choose(stdscr, "Which effect", ["%s" % ed.slot_text(i) for i in range(len(ed.chain["slots"]))])
        if pick is None:
            return
        slot = int(pick.split()[0]) - 1
    names = ["On  (the effect's own on/off)" if p == ed.fx.SLOT_ENABLE else p for p in ed.params_of_slot(slot)]
    pick = _choose(stdscr, "Which parameter", names)
    if pick is None:
        return
    param = ed.fx.SLOT_ENABLE if pick.startswith("On ") else pick
    free = ed.next_free_knob()
    ok, knob = _number(stdscr, "Knob (1-6; 7-12 after holding the footswitch)", None if free is None else free + 1, 1, ed.fx.MAX_KNOB)
    if not ok:
        return
    free_cc = ed.next_free_cc()
    ok, cc = _number(stdscr, "MIDI CC", free_cc, 0, 127)
    if not ok:
        return
    ed.add_mapping(slot, param, None if knob is None else knob - 1, cc)


def _edit_row(stdscr, ed, kind, idx):
    c = ed.chain
    if kind == "name":
        name = _prompt(stdscr, "Name of the chain (it is drawn on the home screen: about 10 characters fit)", c["name"])
        if name is not None:
            ed.set_name(name)
    elif kind == "primary" and c["slots"]:
        pick = _choose(stdscr, "Primary slot (LED, footswitch)", [ed.slot_text(i) for i in range(len(c["slots"]))])
        if pick:
            c["primary"] = int(pick.split()[0]) - 1
            ed.touch()
    elif kind == "toggle" and c["slots"]:
        chosen = _checklist(stdscr, "The footswitch switches on and off", [ed.slot_text(i) for i in range(len(c["slots"]))], c.get("toggle", []))
        if chosen is not None:
            c["toggle"] = sorted(chosen)
            ed.touch()
    elif kind == "slot":
        pick = _choose(stdscr, "Edit", ["tag: " + c["slots"][idx]["tag"], "effect: " + c["slots"][idx]["effect"]])
        if pick and pick.startswith("tag"):
            tag = _prompt(stdscr, "Tag (3 characters)", c["slots"][idx]["tag"])
            if tag is not None:
                ed.set_tag(idx, tag.strip()[:3])
        elif pick:
            eff = _choose(stdscr, "Effect", ["%-18s %s" % (e["id"], e.get("note", "")) for e in ed.chainable()])
            if eff:
                ed.set_effect(idx, eff.split()[0])
    elif kind == "map":
        mp = c["mappings"][idx]
        ok, knob = _number(stdscr, "Knob (1-12)", None if mp.get("knob") is None else mp["knob"] + 1, 1, ed.fx.MAX_KNOB)
        if not ok:
            return
        ok, cc = _number(stdscr, "MIDI CC", mp.get("cc"), 0, 127)
        if ok:
            ed.edit_mapping(idx, None if knob is None else knob - 1, cc)


def chains_screen(stdscr, sel, colors):
    pos = 0
    while True:
        stdscr.erase()
        h, w = stdscr.getmaxyx()
        _put(stdscr, 0, 0, " Effect chains", curses.A_BOLD)
        if not sel.chains:
            _put(stdscr, 2, 2, "No chains yet. Press n to make one.")
        pos = max(0, min(pos, len(sel.chains) - 1))
        for i, c in enumerate(sel.chains[:h - 5]):
            members = " -> ".join("[%s] %s" % (sl["tag"], sl["effect"]) for sl in c["slots"])
            bad = sel.fx.chain_problems(c, sel.enabled, sel.chains, sel.m, sel.mem)
            line = " %-12s %s%s" % (c["name"], members, "   ! " + bad[0] if bad else "")
            _put(stdscr, i + 2, 0, line.ljust(w - 1), curses.A_REVERSE if i == pos else (colors.get(3, 0) if bad else 0))
        _put(stdscr, h - 3, 1, sel.message or "Enter: edit   n: new chain   d: delete   Esc: back", colors.get(4, 0))
        draw_status(stdscr, sel, colors, hint="Esc: back ")
        stdscr.refresh()
        key = stdscr.getch()
        sel.message = ""
        if key in (curses.KEY_UP, ord("k")):
            pos -= 1
        elif key in (curses.KEY_DOWN, ord("j")):
            pos += 1
        elif key in (27, ord("q")):
            return
        elif key == ord("n"):
            name = _prompt(stdscr, "Name of the new chain", "Chain%d" % (len(sel.chains) + 1))
            if name and name.strip():
                sel.chains.append({"name": name.strip(), "slots": [], "mappings": [], "primary": 0, "toggle": []})
                sel.dirty = True
                pos = len(sel.chains) - 1
                edit_chain(stdscr, sel, sel.chains[pos], colors)
        elif key in (10, 13, curses.KEY_ENTER) and sel.chains:
            edit_chain(stdscr, sel, sel.chains[pos], colors)
        elif key in (ord("d"), curses.KEY_DC) and sel.chains:
            answer = _popup(stdscr, ["Delete the chain %s?" % sel.chains[pos]["name"], "", "  y  delete     any other key  keep it"], "Delete")
            if answer in (ord("y"), ord("Y")):
                del sel.chains[pos]
                sel.dirty = True


def ask_save(stdscr):
    return _popup(stdscr, ["Save the changes?", "", "  y  save and quit     n  quit without saving     Esc  keep editing"], "Unsaved changes")


def save_selection(stdscr, sel):
    """Saves; when the tool thinks the selection will not work on the pedal it asks first, and the user can try it anyway."""
    why = sel.misfit()
    if why:
        answer = _popup(stdscr, ["The tool thinks this selection will not work on the pedal:", ""] + ["  " + w for w in why] +
                        ["", "It may not boot (the heap is unchecked) or may not link.", "",
                         "  y  save and try it anyway     any other key  do not save"], "May not work")
        if answer not in (ord("y"), ord("Y")):
            sel.message = "not saved"
            return False
    return sel.save()


def effects_screen(stdscr, fx, colors):
    """Configure effects: the list of effects. Returns to the caller (the start menu) on quit."""
    sel = Selector(fx)
    while True:
        draw(stdscr, sel, colors)
        key = stdscr.getch()
        h, _ = stdscr.getmaxyx()
        page = max(1, h - 6)
        if key in (curses.KEY_UP, ord("k")):
            sel.move(-1)
        elif key in (curses.KEY_DOWN, ord("j")):
            sel.move(1)
        elif key == curses.KEY_PPAGE:
            sel.move(-1, page)
        elif key == curses.KEY_NPAGE:
            sel.move(1, page)
        elif key == curses.KEY_HOME:
            sel.cursor = 0
        elif key == curses.KEY_END:
            sel.cursor = len(sel.effects) - 1
        elif key == ord(" "):
            cur = sel.current()["id"]
            if cur not in sel.enabled and sel.needs_gpl(cur) and not sel.options.get("ALLOW_GPL"):
                answer = _popup(stdscr, ["%s is GPL-3.0-or-later: a firmware binary that contains it must be" % cur,
                                         "distributed under GPLv3 (or not at all).", "",
                                         "  y  allow the GPL effects and select it     any other key  cancel"], "GPL effect")
                if answer in (ord("y"), ord("Y")):
                    sel.toggle_gpl()
                    sel.toggle()
            else:
                sel.toggle()
        elif key == ord("s"):
            save_selection(stdscr, sel)
        elif key == ord("a"):
            sel.toggle_gpl()
        elif key == ord("p"):
            name = _choose(stdscr, "Preset", sel.presets())
            if name:
                sel.load_preset(name)
        elif key == ord("c"):
            chains_screen(stdscr, sel, colors)
        elif key == ord("x"):
            what = "%d effects" % len(sel.enabled) + (" and %d chains" % len(sel.chains) if sel.chains else "")
            answer = _popup(stdscr, ["Deselect all %s, and forbid the GPL effects again?" % what, "",
                                     "Nothing is written until you save (s); quitting without saving undoes it.", "",
                                     "  y  clear everything     any other key  cancel"], "Clear everything")
            if answer in (ord("y"), ord("Y")):
                sel.clear()
        elif key == ord("i"):
            _popup(stdscr, sel.info_lines(min(80, stdscr.getmaxyx()[1] - 6)), sel.current()["id"])
        elif key == ord("r"):
            _popup(stdscr, sel.report_lines(), "Memory budget")
        elif key in (ord("h"), ord("?")):
            _popup(stdscr, HELP, "Help")
        elif key in (ord("q"), 27):
            if not sel.dirty:
                return
            answer = ask_save(stdscr)
            if answer in (ord("y"), ord("Y")):
                if save_selection(stdscr, sel):
                    return
            elif answer in (ord("n"), ord("N")):
                return
        elif key == curses.KEY_RESIZE:
            pass


NAM_HELP = [
    "Choose which NAM amp models are built into the firmware. The list has the models that are",
    "installed (the files in Effect-Modules/Nam/models/) and the .nam and .zip files in",
    "Effect-Modules/Nam/models_nam/ that have not been installed yet ('new'; put the files from",
    "TONE3000 there). Every model takes program memory (about 7 KB of weights each); the MODEL",
    "choices of the NAM effect are the selected models in file-name order.",
    "",
    "  Up / Down, k / j     move          PgUp / PgDn, Home / End   jump",
    "  Space                select or deselect the model; for a new model it asks for the display name first:",
    "                       the gear model and the name from the .nam file are listed, edit the suggestion down",
    "                       to at most 14 characters",
    "  e                    edit the display name of a new model again",
    "  a                    select all the models          x   deselect all (at least one is needed to save)",
    "  s                    save: converts the selected new models (with the names you gave them),",
    "                       writes models.config and runs update_models.sh, which rewrites models_list.inc.",
    "                       It asks first if the volume of the models should be calibrated too (a minute)",
    "  h                    this help               q or Esc   back to the start menu",
    "",
    "A new model starts at output level 1.0 unless you calibrate when saving (or run",
    "Effect-Modules/Nam/update_models.sh --calibrate). Changing the models changes the MODEL",
    "choices of the NAM effect: a saved NAM preset may point at another model afterwards.",
    "Only A2 Nano models can be used (they have the same number of weights); others are shown",
    "as 'not usable'. Rebuild to use the new list.",
    "",
    "A '*' means unsaved changes.   Press any key to close.",
]


def draw_nam(stdscr, sel, colors):
    """The NAM screen looks like the effect list in other colors: magenta (and a title bar) instead of green."""
    stdscr.erase()
    h, w = stdscr.getmaxyx()
    if h < 6 or w < 40:
        _put(stdscr, 0, 0, "The terminal is too small (needs 40x6).")
        stdscr.refresh()
        return
    _put(stdscr, 0, 0, " NAM models in the firmware".ljust(w - 1), curses.A_REVERSE | colors.get(5, 0) | curses.A_BOLD)
    _put(stdscr, 1, 0, "      " + ("%-20s %-26s %8s  %s" % ("model", "name", "weights", "source"))[0:], curses.A_UNDERLINE)
    rows = h - 5
    if sel.cursor < sel.top:
        sel.top = sel.cursor
    if sel.cursor >= sel.top + rows:
        sel.top = sel.cursor - rows + 1
    for row in range(rows):
        i = sel.top + row
        if i >= len(sel.models):
            break
        m = sel.models[i]
        on = m["stem"] in sel.selected
        kb = sel.kilobytes(m["weights"])
        tag = {"installed": "installed", "new": "new: " + m.get("from", ""), "unsupported": "not usable: " + m["note"]}[m["state"]]
        line = " %s %-20s %-26s %8s  %s" % ("[x]" if on else ("[-]" if m["state"] == "unsupported" else "[ ]"), m["stem"][:20], m["name"][:26], "%.1f K" % kb if kb is not None else "?", tag)
        attr = curses.A_REVERSE if i == sel.cursor else 0
        if on:
            attr |= curses.A_BOLD | colors.get(5, 0)
        elif m["state"] == "unsupported":
            attr |= curses.A_DIM
        _put(stdscr, row + 2, 0, line.ljust(w - 1), attr)
    if sel.top > 0:
        _put(stdscr, 1, w - 3, "^")
    if sel.top + rows < len(sel.models):
        _put(stdscr, h - 4, w - 3, "v")
    _put(stdscr, h - 3, 1, sel.message, colors.get(6, 0) | curses.A_BOLD)
    text, sev = sel.status()
    hint = "h: help "
    bar = (" " + text).ljust(w - 1 - len(hint)) + hint
    _put(stdscr, h - 1, 0, bar.ljust(w - 1), curses.A_REVERSE | (colors.get(5, 0) if sev == 0 else colors.get(3, 0)))
    stdscr.refresh()


def save_nam(stdscr, colors, sel):
    """Saves the NAM choice; first asks whether the output levels of the models should be calibrated too (it takes a while)."""
    problem = sel.problem()
    if problem:
        sel.message = "cannot save: " + problem
        return False
    answer = _popup(stdscr, ["Calibrate the output levels of the models too?", "",
                             "It makes all the models equally loud (the default level, +12 dB). It builds and runs a",
                             "test on this computer for every model and takes some time: about a minute.", "",
                             "  y  save and calibrate     n  save without calibrating     Esc  do not save"], "Volume calibration", colors.get(5, 0))
    if answer not in (ord("y"), ord("Y"), ord("n"), ord("N")):
        sel.message = "not saved"
        return False
    calibrate = answer in (ord("y"), ord("Y"))
    sel.message = "calibrating the levels of the models, this takes about a minute..." if calibrate else "running update_models.sh..."
    draw_nam(stdscr, sel, colors)
    return sel.save(calibrate)


def ask_name(stdscr, sel, m):
    """Asks for the name of a new model, starting with the gear model and the name in the .nam file together; it has to be edited down to
    NamSelector.MAX_NAME characters (Enter does nothing until it is: the field turns green then). Returns False if cancelled."""
    text = m["name"] if m.get("named") else sel.suggested_name(m)
    info = ["name in the file:  " + m.get("meta_name", m["name"]), "gear model:        " + (m.get("gear_model") or "-")]
    hint = "At most %d characters (the display is small); Enter when green, Esc cancels" % sel.MAX_NAME
    answer = _prompt(stdscr, "Name of the model in the NAM effect", text, hint, info, valid=lambda t: not sel.name_problem(t))
    if answer is None:
        return False
    sel.rename(m, answer)
    return True


def nam_screen(stdscr, fx, colors):
    """NAM setup: which NAM models are built in. Returns to the start menu on quit."""
    sel = NamSelector(fx)
    while True:
        draw_nam(stdscr, sel, colors)
        key = stdscr.getch()
        h, _ = stdscr.getmaxyx()
        page = max(1, h - 6)
        if key in (curses.KEY_UP, ord("k")):
            sel.move(-1)
        elif key in (curses.KEY_DOWN, ord("j")):
            sel.move(1)
        elif key == curses.KEY_PPAGE:
            sel.move(-1, page)
        elif key == curses.KEY_NPAGE:
            sel.move(1, page)
        elif key == curses.KEY_HOME:
            sel.cursor = 0
        elif key == curses.KEY_END:
            sel.cursor = max(0, len(sel.models) - 1)
        elif key == ord(" "):
            m = sel.current() if sel.models else None
            if m and m["state"] == "new" and m["stem"] not in sel.selected and not ask_name(stdscr, sel, m):
                continue  # the name dialog was cancelled: not selected
            sel.toggle()
        elif key == ord("e"):
            m = sel.current() if sel.models else None
            if m and m["state"] == "new":
                ask_name(stdscr, sel, m)
            elif m:
                sel.message = "only the name of a new model can be edited (an installed model has its name in its file)"
        elif key == ord("a"):
            sel.set_all(True)
        elif key == ord("x"):
            sel.set_all(False)
        elif key == ord("s"):
            save_nam(stdscr, colors, sel)
        elif key in (ord("h"), ord("?")):
            _popup(stdscr, NAM_HELP, "NAM setup help", colors.get(5, 0))
        elif key in (ord("q"), 27):
            if not sel.dirty:
                return
            answer = ask_save(stdscr)
            if answer in (ord("y"), ord("Y")):
                if save_nam(stdscr, colors, sel):
                    return
            elif answer in (ord("n"), ord("N")):
                return
        elif key == curses.KEY_RESIZE:
            pass


def menu(stdscr, title, items, hint=""):
    """A full-screen menu of (label, action) items. Enter runs the action of the item under the cursor; Esc or q leaves the menu."""
    pos = 0
    while True:
        stdscr.erase()
        h, w = stdscr.getmaxyx()
        _put(stdscr, 0, 1, title, curses.A_BOLD)
        for i, (label, _) in enumerate(items):
            _put(stdscr, 2 + i, 2, ("> " if i == pos else "  ") + label, curses.A_REVERSE if i == pos else 0)
        _put(stdscr, h - 1, 1, hint)
        stdscr.refresh()
        key = stdscr.getch()
        if key in (curses.KEY_UP, ord("k")):
            pos = max(0, pos - 1)
        elif key in (curses.KEY_DOWN, ord("j")):
            pos = min(len(items) - 1, pos + 1)
        elif key in (10, 13, curses.KEY_ENTER):
            if items[pos][1]() == "back":
                return
        elif key in (27, ord("q")):
            return


def options_screen(stdscr):
    """Placeholder for the options of the tool that will come."""
    menu(stdscr, "Options", [("Back", lambda: "back")], "Enter: back   Esc: back")


def loop(stdscr, fx):
    curses.curs_set(0)
    stdscr.keypad(True)
    colors = _init_colors()
    menu(stdscr, "Pedal setup", [("Configure effects", lambda: effects_screen(stdscr, fx, colors)),
                                 ("NAM setup", lambda: nam_screen(stdscr, fx, colors)),
                                 ("Options", lambda: options_screen(stdscr))], "Up/Down: move   Enter: select   q: quit")


def run(fx):
    os.environ.setdefault("ESCDELAY", "25")
    curses.wrapper(loop, fx)
