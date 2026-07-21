from printer_hardware.axis import Axis
from printer_hardware.heatbed import Heatbed
from printer_hardware.input_pin import InputPin
from printer_hardware.output_pin import OutputPin
from printer_hardware.pin_handler import PinHandler
from printer_hardware.probe import BedSurface, Probe
from printer_hardware.server import FakeHardwareServer
from printer_hardware.single_extruder import Extruder
from printer_hardware.state_io import STATE_FILE_PATH

axis = [
    Axis(
        name="axis-x",
        position=150,
        min_pos=-15,
        max_pos=300,
        step_pin="chip1_gpio0",
        dir_pin="chip1_gpio1",
        enable_pin="chip1_gpio2",
        endstop_pin="chip1_gpio3",
        endstop_pos=0,
    ),
    Axis(
        name="axis-y",
        position=150,
        min_pos=-15,
        max_pos=300,
        step_pin="chip2_gpio0",
        dir_pin="chip2_gpio1",
        enable_pin="chip2_gpio2",
        endstop_pin="chip2_gpio3",
        endstop_pos=0,
    ),
    Axis(
        name="axis-z",
        position=30,
        min_pos=-2,
        max_pos=250,
        step_pin="chip3_gpio0",
        dir_pin="chip3_gpio1",
        enable_pin="chip3_gpio2",
        endstop_pin="chip3_gpio3",
        endstop_pos=0,
    ),
]
heatbed = Heatbed(name="heatbed", heater_pin="chip0_gpio0", sensor_pin="analog0")
extruder = Extruder(
    name="extruder",
    step_pin="chip4_gpio0",
    dir_pin="chip4_gpio1",
    enable_pin="chip4_gpio2",
    heater_pin="chip4_gpio3",
    sensor_pin="analog1",
)
probe = Probe(
    name="probe",
    pin="chip5_gpio0",
    x_axis=axis[0],
    y_axis=axis[1],
    z_axis=axis[2],
    z_offset=0.5,
    bed_surface=BedSurface(
        tilt_x=0.0008,
        tilt_y=-0.0004,
        bump_x=150.0,
        bump_y=150.0,
        bump_height=0.3,
        bump_radius=80.0,
    ),
)
io_pins: list[PinHandler] = [
    InputPin(name="filament_sensor", pin="chip6_gpio3", initial_value=1),
    OutputPin(name="fan", pin="chip6_gpio0"),
    OutputPin(name="heater_fan", pin="chip6_gpio1"),
    OutputPin(name="controller_fan", pin="chip6_gpio2"),
    OutputPin(name="output_pin", pin="chip6_gpio4"),
]

if __name__ == "__main__":
    fake_server = FakeHardwareServer(
        hardware_components=axis + [heatbed, extruder, probe] + io_pins,
        state_file_path=STATE_FILE_PATH,
    )
    fake_server.run()
