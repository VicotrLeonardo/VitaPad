"""Testes do cliente (rodar de linux-client/: python3 -m unittest -v)."""

import unittest

from evdev import ecodes

from vitaremote.mapper import Mapper, normalize_stick
from vitaremote.profile import ProfileError, builtin_profiles, load_profile, profile_from_dict
from vitaremote.protocol_v1 import BUTTONS, PACKET_SIZE, VitaState, pack, parse

DT = 1 / 120


class Recorder:
    def __init__(self):
        self.events = []
        self.syncs = 0

    def set_button(self, device, code, pressed):
        self.events.append(("btn", device, code, pressed))

    def set_axis(self, name, value):
        self.events.append(("axis", name, value))

    def move_mouse(self, dx, dy):
        self.events.append(("move", dx, dy))

    def scroll(self, v, h):
        self.events.append(("scroll", v, h))

    def sync(self):
        self.syncs += 1

    def take(self):
        ev, self.events = self.events, []
        return ev


def make(data):
    rec = Recorder()
    return Mapper(profile_from_dict(data), rec), rec


class ProtocolTest(unittest.TestCase):
    def test_packet_size_matches_vita_struct(self):
        self.assertEqual(PACKET_SIZE, 16)

    def test_roundtrip(self):
        s = VitaState(buttons=BUTTONS["cross"] | BUTTONS["l"], lx=0, ly=255, rx=10, ry=20,
                      touch_x=1919, touch_y=1087, front_touch=True, rear_right=True)
        self.assertEqual(parse(pack(s)), s)

    def test_parse_raw_bytes_from_original_layout(self):
        raw = bytes([0x00, 0x40, 0, 0, 1, 2, 3, 4, 0x10, 0x00, 0x20, 0x00, 0x09, 0, 0, 0])
        s = parse(raw)
        self.assertTrue(s.pressed("cross"))
        self.assertEqual((s.lx, s.ly, s.rx, s.ry, s.touch_x, s.touch_y), (1, 2, 3, 4, 16, 32))
        self.assertTrue(s.front_touch and s.rear_left and not s.rear_right)


class ProfileTest(unittest.TestCase):
    def test_builtin_profiles_load(self):
        names = builtin_profiles()
        self.assertIn("gamepad", names)
        for name in names:
            load_profile(name)

    def test_combo_and_aliases(self):
        p = profile_from_dict({"buttons": {"cross": "key:LEFTCTRL+c", "circle": "mouse:right",
                                           "square": "pad:a"}})
        self.assertEqual(p.buttons["cross"].codes, (ecodes.KEY_LEFTCTRL, ecodes.KEY_C))
        self.assertEqual(p.buttons["circle"].codes, (ecodes.BTN_RIGHT,))
        self.assertEqual(p.buttons["square"].codes, (ecodes.BTN_SOUTH,))
        self.assertEqual(p.needed_devices(), {"key", "mouse", "pad"})

    def test_errors_are_explained(self):
        bad = [
            {"buttons": {"cross": "key:NAOEXISTE"}},
            {"buttons": {"botao": "key:A"}},
            {"buttons": {"cross": "joy:A"}},
            {"buttons": {"cross": "mouse:A"}},
            {"sticks": {"left": "pad:meio"}},
            {"sticks": {"left": "keys:W,A"}},
            {"sticks": {"deadzone": 2}},
            {"touch": {"front": "absoluto"}},
            {"extra": {}},
        ]
        for data in bad:
            with self.subTest(data=data), self.assertRaises(ProfileError):
                profile_from_dict(data)


