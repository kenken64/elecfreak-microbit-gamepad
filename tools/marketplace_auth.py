"""2ndBrain marketplace launch tokens, browser sessions and live access checks."""
import base64
import hashlib
import hmac
import http.cookies
import json
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request

TOOL_ID = "microbit-gamepad"
COOKIE_NAME = "microbit_gamepad_session"
SESSION_SECONDS = 8 * 60 * 60


class AccessError(Exception):
    def __init__(self, message, status=401):
        super().__init__(message)
        self.status = status


def encode(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def decode(value):
    return base64.b64decode(value + "=" * (-len(value) % 4), altchars=b"-_", validate=True)


class MarketplaceAuth:
    def __init__(self, secret, verify_url, verify_secret):
        self.secret = secret.encode("utf-8")
        self.verify_url = verify_url
        self.verify_secret = verify_secret

    @property
    def workflows_url(self):
        url = urllib.parse.urlsplit(self.verify_url)
        return urllib.parse.urlunsplit((url.scheme, url.netloc, "/dashboard/workflows", "", ""))

    @classmethod
    def from_env(cls, required=False):
        secret = os.environ.get("MICROBIT_GAMEPAD_SESSION_SECRET", "").strip()
        url = os.environ.get("MARKETPLACE_LAUNCH_VERIFY_URL", "").strip()
        verify_secret = os.environ.get("MARKETPLACE_LAUNCH_VERIFY_SECRET", "").strip()
        if not (required or secret or url or verify_secret):
            return None
        if len(secret.encode("utf-8")) < 32:
            raise ValueError("MICROBIT_GAMEPAD_SESSION_SECRET must be at least 32 bytes")
        if not url.startswith(("https://", "http://")):
            raise ValueError("MARKETPLACE_LAUNCH_VERIFY_URL must be an HTTP(S) URL")
        if len(verify_secret.encode("utf-8")) < 32:
            raise ValueError("MARKETPLACE_LAUNCH_VERIFY_SECRET must be at least 32 bytes")
        return cls(secret, url, verify_secret)

    def sign(self, claims):
        header = encode(b'{"alg":"HS256","typ":"JWT"}')
        payload = encode(json.dumps(claims, separators=(",", ":")).encode("utf-8"))
        content = header + "." + payload
        signature = encode(hmac.digest(self.secret, content.encode("ascii"), hashlib.sha256))
        return content + "." + signature

    def verify(self, token, session=False):
        try:
            if len(token) > 8192:
                raise ValueError()
            header, payload, signature = token.split(".")
            expected = hmac.digest(self.secret, (header + "." + payload).encode("ascii"), hashlib.sha256)
            if not hmac.compare_digest(decode(signature), expected):
                raise ValueError()
            if json.loads(decode(header)).get("alg") != "HS256":
                raise ValueError()
            claims = json.loads(decode(payload))
            now = int(time.time())
            if not isinstance(claims, dict):
                raise ValueError()
            if type(claims.get("exp")) is not int or claims["exp"] <= now:
                raise ValueError()
            if type(claims.get("iat")) is not int or not 0 < claims["iat"] <= now + 30:
                raise ValueError()
            if claims["exp"] <= claims["iat"]:
                raise ValueError()
            if claims.get("iss") != "2ndBrain.ceo" or claims.get("tool_id") != TOOL_ID:
                raise ValueError()
            for name in ("user_id", "install_id"):
                if not isinstance(claims.get(name), str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,127}", claims[name]):
                    raise ValueError()
            if session:
                if claims.get("session") is not True:
                    raise ValueError()
                if type(claims.get("launch_iat")) is not int or not 0 < claims["launch_iat"] <= claims["iat"]:
                    raise ValueError()
            elif "session" in claims:
                raise ValueError()
            return claims
        except (ValueError, TypeError, KeyError, AttributeError, UnicodeError):
            raise AccessError("Launch Gamepad Blocks from 2ndBrain to continue.") from None

    def check_active(self, claims):
        body = json.dumps({
            "userId": claims["user_id"],
            "installId": claims["install_id"],
            "toolId": TOOL_ID,
            "issuedAt": claims.get("launch_iat", claims["iat"]),
        }).encode("utf-8")
        request = urllib.request.Request(self.verify_url, data=body, headers={
            "Authorization": "Bearer " + self.verify_secret,
            "Content-Type": "application/json",
        }, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                active = json.load(response).get("active") is True
        except urllib.error.HTTPError as error:
            status = error.code
            error.close()
            if status in (400, 401, 403, 404, 409, 423):
                raise AccessError("Your 2ndBrain access has ended. Relaunch from My Workflows.", 403) from None
            raise AccessError("2ndBrain access verification is unavailable. Try again shortly.", 503) from None
        except urllib.error.URLError:
            hostname = urllib.parse.urlsplit(self.verify_url).hostname or "the configured server"
            raise AccessError("Cannot reach 2ndBrain at %s. Check the verification URL and try again." % hostname, 503) from None
        except (OSError, ValueError, AttributeError):
            raise AccessError("2ndBrain access verification is unavailable. Try again shortly.", 503) from None
        if not active:
            raise AccessError("Your 2ndBrain access has ended. Relaunch from My Workflows.", 403)

    def launch_cookie(self, token):
        claims = self.verify(token)
        self.check_active(claims)
        now = int(time.time())
        session = {name: claims[name] for name in ("user_id", "install_id", "tool_id", "iss")}
        session.update(iat=max(now, claims["iat"]), launch_iat=claims["iat"],
                       exp=now + SESSION_SECONDS, session=True)
        return "%s=%s; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=%d" % (
            COOKIE_NAME, self.sign(session), SESSION_SECONDS)

    def authenticate(self, cookie_header):
        cookies = http.cookies.SimpleCookie()
        try:
            cookies.load(cookie_header)
        except http.cookies.CookieError:
            raise AccessError("Launch Gamepad Blocks from 2ndBrain to continue.") from None
        cookie = cookies.get(COOKIE_NAME)
        claims = self.verify(cookie.value if cookie else "", session=True)
        self.check_active(claims)
        return claims
