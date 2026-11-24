"""Testes do protocolo v2 e da integração com o Vita falso."""

import sys
import threading
import time
import unittest
from pathlib import Path

from evdev import ecodes

from vitaremote import net_v2
from vitaremote import protocol_v2 as p
from vitaremote.__main__ import V2Controller
from vitaremote.profile import ProfileError, load_profile_set, profile_from_dict, profile_set_from_dict
from vitaremote.protocol_v1 import BUTTONS, VitaState

from .test_client import DT, Recorder, make

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import mock_vita_v2  # noqa: E402


class ProtocolV2Test(unittest.TestCase):
    def test_sizes_match_c_header(self):
        self.assertEqual(p.STATE_SIZE, 31)
        self.assertEqual(len(p.hello(120, 40000)), 2 + 8)
        self.assertEqual(len(p.beacon()), 7)

    def test_state_roundtrip_with_two_fingers_and_rear(self):
        s = VitaState(buttons=BUTTONS["cross"], lx=1, ly=2, rx=3, ry=4, mode=p.MODE_MOUSE,
                      front_points=((100, 200), (300, 400)))
        seq, got = p.parse_state(p.pack_state(65535, s, rear=((1500, 500),)))
        self.assertEqual(seq, 65535)
        self.assertEqual(got.front_points, ((100, 200), (300, 400)))
        self.assertEqual((got.touch_x, got.touch_y, got.finger_count), (100, 200, 2))
        self.assertTrue(got.rear_right and not got.rear_left)
        self.assertEqual(got.mode, p.MODE_MOUSE)

    def test_scroll_zone_flag_roundtrip(self):
        s = VitaState(front_touch=True, touch_x=960, touch_y=500, front_points=((960, 500),), scroll_zone=True)
        self.assertTrue(p.parse_state(p.pack_state(1, s))[1].scroll_zone)
        self.assertFalse(p.parse_state(p.pack_state(1, VitaState()))[1].scroll_zone)

    def test_frame_reader_handles_partial_and_joined_frames(self):
        data = p.frame(p.MSG_MODE, b"\x01") + p.frame(p.MSG_PONG) + p.frame(p.MSG_KEY, b"\x1e\x00\x01")
        r = p.FrameReader()
        msgs = r.feed(data[:4]) + r.feed(data[4:6]) + r.feed(data[6:])
        self.assertEqual([m.type for m in msgs], [p.MSG_MODE, p.MSG_PONG, p.MSG_KEY])
        self.assertEqual(p.parse_key(msgs[2].payload), (ecodes.KEY_A, True))

    def test_sequence_wraparound(self):
        self.assertTrue(p.is_newer(0, 65535))
        self.assertTrue(p.is_newer(5, None))
        self.assertFalse(p.is_newer(10, 10))
        self.assertFalse(p.is_newer(65530, 3))

    def test_beacon_and_welcome_validation(self):
        self.assertEqual(p.parse_beacon(p.beacon(5001)), 5001)
        self.assertIsNone(p.parse_beacon(b"XXXX\x02\x89\x13"))
        with self.assertRaises(p.ProtocolError):
            p.parse_welcome(p._WELCOME.pack(p.MAGIC, 9, 0))


class TwoFingerTest(unittest.TestCase):
    def two(self, y, x=800):
        return VitaState(front_touch=True, touch_x=x, touch_y=y, front_points=((x, y), (x + 200, y)))

    def test_two_finger_drag_scrolls_without_moving_cursor(self):
        m, rec = make({"touch": {"front": "touchpad", "two_finger_scroll": True, "scroll_sensitivity": 0.05}})
        for y in range(300, 700, 20):
            m.update(self.two(y), DT)
        ev = rec.take()
        self.assertFalse([e for e in ev if e[0] == "move"])
        self.assertGreater(sum(e[1] for e in ev if e[0] == "scroll"), 0)  # dedos descem = rola p/ cima

    def test_two_finger_tap_is_right_click(self):
        m, rec = make({"touch": {"front": "touchpad", "tap": "mouse:LEFT", "two_finger_tap": "mouse:RIGHT"}})
        m.update(self.two(500), DT)
        m.update(VitaState(), DT)
        self.assertEqual(rec.take(), [("btn", "mouse", ecodes.BTN_RIGHT, True)])

    def test_second_finger_arriving_does_not_jump_cursor(self):
        m, rec = make({"touch": {"front": "touchpad", "sensitivity": 1.0}})
        m.update(VitaState(front_touch=True, touch_x=500, touch_y=500, front_points=((500, 500),)), DT)
        # 2º dedo chega e o Vita passa a listar ele primeiro: não pode virar um salto de 1000 px
        m.update(VitaState(front_touch=True, touch_x=1500, touch_y=500,
                           front_points=((1500, 500), (500, 500))), DT)
        m.update(VitaState(front_touch=True, touch_x=1500, touch_y=500, front_points=((1500, 500),)), DT)
        self.assertFalse([e for e in rec.take() if e[0] == "move"])


