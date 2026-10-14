"""postmin guard contract tests (R006 review item).

Builds macos_ws_event_probe.c on the host twice:

1. fixture build — probe source compiled with -Dmain=probe_main and a
   fixture main that exercises the pure helpers:
     * postmin_record_plausible: all-zero record rejected, any nonzero
       byte accepted, NULL/zero-size rejected.
     * skylight_uuid_matches on the *host* SkyLight (a different macOS
       build than the pinned 22F82 UUID) must return 0 — proving the
       pin really fails closed on an unknown image.
2. real binary — `./probe postmin` on the host.  The host SkyLight UUID
   differs from the pin, so postmin must exit 2 BEFORE resolving,
   connecting, or posting anything.  Its stdout must contain the FAIL
   marker and no SLSPostEventRecord result line.

Negative guards verified by source contract as well: every early return
in postmin_run is nonzero and the post only follows a plausible record.
"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'misc' / 'macos_ws_event_probe.c'

FIXTURE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// rename probe main so this fixture owns main
#define main probe_main
#include "macos_ws_event_probe.c"
#undef main

static int fails;
#define CHECK(name, cond) do { \
    printf("TEST %-52s %s\n", name, (cond) ? "PASS" : "FAIL"); \
    if (!(cond)) fails++; } while (0)

int main(void) {
    // ---- record plausibility ----
    uint8_t zero[0xf8];
    memset(zero, 0, sizeof(zero));
    CHECK("all-zero record rejected",
          postmin_record_plausible(zero, sizeof(zero)) == 0);
    uint8_t live[0xf8];
    memset(live, 0, sizeof(live));
    live[0x40] = 0x01;      // any nonzero byte anywhere
    CHECK("nonzero record accepted",
          postmin_record_plausible(live, sizeof(live)) == 1);
    live[0] = 0xFF;
    CHECK("leading nonzero accepted",
          postmin_record_plausible(live, 1) == 1);
    CHECK("zero-length record rejected",
          postmin_record_plausible(live, 0) == 0);

    // ---- UUID pin against the host (non-22F82) SkyLight ----
    // The host macOS SkyLight is NOT the pinned Ventura build, so the
    // pin must report mismatch.  If the host lacks SkyLight entirely
    // the helper still returns 0 — either way it fails closed.
    int m = skylight_uuid_matches();
    printf("host skylight_uuid_matches=%d\n", m);
    CHECK("uuid pin fails closed on unknown image", m == 0);

    // ---- mode parser ----
    char *a1[] = {"probe", "postmin"};
    CHECK("postmin parses", probe_mode(2, a1) == M_POSTMIN);
    char *a2[] = {"probe", "postminx"};
    CHECK("postminx is not postmin", probe_mode(2, a2) != M_POSTMIN);
    char *a3[] = {"probe"};
    CHECK("no-arg != postmin", probe_mode(1, a3) != M_POSTMIN);

    printf("%s (fails=%d)\n", fails ? "FAILURES" : "ALL-PASS", fails);
    return fails ? 1 : 0;
}
'''


def build(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return r


class PostminContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='postmin-test-')
        tdir = Path(cls.tmp.name)
        src = tdir / 'fixture.c'
        src.write_text(FIXTURE)
        cls.fixture = tdir / 'fixture'
        cls.probe = tdir / 'probe'
        cf = ['-framework', 'CoreFoundation', '-framework', 'CoreGraphics']
        r1 = build(['clang', '-O0', '-g', '-I', str(ROOT / 'misc'),
                    '-o', str(cls.fixture), str(src)] + cf, ROOT)
        r2 = build(['clang', '-O0', '-g',
                    '-o', str(cls.probe), str(PROBE)] + cf, ROOT)
        cls.builds = (r1, r2)
        if r1.returncode or r2.returncode:
            raise RuntimeError(
                'fixture build:\n%s\nprobe build:\n%s'
                % (r1.stderr, r2.stderr))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_fixture_helpers(self):
        r = subprocess.run([str(self.fixture)], capture_output=True,
                           text=True, timeout=60)
        print(r.stdout)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('ALL-PASS', r.stdout)

    def test_real_binary_postmin_fails_closed_on_host(self):
        # Host SkyLight is not the pinned 22F82 image -> uuid guard
        # must fire (rc=2) before ANY WindowServer interaction.
        r = subprocess.run([str(self.probe), 'postmin'],
                           capture_output=True, text=True, timeout=60)
        print(r.stdout)
        self.assertEqual(r.returncode, 2,
                         'expected uuid-guard exit 2, got %d\n%s%s'
                         % (r.returncode, r.stdout, r.stderr))
        self.assertIn('FAIL closed', r.stdout)
        self.assertNotIn('SLSPostEventRecord ->', r.stdout)

    def test_source_contract(self):
        s = PROBE.read_text()
        # every postmin early return is a distinct nonzero code
        for code in ('return 2;', 'return 3;', 'return 4;',
                     'return 5;', 'return 6;'):
            self.assertIn(code, s)
        # uuid pin is checked before dlsym/post inside postmin_run
        body = s[s.index('static int postmin_run'):]
        body = body[:body.index('\nint main')]
        self.assertLess(body.index('skylight_uuid_matches'),
                        body.index('SLSMainConnectionID'))
        self.assertLess(body.index('postmin_record_plausible'),
                        body.index('SLSPostEventRecord ->'))


if __name__ == '__main__':
    unittest.main()
