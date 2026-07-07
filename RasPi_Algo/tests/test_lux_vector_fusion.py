import unittest

from kuglass.lux_vector_fusion import LuxVectorFusion
from kuglass.models import DirectionalLuxVector, FrontGlare


class LuxFusionTest(unittest.TestCase):
    def test_lux_angle_is_preserved_without_camera_cue(self):
        lux = DirectionalLuxVector(100, 12000, 100, 100, 90.0, 0.8, 12300)
        glare = FrontGlare(0.0, 0.0, 0.0, False, "center")
        fused = LuxVectorFusion().fuse(lux, glare)
        self.assertAlmostEqual(fused.theta_deg, 90.0, delta=0.1)
        self.assertGreater(fused.confidence, 0.7)

    def test_front_right_glare_pulls_angle_forward(self):
        lux = DirectionalLuxVector(100, 12000, 100, 100, 90.0, 0.4, 12300)
        glare = FrontGlare(0.1, 0.9, 0.9, True, "right")
        fused = LuxVectorFusion().fuse(lux, glare)
        self.assertLess(fused.theta_deg, 80.0)


if __name__ == "__main__":
    unittest.main()

