# Reaction Speed Test

A native desktop reaction-time benchmark with a private friends scoreboard.
The C++ client measures five-round sets and saves local results. The Python
server accepts signed scores and serves a leaderboard, client downloads, and
a page explaining the score-verification protocol.

## Project layout

| Path | Purpose |
| --- | --- |
| `reaction_benchmark.cpp` | Desktop client entry point and command-line options |
| `app.hpp` | Game state, input handling, and background networking |
| `platform_window.hpp`, `canvas.hpp`, `ui_renderer.hpp` | Native windows and software-rendered interface |
| `history.hpp` | Local CSV history and terminal summaries |
| `net_client.hpp`, `crypto.hpp` | HTTP requests, client identity, signatures, and binary hashing |
| `embedded_font.hpp`, `gen_font.py` | Embedded font data and optional regeneration script |
| `Makefile` | Linux build, Windows cross-compilation, and release hashes |
| `server/app/` | FastAPI application, API routes, services, SQLite storage, and web UI |
| `server/tests/` | Server integration tests |
| `server/binaries/` | Built clients served by the download endpoints |

## Desktop client

The client uses C++20, X11 on Linux, and Win32 on Windows. Linux uses OpenSSL
for cryptography and requires `curl` for HTTPS requests. Windows uses WinHTTP
and links the system libraries listed in the Makefile.

### Build and run on Linux

On Debian or Ubuntu, install the build dependencies:

```sh
sudo apt install build-essential libx11-dev libssl-dev curl
```

From the project root, build a client for a local server:

```sh
make -B linux SERVER_URL=http://127.0.0.1:8787 INVITE_CODE=FRIENDS-2026
./reaction_benchmark
```

`SERVER_URL` and `INVITE_CODE` are compiled into the client; use the URL and
invite code of your server. The Makefile's existing defaults target a hosted
deployment, so pass these values explicitly for local development. `-B` forces
a rebuild because changes to these variables alone do not invalidate the target.
The Linux GUI needs an X11 display, including XWayland on a Wayland desktop.

The build also copies the executable to `reaction_benchmark_linux_x86_64` and
`server/binaries/reaction_benchmark_linux_x86_64`.

### Build for Windows

Cross-compile from Linux with MinGW-w64:

```sh
sudo apt install g++-mingw-w64-x86-64
make windows SERVER_URL=http://127.0.0.1:8787 INVITE_CODE=FRIENDS-2026
```

This produces `reaction_benchmark.exe` and copies it to `server/binaries/`.
Use your server's reachable address when building for another computer.

### Play and view results

Click or press Space to start, wait for green, then react. Complete five rounds
to record a set average and attempt an online submission. Early input requires
retrying the round. Local play and history remain available without a server.

| Key | Action |
| --- | --- |
| Space / Enter | Start or advance play; react when green |
| N | Edit nickname |
| F | Toggle the friends panel and refresh scores |
| G | Toggle the chart |
| R | Reset the current set |
| H | Print history to the terminal |
| C | Clear local history |
| K | Show and copy the private identity seed |
| Esc / Q | Quit |

Nickname, key, and chart controls are available outside timed rounds.

Useful terminal commands:

```sh
./reaction_benchmark --help
./reaction_benchmark --history
./reaction_benchmark --hash
./reaction_benchmark --clear
```

`--clear` deletes local history. `--key` prints both the public key and private
seed. The client stores `reaction_history.csv` and `reaction_config.ini` beside
the executable. The configuration includes the API token and private identity
seed, so keep it private; both local files are ignored by Git. Existing
configuration can retain a previous server URL or invite code after rebuilding;
update those fields when switching servers. Set `online_enabled=false` there
for offline use.

## Scoreboard server

The server requires Python 3.11 or newer and uses FastAPI, Uvicorn, SQLite,
Jinja2, and the Python `cryptography` package. It initializes its database and
binary allowlist on startup. There is no separate JavaScript build step.

### Run locally

