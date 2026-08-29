# Walkthrough: two firmware releases, switched with `docker run`

Build two versions of an application, ship each as an ordinary container image,
and switch the MCU between them from the Docker CLI. Every command and every
piece of output below was run against a Raspberry Pi Pico; the transcripts are
what the machine actually printed.

**Time:** about fifteen minutes, most of it firmware builds.

## What you need

* A Pico already provisioned with MCUboot — one physical act, see
  `docs/PROVISIONING.md`. Check it with:

  ```bash
  cargo run -p smp-client --example ping -- /dev/balena-mcu/*-mgmt
  ```

  You want `echo -> "balena"` and a `describe` line back.
* The udev rules installed, so the board appears under `/dev/balena-mcu/`.
* Docker (tested on 28.5.2) and a Zephyr workspace (`west`, SDK 1.0.1).

---

## 1. Register the runtime with Docker

```bash
cargo build
sudo ./scripts/register-docker.sh
```

That installs the binary to `/usr/local/bin/mcu-runtime` and adds it to
`/etc/docker/daemon.json` under `runtimes`, merging rather than overwriting.
Confirm:

```bash
docker info | grep -A1 Runtimes
#  Runtimes: io.containerd.runc.v2 mcu-runtime runc
```

> **Re-run this after rebuilding the runtime.** The script *copies* the binary,
> it does not symlink it, so a stale `/usr/local/bin/mcu-runtime` keeps being
> used and everything still appears to work. Compare them:
>
> ```bash
> md5sum /usr/local/bin/mcu-runtime target/debug/mcu-runtime
> ```

## 2. Build two application versions

They differ only in the `VERSION` file, which is enough: `APP_VERSION_STRING`
appears in the application's own boot log, so the running version shows up in
`docker logs` with no extra instrumentation.

```bash
for V in 1 2; do
  printf 'VERSION_MAJOR = %s\nVERSION_MINOR = 0\nPATCHLEVEL = 0\nVERSION_TWEAK = 0\nEXTRAVERSION =\n' "$V" \
    > firmware/app/VERSION
  ./scripts/build-pico.sh mcuboot
  python3 bootloader/mcuboot/scripts/imgtool.py sign \
    --key bootloader/mcuboot/root-rsa-2048.pem \
    --header-size 0x200 --align 4 \
    --version "$V.0.0" --slot-size 0xd0000 \
    build-pico-mcuboot/app/zephyr/zephyr.bin "/tmp/app-v$V.signed.bin"
done
git checkout firmware/app/VERSION      # put it back
```

> ### ⚠️ No `--pad-header`, and check the result
>
> An app built for MCUboot sets `CONFIG_ROM_START_OFFSET=0x200` and **already
> reserves** its header space. `--pad-header` prepends a second one, so the
> header declares `hdr_size=0x200` while the image really begins at `0x400`.
> `imgtool verify` still passes. MCUboot jumps to `image + 0x200`, lands on
> padding, loads `SP = 0`, and the board locks up unrecoverably.
>
> One line catches it — the word at `hdr_size` must be a RAM address:
>
> ```bash
> python3 -c "
> import struct,pathlib,sys
> d=pathlib.Path(sys.argv[1]).read_bytes()
> h=struct.unpack('<H',d[8:10])[0]; sp=struct.unpack('<I',d[h:h+4])[0]
> print(f'hdr={h:#x} sp={sp:#010x}', 'OK' if sp>>24==0x20 else 'MALFORMED')
> " /tmp/app-v1.signed.bin
> # hdr=0x200 sp=0x20003890 OK
> ```
>
> `--pad-header` *is* correct for native_sim, where `ROM_START_OFFSET=0`. That
> asymmetry is what makes this easy to get wrong: a command copied from a
> working simulator gate quietly produces a broken hardware image.

## 3. Wrap each as a container image

`FROM scratch`, the signed image, and an entrypoint naming it. That entrypoint
is the entire contract between your image and the runtime.

