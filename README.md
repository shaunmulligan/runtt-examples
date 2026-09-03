# runtt-examples

**Two firmware applications that deploy to a microcontroller with one container
run.**

Each directory is a self-contained container project. A build produces an image
whose entire contents are a signed firmware binary and an entrypoint naming it —
which is all [runtt](https://github.com/shaunmulligan/runtt) reads.

```bash
cd app1
podman build -t app1:v1 .
podman run --rm --network none --runtime=/usr/local/bin/runtt \
  --annotation dev.runtt.target=usb:feather-01 app1:v1
```

Podman throughout, because it takes `--runtime` as a **path** and needs no
daemon configuration — nothing to install into `/etc/docker/daemon.json` and no
restart. Docker works too, but the runtime has to be registered with the daemon
first (`sudo scripts/register-docker.sh` in `runtt`), and only then does
`--runtime=runtt` resolve by name.

Pick one engine and stay on it within a session: podman and docker keep
**separate image stores**, so `docker build -t app1:v1 .` followed by
`podman run app1:v1` fails to find the image it just built.

## Start here

**[docs/WALKTHROUGH.md](docs/WALKTHROUGH.md)** — build both applications, deploy
one to a board, switch to the other, and switch back. Every command and every
transcript in it was run against real hardware.

If you have no board, [`runtt`](https://github.com/shaunmulligan/runtt)'s README has a
version of this against a mock device on a pty, with nothing physical involved.

## The two applications

| | What it does | Why two |
|---|---|---|
| `app1` | counts, logging each tick | something recognisable in the container's logs |
| `app2` | cycles through phases | visibly different from app1, so a switch is unmistakable |

They exist to make a deploy *observable*. Watching the log output change from
counting to cycling is the proof that the new image is the one running.

## Building for a different board

`BOARD` is a build argument, so nothing here is board-specific:

```bash
podman build --build-arg BOARD=adafruit_feather_nrf52840/nrf52840 -t app1:v1 .
podman build --build-arg BOARD=rpi_pico/rp2040/mcuboot           -t app1:v1 .
podman build --build-arg BOARD=rpi_pico2/rp2350a/m33/w/mcuboot   -t app1:v1 .
```

Two things to get right: the target must be one that **has MCUboot slots**
(`.../nrf52840` does, `.../uf2` does not), and the board must already be
provisioned. Both are covered in
[`runtt-boards`](https://github.com/shaunmulligan/runtt-boards).

The build needs the builder image from
[`runtt-boards`](https://github.com/shaunmulligan/runtt-boards) — that is what makes these
directories small enough to copy into your own project as a starting point.

## The runtt repositories

| Repo | What it holds | Start here if |
|---|---|---|
| [`runtt`](https://github.com/shaunmulligan/runtt) | the OCI runtime — the **host** side | you want to know what runtt is, or to work on the runtime |
| [`runtt-zephyr-module`](https://github.com/shaunmulligan/runtt-zephyr-module) | the Zephyr module — the **device** side | you have firmware and want it manageable |
| [`runtt-boards`](https://github.com/shaunmulligan/runtt-boards) | provisioning, board bring-up, the west manifest | you have a board that has never run runtt |
| [`runtt-examples`](https://github.com/shaunmulligan/runtt-examples) | two worked applications, and the walkthrough | you want to watch it work end to end |

**New here?** Read [`runtt`](https://github.com/shaunmulligan/runtt)’s README for what this
is and why, then follow the walkthrough in
[`runtt-examples`](https://github.com/shaunmulligan/runtt-examples).

## Licence

Dual licensed under [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT), at your
option.
