import unittest

from kuglass.mi_servo import MIServo
from kuglass.models import ChannelTarget, OpticalState, PolicyDecision, VehicleMode, DemoMode


class MIServoTest(unittest.TestCase):
    def test_release_is_rate_limited(self):
        servo = MIServo()
        d1 = PolicyDecision(1, [ChannelTarget(0, 0.1, 0.1, True, OpticalState.DIM, 1.0, "")], 0, 0, False, VehicleMode.DRIVING, DemoMode.NONE)
        servo.update(d1, now=1000)
        d2 = PolicyDecision(2, [ChannelTarget(0, 0.9, 0.9, True, OpticalState.CLEAR, 0.0, "")], 0, 0, False, VehicleMode.DRIVING, DemoMode.NONE)
        out = servo.update(d2, now=1050)
        self.assertLess(out.targets[0].target_mi, 0.2)

    def test_front_fast_attack_can_drop_immediately(self):
        servo = MIServo()
        d1 = PolicyDecision(1, [ChannelTarget(0, 0.9, 0.9, True, OpticalState.CLEAR, 0.0, "")], 0, 0, False, VehicleMode.DRIVING, DemoMode.NONE)
        servo.update(d1, now=1000)
        d2 = PolicyDecision(2, [ChannelTarget(0, 0.2, 0.2, True, OpticalState.DIM, 1.0, "")], 0, 0, True, VehicleMode.DRIVING, DemoMode.NONE)
        out = servo.update(d2, now=1050)
        self.assertAlmostEqual(out.targets[0].target_mi, 0.2, delta=0.001)


if __name__ == "__main__":
    unittest.main()