```bash
for V in 1 2; do
  mkdir -p "/tmp/ctx-v$V" && cp "/tmp/app-v$V.signed.bin" "/tmp/ctx-v$V/app.signed.bin"
  printf 'FROM scratch\nADD app.signed.bin /\nENTRYPOINT ["app.signed.bin"]\n' \
    > "/tmp/ctx-v$V/Dockerfile"
  docker build -t "mcu-app:v$V" "/tmp/ctx-v$V"
done
```

Both come out around 131 kB — one layer holding one file.

## 4. Deploy v1

```bash
docker run --rm --runtime=mcu-runtime --network none \
  --annotation io.balena.mcu.target=usb:3-4 mcu-app:v1
```

Replace `3-4` with your board's USB port path (`ls /dev/balena-mcu/`, or
`lsusb -t`).

```
mcu: device is rpi_pico/rp2040/mcuboot running 0.1.0 (contract 1.2.0, 2 channels)
mcu: uploading 71664/71664 bytes (100%)
mcu: image staged and marked test, resetting
mcu: image confirmed
*** Booting Zephyr OS build dccb09599635 ***
<inf> balena_mcu_usbd: USB device up with all contract channels registered
<inf> app: balena-mcu template app 1.0.0 starting on rpi_pico/rp2040/mcuboot
<inf> app: alive, tick 0
```

Note the ordering: **staged and marked test, reset, and only then confirmed.**
Confirmation happens after the new firmware has come back and answered, so an
image that cannot speak the contract can never confirm itself and MCUboot
reverts it on the next boot. See `docs/ARCHITECTURE.md`.

Everything after the reset is the MCU's own output, arriving on the log channel
and going straight to container stdio. The container stays running: it is the
firmware's supervisor, and stopping it is how you release the board.

> `--network none` because a firmware container needs no networking — the
> runtime talks to the MCU over USB from *outside* the container. It also skips
> Docker's bridge setup, which on some hosts collides with an existing route.

## 5. Switch to v2

```bash
docker run --rm --runtime=mcu-runtime --network none \
  --annotation io.balena.mcu.target=usb:3-4 mcu-app:v2
```

```
mcu: device is rpi_pico/rp2040/mcuboot running 1.0.0 (contract 1.2.0, 2 channels)
mcu: uploading 71664/71664 bytes (100%)
mcu: image staged and marked test, resetting
mcu: image confirmed
<inf> app: balena-mcu template app 2.0.0 starting on rpi_pico/rp2040/mcuboot
```

It reports what is **currently** on the board (`running 1.0.0`) before replacing
it. Run `mcu-app:v1` again to switch back — verified in both directions.

## 6. Redeploy the same image

```
mcu: device is rpi_pico/rp2040/mcuboot running 1.0.0 (contract 1.2.0, 2 channels)
mcu: device already runs this digest, confirmed; nothing to do
```

No upload, no flash write, no reset. Redeploying a release already on the board
is free, which is what makes it safe for a supervisor to reconcile continuously.

**The flip side, when testing:** rebuild without changing the version and the
digest is unchanged, so the deploy is a no-op — a test can pass having done
nothing at all. Vary `--version` per build, as this walkthrough does. Opt out
with `--annotation io.balena.mcu.skip-if-same-hash=false`.

---

## What to take from it

* **Firmware is a normal container image.** `docker pull`, `docker run`, image
  tags, restart policies — no special tooling.
* **The container is the firmware's supervisor.** It stays up, pipes MCU logs to
  stdio, and heartbeats. Losing the board exits non-zero, so restart policies
  fire.
* **The container needs no privileges and no device mappings.** The runtime holds
  the USB device from outside; nothing inside the container ever touches it.
* **Rollback is structural.** Confirmation is reachable only through the
  contract, so a broken image cannot confirm itself.

## If something goes wrong

| Symptom | Cause |
|---|---|
| `Unable to acquire exclusive lock on serial port` | a leftover proxy holds the device — `pgrep -af mcu-runtime`, then `docker rm -f` the container |
| `could not resolve target usb:N-M` | wrong port path, or udev rules not installed |
| Deploy says `nothing to do` unexpectedly | same digest; vary `--version` |
| Board stops answering after a deploy | check the signed image with the `hdr_size` snippet in §2 before suspecting anything else |
| `failed to set up container networking` | use `--network none` |

---

*Co-authored with Claude*
