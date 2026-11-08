"""Happy Hare replay checks: python3 -m unittest discover -s tools -p test_hh_console.py."""
import json
from pathlib import Path
import unittest
from unittest.mock import AsyncMock, patch

from moonsim import klipper, mmu, testbench


class HappyHareConsoleTest(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        path = Path(__file__).parent / "moonsim/hh_console_samples.json"
        self.samples = json.loads(path.read_text(encoding="utf-8"))["samples"]

    async def test_all_and_individual_captures(self):
        for sample in ("all", "boot", "thin", "sensors", "THIN"):
            with self.subTest(sample=sample), patch.object(mmu, "BACKEND", "hh"), \
                    patch.object(testbench.S, "respond_info", new_callable=AsyncMock) as out:
                await testbench.sim_hh_console(klipper.Cmd("SIM_HH_CONSOLE SAMPLE=" + sample))
                expected = [message for name, messages in self.samples.items()
                            if sample == "all" or sample.lower() == name for message in messages]
                self.assertEqual([call.args[0] for call in out.await_args_list], expected)

    async def test_default_replays_every_sample(self):
        with patch.object(mmu, "BACKEND", "hh"), \
                patch.object(testbench.S, "respond_info", new_callable=AsyncMock) as out:
            await testbench.sim_hh_console(klipper.Cmd("SIM_HH_CONSOLE"))
            self.assertEqual(out.await_count, 6)

    async def test_invalid_sample_emits_nothing(self):
        with patch.object(mmu, "BACKEND", "hh"), \
                patch.object(testbench.S, "respond_info", new_callable=AsyncMock) as out:
            with self.assertRaisesRegex(testbench.S.GcodeError, "SAMPLE must be"):
                await testbench.sim_hh_console(klipper.Cmd("SIM_HH_CONSOLE SAMPLE=missing"))
            out.assert_not_awaited()

    async def test_other_backends_receive_explanation_only(self):
        for backend in ("afc", "none"):
            with patch.object(mmu, "BACKEND", backend), \
                    patch.object(testbench.S, "respond_info", new_callable=AsyncMock) as out:
                await testbench.sim_hh_console(klipper.Cmd("SIM_HH_CONSOLE"))
                out.assert_awaited_once_with("SIM_HH_CONSOLE needs --backend hh")

    def test_capture_contains_screenshot_formatting(self):
        boot = "\n".join(self.samples["boot"])
        self.assertIn('Gate : | 0 | 1 | 2 | 3 |Byp|', boot)
        self.assertIn('Tools: |T0 |T1 |T2 |T3 | - |', boot)
        self.assertIn('| \u00a0 |', boot)
        self.assertIn('<span style="color:#000000">▼</span>', boot)
        self.assertIn('<b><span style="color:#87CEEB">UNLOADED</span></b>', boot)
        self.assertIn('<b>0.0mm</b>', boot)
        self.assertIn('┈', boot)
        self.assertIn('■', boot)
        thin = "\n".join(self.samples["thin"])
        self.assertIn('━', thin)
        self.assertIn('┫Nz▶', thin)
        sensors = "\n".join(self.samples["sensors"])
        for symbol in ('◯', '◉', 'ê', '▷'):
            self.assertIn(symbol, sensors)
        self.assertNotIn('\t', boot + thin + sensors)


if __name__ == "__main__":
    unittest.main()
