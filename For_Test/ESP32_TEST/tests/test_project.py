from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "main" / "main.c").read_text(encoding="utf-8")
SDKCONFIG = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")


class Esp32PinTestProject(unittest.TestCase):
    def test_exact_output_pin_rows(self):
        row_pattern = re.compile(
            r"\{GPIO_NUM_(\d+),\s*(\d),\s*SIGNAL_[A-Z_]+,\s*"
            r'"([A-Z0-9_]+)",\s*"(J[13]-\d+)",\s*(\d+)\}'
        )
        rows = {
            (int(gpio), int(channel), name, header, int(j7))
            for gpio, channel, name, header, j7 in row_pattern.findall(MAIN)
        }
        self.assertEqual(
            rows,
            {
                (10, 0, "PWM_MAG_CH0", "J1-16", 1),
                (11, 0, "DIR_CH0", "J1-17", 3),
                (12, 0, "ENABLE_CH0", "J1-18", 5),
                (14, 1, "PWM_MAG_CH1", "J1-20", 17),
                (15, 1, "DIR_CH1", "J1-8", 19),
                (16, 1, "ENABLE_CH1", "J1-9", 21),
                (18, 2, "PWM_MAG_CH2", "J1-11", 33),
                (21, 2, "DIR_CH2", "J3-18", 35),
                (38, 2, "ENABLE_CH2", "J3-10", 37),
                (40, 3, "PWM_MAG_CH3", "J3-8", 49),
                (41, 3, "DIR_CH3", "J3-7", 51),
                (42, 3, "ENABLE_CH3", "J3-6", 53),
            },
        )

    def test_input_only_pins_are_reserved(self):
        self.assertIn("SAFETY_INPUT_GPIO_MASK", MAIN)
        self.assertIn("ADC_INPUT_GPIO_MASK", MAIN)
        self.assertIn("OUTPUT_GPIO_MASK & SAFETY_INPUT_GPIO_MASK", MAIN)
        self.assertIn("OUTPUT_GPIO_MASK & ADC_INPUT_GPIO_MASK", MAIN)
        for gpio in (1, 2, 3, 4, 5, 6, 7, 8, 13, 17, 19, 39, 47):
            self.assertIn(f"GPIO_BIT({gpio})", MAIN)

    def test_no_pwm_peripheral_or_waveform_generation(self):
        lowered = MAIN.lower()
        for forbidden in ("driver/ledc", "driver/mcpwm", "ledc_", "mcpwm_", "sinf(", "cosf("):
            self.assertNotIn(forbidden, lowered)

    def test_native_usb_console_is_disabled(self):
        self.assertIn("CONFIG_ESP_CONSOLE_UART_DEFAULT=y", SDKCONFIG)
        self.assertIn("CONFIG_ESP_CONSOLE_SECONDARY_NONE=y", SDKCONFIG)
        self.assertIn("CONFIG_ESP_CONSOLE_USB_CDC=n", SDKCONFIG)
        self.assertIn("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=n", SDKCONFIG)


if __name__ == "__main__":
    unittest.main()
