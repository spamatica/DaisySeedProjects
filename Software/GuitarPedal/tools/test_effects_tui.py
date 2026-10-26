#!/usr/bin/env python3
"""Tests of the interactive effect selection (tools/effects_tui.py): the state without a terminal, and the real curses screen driven through a pseudo terminal.
Run: python3 tools/test_effects_tui.py (nothing in the repository is changed: the configuration and the generated files go to a temporary directory)."""
import fcntl
import os
import pty
import re
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import termios
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import effects as fx  # noqa: E402
import effects_tui as tui  # noqa: E402


def make_root(path):
    """A copy of the tool's inputs in a temporary directory (the sources are linked): uranus alone selected, no chains."""
    for name in ("effects.json", "effects-memory.json"):
        shutil.copy(os.path.join(fx.ROOT, name), os.path.join(path, name))
    for name in ("Effect-Modules", "configs"):
        os.symlink(os.path.join(fx.ROOT, name), os.path.join(path, name))
    with open(os.path.join(path, "effects.config"), "w") as f:
        f.write("ALLOW_GPL=n\nfunbox-uranus=y\n")
    with open(os.path.join(path, "chains.json"), "w") as f:
        f.write('{"chains": []}\n')


def nam_file(name, weights=1871, arch="WaveNet", slimmable=False, gear=None):
    """The JSON of a .nam file with the given name in its metadata (a SlimmableContainer gets a second, larger submodel that must not be picked)."""
    import json
    w = [0.001 * (i % 7) for i in range(weights)]
    if slimmable:
        d = {"architecture": "SlimmableContainer", "metadata": {"name": name, "gear_model": gear},
             "config": {"submodels": [{"max_value": 0.5, "model": {"weights": w}}, {"max_value": 1.0, "model": {"weights": w + [0.5] * 100}}]}}
    else:
        d = {"architecture": arch, "metadata": {"name": name}, "weights": w}
    return json.dumps(d).encode()


def make_nam_dir(path, sources=False):
    """A NAM directory in a temporary place: the real update_models.sh and nam_to_cpp_array.py, three small model files (name, weights comment,
    kOutputGain) and, with sources, a models_nam directory with a .nam file, a .zip with a .nam file in it and two that cannot be used."""
    import zipfile
    os.makedirs(os.path.join(path, "models"))
    for f in ("update_models.sh", "nam_to_cpp_array.py"):
        shutil.copy(os.path.join(fx.NAM_DIR_DEFAULT, f), os.path.join(path, f))
    with open(os.path.join(path, "nam_a2_runtime.h"), "w") as f:
        f.write("static constexpr int kA2WeightCount = 1871;\n")
    for stem, name, weights in (("alpha", "Alpha", 1871), ("beta", "Beta", 2000), ("gamma", "Gamma", 1871)):
        with open(os.path.join(path, "models", stem + ".h"), "w") as f:
            f.write('#pragma once\n// NAM A2 model "%s", source: %s.nam\n// (%d weights, A2 Nano/Lite: C=3 channels, 23 layers).\nnamespace nam_model_%s {\n'
                    'inline constexpr const char *kName = "%s";\ninline constexpr float kOutputGain = 1.0f;\n}\n' % (name, stem, weights, stem, name))
    # the calibration measures with tests/host/calibrate_nam.sh (two directories up): a stand-in that reports 6 dB for every model in models_list.inc
    host = os.path.normpath(os.path.join(path, "..", "..", "tests", "host"))
    os.makedirs(host, exist_ok=True)
    with open(os.path.join(host, "calibrate_nam.sh"), "w") as f:
        f.write('cd "$(dirname "$0")/../.." || exit 1\ni=0\nfor name in $(grep -o "nam_model_[A-Za-z0-9_]*::kName" Effect-Modules/Nam/models_list.inc | sed "s/nam_model_//;s/::kName//"); do\n'
                '    echo "$i $name 6.0"\n    i=$((i + 1))\ndone\n')
    if sources:
        src = os.path.join(path, "models_nam")
        os.makedirs(src)
        open(os.path.join(src, "amp1.nam"), "wb").write(nam_file('Cool "Amp" 1'))
        open(os.path.join(src, "alpha.nam"), "wb").write(nam_file("Alpha again"))  # the source of the installed model alpha: converted already
        open(os.path.join(src, "bad.nam"), "wb").write(nam_file("Bad", arch="LSTM"))
        open(os.path.join(src, "small.nam"), "wb").write(nam_file("Small", weights=100))
        with zipfile.ZipFile(os.path.join(src, "pack.zip"), "w") as z:
            z.writestr("inner/Pack Model.nam", nam_file("Pack Model", slimmable=True, gear="Pack Gear"))


class NamSelectorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.nam = os.path.join(self.tmp.name, "Effect-Modules", "Nam")
        make_nam_dir(self.nam)
        self.saved = fx.NAM_DIR
        fx.NAM_DIR = self.nam
        self.sel = tui.NamSelector(fx)

    def tearDown(self):
        fx.NAM_DIR = self.saved
        self.tmp.cleanup()

    def listed(self):
        return [l.split("/")[1][:-4] for l in open(os.path.join(self.nam, "models_list.inc")) if l.startswith("#include")] if os.path.exists(os.path.join(self.nam, "models_list.inc")) else None

    def test_lists_the_model_files_with_name_and_weights_all_selected(self):
        self.assertEqual([(m["stem"], m["name"], m["weights"], m["state"]) for m in self.sel.models],
                         [("alpha", "Alpha", 1871, "installed"), ("beta", "Beta", 2000, "installed"), ("gamma", "Gamma", 1871, "installed")])
        self.assertEqual(self.sel.selected, {"alpha", "beta", "gamma"})
        self.assertFalse(self.sel.dirty)
        self.assertAlmostEqual(self.sel.kilobytes(1871), 7.3, 1)
        self.assertEqual(self.sel.status()[1], 0)

    def test_toggle_marks_dirty_and_none_selected_cannot_be_saved(self):
        self.sel.toggle()
        self.assertEqual(self.sel.selected, {"beta", "gamma"})
        self.assertTrue(self.sel.dirty)
        self.assertTrue(self.sel.status()[0].startswith("* "))
        self.sel.set_all(False)
        self.assertEqual(self.sel.status()[1], 2)
        self.assertFalse(self.sel.save())
        self.assertIn("at least one", self.sel.message)
        self.assertFalse(os.path.exists(os.path.join(self.nam, "models.config")))  # nothing written

    def test_save_writes_the_config_and_runs_update_models(self):
        self.sel.toggle()          # alpha off
        self.sel.move(1)
        self.sel.move(1)
        self.sel.toggle()          # gamma off
        self.assertTrue(self.sel.save(), self.sel.message)
        self.assertFalse(self.sel.dirty)
        self.assertEqual(self.listed(), ["beta"])
        cfg = open(os.path.join(self.nam, "models.config")).read()
        self.assertIn("alpha=n", cfg)
        self.assertIn("beta=y", cfg)
        self.assertIn("gamma=n", cfg)
        self.assertEqual(tui.NamSelector(fx).selected, {"beta"})  # read back
        # the script on its own (what make runs) keeps the choice
        subprocess.run(["sh", os.path.join(self.nam, "update_models.sh"), "--no-calibrate"], check=True)
        self.assertEqual(self.listed(), ["beta"])

    def test_new_nam_and_zip_files_are_listed_and_converted_when_saved(self):
        shutil.rmtree(self.nam)  # start over with the sources
        make_nam_dir(self.nam, sources=True)
        sel = tui.NamSelector(fx)
        by = {m["stem"]: m for m in sel.models}
        self.assertEqual(sorted(by), ["alpha", "bad", "beta", "cool_amp_1", "gamma", "pack_model", "small"])
        self.assertEqual(by["cool_amp_1"]["state"], "new")
        self.assertEqual(by["cool_amp_1"]["name"], "Cool 'Amp' 1")      # the name in the metadata (the quotes would break the C string)
        self.assertEqual(by["pack_model"]["state"], "new")               # from the zip; the smaller submodel's weights
        self.assertEqual(by["pack_model"]["gear_model"], "Pack Gear")
        self.assertEqual(sel.suggested_name(by["pack_model"]), "Pack Gear - Pack Model")  # both, to be edited down
        self.assertEqual(sel.suggested_name(by["cool_amp_1"]), "Cool 'Amp' 1")             # no gear model: the name
        self.assertIn("at most 14", sel.rename(by["pack_model"], "Pack Gear - Pack Model"))  # 22 characters: too long
        self.assertIn("empty", sel.rename(by["pack_model"], "  "))
        self.assertEqual(sel.rename(by["pack_model"], 'Pack "Gear"'), "")
        self.assertEqual(by["pack_model"]["name"], "Pack 'Gear'")
        self.assertEqual(by["pack_model"]["weights"], 1871)
        self.assertEqual(by["pack_model"]["from"], "pack.zip")
        self.assertNotIn("alpha_again", by)                              # alpha.nam is the source of the installed alpha
        self.assertEqual([m["state"] for m in sel.models if m["name"] in ("Bad", "Small")], ["unsupported", "unsupported"])
        self.assertEqual(sel.selected, {"alpha", "beta", "gamma"})       # new models start unselected
        sel.cursor = [m["stem"] for m in sel.models].index("bad")
        sel.toggle()                                                    # an unsupported model cannot be selected
        self.assertIn("cannot be used", sel.message)
        self.assertNotIn("bad", sel.selected)
        sel.cursor = [m["stem"] for m in sel.models].index("cool_amp_1")
        sel.toggle()
        sel.cursor = [m["stem"] for m in sel.models].index("pack_model")
        sel.toggle()
        self.assertIn("2 to convert", sel.status()[0])
        self.assertTrue(sel.save(), sel.message)
        header = open(os.path.join(self.nam, "models", "cool_amp_1.h")).read()
        self.assertIn('kName = "Cool \'Amp\' 1"', header)
        self.assertIn("source: amp1.nam", header)
        self.assertIn("(1871 weights", open(os.path.join(self.nam, "models", "pack_model.h")).read())
        self.assertIn("kName = \"Pack 'Gear'\"", open(os.path.join(self.nam, "models", "pack_model.h")).read())
        listed = open(os.path.join(self.nam, "models_list.inc")).read()
        for stem in ("alpha", "beta", "gamma", "cool_amp_1", "pack_model"):
            self.assertIn("models/%s.h" % stem, listed)
        again = tui.NamSelector(fx)                                      # converted: installed now, and not listed twice
        self.assertEqual([m["state"] for m in again.models if m["stem"] in ("cool_amp_1", "pack_model")], ["installed", "installed"])
        self.assertEqual(len([m for m in again.models if m["name"].startswith("Cool")]), 1)
        self.assertEqual(again.selected, {"alpha", "beta", "gamma", "cool_amp_1", "pack_model"})

    def test_a_name_that_is_too_long_blocks_saving(self):
        shutil.rmtree(self.nam)
        make_nam_dir(self.nam, sources=True)
        sel = tui.NamSelector(fx)
        m = next(m for m in sel.models if m["stem"] == "pack_model")
        m["name"] = "A name that is much too long for the display"
        sel.cursor = sel.models.index(m)
        sel.toggle()
        self.assertFalse(sel.save())
        self.assertIn("at most 14", sel.message)
        self.assertFalse(os.path.exists(os.path.join(self.nam, "models", "pack_model.h")))

    def test_calibrating_when_saving_levels_the_models(self):
        self.sel.toggle()  # alpha off
        self.assertTrue(self.sel.save(calibrate=True), self.sel.message)
        self.assertIn("calibrated", self.sel.message)
        self.assertIn("kOutputGain = 1.9953f", open(os.path.join(self.nam, "models", "beta.h")).read())   # 6 dB measured, +12 dB wanted: x 10^(6/20)
        self.assertIn("kOutputGain = 1.0f", open(os.path.join(self.nam, "models", "alpha.h")).read())     # not selected: not touched
        self.assertTrue(self.sel.save(calibrate=False))
        self.assertNotIn("calibrated", self.sel.message)

    def test_a_new_model_is_included_until_it_is_deselected(self):
        self.sel.toggle()
        self.assertTrue(self.sel.save())
        shutil.copy(os.path.join(self.nam, "models", "beta.h"), os.path.join(self.nam, "models", "delta.h"))
        self.assertEqual(tui.NamSelector(fx).selected, {"beta", "gamma", "delta"})


class SelectorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        make_root(self.tmp.name)
        self.saved = (fx.CONFIG, fx.GEN, fx.CHAINS)
        fx.CONFIG = os.path.join(self.tmp.name, "effects.config")
        fx.GEN = os.path.join(self.tmp.name, "generated")
        fx.CHAINS = os.path.join(self.tmp.name, "chains.json")
        self.sel = tui.Selector(fx)

    def tearDown(self):
        fx.CONFIG, fx.GEN, fx.CHAINS = self.saved
        self.tmp.cleanup()

    def index(self, effect_id):
        return [e["id"] for e in self.sel.effects].index(effect_id)

    def test_starts_from_the_config(self):
        self.assertEqual(self.sel.enabled, {"funbox-uranus"})
        self.assertFalse(self.sel.dirty)
        text, sev = self.sel.status()
        self.assertIn("1 effects", text)
        self.assertEqual(sev, 0)

    def test_space_selects_and_deselects(self):
        self.sel.cursor = self.index("funbox-venus")
        self.sel.toggle()
        self.assertIn("funbox-venus", self.sel.enabled)
        self.assertTrue(self.sel.dirty)
        self.assertTrue(self.sel.status()[0].startswith("* "))
        self.sel.toggle()
        self.assertNotIn("funbox-venus", self.sel.enabled)

    def test_cursor_stays_in_the_list(self):
        self.assertEqual(self.sel.cursor, self.index("funbox-uranus"))  # starts on the first selected effect
        self.sel.move(-1, 10000)
        self.assertEqual(self.sel.cursor, 0)
        self.sel.move(1, 10000)
        self.assertEqual(self.sel.cursor, len(self.sel.effects) - 1)

    def test_the_status_follows_the_selection(self):
        before = self.sel.usage()[0]
        self.sel.toggle(next(e["id"] for e in self.sel.effects if self.sel.effect_cost(e)["sdram"] > 1 << 20))
        after = self.sel.usage()[0]
        self.assertGreater(after["sdram"], before["sdram"])

    def test_a_selection_that_does_not_fit_is_flagged(self):
        self.sel.load_preset("everything")
        text, sev = self.sel.status()
        self.assertEqual(sev, 2, text)
        self.assertTrue("does NOT fit" in text or "cannot be built" in text)

    def test_a_selection_that_does_not_fit_is_a_warning_not_an_error(self):
        self.assertEqual(self.sel.misfit(), [])
        self.sel.load_preset("everything")
        self.assertEqual(self.sel.problems(), [])
        self.assertTrue(self.sel.misfit())
        self.assertTrue(self.sel.save())  # the dialog of the screen asks first; the state itself saves
        self.assertIn("may not fit", self.sel.message)

    def test_gpl_needs_allow_gpl_and_cannot_be_saved_without(self):
        self.sel.toggle("funbox-earth")
        self.assertTrue(self.sel.problems())
        self.assertEqual(self.sel.status()[1], 2)
        self.assertFalse(self.sel.save())
        self.assertIn("cannot save", self.sel.message)
        self.assertEqual(open(fx.CONFIG).read(), "ALLOW_GPL=n\nfunbox-uranus=y\n")  # untouched
        self.sel.toggle_gpl()
        self.assertFalse([p for p in self.sel.problems() if "GPL" in p])

    def test_save_writes_the_config_and_the_build_files(self):
        self.sel.toggle("funbox-venus")
        self.assertTrue(self.sel.save())
        self.assertFalse(self.sel.dirty)
        enabled, _ = fx.read_config()
        self.assertEqual(enabled, {"funbox-uranus", "funbox-venus"})
        mk = open(os.path.join(fx.GEN, "effects.mk")).read()
        self.assertIn("venus_module.cpp", mk)
        self.assertIn("new VenusModule()", open(os.path.join(fx.GEN, "effects_instances.inc")).read())

    def test_the_defines_are_in_a_file_of_their_own_and_change_with_the_engines(self):
        # CLOUDSEED_ENGINES sizes the SDRAM pool; the Makefile rebuilds every object when generated/defines.mk changes (a stale pool froze the pedal with two engines)
        defines = lambda: open(os.path.join(fx.GEN, "defines.mk")).read()
        self.sel.toggle("funbox-jupiter")
        self.assertTrue(self.sel.save(), self.sel.message)
        self.assertIn("-DCLOUDSEED_ENGINES=1", defines())
        stamp = os.stat(os.path.join(fx.GEN, "defines.mk")).st_mtime_ns
        self.sel.toggle("modulated_tremolo")          # no new defines: the file is not touched, so nothing is rebuilt
        self.assertTrue(self.sel.save(), self.sel.message)
        self.assertEqual(os.stat(os.path.join(fx.GEN, "defines.mk")).st_mtime_ns, stamp)
        self.sel.toggle("funbox-neptune")
        self.assertTrue(self.sel.save(), self.sel.message)
        self.assertIn("-DCLOUDSEED_ENGINES=2", defines())
        self.assertNotIn("CLOUDSEED", open(os.path.join(fx.GEN, "effects.mk")).read().replace("cloudseed_pool", ""))  # not in effects.mk any more

    def test_a_preset_does_not_forbid_gpl_again(self):
        self.sel.toggle_gpl()
        self.assertTrue(self.sel.options.get("ALLOW_GPL"))
        self.sel.load_preset("uranus-only")  # its file says ALLOW_GPL=n
        self.assertTrue(self.sel.options.get("ALLOW_GPL"))

    def test_clear_everything(self):
        self.sel.toggle_gpl()
        self.sel.chains = [{"name": "Two", "slots": [{"tag": "Sc", "effect": "screamer"}, {"tag": "Ch", "effect": "chainsaw"}], "mappings": [], "primary": 0, "toggle": []}]
        self.sel.clear()
        self.assertEqual(self.sel.enabled, set())
        self.assertEqual(self.sel.chains, [])
        self.assertFalse(self.sel.options.get("ALLOW_GPL"))
        self.assertTrue(self.sel.dirty)
        self.assertEqual(self.sel.ids(), [])
        self.assertIn("nothing is selected", " ".join(self.sel.problems()))
        self.assertFalse(self.sel.save())  # an empty selection cannot be saved (the firmware needs an effect)
        self.assertEqual(open(fx.CONFIG).read(), "ALLOW_GPL=n\nfunbox-uranus=y\n")  # nothing written
        self.sel.toggle("screamer")
        self.assertEqual(self.sel.problems(), [])
        self.assertTrue(self.sel.save())

    def test_presets(self):
        self.assertIn("everything", self.sel.presets())
        self.assertIn("uranus-only", self.sel.presets())
        self.sel.load_preset("uranus-only")
        self.assertEqual(self.sel.enabled, {"funbox-uranus"})
        self.assertTrue(self.sel.dirty)

    def test_info_about_an_effect(self):
        self.sel.cursor = self.index("funbox-uranus")
        info = "\n".join(self.sel.info_lines())
        self.assertIn("FunBox", info)          # the origin from the manifest
        self.assertIn("committed by", info)    # the people from git
        self.assertIn("Copyright", info)       # a copyright line from the source
        self.assertIn("Uranus/README.md", info)
        self.assertIn("from FunBox", self.sel.detail(self.sel.current()))

    def test_info_finds_the_copyright_line_not_the_licence_text(self):
        e = next(e for e in self.sel.effects if e["id"] == "duck_compressor")
        info = "\n".join(t for _, t in fx.effect_info(e, self.sel.m))
        self.assertIn("ChunkWare", info)
        self.assertNotIn("above copyright notice", info)

    def test_the_texts_are_scanned_and_measured(self):
        source = """
            params[X::A] = { name : "Depth", valueType : 1 };
            params[X::B] = { name : "Delay Feedback", valueType : 1 };
            params[X::C] = { name : "A parameter name that is far too long to show", valueType : 1 };
            static const char *s_modes[3] = {"Normal", "Dotted 8th Note", "Triplet"};
        """
        names, choices = fx.scan_texts(source)
        self.assertEqual(names, ["Depth", "Delay Feedback", "A parameter name that is far too long to show"])
        self.assertEqual(choices, ["Normal", "Dotted 8th Note", "Triplet"])
        m = {"effects": [{"id": "x", "sources": ["Effect-Modules/__none__.cpp"]}]}
        self.assertEqual(fx.lint_texts(m), [])  # no such file: nothing found, no crash

    def test_no_parameter_name_or_choice_is_cut_on_the_display(self):
        # The display fits 22 characters in the smallest font (UI/fit_text.h); a longer text would be cut with a "~". Longer than 10 is only drawn smaller: see `tools/effects.py lint`.
        cut = [f for f in fx.lint_texts(self.sel.m, self.sel.chains) if f["result"] == "cut"]
        self.assertEqual(cut, [], "texts the display cannot show in full: %s" % cut)

    def test_a_chain_parameter_is_measured_with_its_tag(self):
        found = fx.lint_texts(self.sel.m, [{"name": "C", "slots": [{"tag": "Ur", "effect": "funbox-uranus"}], "mappings": []}])
        self.assertIn("Ur Env Sustain", [f["text"] for f in found if f["effect"] == "chain C"])  # 14 characters: drawn smaller

    def test_info_without_origin_falls_back_to_the_committers(self):
        e = next(e for e in self.sel.effects if e["id"] == "delay")
        self.assertIn("committed by", self.sel.detail(e))

    def test_effects_that_use_the_impulse_response_get_its_sources(self):
        # nam_a2_module.cpp includes ImpulseResponse/...: without the component the link failed (the manifest once missed it)
        for e in self.sel.effects:
            text = "".join(open(os.path.join(fx.ROOT, s), errors="replace").read() for s in e["sources"] if os.path.exists(os.path.join(fx.ROOT, s)))
            if '"ImpulseResponse/' in text or '"ImpulseResponse.h"' in text:
                self.assertIn("impulse_response", e.get("requires", []), e["id"])

    def test_report_and_detail(self):
        self.assertTrue(any("verdict" in l for l in self.sel.report_lines()))
        gpl = next(e for e in self.sel.effects if e.get("license") == "gpl")
        self.assertIn("GPL", self.sel.detail(gpl))


class ChainTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        make_root(self.tmp.name)
        self.saved = (fx.CONFIG, fx.GEN, fx.CHAINS)
        fx.CONFIG = os.path.join(self.tmp.name, "effects.config")
        fx.GEN = os.path.join(self.tmp.name, "generated")
        fx.CHAINS = os.path.join(self.tmp.name, "chains.json")
        self.sel = tui.Selector(fx)
        self.chain = {"name": "TremVerb", "slots": [], "mappings": [], "primary": 0, "toggle": []}
        self.sel.chains.append(self.chain)
        self.ed = tui.ChainEditor(self.sel, self.chain)

    def tearDown(self):
        fx.CONFIG, fx.GEN, fx.CHAINS = self.saved
        self.tmp.cleanup()

    def two(self):
        self.ed.add_slot("modulated_tremolo", "Tr")
        self.ed.add_slot("reverb", "Rv")
        self.ed.add_mapping(0, "DEPTH", 0, 20)
        self.ed.add_mapping(1, "TIME", 3, 23)
        self.ed.add_mapping(1, fx.SLOT_ENABLE, None, 30)

    def test_parameters_come_from_the_header_enum(self):
        self.assertEqual(fx.effect_params(self.sel.m["by_id"]["reverb"]), ["TIME", "DAMP", "MIX"])
        self.assertIn("DEPTH", fx.effect_params(self.sel.m["by_id"]["modulated_tremolo"]))

    def test_a_chain_needs_two_effects(self):
        self.ed.add_slot("reverb", "Rv")
        self.assertTrue(any("at least two" in p for p in self.ed.problems()))
        self.ed.add_slot("modulated_tremolo", "Tr")
        self.assertEqual(self.ed.problems(), [])

    def test_default_tags_are_short_and_distinct(self):
        self.ed.add_slot("modulated_tremolo")
        self.ed.add_slot("multi_delay")
        tags = [s["tag"] for s in self.chain["slots"]]
        self.assertTrue(all(0 < len(t) <= 3 for t in tags))
        self.assertEqual(len(set(tags)), 2)

    def test_deleting_and_moving_a_slot_fixes_the_references(self):
        self.two()
        self.chain["primary"] = 1
        self.chain["toggle"] = [0]
        self.ed.move_slot(0, 1)                      # the reverb is first now
        self.assertEqual([s["effect"] for s in self.chain["slots"]], ["reverb", "modulated_tremolo"])
        self.assertEqual(self.chain["primary"], 0)  # still the reverb
        self.assertEqual(self.chain["toggle"], [1])  # still the tremolo
        self.assertEqual({(m["slot"], m["param"]) for m in self.chain["mappings"]}, {(1, "DEPTH"), (0, "TIME"), (0, fx.SLOT_ENABLE)})
        self.ed.delete_slot(0)                       # the reverb goes: its mappings go with it
        self.assertEqual([m["param"] for m in self.chain["mappings"]], ["DEPTH"])
        self.assertEqual(self.chain["mappings"][0]["slot"], 0)
        self.assertEqual(self.chain["primary"], 0)
        self.assertEqual(self.chain["toggle"], [0])

    def test_changing_the_effect_of_a_slot_drops_mappings_it_cannot_have(self):
        self.two()
        self.ed.set_effect(1, "modulated_tremolo")   # no TIME there
        self.assertNotIn("TIME", [m["param"] for m in self.chain["mappings"]])
        self.assertIn(fx.SLOT_ENABLE, [m["param"] for m in self.chain["mappings"]])

    def test_mapping_mistakes_are_found(self):
        self.two()
        self.ed.add_mapping(0, "FREQ", 0, 40)         # knob 1 twice
        self.ed.add_mapping(0, "WAVE", 5, 20)         # CC 20 twice
        self.ed.add_mapping(0, "NOPE", 4, 41)
        text = "; ".join(self.ed.problems())
        self.assertIn("knob 1 is already used", text)
        self.assertIn("MIDI CC 20 is already used", text)
        self.assertIn("no parameter NOPE", text)

    def test_cloudseed_effects_cannot_be_slots_and_big_ones_only_once(self):
        self.assertTrue(fx.chain_blocked(self.sel.m["by_id"]["cloudseed"], self.sel.m))
        self.assertTrue(fx.chain_blocked(self.sel.m["by_id"]["funbox-jupiter"], self.sel.m))
        self.two()
        self.sel.enabled.add("reverb")                # the reverb on its own as well as in the chain
        self.assertTrue(any("also enabled on its own" in p for p in self.ed.problems()))

    def test_the_chain_builds_its_effects_and_the_status_counts_them(self):
        self.two()
        self.assertEqual({"funbox-uranus", "modulated_tremolo", "reverb"}, self.sel.built())
        self.assertIn("1 chains", self.sel.status()[0])
        self.assertTrue(self.sel.in_chain("reverb"))

    def test_the_generated_code(self):
        self.two()
        self.chain["toggle"] = [0]
        self.chain["primary"] = 1
        code = fx.chain_code(self.chain, self.sel.m)
        self.assertIn('"TremVerb"', code)
        self.assertIn('{{"Tr", new ModulatedTremoloModule()}, {"Rv", new ReverbModule()}}', code)
        self.assertIn("{0, ModulatedTremoloModule::DEPTH, 0, 20}", code)
        self.assertIn("{1, ReverbModule::TIME, 3, 23}", code)
        self.assertIn("{1, EffectChain::SLOT_ENABLE, -1, 30}", code)
        self.assertIn("/* primarySlot */ 1,", code)
        self.assertIn("/* footswitchTogglesSlots */ {0}", code)

    def test_save_writes_chains_json_and_the_build_files(self):
        self.two()
        self.assertTrue(self.sel.save(), self.sel.message)
        self.assertEqual(fx.read_chains()[0]["name"], "TremVerb")
        gen = lambda n: open(os.path.join(fx.GEN, n)).read()
        self.assertIn("new EffectChain(", gen("effects_chains.inc"))
        self.assertIn("modulated_tremolo_module.cpp", gen("effects.mk"))      # compiled for the chain
        self.assertIn("modulated_tremolo_module.h", gen("effects_includes.inc"))
        self.assertNotIn("new ModulatedTremoloModule()", gen("effects_instances.inc"))  # but not an entry of its own

    def test_the_heap_estimates_are_generated_for_every_entry(self):
        self.two()
        self.assertTrue(self.sel.save(), self.sel.message)
        lines = [l for l in open(os.path.join(fx.GEN, "effects_heap.inc")).read().splitlines() if not l.startswith("//")]
        self.assertEqual(len(lines), 2)  # uranus, then the chain
        self.assertGreater(int(lines[0].split(",")[0]), 0)   # uranus: from effects-memory.json
        self.assertIn("chain TremVerb", lines[1])
        reverb = self.sel.mem["effects"]["reverb"].get("heap") or 0
        tremolo = self.sel.mem["effects"]["modulated_tremolo"].get("heap") or 0
        self.assertEqual(int(lines[1].split(",")[0]), reverb + tremolo)  # a chain: the sum of its effects

    def test_observations_are_recorded_and_shown_in_the_matrix(self):
        import argparse
        import contextlib
        import io
        saved = fx.MEMORY
        fx.MEMORY = os.path.join(self.tmp.name, "effects-memory.json")
        shutil.copy(os.path.join(fx.ROOT, "effects-memory.json"), fx.MEMORY)
        try:
            fx.cmd_observe(argparse.Namespace(result="boots and runs", heap_used=100, heap_free=150, note="test", set_base_heap=False))
            fx.cmd_observe(argparse.Namespace(result="does not boot", heap_used=None, heap_free=None, note=None, set_base_heap=False))  # the same selection again replaces it
            mem = fx.load_json(fx.MEMORY)
            mine = [o for o in mem["observed"] if o["effects"] == ["funbox-uranus"]]
            self.assertEqual(len(mine), 1)
            self.assertEqual(mine[0]["result"], "does not boot")
            fx.cmd_observe(argparse.Namespace(result="boots and runs", heap_used=100, heap_free=150, note=None, set_base_heap=False))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                fx.cmd_matrix(argparse.Namespace(ids=["funbox-uranus"]))
            self.assertIn("heap used 100 K, free 150 K", out.getvalue())
            # --set-base-heap: the framework's own heap is what was used minus what the effects are estimated to need
            fx.cmd_observe(argparse.Namespace(result="boots and runs", heap_used=45, heap_free=116, note=None, set_base_heap=True))
            mem = fx.load_json(fx.MEMORY)
            effects_heap = mem["effects"]["funbox-uranus"]["heap"]
            self.assertEqual(mem["base"]["heap"], 45 * 1024 - effects_heap)
        finally:
            fx.MEMORY = saved

    def test_a_broken_chain_cannot_be_saved(self):
        self.ed.add_slot("reverb", "Rv")
        self.assertFalse(self.sel.save())
        self.assertIn("cannot save", self.sel.message)


