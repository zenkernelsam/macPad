import unittest
import qoder_iterm_relay as relay


class Screen:
    def __init__(self, text):
        self.lines = text.splitlines()
        self.number_of_lines = len(self.lines)

    def line(self, i):
        class Line:
            pass
        result = Line()
        result.string = self.lines[i]
        return result


class RelayTests(unittest.TestCase):
    def test_fixed_reminder_ignores_historical_thinking_only(self):
        text = 'Thinking\nCompleted background notification\nType your message or @path/to/file'
        self.assertTrue(relay.reminder_ready(text, 'macPad | Ready (qoderclicn)'))
        for busy in ['Generating... (esc to cancel)', '└ Running…', 'Queued (press ↑ to edit):']:
            self.assertFalse(relay.reminder_ready(text + '\n' + busy, 'macPad | Ready (qoderclicn)'))
        self.assertFalse(relay.reminder_ready(text, 'macPad | Working (qoderclicn)'))
        self.assertFalse(relay.idle(Screen(text), 'macPad | Ready (qoderclicn)'))

    def test_input_prompt_does_not_override_busy_title(self):
        self.assertFalse(relay.idle(Screen('Type your message or @path/to/file'),
                                    'macPad | Working (qoderclicn)'))

    def test_running_with_stale_handover_is_not_ready(self):
        for busy in ('Generating... (esc to cancel)', '└ Running… (19s)',
                     'Thinking... (esc to cancel)', 'Queued (press ↑ to edit):'):
            with self.subTest(busy=busy):
                self.assertFalse(relay.idle(Screen(
                    '(handover to codex now)\n' + busy +
                    '\nType your message or @path/to/file'), 'macPad | Ready'))

    def test_ready_handover_and_initial_idle(self):
        self.assertTrue(relay.idle(Screen('(handover to codex now)\n'
                                         'Type your message or @path/to/file'),
                                   'macPad | Ready'))
        self.assertTrue(relay.idle(Screen('Type your message or @path/to/file'),
                                   'macPad | Ready'))

    def test_softwrap_marker(self):
        self.assertTrue(relay.marker_visible('[macPad\x00qoder\n relay abc-123]',
                                             '[macPad qoder relay abc-123]'))
        self.assertFalse(relay.marker_visible('[macPad qoder relay abc-124]',
                                              '[macPad qoder relay abc-123]'))


if __name__ == '__main__':
    unittest.main()
