"""Unit test for check-deps.py's redirect handling (issue #39). Offline.

    python3 tools/test_check_deps.py
"""

import importlib.util
import io
import pathlib
import unittest
import urllib.request

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


if __name__ == "__main__":
    unittest.main()
