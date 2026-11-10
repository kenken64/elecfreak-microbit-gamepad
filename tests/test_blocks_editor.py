"""HTTP integration tests for hosted auth, volume persistence and user isolation."""
import contextlib
import http.client
import http.server
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import blocks_editor as editor
from marketplace_auth import COOKIE_NAME, MarketplaceAuth, TOOL_ID


class QuietHandler(editor.Handler):
    def log_request(self, code="-", size="-"):
        pass


class HostedEditorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.project = self.root / "app"
        self.volume = self.root / "volume"
        self.project.mkdir()
        self.active = True
        self.verifications = []
        owner = self

        class Verifier(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                owner.assertEqual(self.headers["Authorization"], "Bearer " + "v" * 32)
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                owner.verifications.append(body)
                self.send_response(200 if owner.active else 403)
                self.end_headers()
                self.wfile.write(json.dumps({"active": owner.active}).encode())

            def log_message(self, *args):
                pass

        verifier = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Verifier)
        thread = threading.Thread(target=verifier.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(thread.join, 5)
        self.addCleanup(verifier.server_close)
        self.addCleanup(verifier.shutdown)
        self.auth = MarketplaceAuth("s" * 32, "http://127.0.0.1:%d/verify" % verifier.server_port, "v" * 32)
        firmware = self.project / ".pio" / "build" / "microbit_v1" / "firmware.hex"
        self.cpp = self.project / "src" / "v1" / "main.cpp"
        self.default_cpp = editor.DEFAULT_CPP
        editor.write_text(self.cpp, self.default_cpp)
        self.fake_pio = self.project / "pio"
        self.fake_pio.write_text(
            "#!%s\n" % sys.executable +
            "import pathlib, sys\n"
            "root = pathlib.Path.cwd()\n"
            "out = root / '.pio/build/microbit_v1/firmware.hex'\n"
            "out.parent.mkdir(parents=True, exist_ok=True)\n"
            "out.write_bytes((root / 'src/v1/main.cpp').read_bytes())\n"
            "print('Compiled test firmware', flush=True)\n"
            "sys.exit(1 if (root / 'fail-build').exists() else 0)\n",
            encoding="utf-8")
        self.fake_pio.chmod(0o755)
        patcher = mock.patch.multiple(editor, ROOT=self.project, MAIN_CPP=self.cpp,
                                      PROGRAM=self.project / "blocks/gamepad.json",
                                      FIRMWARE_HEX=firmware, DATA_DIR=self.volume, AUTH=self.auth)
        patcher.start()
        self.addCleanup(patcher.stop)
        pio_patch = mock.patch.object(editor, "find_pio", return_value=str(self.fake_pio))
        pio_patch.start()
        self.addCleanup(pio_patch.stop)
        self.start_server()
        self.addCleanup(self.stop_server)

    def start_server(self):
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), QuietHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def stop_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(5)

    def claims(self, user="alice", **changes):
        now = int(time.time())
        claims = dict(iss="2ndBrain.ceo", tool_id=TOOL_ID, user_id=user,
                      install_id="install-" + user, iat=now - 5, exp=now + 300)
        claims.update(changes)
        return claims

    def request(self, path, method="GET", body=None, cookie=None, headers=None):
        values = {"Host": "gamepad.example.test"}
        if cookie:
            values["Cookie"] = cookie
        if method == "POST":
            values.update({"Content-Type": "application/json", "X-GP-Token": editor.TOKEN})
        values.update(headers or {})
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
        try:
            connection.request(method, path, json.dumps(body) if body is not None else None, values)
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def login(self, user="alice"):
        token = self.auth.sign(self.claims(user))
        status, headers, body = self.request("/api/2ndbrain/launch?launch_token=" + token)
        self.assertEqual(status, 303, body)
        self.assertEqual(headers["Location"], "/")
        for flag in ("HttpOnly", "Secure", "SameSite=Lax"):
            self.assertIn(flag, headers["Set-Cookie"])
        return headers["Set-Cookie"].split(";", 1)[0]

    def save(self, cookie, name):
        code = self.default_cpp + "\n// " + name + "\n"
        status, _, body = self.request("/api/save", "POST", {"program": {"name": name}, "code": code}, cookie)
        self.assertEqual(status, 200, body)
        return code.encode()

    def build(self, cookie, expected=True):
        status, _, body = self.request("/api/build", "POST", {}, cookie)
        self.assertEqual(status, 200, body)
        self.assertIn((editor.RESULT_MARK + json.dumps({"ok": expected})).encode(), body)

    def test_guard_protects_all_routes_and_health_is_public(self):
        for path in ("/", "/gp_blocks.js", "/solution.html", "/api/program", "/api/firmware.hex"):
            self.assertEqual(self.request(path)[0], 401, path)
        for path in ("/api/save", "/api/build"):
            self.assertEqual(self.request(path, "POST", {})[0], 401, path)
        self.assertEqual(self.request("/healthz", headers={"Host": "healthcheck.railway.app"})[0], 200)

    def test_launch_and_logout_revocation_preserve_original_timestamp(self):
        cookie = self.login()
        issued_at = self.verifications[-1]["issuedAt"]
        self.assertEqual(self.request("/", cookie=cookie)[0], 200)
        self.assertEqual(self.verifications[-1]["issuedAt"], issued_at)
        self.active = False
        self.assertEqual(self.request("/api/program", cookie=cookie)[0], 403)
        self.assertEqual(self.request("/api/build", "POST", {}, cookie)[0], 403)

    def test_verifier_outage_denies_access(self):
        cookie = self.login()
        with mock.patch("marketplace_auth.urllib.request.urlopen", side_effect=OSError("offline")):
            self.assertEqual(self.request("/api/program", cookie=cookie)[0], 503)

    def test_verifier_connection_error_identifies_hostname_without_leaking_details(self):
        self.auth.verify_url = "https://missing.example.invalid/api/marketplace/launch-session/verify"
        token = self.auth.sign(self.claims())
        with mock.patch("marketplace_auth.urllib.request.urlopen", side_effect=urllib.error.URLError("private transport details")):
            status, _, body = self.request("/api/2ndbrain/launch?launch_token=" + token)
        self.assertEqual(status, 503)
        self.assertIn(b"Cannot reach 2ndBrain at missing.example.invalid", body)
        self.assertNotIn(b"private transport details", body)
        self.assertNotIn(token.encode(), body)

    def test_launch_required_link_uses_the_configured_2ndbrain_origin(self):
        self.auth.verify_url = "https://workspace.example.test/api/marketplace/launch-session/verify"
        status, _, body = self.request("/")
        self.assertEqual(status, 401)
        self.assertIn(b'href="https://workspace.example.test/dashboard/workflows"', body)
        self.assertNotIn(b"https://2ndbrain.ceo/", body)

    def test_invalid_tokens_cannot_launch_or_impersonate_a_user(self):
        for changes in ({"exp": int(time.time()) - 1}, {"iat": int(time.time()) + 100},
                        {"iss": "other"}, {"tool_id": "other"}, {"user_id": "../bob"},
                        {"install_id": ""}, {"exp": True}, {"session": True}):
            token = self.auth.sign(self.claims(**changes))
            self.assertEqual(self.request("/api/2ndbrain/launch?launch_token=" + token)[0], 401, changes)
        token = self.auth.sign(self.claims())
        header, payload, signature = token.split(".")
        forged = header + "." + payload + "." + ("A" if signature[0] != "A" else "B") + signature[1:]
        self.assertEqual(self.request("/api/2ndbrain/launch?launch_token=" + forged)[0], 401)
        self.assertEqual(self.request("/api/program", cookie=COOKIE_NAME + "=" + token)[0], 401)

    def test_saved_hex_survives_restart_and_failed_builds(self):
        cookie = self.login()
        first = self.save(cookie, "first")
        self.build(cookie)
        artifact = self.volume / "users/alice/firmware/microbit-gamepad.hex"
        self.assertEqual(artifact.read_bytes(), first)
        self.assertEqual(self.request("/api/firmware.hex", cookie=cookie)[2], first)
        self.stop_server()
        # Simulate a fresh deploy replacing all compiler intermediates and generated source.
        editor.FIRMWARE_HEX.unlink()
        editor.write_text(self.cpp, "new image source")
        self.start_server()
        self.assertEqual(self.request("/api/firmware.hex", cookie=cookie)[2], first)
        saved = json.loads(self.request("/api/program", cookie=cookie)[2])
        self.assertEqual(saved, {"program": {"name": "first"}, "hasFirmware": True})
        self.save(cookie, "second")
        (self.project / "fail-build").touch()
        self.build(cookie, expected=False)
        self.assertEqual(artifact.read_bytes(), first)
        (self.project / "fail-build").unlink()
        self.build(cookie)
        self.assertIn(b"// second", artifact.read_bytes())

    def test_users_cannot_overwrite_or_download_each_others_firmware(self):
        alice, bob = self.login("alice"), self.login("bob")
        code_alice = self.save(alice, "alice")
        code_bob = self.save(bob, "bob")
        self.build(alice)
        self.assertEqual(self.request("/api/firmware.hex", cookie=bob)[0], 404)
        self.build(bob)
        self.assertEqual(self.request("/api/firmware.hex", cookie=alice)[2], code_alice)
        self.assertEqual(self.request("/api/firmware.hex", cookie=bob)[2], code_bob)
        self.assertEqual(json.loads(self.request("/api/program", cookie=alice)[2])["program"], {"name": "alice"})

    def test_csrf_and_build_lock_reject_conflicting_writes(self):
        cookie = self.login()
        self.assertEqual(self.request("/api/build", "POST", {}, cookie, {"X-GP-Token": "wrong"})[0], 403)
        with editor.build_lock:
            self.assertEqual(self.request("/api/build", "POST", {}, cookie)[0], 409)
            payload = {"program": {}, "code": self.default_cpp}
            self.assertEqual(self.request("/api/save", "POST", payload, cookie)[0], 409)

    def test_access_logs_omit_launch_tokens(self):
        log = io.StringIO()
        handler = object.__new__(editor.Handler)
        handler.command = "GET"
        handler.path = "/api/2ndbrain/launch?launch_token=secret-token"
        with contextlib.redirect_stderr(log):
            handler.log_request(303)
        self.assertNotIn("secret-token", log.getvalue())

    def test_local_mode_keeps_repository_behavior_and_host_checks(self):
        with mock.patch.multiple(editor, AUTH=None, DATA_DIR=None):
            self.assertEqual(self.request("/")[0], 403)
            status, _, _ = self.request("/", headers={"Host": "localhost"})
            self.assertEqual(status, 200)
            code = self.default_cpp + "\n// local\n"
            payload = {"program": {"local": True}, "code": code}
            status, _, _ = self.request("/api/save", "POST", payload, headers={"Host": "localhost"})
            self.assertEqual(status, 200)
            self.assertEqual(self.cpp.read_text(), code)


class AuthConfigurationTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "container build UID separation is Linux-only")
    def test_hosted_compilers_do_not_inherit_guard_secrets(self):
        import pwd
        account = pwd.getpwuid(os.getuid())
        with mock.patch.dict(os.environ, {"GP_BUILD_USER": account.pw_name,
                                        "MICROBIT_GAMEPAD_SESSION_SECRET": "private",
                                        "MARKETPLACE_LAUNCH_VERIFY_SECRET": "private",
                                        "UNRELATED_PRIVATE_SECRET": "private"}):
            options = editor.build_process_options()
        self.assertEqual(options["user"], account.pw_uid)
        self.assertEqual(options["extra_groups"], [])
        self.assertFalse(any("SECRET" in name for name in options["env"]))

    def test_hosted_mode_requires_guard_and_local_mode_needs_no_config(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertIsNone(MarketplaceAuth.from_env())
            with self.assertRaises(ValueError):
                MarketplaceAuth.from_env(required=True)
            result = subprocess.run([sys.executable, "tools/blocks_editor.py", "--host", "0.0.0.0", "--port", "0", "--no-browser"],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("MICROBIT_GAMEPAD_SESSION_SECRET", result.stderr)


if __name__ == "__main__":
    unittest.main()