class ScrollZoneTest(unittest.TestCase):
    def zone(self, y):
        return VitaState(front_touch=True, touch_x=960, touch_y=y, front_points=((960, y),), scroll_zone=True)

    def test_scroll_zone_scrolls_and_never_moves_or_clicks(self):
        m, rec = make({"touch": {"front": "touchpad", "tap": "mouse:LEFT", "scroll_sensitivity": 0.05}})
        for y in range(300, 700, 20):
            m.update(self.zone(y), DT)
        m.update(VitaState(), DT)
        m.update(VitaState(), DT)
        ev = rec.take()
        self.assertFalse([e for e in ev if e[0] in ("move", "btn")])
        self.assertGreater(sum(e[1] for e in ev if e[0] == "scroll"), 0)

    def test_quick_tap_in_scroll_zone_is_not_a_click(self):
        m, rec = make({"touch": {"front": "touchpad", "tap": "mouse:LEFT"}})
        m.update(self.zone(500), DT)
        m.update(VitaState(), DT)
        m.update(VitaState(), DT)
        self.assertEqual(rec.take(), [])

    def test_keyboard_mode_has_no_button_mappings(self):
        # No modo teclado o Vita trata os botões; o PC não deve mapear nada para eles.
        self.assertFalse(any(load_profile_set("padrao").for_mode("keyboard").buttons.values()))


class ProfileSetTest(unittest.TestCase):
    def test_default_profile_has_three_modes(self):
        ps = load_profile_set("padrao")
        self.assertEqual(ps.for_mode("gamepad").buttons["cross"].device, "pad")
        self.assertEqual(ps.for_mode("mouse").buttons["cross"].codes, (ecodes.BTN_LEFT,))
        self.assertTrue(ps.for_mode("mouse").two_finger_scroll)
        self.assertIsNone(ps.for_mode("mouse").buttons["rear_left"])

    def test_single_profile_applies_to_all_modes(self):
        ps = profile_set_from_dict({"buttons": {"cross": "key:A"}})
        self.assertIs(ps.for_mode("gamepad"), ps.for_mode("keyboard"))

    def test_error_names_the_mode(self):
        with self.assertRaisesRegex(ProfileError, r"\[mouse\]"):
            profile_set_from_dict({"mouse": {"buttons": {"cross": "key:NADA"}}})
        with self.assertRaises(ProfileError):
            profile_set_from_dict({"gamepad": {}, "joystick": {}})

    def test_cursor_section_is_not_confused_with_mouse_mode(self):
        p_ = profile_from_dict({"cursor": {"stick_speed": 500}})
        self.assertEqual(p_.stick_mouse_speed, 500)


class ControllerTest(unittest.TestCase):
    def test_mode_switch_releases_previous_mode(self):
        rec = Recorder()
        ctl = V2Controller(load_profile_set("padrao"), rec)
        ctl.on_state(VitaState(buttons=BUTTONS["cross"], mode=p.MODE_GAMEPAD), DT)
        rec.take()
        ctl.on_state(VitaState(buttons=BUTTONS["cross"], mode=p.MODE_MOUSE), DT)
        ev = rec.take()
        self.assertIn(("btn", "pad", ecodes.BTN_SOUTH, False), ev)
        self.assertIn(("btn", "mouse", ecodes.BTN_LEFT, True), ev)

    def test_virtual_keyboard_keys_released_on_disconnect(self):
        rec = Recorder()
        ctl = V2Controller(load_profile_set("padrao"), rec)
        ctl.on_key(ecodes.KEY_LEFTSHIFT, True)
        ctl.on_key(ecodes.KEY_LEFTSHIFT, True)  # repetido: ignora
        ctl.on_key(9999, True)                  # inválido: ignora
        self.assertEqual(rec.take(), [("btn", "key", ecodes.KEY_LEFTSHIFT, True)])
        ctl.on_disconnect()
        self.assertIn(("btn", "key", ecodes.KEY_LEFTSHIFT, False), rec.take())


class IntegrationTest(unittest.TestCase):
    """Cliente real (rede de verdade em 127.0.0.1) contra o Vita falso."""

    PORT = 5071

    def test_session_receives_state_modes_and_keys(self):
        stop, ready = threading.Event(), threading.Event()
        server = threading.Thread(target=mock_vita_v2.serve, daemon=True,
                                  kwargs=dict(port=self.PORT, stop=stop, beacon=False, ready=ready))
        server.start()
        ready.wait(2)

        states, keys, modes, disconnects = [], [], [], []
        started = time.monotonic()
        handlers = net_v2.Handlers(
            on_state=lambda s, dt: states.append(s),
            on_key=lambda c, pr: keys.append((c, pr)),
            on_mode=modes.append,
            on_disconnect=lambda: disconnects.append(1),
        )
        net_v2.run("127.0.0.1", self.PORT, 120, handlers,
                   should_stop=lambda: time.monotonic() - started > 7.2)
        stop.set()
        server.join(2)

        self.assertGreater(len(states), 300)
        self.assertEqual(modes[:3], [p.MODE_GAMEPAD, p.MODE_MOUSE, p.MODE_KEYBOARD])
        self.assertTrue(any(s.scroll_zone for s in states))
        self.assertIn((ecodes.KEY_O, True), keys)
        self.assertEqual(disconnects, [1])


if __name__ == "__main__":
    unittest.main()
