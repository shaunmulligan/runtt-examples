# runtt-examples

**Two firmware applications that deploy to a microcontroller with `docker run`.**

Each directory is a self-contained container project. `docker build .` produces
an image whose entire contents are a signed firmware binary and an entrypoint
naming it — which is all [runtt](https://github.com/shaunmulligan/runtt) reads.

```bash
cd app1
docker build -t app1:v1 .
docker run --rm --network none --runtime=runtt \
  --annotation dev.runtt.target=usb:3-4 app1:v1
```

## Start here

**[docs/WALKTHROUGH.md](docs/WALKTHROUGH.md)** — build both applications, deploy
one to a board, switch to the other, and switch back. Every command and every
transcript in it was run against real hardware.

If you have no board, [`runtt`](https://github.com/shaunmulligan/runtt)'s README has a
version of this against a mock device on a pty, with nothing physical involved.

## The two applications

| | What it does | Why two |
|---|---|---|
| `app1` | counts, logging each tick | something recognisable in `docker logs` |
| `app2` | cycles through phases | visibly different from app1, so a switch is unmistakable |

They exist to make a deploy *observable*. Watching the log output change from
counting to cycling is the proof that the new image is the one running.

## Building for a different board

`BOARD` is a build argument, so nothing here is board-specific:

```bash
docker build --build-arg BOARD=adafruit_feather_nrf52840/nrf52840 -t app1:v1 .
docker build --build-arg BOARD=rpi_pico/rp2040/mcuboot          -t app1:v1 .
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
