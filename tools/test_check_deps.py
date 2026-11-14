"""Unit tests for check-deps.py: redirect handling (issue #39) and
upstream_acknowledged (issue #45). Offline.

    python3 tools/test_check_deps.py
"""

import contextlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
import urllib.request
from unittest import mock

_spec = importlib.util.spec_from_file_location("check_deps", pathlib.Path(__file__).with_name("check-deps.py"))
check_deps = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check_deps)


class RedirectCredentials(unittest.TestCase):
    def follow(self, src, dst):
        req = urllib.request.Request(src, headers={"apiKey": "DUMMY", "Authorization": "token DUMMY", "Accept": "application/json"})
        new = check_deps._StripAuthOnCrossHostRedirect().redirect_request(req, io.BytesIO(), 302, "Found", {}, dst)
        return {k.lower() for k in list(new.headers) + list(new.unredirected_hdrs)}

    def test_cross_host_redirect_drops_credentials(self):
        headers = self.follow("https://services.nvd.nist.gov/rest/json/cves/2.0", "https://elsewhere.example/x")
        self.assertNotIn("apikey", headers)
        self.assertNotIn("authorization", headers)
        self.assertIn("accept", headers)

    def test_same_host_redirect_keeps_credentials(self):
        headers = self.follow("https://services.nvd.nist.gov/rest/json/cves/2.0", "https://services.nvd.nist.gov/other")
        self.assertIn("apikey", headers)
        self.assertIn("authorization", headers)


class UpstreamAcknowledged(unittest.TestCase):
    """upstream_acknowledged silences exactly the listed version (issue #45)."""

    COMPONENT = {
        "name": "zlib", "version": "1.3.2", "kind": "toolchain-port",
        "watch": {"rule": "github-release", "repo": "madler/zlib"},
        "security_critical": True,
        "upstream_acknowledged": {"1.4": "not takeable, not reachable"},
    }

    def run_updates(self, latest, component=None):
        with tempfile.TemporaryDirectory() as d:
            manifest = pathlib.Path(d, "deps.json")
            manifest.write_text(json.dumps({"components": [component or self.COMPONENT]}), encoding="utf-8")
            stdout = io.StringIO()
            with mock.patch.object(check_deps, "MANIFEST", manifest), \
                 mock.patch.object(check_deps, "latest_upstream",
                                   lambda watch: {"kind": "version", "latest": latest, "source": "test"}), \
                 mock.patch.object(sys, "argv", ["check-deps.py", "--updates", "--json"]), \
                 contextlib.redirect_stdout(stdout):
                status = check_deps.main()
        return status, json.loads(stdout.getvalue())

    def test_acknowledged_version_is_not_a_finding(self):
        status, report = self.run_updates("1.4")
        self.assertEqual(status, 0)
        self.assertEqual(report["problems"], [])
        rec = report["components"][0]
        self.assertTrue(rec["behind"])
        self.assertTrue(rec["acknowledged"])

    def test_next_release_is_a_finding_again(self):
        status, report = self.run_updates("1.4.1")
        self.assertEqual(status, 1)
        self.assertEqual(report["problems"], ["zlib: 1.3.2 -> 1.4.1 available [security-critical]"])
        self.assertFalse(report["components"][0]["acknowledged"])

    def test_stale_entry_is_reported(self):
        caught_up = dict(self.COMPONENT, version="1.4")
        self.assertEqual(check_deps.stale_upstream_acks(caught_up), ["1.4"])
        self.assertEqual(check_deps.stale_upstream_acks(self.COMPONENT), [])


if __name__ == "__main__":
    unittest.main()
