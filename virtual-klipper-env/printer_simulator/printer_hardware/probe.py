import math

from .axis import Axis
from .pin_handler import PinHandler


class BedSurface:
    """
    Synthetic bed surface used to give a simulated probe something interesting
    to measure. Height is a tilted plane (to mimic an unlevel bed) plus an
    optional gaussian bump/dip (to mimic a warped bed), sampled at (x, y).
    """

    def __init__(
        self,
        tilt_x: float = 0.0,
        tilt_y: float = 0.0,
        bump_x: float = 150.0,
        bump_y: float = 150.0,
        bump_height: float = 0.0,
        bump_radius: float = 60.0,
    ):
        self.tilt_x = tilt_x
        self.tilt_y = tilt_y
        self.bump_x = bump_x
        self.bump_y = bump_y
        self.bump_height = bump_height
        self.bump_radius = bump_radius

    def height_at(self, x: float, y: float) -> float:
        height = self.tilt_x * x + self.tilt_y * y
        if self.bump_height and self.bump_radius > 0:
            dist = math.hypot(x - self.bump_x, y - self.bump_y)
            height += self.bump_height * math.exp(-(dist**2) / (2 * self.bump_radius**2))
        return height


class Probe(PinHandler):
    """
    Simulates a bed-contact probe (inductive sensor / Klicky-style microprobe).
    The trigger pin fires once the probe's Z position dips below the bed
    surface directly beneath it, so BED_MESH_CALIBRATE sees a realistic,
    position-dependent trigger height instead of a flat plane.

    Sample Config:

    [probe]
    pin: ^gpiochip5/gpio0
    x_offset: 0
    y_offset: 0
    z_offset: 0.5
    samples: 1

    [bed_mesh]
    mesh_min: 10, 10
    mesh_max: 290, 290
    probe_count: 5, 5
    """

    def __init__(
        self,
        name: str,
        pin: str,
        x_axis: Axis,
        y_axis: Axis,
        z_axis: Axis,
        x_offset: float = 0.0,
        y_offset: float = 0.0,
        z_offset: float = 0.5,
        bed_surface: BedSurface | None = None,
    ):
        self.name = name
        self.pin = pin
        self.x_axis = x_axis
        self.y_axis = y_axis
        self.z_axis = z_axis
        self.x_offset = x_offset
        self.y_offset = y_offset
        self.z_offset = z_offset
        self.bed_surface = bed_surface or BedSurface()

    def get_name(self) -> str:
        return self.name

    @property
    def read_pins(self) -> list[str]:
        return [self.pin]

    @property
    def write_pins(self) -> list[str]:
        return []

    def set(self, key: str, value: int) -> None:
        print(f"[{self.name}] cannot set pin {key} to {value} - pin is read-only")

    def _surface_height_at_probe(self) -> float:
        probe_x = self.x_axis.position + self.x_offset
        probe_y = self.y_axis.position + self.y_offset
        return self.bed_surface.height_at(probe_x, probe_y)

    def get(self, key: str) -> int:
        if key == self.pin:
            trigger_z = self._surface_height_at_probe() + self.z_offset
            return 1 if self.z_axis.position <= trigger_z else 0
        print(f"[{self.name}] unknown pin: {key}")
        return 0

    def state(self) -> dict[str, float | int | bool | str]:
        return {
            "type": "probe",
            "triggered": bool(self.get(self.pin)),
            "surface_height_mm": self._surface_height_at_probe(),
            "z_offset_mm": self.z_offset,
        }
