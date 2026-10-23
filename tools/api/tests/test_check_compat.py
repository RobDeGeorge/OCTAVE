"""tools/api/check_compat.py: what counts as breaking (ctest api_compat)."""
import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import check_compat  # noqa: E402

BASE = {
    "index.json": {"protocolVersion": 2, "objects": {"vol": "Vol"}},
    "vol.json": {
        "class": "Vol",
        "object": "vol",
        "properties": [
            {"name": "percent", "type": "int", "readable": True, "writable": True, "notify": "percentChanged"},
            {"name": "draft", "type": "int", "readable": True, "writable": False, "stability": "experimental"},
            {"name": "mode", "type": "int", "readable": True, "writable": False, "enum": ["A", "B"]},
        ],
        "methods": [
            {"name": "apply", "kind": "slot", "params": [{"name": "p", "type": "int"}], "returns": "",
             "scope": "media"},
            {"name": "linear", "kind": "slot", "params": [{"name": "p", "type": "float"}], "returns": "float"},
        ],
        "signals": [{"name": "applied", "params": [{"name": "p", "type": "int"}]}],
    },
    "parameters.json": [{"id": "RPM", "unit": "rpm"}],
}


def changed(fn):
    tree = copy.deepcopy(BASE)
    fn(tree)
    return check_compat.compare(BASE, tree)


def vol(tree):
    return tree["vol.json"]


class Compare(unittest.TestCase):
    def assertBreaks(self, fn, key):
        breaks, _ = changed(fn)
        self.assertIn(key, [k for k, _ in breaks], breaks)

    def assertCompatible(self, fn):
        breaks, _ = changed(fn)
        self.assertEqual(breaks, [])

    def test_identical(self):
        self.assertEqual(check_compat.compare(BASE, BASE), ([], []))

    def test_removals_break(self):
        self.assertBreaks(lambda t: vol(t)["properties"].pop(0), "vol.percent")
        self.assertBreaks(lambda t: vol(t)["methods"].pop(0), "vol.apply")
        self.assertBreaks(lambda t: vol(t)["signals"].pop(0), "vol.applied")
        self.assertBreaks(lambda t: t["index.json"]["objects"].pop("vol"), "vol")
        self.assertBreaks(lambda t: t["parameters.json"].pop(0), "parameters.RPM")

    def test_changes_break(self):
        self.assertBreaks(lambda t: vol(t)["properties"][0].update(type="float"), "vol.percent")
        self.assertBreaks(lambda t: vol(t)["properties"][0].update(writable=False), "vol.percent")
        self.assertBreaks(lambda t: vol(t)["properties"][0].pop("notify"), "vol.percent")
        self.assertBreaks(lambda t: vol(t)["properties"][2].update(enum=["A"]), "vol.mode")
        self.assertBreaks(lambda t: vol(t)["methods"][1].update(returns="int"), "vol.linear")
        self.assertBreaks(lambda t: vol(t)["methods"][0]["params"][0].update(type="float"), "vol.apply")
        self.assertBreaks(lambda t: vol(t)["signals"][0]["params"][0].update(type="float"), "vol.applied")
        self.assertBreaks(lambda t: vol(t)["properties"][0].update(stability="experimental"), "vol.percent")
        self.assertBreaks(lambda t: vol(t).update(stability="experimental"), "vol")
        self.assertBreaks(lambda t: t["parameters.json"][0].update(unit="Hz"), "parameters.RPM")

    def test_a_new_argument_is_a_different_method(self):
        # `call` matches by name and argument count
        self.assertBreaks(lambda t: vol(t)["methods"][0]["params"].append({"name": "q", "type": "int"}), "vol.apply")

    def test_additions_are_compatible(self):
        def add(t):
            vol(t)["properties"].append({"name": "new", "type": "int", "readable": True})
            vol(t)["methods"].append({"name": "apply", "kind": "slot", "params": [], "returns": ""})
            vol(t)["signals"][0]["params"].append({"name": "linear", "type": "float"})
            vol(t)["methods"][0]["returns"] = "bool"   # returned nothing before
            vol(t)["properties"][2]["enum"].append("C")
            t["parameters.json"].append({"id": "SPEED", "unit": "MPH"})
        self.assertCompatible(add)
        _, adds = changed(add)
        self.assertIn("vol.new", [k for k, _ in adds])
        self.assertIn("parameters.SPEED", [k for k, _ in adds])

    def test_experimental_members_are_not_covered(self):
        self.assertCompatible(lambda t: vol(t)["properties"].pop(1))
        self.assertCompatible(lambda t: vol(t)["properties"][1].update(type="QString"))

    def test_permissions(self):
        # Needing another scope locks out devices that had the old one; read opens it up
        self.assertBreaks(lambda t: vol(t)["methods"][0].update(scope="settings"), "vol.apply")
        self.assertBreaks(lambda t: vol(t)["methods"][0].update(scope="full"), "vol.apply")
        self.assertCompatible(lambda t: vol(t)["methods"][0].update(scope="read"))
        self.assertCompatible(lambda t: vol(t)["methods"][1].update(scope="media"))   # had none: newly declared

    def test_deprecation_is_compatible(self):
        self.assertCompatible(lambda t: vol(t)["properties"][0].update(deprecated="use level"))


class Changelog(unittest.TestCase):
    def test_sections(self):
        two = check_compat.changelog_section(2)
        self.assertIsNotNone(two)
        self.assertIn("`pair`", two)
        self.assertNotIn("The first published API", two)   # stops at the next version
        self.assertIsNone(check_compat.changelog_section(99))


if __name__ == "__main__":
    unittest.main()