class ScreenTest(unittest.TestCase):
    """The real screen, in a pseudo terminal of 24 x 80."""

    def run_keys(self, keys, rows=24, cols=80, nam_sources=False):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.root = tempfile.TemporaryDirectory()
        make_root(self.root.name)
        self.nam = tempfile.TemporaryDirectory()
        make_nam_dir(os.path.join(self.nam.name, "Effect-Modules", "Nam"), sources=nam_sources)
        env = dict(os.environ, TERM="xterm-256color", ESCDELAY="25", EFFECTS_TOOL_ROOT=self.root.name, EFFECTS_NAM_DIR=os.path.join(self.nam.name, "Effect-Modules", "Nam"))
        proc = subprocess.Popen([sys.executable, os.path.join(HERE, "effects.py"), "tui"], stdin=slave, stdout=slave, stderr=slave, env=env, close_fds=True)
        os.close(slave)
        out = b""

        def drain(wait):
            nonlocal out
            end = time.time() + wait
            while time.time() < end:
                r, _, _ = select.select([master], [], [], 0.05)
                if r:
                    try:
                        out += os.read(master, 65536)
                    except OSError:
                        return

        drain(0.8)
        for k in [b"\r", *keys, b"q"]:  # Enter: Configure effects (the first item of the start menu); at the end q leaves the start menu
            try:
                os.write(master, k)
            except OSError:
                break
            drain(0.25)
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        drain(0.2)
        os.close(master)
        text = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]|\x1b[=>]", b" ", out).decode("utf-8", "replace")
        return text, proc.returncode

    def test_screen_shows_the_list_the_status_bar_and_the_help_hint(self):
        text, code = self.run_keys([b"q"])
        self.assertEqual(code, 0)
        self.assertIn("funbox-uranus", text)  # the cursor starts on the first selected effect, so the list is scrolled to show it
        self.assertIn("funbox-neptune", text)
        self.assertIn("DTCM", text)
        self.assertIn("SDRAM", text)
        self.assertIn("h: help", text)

    def test_arrows_space_help_and_quit_without_saving(self):
        text, code = self.run_keys([b"\x1bOB", b"\x1bOB", b" ", b"h", b"x", b"q", b"n"])
        self.assertEqual(code, 0)
        self.assertIn("Help", text)
        self.assertIn("Unsaved changes", text)  # the selection was changed, so quitting asked
        self.assertIn("* ", text)
        self.assertEqual(open(os.path.join(self.root.name, "effects.config")).read(), "ALLOW_GPL=n\nfunbox-uranus=y\n")  # n: nothing saved

    def test_clear_asks_first_and_is_not_saved_until_s(self):
        cancelled, code = self.run_keys([b"x", b"n", b"q"])
        self.assertEqual(code, 0)
        self.assertIn("Clear everything", cancelled)
        self.assertNotIn("Unsaved changes", cancelled)  # n: nothing changed, so quitting does not ask
        cleared, code = self.run_keys([b"x", b"y", b"q", b"n"])
        self.assertEqual(code, 0)
        self.assertIn("0 effects", cleared)
        self.assertIn("Unsaved changes", cleared)
        self.assertEqual(open(os.path.join(self.root.name, "effects.config")).read(), "ALLOW_GPL=n\nfunbox-uranus=y\n")  # n: nothing saved

    def test_selecting_a_gpl_effect_asks_to_allow_it(self):
        gpl_cancel, code = self.run_keys([*[b"k"] * 7, b" ", b"n", b"q", b"n"])  # up from uranus to funbox-earth
        self.assertEqual(code, 0)
        self.assertIn("GPL effect", gpl_cancel)
        self.assertNotIn("GPL allowed", gpl_cancel)
        text, code = self.run_keys([*[b"k"] * 7, b" ", b"y", b"s", b"q"])
        self.assertEqual(code, 0)
        config = open(os.path.join(self.root.name, "effects.config")).read()
        self.assertIn("ALLOW_GPL=y", config)
        self.assertIn("funbox-earth=y", config)

    def test_the_info_popup(self):
        text, code = self.run_keys([b"i", b"x", b"q"])
        self.assertEqual(code, 0)
        self.assertIn("committed by", text)
        self.assertIn("origin", text)

    def test_making_and_saving_a_chain_on_the_screen(self):
        keys = [b"c", b"n", b"\r",                      # chains, a new chain, keep the suggested name
                b"j", b"j", b"j", b"\r", b"\r", b"\r",  # to "+ add an effect", the first effect, keep its tag
                b"a", b"j", b"\r", b"\r",              # another effect (the second in the list), keep its tag
                b"\x1b", b"\x1b", b"s", b"q"]          # back, back, save, quit
        text, code = self.run_keys(keys)
        self.assertEqual(code, 0)
        self.assertIn("Chain1", text)
        self.assertIn("saved", text)
        import json
        chains = json.load(open(os.path.join(self.root.name, "chains.json")))["chains"]
        self.assertEqual(len(chains), 1)
        self.assertEqual(len(chains[0]["slots"]), 2)
        gen = open(os.path.join(self.root.name, "generated", "effects_chains.inc")).read()
        self.assertIn("new EffectChain(", gen)

    def test_start_menu_has_configure_effects_and_a_placeholder_options_menu(self):
        text, code = self.run_keys([b"q", b"j", b"j", b"\r", b"\r"], rows=24, cols=80)  # leave Configure effects, to Options (the third item), open it, Back
        self.assertEqual(code, 0)
        self.assertIn("Pedal setup", text)
        self.assertIn("Configure effects", text)
        self.assertIn("Options", text)
        self.assertIn("Back", text)

    def test_nam_setup_lists_the_models_and_s_runs_update_models(self):
        text, code = self.run_keys([b"q", b"j", b"\r", b"h", b"x", b" ", b"s", b"n", b"q"])  # NAM setup, help, close it, deselect the first model, save without calibrating, back
        self.assertEqual(code, 0)
        self.assertIn("NAM models in the firmware", text)
        self.assertIn("Alpha", text)
        self.assertIn("Gamma", text)
        self.assertIn("NAM setup help", text)
        self.assertIn("2 of 3 models", text)
        self.assertIn("saved: models.config", text)
        nam = os.path.join(self.nam.name, "Effect-Modules", "Nam")
        self.assertIn("alpha=n", open(os.path.join(nam, "models.config")).read())
        self.assertNotIn("alpha", open(os.path.join(nam, "models_list.inc")).read())

    def test_nam_setup_converts_a_selected_new_model_when_saving(self):
        keys = [b"q", b"j", b"\r", *[b"j"] * 3, b" ", b"\r", b"s", b"n", b"q"]  # NAM setup, down to the first new model (after the three installed), select it, accept its name, save (no calibration), back
        text, code = self.run_keys(keys, nam_sources=True)
        self.assertEqual(code, 0)
        self.assertIn("new: amp1.nam", text)
        self.assertIn("not usable", text)
        self.assertIn("1 converted", text)
        nam = os.path.join(self.nam.name, "Effect-Modules", "Nam")
        self.assertTrue(os.path.exists(os.path.join(nam, "models", "cool_amp_1.h")))
        self.assertIn("cool_amp_1.h", open(os.path.join(nam, "models_list.inc")).read())

    def test_nam_setup_asks_for_the_name_and_makes_the_user_edit_a_long_one_down(self):
        # the models are in file-name order: alpha, beta, gamma, then the new ones as found (amp1, pack's), bad and small are not usable
        keys = [b"q", b"j", b"\r", *[b"j"] * 5, b" ",   # the zip's model (the suggestion is "Pack Gear - Pack Model", 22 characters)
                b"\r",                                    # too long: Enter does nothing
                *[b"\x7f"] * 13, b"\r",                   # edit it down to "Pack Gear" and accept
                b"s", b"n", b"q"]
        text, code = self.run_keys(keys, nam_sources=True)
        self.assertEqual(code, 0)
        self.assertIn("Name of the model", text)
        self.assertIn("gear model:", text)
        self.assertIn("Pack Gear - Pack Model", text)
        self.assertIn("At most 14", text)
        self.assertIn("1 converted", text)
        header = open(os.path.join(self.nam.name, "Effect-Modules", "Nam", "models", "pack_model.h")).read()
        self.assertIn('kName = "Pack Gear"', header)

    def test_saving_asks_about_the_calibration_and_y_calibrates(self):
        text, code = self.run_keys([b"q", b"j", b"\r", b" ", b"s", b"y", b"q"])  # alpha off, save with calibration
        self.assertEqual(code, 0)
        self.assertIn("Calibrate the output levels", text)
        self.assertIn("about a minute", text)
        self.assertIn("output levels calibrated", text)
        self.assertIn("kOutputGain = 1.9953f", open(os.path.join(self.nam.name, "Effect-Modules", "Nam", "models", "gamma.h")).read())

    def test_esc_at_the_calibration_question_does_not_save(self):
        text, code = self.run_keys([b"q", b"j", b"\r", b" ", b"s", b"\x1b", b"q", b"n"])  # the question, Esc, then back: asks about the unsaved change, n
        self.assertEqual(code, 0)
        self.assertIn("not saved", text)
        self.assertFalse(os.path.exists(os.path.join(self.nam.name, "Effect-Modules", "Nam", "models.config")))

    def test_enter_does_nothing_while_the_name_is_too_long(self):
        # Enter on the 22 character suggestion is ignored (the dialog stays, no second dialog); Esc then cancels: nothing selected, nothing to save
        keys = [b"q", b"j", b"\r", *[b"j"] * 5, b" ", b"\r", b"\r", b"\x1b", b"q"]
        text, code = self.run_keys(keys, nam_sources=True)
        self.assertEqual(code, 0)
        self.assertNotIn("Unsaved changes", text)
        self.assertFalse(os.path.exists(os.path.join(self.nam.name, "Effect-Modules", "Nam", "models", "pack_model.h")))

    def test_cancelling_the_name_dialog_does_not_select_the_model(self):
        text, code = self.run_keys([b"q", b"j", b"\r", *[b"j"] * 3, b" ", b"\x1b", b"q"], nam_sources=True)  # no "Unsaved changes" when leaving
        self.assertEqual(code, 0)
        self.assertNotIn("Unsaved changes", text)

    def test_nam_setup_asks_before_leaving_with_unsaved_changes(self):
        text, code = self.run_keys([b"q", b"j", b"\r", b" ", b"q", b"n"])
        self.assertEqual(code, 0)
        self.assertIn("Unsaved changes", text)
        self.assertFalse(os.path.exists(os.path.join(self.nam.name, "Effect-Modules", "Nam", "models.config")))

    def test_esc_leaves_configure_effects_for_the_start_menu(self):
        text, code = self.run_keys([b"\x1b"])  # Esc in Configure effects, then the final q quits the start menu
        self.assertEqual(code, 0)
        self.assertGreaterEqual(text.count("Pedal setup"), 2)  # the start menu was drawn again

    def test_saving_a_selection_that_does_not_fit_asks_first(self):
        text, code = self.run_keys([b"p", *[b"j"] * 10, b"\r", b"s", b"n", b"q", b"n"])
        self.assertEqual(code, 0)
        self.assertIn("May not work", text)
        self.assertEqual(open(os.path.join(self.root.name, "effects.config")).read(), "ALLOW_GPL=n\nfunbox-uranus=y\n")  # n: not saved
        text, code = self.run_keys([b"p", *[b"j"] * 10, b"\r", b"s", b"y", b"q"])
        self.assertEqual(code, 0)
        self.assertIn("May not work", text)
        self.assertIn("ALLOW_GPL=y", open(os.path.join(self.root.name, "effects.config")).read())  # y: saved anyway

    def test_a_small_terminal_does_not_crash(self):
        text, code = self.run_keys([b"q"], rows=5, cols=30)
        self.assertEqual(code, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
