"""Handover gating only; these tests never connect to or type in a terminal."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('relay', Path(__file__).with_name('macpad_devin_relay.py'))
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)


class HandoverGate(unittest.TestCase):
    def test_explicit_handover_then_idle_is_ready(self):
        self.assertTrue(relay.handover_ready('Done.\n(handover to codex now)\n❭ Ask Devin to build\nSWE-2'))
        self.assertTrue(relay.handover_ready('(handover to codex)\n❭ Ask Devin'))

    def test_idle_fresh_cli_or_quoted_marker_is_not_handover(self):
        for screen in ['❭ Ask Devin', 'Devin CLI\n❭ Ask Devin',
                       '│ (handover to codex now)\n❭ Ask Devin',
                       '(handover to codex now)\nDevin CLI\n❭ Ask Devin']:
            self.assertFalse(relay.handover_ready(screen))

    def test_busy_queue_menu_or_draft_prevents_send(self):
        ready = '(handover to codex now)\n❭ Ask Devin\n'
        for status in ['Guide Devin while it works', 'Thinking (esc twice to interrupt)',
                       'Press Enter to send queued messages now', '↵ accept · esc close',
                       '[Pasted text #1]']:
            self.assertFalse(relay.handover_ready(ready + status))

    def test_already_consumed_handover_is_not_reused(self):
        self.assertFalse(relay.handover_ready(
            '(handover to codex now)\n❭ Read task [macPad relay abc]\n❭ Ask Devin'))

    def test_exact_delivery_marker_survives_soft_wrap(self):
        marker = '[macPad relay abcdef123456-7890abcd]'
        self.assertTrue(relay.marker_visible('❭ [macPad\x00relay abcdef1234\n  56-7890abcd]', marker))
        self.assertFalse(relay.marker_visible('[macPad relay abcdef123456-7890abce]', marker))

    def test_marker_after_prompt_is_not_completed_handover(self):
        self.assertFalse(relay.handover_ready('❭ Ask Devin\n(handover to codex now)'))


if __name__ == '__main__':
    unittest.main()
