"""Transport fixtures: fake password and host; no network is used."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, MagicMock

spec = importlib.util.spec_from_file_location('macpad_ssh', Path(__file__).with_name('macpad_ssh.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class CommandContract(unittest.TestCase):
    def invoke(self, arguments, rc=0):
        process = MagicMock(returncode=rc)
        process.communicate.return_value = ('', '')
        env = {'MACWS_DEVICE': 'root@device.invalid', 'MACWS_SSH_PASSWORD': 'samplepass'}
        with patch.dict(os.environ, env, clear=True), patch('sys.argv', ['macpad_ssh.py'] + arguments), \
             patch.object(runner.subprocess, 'Popen', return_value=process) as start, \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = runner.main()
        return result, start

    def test_upload_path_containing_password_substring_is_not_a_secret_argument(self):
        archive = Path(__file__).parent / '.test-artifacts'
        archive.mkdir(exist_ok=True)
        # Repository policy keeps generated fixtures recoverable.
        directory = tempfile.mkdtemp(prefix='samplepass_user_', dir=archive)
        source = Path(directory) / 'probe'
        source.write_bytes(b'fixture')
        rc, start = self.invoke(['--upload', str(source), '--remote-path', '/tmp/probe.new'])
        self.assertEqual(rc, 0)
        argv = start.call_args.args[0]
        self.assertIn(str(source.resolve()), argv)
        self.assertNotIn('samplepass', argv)
        self.assertIn('PreferredAuthentications=password', argv)
        self.assertEqual(start.call_args.kwargs['env']['SSHPASS'], 'samplepass')

    def test_real_remote_failure_is_preserved_and_similar_path_is_allowed(self):
        rc, _ = self.invoke(['--command', 'test -f /Users/samplepass_user/probe'], rc=7)
        self.assertEqual(rc, 7)

    def test_literal_secret_in_command_is_rejected(self):
        with self.assertRaises(SystemExit) as error:
            self.invoke(['--command', 'echo samplepass'])
        self.assertEqual(error.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
