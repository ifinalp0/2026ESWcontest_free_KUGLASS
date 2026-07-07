import unittest

from kuglass.models import (
    AEMetadata,
    CameraMetrics,
    ControlInputs,
    DemoMode,
    DirectionalLuxVector,
    FrontGlare,
    FusedLightVector,
    ROIStats,
    VehicleMode,
    WeatherContext,
)
from kuglass.policy_engine import PolicyEngine


def base_inputs(mode=VehicleMode.DRIVING, demo=DemoMode.NONE, theta=0.0, glare=None):
    front = CameraMetrics("front", [ROIStats("front_left", 0, 0, 10, 10, 0.2, 0.0, 0.1, 0.0)], AEMetadata())
    glare = glare or FrontGlare(0.0, 0.0, 0.0, False, "center")
    lux = DirectionalLuxVector(12000, 500, 500, 500, theta, 0.8, 13500)
    fused = FusedLightVector(theta, 0.8, theta, 0.8, "lux")
    weather = WeatherContext(26.0, 0.2, 0.0, 5.0, False, "test")
    return ControlInputs(front, None, glare, lux, fused, weather, 28.0, mode, demo)


class PolicyEngineTest(unittest.TestCase):
    def test_camping_sets_all_channels_low_mi(self):
        decision = PolicyEngine().decide(base_inputs(VehicleMode.CAMPING))
        self.assertTrue(all(target.target_mi < 0.1 for target in decision.targets))

    def test_front_glare_targets_front_channel(self):
        glare = FrontGlare(0.8, 0.1, 0.8, True, "left")
        decision = PolicyEngine().decide(base_inputs(glare=glare))
        ch0 = next(target for target in decision.targets if target.channel_id == 0)
        ch5 = next(target for target in decision.targets if target.channel_id == 5)
        self.assertLess(ch0.target_mi, ch5.target_mi)
        self.assertIn("front", ch0.reason)

    def test_hot_summer_prioritizes_sunroof(self):
        inputs = base_inputs(demo=DemoMode.HOT_SUMMER, theta=90.0)
        decision = PolicyEngine().decide(inputs)
        ch7 = next(target for target in decision.targets if target.channel_id == 7)
        ch0 = next(target for target in decision.targets if target.channel_id == 0)
        self.assertLess(ch7.target_mi, ch0.target_mi)


if __name__ == "__main__":
    unittest.main()

