#!/usr/bin/env python3
"""Capture real Happy Hare formatting without booting Klipper.

Usage: python3 tools/make_hh_console_samples.py [../Happy-Hare]
Only formatting methods and the upstream test adapter are loaded via AST;
printer imports, constructors and hardware operations are never executed.
"""
import ast
import dataclasses
import json
from pathlib import Path
import re
import subprocess
import sys
import types
import typing


def load_nodes(path, namespace, names):
    """Load selected upstream definitions with their original method bodies."""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    nodes = [node for node in tree.body if getattr(node, "name", None) in names]
    if len(nodes) != len(names):
        raise ValueError("Missing formatting definitions in %s" % path)
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(path), "exec"), namespace)


def capture(source):
    module = types.ModuleType("hh_console_capture")
    sys.modules[module.__name__] = module
    ns = module.__dict__
    ns.update(Dict=typing.Dict, Optional=typing.Optional,
              dataclass=dataclasses.dataclass, field=dataclasses.field, re=re)
    extras = source / "extras/mmu"
    exec((extras / "mmu_constants.py").read_text(encoding="utf-8"), ns)
    load_nodes(extras / "mmu_utils.py", ns, {"MmuColorUtils"})

    tree = ast.parse((extras / "mmu_controller.py").read_text(encoding="utf-8"))
    controller = next(node for node in tree.body if getattr(node, "name", None) == "MmuController")
    methods = {"get_filament_position_string", "_get_filament_char", "_mmu_visual_to_string"}
    controller.bases = []
    controller.body = [node for node in controller.body if getattr(node, "name", None) in methods]
    exec(compile(ast.Module(body=[controller], type_ignores=[]), str(extras), "exec"), ns)

    load_nodes(source / "test/filament_display.py", ns, {
        "FilamentDisplayState", "_ConstantIndex", "_RealMmuController", "_ConsoleParams",
        "_MmuUnitShim", "_UnitParams", "_SensorManagerShim", "_DriveShim",
        "get_filament_position_string",
    })
    load_nodes(extras / "mmu_logger.py", ns, {"MmuLogger"})
    ns["CONSOLE_COLOR_TOKENS_RE"] = re.compile(r"\{\{[^{}]*\}\}|\{[^{}]*\}")
    ns["CONSOLE_COLOR_SPAN_RE"] = re.compile(r"\{\{([0-9a-fA-F]{3,8})\}\}|\{\{\}\}")
    logger_context = types.SimpleNamespace(
        mmu=types.SimpleNamespace(p=types.SimpleNamespace(console_show_colored_text=True)))

    def html(text):
        return ns["MmuLogger"]._color_message(logger_context, text)[0]

    unit = types.SimpleNamespace(first_gate=0, num_gates=4, name="unit0",
                                 selector=types.SimpleNamespace(is_homed=True),
                                 has_filament_buffer=lambda: False)
    context = types.SimpleNamespace(
        p=types.SimpleNamespace(console_show_filament_color=True, endless_spool_eject_gate=-1),
        gate_selected=0, tool_selected=0, filament_pos=ns["FILAMENT_POS_UNLOADED"],
        gate_status=[1, 1, 1, 1], gate_color=["000000", "000000", "ff00ff", "ff00ff"],
        ttg_map=[0, 1, 2, 3], num_gates=4, endless_spool_enabled=False,
        mmu_machine=types.SimpleNamespace(units=[unit], unit_with_bypass=unit),
        mmu_unit=lambda gate=None: unit, has_espooler=lambda gate: False,
        selected_tool_string=lambda: "T0")
    context._get_filament_char = types.MethodType(ns["MmuController"]._get_filament_char, context)
    boot = ["Selecting last gate used (#0)...",
            html(ns["MmuController"]._mmu_visual_to_string(context))]

    state_class = ns["FilamentDisplayState"]

    def diagram(**kwargs):
        return html(ns["get_filament_position_string"](state_class(color=True, **kwargs)))

    boot.append(diagram(bold=True, gate_color="000000"))
    thin = [diagram(bold=False, pos=ns["FILAMENT_POS_LOADED"],
                    gate_color="ff00ff", filament_position=650.0)]
    sensors = [diagram(bold=True, pos=ns["FILAMENT_POS_IN_BOWDEN"],
                       direction=ns["DIRECTION_LOAD"], gate_color="ff00ff",
                       has_encoder=True, has_buffer=True, sync_feedback_state="compressed",
                       sensors={ns["SENSOR_ENTRY_PREFIX"]: True,
                                ns["SENSOR_EXIT_PREFIX"]: True,
                                ns["SENSOR_EXTRUDER_ENTRY"]: False,
                                ns["SENSOR_TOOLHEAD"]: False}, filament_position=125.0),
               html("{2}Warning: Test warning{0}")]
    return {"source_commit": subprocess.check_output(
                ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip(),
            "samples": {"boot": boot, "thin": thin, "sensors": sensors}}


if __name__ == "__main__":
    source = Path(sys.argv[1] if len(sys.argv) > 1 else "../Happy-Hare").resolve()
    destination = Path(__file__).parent / "moonsim/hh_console_samples.json"
    destination.write_text(json.dumps(capture(source), ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
