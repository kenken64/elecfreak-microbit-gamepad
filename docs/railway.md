# Railway deployment with persistent firmware

Deploy the repository's `Dockerfile`. The image includes Python, PlatformIO and
the micro:bit V1 compiler/framework. It builds the stock firmware while creating
the image so missing build dependencies fail before deployment. The browser
editor's **Download** button builds on the Railway service and publishes a HEX
to the volume only after a successful build.

## Railway service

1. Create a service from this repository using the root `Dockerfile`.
2. Attach a persistent volume to the service and set its mount path to **`/data`**.
3. Set the runtime variables below. Leave the start command at the Dockerfile default.
4. Set the service healthcheck path to **`/healthz`**.
5. Generate an HTTPS domain for the service and configure the matching URL in
   2ndBrain.ceo as described below. Use one service replica.

```dotenv
GP_DATA_DIR=/data
MICROBIT_GAMEPAD_SESSION_SECRET=<random secret of at least 32 bytes>
MARKETPLACE_LAUNCH_VERIFY_URL=https://kere.ceo/api/marketplace/launch-session/verify
MARKETPLACE_LAUNCH_VERIFY_SECRET=<2ndBrain's existing verifier secret, at least 32 bytes>
```

Use the same gamepad signing secret on both services and a separate verifier
secret. These are server-to-server credentials; users enter no new password.
Use the actual origin serving your 2ndBrain app (`https://kere.ceo` for this
deployment). `2ndBrain.ceo` is the app/token issuer name; it is not a working
verification hostname. The return-to-workflows link uses the configured
verification server's origin.
The example file is `.env.example`. Railway supplies `PORT`; the server binds to
`0.0.0.0` and uses that port. `/healthz` is public so Railway can check readiness;
the editor, saved blocks, build endpoint and firmware downloads require a live
2ndBrain session. A deployed container refuses to start without its guard
configuration.

Railway mounts volumes at runtime, so the app creates user folders when saving
or building, after the mount is present. Attach the volume in Railway itself:
a Dockerfile does not create a Railway volume. Keep the application at `/app`
and mount storage at `/data` so deploys update the server and runtime sources.

References: [Railway volumes](https://docs.railway.com/volumes),
[Dockerfiles](https://docs.railway.com/guides/dockerfiles), and
[healthchecks](https://docs.railway.com/deployments/healthchecks).

## 2ndBrain.ceo guard

The companion `~/Projects/2ndBrain.ceo` repository has a free
**micro:bit Gamepad Blocks** catalog entry using tool/item ID `microbit-gamepad`.
Set these runtime variables on that app, then deploy its changes:

```dotenv
MICROBIT_GAMEPAD_URL=https://your-gamepad.up.railway.app/api/2ndbrain/launch
MICROBIT_GAMEPAD_SESSION_SECRET=<same gamepad signing secret>
MARKETPLACE_LAUNCH_VERIFY_SECRET=<same existing verifier secret>
```

Install the app in 2ndBrain Marketplace and open it from My Workflows. The
launch endpoint verifies 2ndBrain's HS256 JWT, issuer, tool, user, installation
and expiry, then exchanges it for a secure HTTP-only session cookie and redirects
to `/` without the token in the URL. Sessions last eight hours. Protected requests
check the existing 2ndBrain verification endpoint, including logout revocation,
disabled accounts, paused usage and uninstalled tools. Verification failures deny
access. Launch tokens are omitted from HTTP access logs.

No database migration or Supabase keys are needed for this integration.

### Verification connection errors

If the editor reports that verification is unavailable or that it cannot reach
2ndBrain, check `MARKETPLACE_LAUNCH_VERIFY_URL` on the **gamepad service** first.
Set it to the deployed app's full `/api/marketplace/launch-session/verify` URL,
then redeploy the gamepad service and relaunch from My Workflows. The verifier
secret must also match the value on the 2ndBrain service. A connection error
denies access; it does not bypass the launch guard.

## Files on the volume

Each signed-in user has a separate workspace:

```text
/data/users/<2ndbrain-user-id>/
  blocks/gamepad.json
  src/v1/main.cpp
  firmware/microbit-gamepad.hex
```

The HEX is the **last successful build**, saved atomically to the volume and
served by `/api/firmware.hex`. **Last built HEX** appears in the editor after a
successful build and remains available after restarts/redeploys. Failed builds
preserve the previous successful HEX. This is one saved program and one HEX per
user; each successful build replaces that user's previous HEX.

Only the user's saved `main.cpp` is restored into the compiler workspace for a
build; runtime C++ files come from the current deployed image. Builds and saves
share a lock; another build/save receives HTTP 409 while the compiler is busy.
PlatformIO intermediates in `/app/.pio` are disposable; the downloadable HEX is
kept separately on `/data`. No HEX exists for a new user until their first build.
Use Railway volume backups to back up user files.

The container's web process owns the root-only volume directory. Compiler
processes run as the separate `builder` user with a limited environment, so
uploaded C++ cannot read the web process's environment or other users' volume
files. The compiler cache remains writable by that build user.

## Run the image locally

```sh
cp .env.example .env
# Set the matching secrets and a reachable 2ndBrain verification URL in .env.
docker build --platform linux/amd64 -t microbit-gamepad .
docker run --rm --platform linux/amd64 --name microbit-gamepad \
  --env-file .env -p 8080:8080 \
  --mount type=volume,source=microbit-gamepad-data,target=/data \
  microbit-gamepad
```

Use an HTTPS reverse proxy/tunnel for the local guarded image so the secure
session cookie is sent. For the original local editor without launch auth,
run `python3 tools/blocks_editor.py` directly, with no guard environment variables.
It continues to bind to `127.0.0.1:8765` and edit the repository's saved files.

Downloaded firmware is flashed from the user's computer by copying it onto
the USB **MICROBIT** drive. Railway builds/downloads firmware; the USB board
stays connected to the user's computer.
