#!/usr/bin/env python3
"""Offline tests of tools/skin_compat.py: which skins count as using only the file format MPC OS 2.15.1's own skins use.
The 2.x-shaped skin below is built by hand (no Akai file). No device: python3 tools/test_skin_compat.py"""
import copy
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import skin_compat  # noqa: E402


def tui_2x():
    """A small skin in the MPC OS 2.15.1 shape: tab version 1 with the page inline, definitions 2, Knob and Button data 1,
    actions 1, plus one local widget definition."""
    bg = {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""},
          "unfocussed": {"version": 1, "colour": "0", "image": ""}}

    def child(ctype, data, bounds_version=1):
        b = {"version": bounds_version, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
             "whenVisible": "Always", "boundsType": "Absolute", "bounds": "0 0 1 1"}
        if bounds_version == 2:
            b["additionalInvalidatingHandles"] = []
        return {"version": 2, "componentData": {"version": 1, "name": ctype, "type": ctype, "data": data},
                "handle remapping": {"version": 1, "map": []}, "bounds": b}

    def page(kids):
        return {"version": 2, "actions": [{"version": 1, "onAction": "Mouse Down", "handler": "Q-Link",
                                           "handleName": "Data", "additionalData": ""}],
                "backgroundData": bg, "ignoreMousePresses": False, "disableCoarseDataWheel": False,
                "componentsData": kids}
    widget = page([child("Label", {"version": 1, "type": "Value", "handleName": "Data"}, 2)])
    kids = [child("Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "a.png"}),
            child("Knob", {"version": 1, "knobType": "FilmStrip", "filmStrip": "k.png", "numFrames": 127,
                           "handleName": "Data"}),
            child("Button", {"version": 1, "onImage": "a.png", "offImage": "b.png", "buttonId": 1,
                             "numButtonsInGroup": 1, "handleName": "Data"}),
            child("Focus", {"version": 1, "backgroundColour": "0", "outlineColour": "0", "backgroundInset": 4.0,
                            "outlineThickness": 1.0}),
            child("wLocal", {"version": 1, "handleName": "Data"})]
    return {"pageData": {"version": 1, "info": {"version": 1, "type": "CompleteDescription"},
                         "componentDefinitions": {"version": 2, "importFiles": [],
                                                  "localComponentDefinitions": [{"key": "wLocal", "value": widget}]},
                         "tabs": [{"version": 1, "tabName": "T", "fnKeyIndex": 0, "fnKeySubIndex": 0,
                                   "componentDefinition": page(kids), "qlinkBoundsData": ["0 0 0 0"]}]}}


def qlinks(version=4):
    return {"version": version, "info": {"version": 1, "type": "CompleteDescription"},
            "Screen Mode Q-Links": {"version": 4, "map": []}, "Program Mode Q-Links": {}}


def kids(d):
    return d["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]


class SkinCompat(unittest.TestCase):
    def test_a_2x_shaped_skin_has_no_problems(self):
        self.assertEqual(skin_compat.skin_problems(tui_2x(), qlinks()), [])
        self.assertEqual(skin_compat.os_compat("2.30", tui_2x(), qlinks())[0], ["2.x", "3.x"])

    def test_a_missing_qlinks_file_is_not_a_problem(self):
        self.assertEqual(skin_compat.skin_problems(tui_2x(), None), [])

    def flagged(self, mutate, expect):
        d = tui_2x()
        mutate(d)
        problems = skin_compat.skin_problems(d, qlinks())
        self.assertTrue([p for p in problems if expect in p], (expect, problems))
        self.assertEqual(skin_compat.os_compat(None, d, qlinks())[0], ["3.x"])

    def test_each_3x_only_shape_is_flagged(self):
        def tab3(d):
            d["pageData"]["tabs"][0]["version"] = 3

        def def4(d):
            d["pageData"]["tabs"][0]["componentDefinition"].update(version=4, repeats=1, hideQLinkBounds=True)

        def knob5(d):
            kids(d)[1]["componentData"]["data"].update(version=5, invert=False, dragOrientation="Vertical")

        def button2(d):
            kids(d)[2]["componentData"]["data"].update(version=2, gestureBehaviour="Instant")

        def action2(d):
            d["pageData"]["tabs"][0]["componentDefinition"]["actions"][0].update(version=2)
            d["pageData"]["tabs"][0]["componentDefinition"]["actions"][0]["handle remapping"] = {"version": 1, "map": []}

        def slider4(d):
            kids(d)[1]["componentData"].update(type="Slider")
            kids(d)[1]["componentData"]["data"].update(version=4, revealType="x")
        self.flagged(tab3, "tabs[] version 3")
        self.flagged(def4, "componentDefinition version 4")
        self.flagged(knob5, "data:Knob version 5")
        self.flagged(button2, "data:Button version 2")
        self.flagged(action2, "actions[] version 2")
        self.flagged(slider4, "data:Slider version 4")

    def test_a_field_2x_does_not_use_is_flagged_even_at_a_known_version(self):
        def extra(d):
            kids(d)[1]["componentData"]["data"]["dragOrientation"] = "Vertical"   # Knob version 1 has no such field
        self.flagged(extra, "field(s) 2.15.1 does not use: dragOrientation")

    def test_an_unknown_kind_of_object_is_flagged(self):
        def new(d):
            d["pageData"]["tabs"][0]["fancy"] = {"version": 1}
        self.flagged(new, "fancy: not a kind of object")

    def test_missing_pagedata_is_not_compatible(self):
        self.assertEqual(skin_compat.skin_problems({}, None), ["TUI.json has no pageData"])
        self.assertEqual(skin_compat.os_compat(None, None)[0], ["3.x"])

    def test_qlinks_version_5_is_flagged(self):
        self.assertTrue(skin_compat.skin_problems(tui_2x(), qlinks(5)))

    def test_glibc_above_2_32_is_not_2x(self):
        for g, expect in ((None, ["2.x", "3.x"]), ("2.17", ["2.x", "3.x"]), ("2.32", ["2.x", "3.x"]),
                          ("2.32.1", ["2.x", "3.x"]), ("2.33", ["3.x"]), ("2.34", ["3.x"]), ("3.0", ["3.x"])):
            self.assertEqual(skin_compat.os_compat(g, tui_2x(), qlinks())[0], expect, g)

    def test_the_table_has_versions_and_field_names_only(self):
        t = skin_compat.load_table()
        self.assertIn("TUI:tabs[]", t)
        self.assertEqual(sorted(t["TUI:tabs[]"]), ["1"])
        self.assertIn("TUI:data:Knob", t)
        for role, versions in t.items():
            for v, fields in versions.items():
                self.assertTrue(v.isdigit(), (role, v))
                self.assertTrue(all(isinstance(f, str) and len(f) < 40 for f in fields), (role, v))

    def test_checking_does_not_change_the_skin(self):
        d = tui_2x()
        before = copy.deepcopy(d)
        skin_compat.os_compat("2.30", d, qlinks())
        self.assertEqual(d, before)


if __name__ == "__main__":
    unittest.main()
