#!/usr/bin/env python3
"""Regression test for a class of bug, not one specific bug: laser/spindle
power that's supposed to track motion (M4 rate-adjusted / laser mode) but
silently doesn't, or tracks it wrong.

This is deliberately more general than a narrow "spindle_isr_speed_fn must
not be null" check for PR #1847 (github.com/bdring/FluidNC/pull/1847) --
that exact mechanism is unlikely to recur, but "the laser doesn't turn on
during motion" is a whole class of bug (wrong ISR wiring, wrong speed
scaling, mapSpeed() errors, ...) that this can catch regardless of cause,
by observing actual PWM duty over real motion instead of asserting
anything about how the firmware gets there.

How it works: capture/PwmPin.cpp reports every real duty change as a
plain [MSG:PWM: gpio.N,duty] line (see that file's comment). Controller's
on_msg() dispatch (tool/controller.py) delivers those to a callback
registered here, transparently, as a side effect of the ordinary <-/<~
line-matching machinery already skipping past async chatter -- no
separate polling or dump/clear commands needed. This script drives a
rate-adjusted (Laser-type) spindle through M4 + a real G1 move and checks
the shape of what came back:

  - Rendered concretely, PR #1847's bug means this list is empty:
    spindle_isr_speed_fn was null, so PWM::setSpeedfromISR() (the only
    thing that calls setDuty() during motion) never ran at all.
  - Beyond that, correct rate-adjustment (Stepper.cpp: speed *=
    prep.current_speed * prep.inv_rate) should rise as the move
    accelerates and fall back toward 0 as it decelerates/stops -- so this
    also catches a wrong-shaped power curve, not just a missing one.

Run directly against the native simulator:
    python3 test_pwm_rate_adjustment.py sim:macos
(build it first: `pio run -e macos` from the repo root)
or against real hardware, if it happens to have a Laser-type spindle
configured on gpio.2 at 5000 Hz (fixtures/pwm_laser.yaml's config) --
otherwise this is native-simulator-only, since it needs that specific
config loaded.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from tool.controller import Controller, GrblFault  # noqa: E402
from tool.grbl_message import MessageType  # noqa: E402
from tool.transport import create_transport  # noqa: E402
from tool.transport.subprocess_ import SubprocessTransport  # noqa: E402

FIXTURE_CONFIG = Path(__file__).resolve().parent / "fixtures" / "pwm_laser.yaml"


def parse_pwm_sample(arguments: str):
    """"[MSG:PWM: gpio.2,156]"'s body, after parse_msg() splits off the
    "PWM" command tag, leaves arguments == " gpio.2,156" -- pull the duty
    back out of that."""
    _, _, duty = arguments.strip().partition(",")
    return int(duty)


def run(dut: str) -> bool:
    if dut.startswith("sim:"):
        transport = SubprocessTransport(pio_env=dut[len("sim:") :], config_path=FIXTURE_CONFIG, timeout=10)
    else:
        transport = create_transport(dut, timeout=10)
    controller = Controller(transport, timeout=10)

    duty_samples = []
    controller.on_msg("PWM", lambda args: duty_samples.append(parse_pwm_sample(args)))

    def send(line):
        controller.send_line(line)
        msg = controller.expect(MessageType.OK)
        if msg is None:
            raise TimeoutError(f"no response to {line!r}")
        controller.clear_line()

    try:
        send("$X")  # clear any startup alarm
        send("M4 S1000")  # laser mode on, no motion yet -- PWM::setState() forces duty to 0 here
        send("G1 X10 F300 S1000")  # real motion -- this is what #1847 broke
        send("G4 P0")  # zero-length dwell: a barrier that only executes once the G1 above has actually finished
    except GrblFault as e:
        print(f"FAIL: unexpected fault during test sequence: {e.message.raw}")
        return False
    finally:
        controller.close()

    print(f"PWM duty samples observed during the move: {duty_samples}")

    if not duty_samples:
        print(
            "FAIL: no PWM duty changes observed at all during a rate-adjusted "
            "motion -- the laser never turned on. This is exactly PR #1847's bug "
            "shape (spindle_isr_speed_fn left null so PWM::setSpeedfromISR() is "
            "never called), though it could also be caused by anything else that "
            "keeps the laser from firing during motion."
        )
        return False

    max_duty = max(duty_samples)
    if max_duty <= 0:
        print("FAIL: PWM duty changed but never rose above 0 -- laser power never actually engaged.")
        return False

    if duty_samples[-1] != 0:
        print(
            f"FAIL: duty ended at {duty_samples[-1]}, not 0 -- the laser should be "
            "off by the time the move (and the dwell after it) has finished."
        )
        return False

    print(f"PASS: laser engaged during motion (peak duty {max_duty}) and returned to 0 afterward.")
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "dut",
        nargs="?",
        default="sim:macos",
        help="DUT spec (see tool/transport/__init__.py's create_transport()); defaults to sim:macos",
    )
    args = parser.parse_args()
    sys.exit(0 if run(args.dut) else 1)