class MapperTest(unittest.TestCase):
    def test_button_press_and_release_emitted_once(self):
        m, rec = make({"buttons": {"cross": "pad:A"}})
        down = VitaState(buttons=BUTTONS["cross"])
        m.update(down, DT)
        m.update(down, DT)
        self.assertEqual(rec.take(), [("btn", "pad", ecodes.BTN_SOUTH, True)])
        m.update(VitaState(), DT)
        self.assertEqual(rec.take(), [("btn", "pad", ecodes.BTN_SOUTH, False)])

    def test_combo_presses_modifier_first(self):
        m, rec = make({"buttons": {"cross": "key:LEFTCTRL+C"}})
        m.update(VitaState(buttons=BUTTONS["cross"]), DT)
        self.assertEqual([e[2] for e in rec.take()], [ecodes.KEY_LEFTCTRL, ecodes.KEY_C])

    def test_shared_target_stays_down_while_any_source_held(self):
        m, rec = make({"buttons": {"cross": "key:SPACE", "circle": "key:SPACE"}})
        m.update(VitaState(buttons=BUTTONS["cross"] | BUTTONS["circle"]), DT)
        m.update(VitaState(buttons=BUTTONS["circle"]), DT)
        self.assertEqual(rec.take(), [("btn", "key", ecodes.KEY_SPACE, True)])

    def test_dpad_to_hat_and_triggers(self):
        m, rec = make({"buttons": {"up": "pad:HAT_UP", "right": "pad:HAT_RIGHT", "down": "pad:HAT_DOWN",
                                   "left": "pad:HAT_LEFT", "rear_left": "pad:LT"}})
        m.update(VitaState(buttons=BUTTONS["up"] | BUTTONS["right"], rear_left=True), DT)
        ev = rec.take()
        self.assertIn(("axis", "ABS_HAT0X", 1), ev)
        self.assertIn(("axis", "ABS_HAT0Y", -1), ev)
        self.assertIn(("axis", "ABS_Z", 255), ev)

    def test_stick_deadzone_and_full_range(self):
        self.assertEqual(normalize_stick(130, 126, 0.12), (0.0, 0.0))
        x, y = normalize_stick(255, 128, 0.12)
        self.assertAlmostEqual(x, 1.0)
        m, rec = make({"sticks": {"left": "pad:left"}})
        m.update(VitaState(lx=0, ly=128), DT)
        self.assertIn(("axis", "ABS_X", -32767), rec.take())

    def test_stick_as_keys(self):
        m, rec = make({"sticks": {"left": "keys:W,A,S,D"}})
        m.update(VitaState(lx=128, ly=0), DT)
        self.assertEqual(rec.take(), [("btn", "key", ecodes.KEY_W, True)])

    def test_stick_as_mouse_moves_cursor(self):
        m, rec = make({"sticks": {"left": "mouse"}, "mouse": {"stick_speed": 1200}})
        for _ in range(12):
            m.update(VitaState(lx=255, ly=128), DT)
        dx = sum(e[1] for e in rec.take() if e[0] == "move")
        self.assertAlmostEqual(dx, 1200 * 12 * DT, delta=2)

    def test_stick_as_scroll_up_is_positive(self):
        m, rec = make({"sticks": {"right": "scroll"}, "mouse": {"scroll_speed": 60}})
        for _ in range(10):
            m.update(VitaState(rx=128, ry=0), DT)
        self.assertGreater(sum(e[1] for e in rec.take() if e[0] == "scroll"), 0)

    def test_touchpad_moves_relative_without_jump_on_first_touch(self):
        m, rec = make({"touch": {"front": "touchpad", "sensitivity": 1.0}})
        m.update(VitaState(front_touch=True, touch_x=1500, touch_y=900), DT)
        self.assertEqual(rec.take(), [])  # primeiro contato não move
        m.update(VitaState(front_touch=True, touch_x=1510, touch_y=895), DT)
        self.assertEqual(rec.take(), [("move", 10, -5)])

    def test_tap_clicks_but_drag_does_not(self):
        m, rec = make({"touch": {"front": "touchpad", "tap": "mouse:LEFT"}})
        m.update(VitaState(front_touch=True, touch_x=500, touch_y=500), DT)
        m.update(VitaState(), DT)
        self.assertEqual(rec.take(), [("btn", "mouse", ecodes.BTN_LEFT, True)])
        m.update(VitaState(), DT)
        self.assertEqual(rec.take(), [("btn", "mouse", ecodes.BTN_LEFT, False)])

        for x in range(500, 900, 40):  # arrasto longo
            m.update(VitaState(front_touch=True, touch_x=x, touch_y=500), DT)
        m.update(VitaState(), DT)
        m.update(VitaState(), DT)
        self.assertFalse([e for e in rec.take() if e[0] == "btn"])

    def test_release_all_on_disconnect(self):
        m, rec = make({"buttons": {"cross": "key:A", "rear_right": "pad:RT"},
                       "sticks": {"left": "pad:left"}})
        m.update(VitaState(buttons=BUTTONS["cross"], rear_right=True, lx=255), DT)
        rec.take()
        m.release_all()
        ev = rec.take()
        self.assertIn(("btn", "key", ecodes.KEY_A, False), ev)
        self.assertIn(("axis", "ABS_RZ", 0), ev)
        self.assertIn(("axis", "ABS_X", 0), ev)
        # Depois de soltar, apertar de novo volta a emitir.
        m.update(VitaState(buttons=BUTTONS["cross"]), DT)
        self.assertIn(("btn", "key", ecodes.KEY_A, True), rec.take())


if __name__ == "__main__":
    unittest.main()
