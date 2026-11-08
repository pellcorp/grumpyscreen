"""Focused AFC console checks: python3 -m unittest discover -s tools -p test_afc_console.py."""
import unittest
from unittest.mock import AsyncMock, patch

from moonsim import mmu, testbench, klipper


class AFCConsoleTest(unittest.IsolatedAsyncioTestCase):
    async def test_ready_logo_preserves_raw_multiline_markup(self):
        states = {"AFC": {"units": ["Canvas CANVAS_1"], "lanes": []}}
        with patch.object(mmu, "STATE", states), patch.object(mmu, "console", new_callable=AsyncMock) as out:
            await mmu.afc_prep()
        self.assertEqual(out.await_args_list[-1].args[0],
                         '<span class=success--text>R  _____     ____\n'
                         'E /      \\  |  </span><span class=info--text>o</span>'
                         '<span class=success--text> | \n'
                         'A |       |/ ___/ \nD |_________/     \n'
                         'Y <span class=warning--text>|</span><span class=error--text>_</span>'
                         '<span class=warning--text>|</span><span class=error--text>_</span>'
                         '<span class=warning--text>|</span> '
                         '<span class=warning--text>|</span><span class=error--text>_</span>'
                         '<span class=warning--text>|</span><span class=error--text>_</span>'
                         '<span class=warning--text>|</span>\n  CANVAS_1\n')

    async def test_prep_sensor_states_and_error_logo(self):
        states = {
            "AFC": {"units": ["Canvas CANVAS_1"], "lanes": ["a", "b", "c", "d", "e"]},
        }
        for name, prep, load, tooled in (
                ("a", True, True, True), ("b", True, True, False),
                ("c", False, False, False), ("d", True, False, False),
                ("e", False, True, False)):
            states[mmu.lane_key(name)] = dict(prep=prep, load=load, tool_loaded=tooled, map=["T0"])
        with patch.object(mmu, "STATE", states), patch.object(mmu, "console", new_callable=AsyncMock) as out:
            self.assertTrue(await mmu.handle_afc(klipper.Cmd("PREP")))
        self.assertEqual([c.args[0] for c in out.await_args_list], [
            'Box_Turtle CANVAS_1 Prepping lanes',
            'a tool cmd: T0  <span class=success--text>LOCKED</span>'
            '<span class=success--text> AND LOADED</span><span class=primary--text> in ToolHead</span>',
            'b tool cmd: T0  <span class=success--text>LOCKED</span>'
            '<span class=success--text> AND LOADED</span>',
            'c tool cmd: T0  EMPTY READY FOR SPOOL',
            'd tool cmd: T0  <span class=success--text>LOCKED</span>'
            '<span class=error--text> NOT LOADED</span>',
            'e tool cmd: T0  <span class=error--text>CHECK FILAMENT Prep: False - Load: True</span>',
            '<span class=error--text>E  _ _   _ _\nR |_|_|_|_|_|\nR |         \\____\n'
            'O |              \\ \nR |          |\\ <span class=secondary--text>X</span> |\n'
            '! \\_________/ |___|</span>\n  CANVAS_1\n',
        ])

    async def test_samples_require_afc(self):
        for backend in ("hh", "none"):
            with patch.object(mmu, "BACKEND", backend), \
                    patch.object(testbench.S, "respond_info", new_callable=AsyncMock) as info, \
                    patch.object(mmu, "afc_prep", new_callable=AsyncMock) as prep:
                await testbench.sim_afc_console(klipper.Cmd("SIM_AFC_CONSOLE"))
                info.assert_awaited_once_with("SIM_AFC_CONSOLE needs --backend afc")
                prep.assert_not_awaited()


if __name__ == "__main__":
    unittest.main()