First build the client as shown above, then run `make hashes` from the project
root. This updates `server/allowed_binaries.json` with hashes of the root-level
Linux and Windows executables that are present. Regenerate it after rebuilding
clients, and restart the server to synchronize the allowlist.

In another terminal, starting from the project root:

```sh
cd server
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt

export DB_PATH="$PWD/data/scoreboard.db"
export ALLOWED_BINARIES_PATH="$PWD/allowed_binaries.json"
export BINARIES_DIR="$PWD/binaries"
export INVITE_CODE=FRIENDS-2026
export ADMIN_SECRET=replace-with-a-private-admin-secret
export STRICT_BINARY_CHECK=true

python -m uvicorn app.main:app --host 127.0.0.1 --port 8787
```

The invite code must match the client's configuration. With strict checks
enabled, the executable's SHA-256 must appear in the server allowlist for
registration and score submission to succeed.

Open these paths at `http://127.0.0.1:8787`:

| Path | Purpose |
| --- | --- |
| `/` | Leaderboard, statistics, and recent ledger blocks |
| `/download` | Client downloads and their SHA-256 hashes |
| `/crypto` | Protocol explanation and a sample ledger entry |
| `/docs` | Interactive API documentation |
| `/api/v1/health` | Health endpoint |

### Configuration

Settings come from environment variables; see [server/app/config.py](server/app/config.py).

| Variable | Default / behavior |
| --- | --- |
| `DB_PATH` | `/data/scoreboard.db`; use a writable local path outside Docker |
| `ALLOWED_BINARIES_PATH` | `/app/allowed_binaries.json` |
| `BINARIES_DIR` | `/app/binaries` |
| `INVITE_CODE` | `FRIENDS-2026` |
| `ADMIN_SECRET` | Replace the placeholder default to enable admin mutations |
| `STRICT_BINARY_CHECK` | `true`; enforce approved client hashes |
| `ALLOWED_LINUX_SHA256`, `ALLOWED_WINDOWS_SHA256` | Optional hash overrides that replace the JSON allowlist when supplied |
| `PORT` | `8787`; used by `python server.py`; pass `--port` when invoking Uvicorn directly |
| `PROTOCOL_ATTESTATION_SECRET` | Shared protocol value; changing it requires matching client changes |

### Run with Docker

Build clients and run `make hashes` before building the image so its download
directory and allowlist contain the intended releases. From the project root:

```sh
docker build -t reaction-scoreboard ./server
docker run -d --name reaction-scoreboard \
  -p 8787:8787 \
  -e INVITE_CODE=FRIENDS-2026 \
  -e ADMIN_SECRET=replace-with-a-private-admin-secret \
  -v reaction-scoreboard-data:/data \
  reaction-scoreboard
```

The named volume persists SQLite data. Binaries and the allowlist are included
in the image; rebuild it when shipping new clients. The existing
`server/docker-compose.yml` is a deployment-specific example with absolute
TrueNAS paths, bind mounts, and inline settings. Adapt those paths and settings
before using it on another host.

### Score verification and API

The client registers an Ed25519 public key using the invite code, obtains an
API token, and requests a signed challenge containing five random wait delays.
After play, it signs the results and submits them with binary information and
an HMAC attestation. The server checks the challenge, timing constraints,
signature, and approved binary hash, then appends accepted scores to a SHA-256
hash chain. These checks provide protocol validation and ledger auditing;
reaction measurements originate on the client.

Main endpoints include `POST /api/v1/auth/register`, `POST /api/v1/challenge`,
`POST /api/v1/submit`, `GET /api/v1/leaderboard`, `GET /api/v1/distribution`,
and `GET /api/v1/audit/verify`. Authenticated gameplay uses `X-Api-Token`.
Allowlist mutations under `/api/v1/admin/binaries` require `X-Admin-Secret`.
See `/docs` for request schemas and the complete API.

### Server tests

From `server/`, with the virtual environment active:

```sh
python -m pip install pytest httpx
python -m pytest
```

Tests use temporary databases and fixture binaries to exercise registration,
signed submissions, validation, auditing, the web pages, and downloads.
