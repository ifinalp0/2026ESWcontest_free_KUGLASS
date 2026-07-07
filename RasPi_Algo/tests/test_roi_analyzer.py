import unittest

from kuglass.roi_analyzer import ROIAnalyzer


class ROIAnalyzerTest(unittest.TestCase):
    def test_saturation_and_edges_are_measured(self):
        frame = [[30 for _ in range(8)] for _ in range(4)]
        for y in range(4):
            for x in range(4, 8):
                frame[y][x] = 255 if (x + y) % 2 == 0 else 20
        rois = {"left": (0, 0, 4, 4), "right": (4, 0, 4, 4)}
        stats = {item.name: item for item in ROIAnalyzer().analyze(frame, rois)}
        self.assertLess(stats["left"].saturation_ratio, 0.01)
        self.assertGreater(stats["right"].saturation_ratio, 0.45)
        self.assertGreater(stats["right"].edge_density, stats["left"].edge_density)


if __name__ == "__main__":
    unittest.main()

