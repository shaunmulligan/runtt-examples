# Walkthrough: two firmware apps, switched with one container run

Two self-contained application directories. Build each as a container image,
deploy each with one run command, and switch the MCU between them.

Commands use `podman`, which takes `--runtime` as a path and so needs no daemon
configuration. Docker works identically once the runtime is registered (§1).

Every command and transcript below was run against a Raspberry Pi Pico. The
output is what the machine printed.

## What you need

* A Pico provisioned with MCUboot — one physical act, see [`PROVISIONING.md`](https://github.com/shaunmulligan/runtt-boards/blob/main/docs/PROVISIONING.md):
  ```bash
  cargo run -p runtt-smp --example ping -- /dev/runtt/*-mgmt
  ```
  You want `echo -> "runtt"` and a `describe` line back.
* The udev rules installed, so the board appears under `/dev/runtt/`.
* Docker (tested on 28.5.2).

---

## 1. Point the engine at the runtime

Podman takes `--runtime` as a **path**, so there is nothing to register and no
daemon to restart — point it at the binary you just built:

```bash
cd ../runtt          # the runtime repository
cargo build --release
RUNTT=$PWD/target/release/runtt
```

That also sidesteps a trap worth knowing about. The Docker path needs the
runtime installed and registered:

```bash
sudo ./scripts/register-docker.sh
docker info | grep -A1 Runtimes
#  Runtimes: io.containerd.runc.v2 runtt runc
```

> **Re-run that after every rebuild.** `register-docker.sh` *copies* the binary
> rather than symlinking it, so a stale `/usr/local/bin/runtt` goes on being
> used while everything still appears to work. Compare them before believing a
> result: `md5sum /usr/local/bin/runtt target/release/runtt`. Naming the build
> output directly, as the podman line above does, cannot go stale.

> **Do not mix engines within a run.** Podman and Docker keep separate image
> stores, so building with one and running with the other fails to find the
> image — including the large builder image in §2.

## 2. Build the firmware builder image, once

Zephyr, MCUboot and the `runtt` module, fetched once so that application
directories stay small and self-contained.

```bash
cd ../runtt-boards    # the builder and the manifest live here
podman build -f builder/Dockerfile -t runtt-builder:v4.4.2 .
```

The build context is the repository root, not `builder/`, because the Dockerfile
copies `west.yml` from it — that manifest is what pins Zephyr, MCUboot and the
`runtt` module.

It is large (~33 GB, almost all of it the Zephyr SDK's toolchains) and slow the
first time. Rebuild it only when the Zephyr pin or the module revision in
`west.yml` changes. In a real deployment
this is a published image a customer pulls, not something they build.

## 3. The two applications

`app1/` and `app2/` in this repository. Each is a complete Zephyr
application plus a Dockerfile:

```
app1/
├── Dockerfile
├── CMakeLists.txt
├── prj.conf          ordinary app config; no platform boilerplate
├── sysbuild.conf     MCUboot on, swap mode pinned, signing key
├── VERSION           feeds APP_VERSION_STRING and `describe`
└── src/main.c
```

They are genuinely different programs, so switching is visible as behaviour
rather than a version string: **app1 counts**, **app2 cycles through phases**.

Nothing in `src/main.c` is aware of the runtime. The SMP server, the two
contract channels and the `describe` command all arrive with the `runtt`
snippet at build time. This is the whole developer-facing surface:

```dockerfile
FROM runtt-builder:v4.4.2 AS builder
ARG BOARD=rpi_pico/rp2040/mcuboot
COPY . /ws/app
RUN west build -b "${BOARD}" --sysbuild /ws/app -d /ws/build -- \
      -Dapp_SNIPPET=runtt

FROM scratch
COPY --from=builder /ws/build/app/zephyr/zephyr.signed.bin /app.signed.bin
ENTRYPOINT ["app.signed.bin"]
```

Stage two is the entire delivery format: `FROM scratch`, one signed image, an
entrypoint naming it. **That entrypoint is the only contract between your image
and the runtime.**

> Two details worth knowing if you write your own. `-Dapp_SNIPPET=` rather than
> `-S`, because under sysbuild a top-level snippet applies to *every* image
> including the bootloader. And the `COPY . /ws/app` destination is what names
> the sysbuild image, which is why `app_SNIPPET` and `/ws/build/app/` match
> regardless of what the host directory is called.

## 4. Build them

```bash
cd app1 && podman build -t mcu-app1:v1 .
cd ../app2 && podman build -t mcu-app2:v1 .
```

### For a different board: one build arg

The walkthrough uses a Pico because it needs no debug probe, but the application
directories are not Pico-specific. `BOARD` is a build arg, so any supported target
is one flag:

```bash
cd app1

# Adafruit Feather nRF52840
podman build --build-arg BOARD=adafruit_feather_nrf52840/nrf52840 -t app1-feather:v1 .

# Raspberry Pi Pico (the default)
podman build --build-arg BOARD=rpi_pico/rp2040/mcuboot -t app1-pico:v1 .
```

Everything downstream is identical — the same run command, the same annotation,
the same deploy sequence. Only the placement label changes to name the board you
mean. Verified on a Feather: the image above deployed, swapped and confirmed
without a single other change.

Two things that are easy to get wrong here:

* **The board target must be one with MCUboot slots.** `adafruit_feather_nrf52840/nrf52840`
  has them; `.../uf2` does **not** — it keeps Adafruit's bootloader and has no
  slot1 at all, so there is nothing to stage into. For the Pico the equivalent
  distinction is `rpi_pico/rp2040/mcuboot` rather than plain `rpi_pico`. See
  [HARDWARE_TARGETS.md](https://github.com/shaunmulligan/runtt-boards/blob/main/docs/HARDWARE_TARGETS.md).
* **The board must already be provisioned** — MCUboot plus a signed image in
  slot 0. That is the one physical act, and on the Feather it needs a debug probe.
  See [PROVISIONING.md](https://github.com/shaunmulligan/runtt-boards/blob/main/docs/PROVISIONING.md).

```
mcu-app1:v1  132kB
mcu-app2:v1  132kB
```

One layer holding one file. **Note there is no `imgtool` step** — sysbuild signs
the image itself, correctly. Signing by hand is where the `--pad-header` trap
lives (see §7); this workflow avoids it entirely.

## 5. Deploy app1

```bash
podman run --rm --runtime="$RUNTT" --network none \
  --annotation dev.runtt.target=usb:feather-01 mcu-app1:v1
```

The target names the board. A provisioned board answers to its serial, which is
stable across ports and reboots — `ls /dev/runtt/by-serial/`. An unprovisioned
one is addressed by USB port path instead (`usb:3-4`; `ls /dev/runtt/` or
`lsusb -t`), which changes if you move the cable.

```
mcu: device is rpi_pico/rp2040/mcuboot running 1.0.0 (contract 2.0.0, 2 channels)
mcu: uploading 71656/71656 bytes (100%)
mcu: image staged and marked test, resetting
mcu: image confirmed
<inf> app1: app1 1.0.0 up on rpi_pico/rp2040/mcuboot -- counting
<inf> app1: app1: count = 0
<inf> app1: app1: count = 1
<inf> app1: app1: count = 2
```

Note the ordering: **staged and marked test, reset, and only then confirmed.**
Confirmation happens after the new firmware has come back and answered, so an
image that cannot speak the contract can never confirm itself, and MCUboot
reverts it at the next boot — a boot the device schedules for itself if the
confirm does not arrive within 60 s (`CONFIG_RUNTT_CONFIRM_DEADLINE`), since
nothing else would. See [`ARCHITECTURE.md`](https://github.com/shaunmulligan/runtt/blob/main/docs/ARCHITECTURE.md).

Everything after the reset is the MCU's own output, arriving on the log channel
and going straight to container stdio. The container stays up: it is the
firmware's supervisor, and stopping it releases the board.

> `--network none` because a firmware container needs no networking — the
> runtime talks to the MCU over USB from *outside* the container. It also skips
> bridge setup, which on some hosts collides with an existing route; that is a
> real failure, seen on Docker as `cannot program address ... conflicts with
> existing route`. CAN is the exception and needs `--network host`, because
> SocketCAN interfaces live in the host's network namespace.

## 6. Switch to app2, and back

```bash
podman run --rm --runtime="$RUNTT" --network none \
  --annotation dev.runtt.target=usb:feather-01 mcu-app2:v1
```

```
mcu: device is rpi_pico/rp2040/mcuboot running 1.0.0 (contract 2.0.0, 2 channels)
mcu: image confirmed
<inf> app2: app2 2.0.0 up on rpi_pico/rp2040/mcuboot -- cycling phases
<inf> app2: app2: phase = idle
<inf> app2: app2: phase = sensing
<inf> app2: app2: phase = reporting
```

It reports what is **currently** on the board (`running 1.0.0`) before replacing
it. Running `mcu-app1:v1` again switches back — verified in both directions:

```
mcu: device is rpi_pico/rp2040/mcuboot running 2.0.0 (contract 2.0.0, 2 channels)
mcu: image confirmed
<inf> app1: app1 1.0.0 up on rpi_pico/rp2040/mcuboot -- counting
```

Redeploying an image already on the board is free:

```
mcu: device already runs this digest, confirmed; nothing to do
```

No upload, no flash write, no reset — which is what makes it safe for a
supervisor to reconcile continuously. **The flip side when testing:** rebuild
without changing anything and the digest is unchanged, so a test can pass having
done nothing. Bump `VERSION`, or opt out with
`--annotation dev.runtt.skip-if-same-hash=false`.

## 7. If you sign by hand instead

You do not need to — §4 has no signing step. But if you build outside this
workflow:

> ### ⚠️ No `--pad-header` for a hardware image
>
> An app built for MCUboot sets `CONFIG_ROM_START_OFFSET=0x200` and **already
> reserves** its header space. `--pad-header` prepends a second one, so the
> header declares `hdr_size=0x200` while the image really begins at `0x400`.
> `imgtool verify` still passes. MCUboot jumps to `image + 0x200`, lands on
> padding, loads `SP = 0`, and the board locks up unrecoverably.
>
> ```bash
> python3 -c "
> import struct,pathlib,sys
> d=pathlib.Path(sys.argv[1]).read_bytes()
> h=struct.unpack('<H',d[8:10])[0]; sp=struct.unpack('<I',d[h:h+4])[0]
> print(f'hdr={h:#x} sp={sp:#010x}', 'OK' if sp>>24==0x20 else 'MALFORMED')
> " image.signed.bin
> # hdr=0x200 sp=0x20003890 OK
> ```
>
> `--pad-header` *is* correct for native_sim, where `ROM_START_OFFSET=0`. That
> asymmetry is what makes it easy to get wrong: the command copied from a
> working simulator gate produces a broken hardware image.

---

## What to take from it

* **A firmware app is an ordinary container project.** A directory with a
  Dockerfile, built as an image, run with one run command.
* **The application source knows nothing about the platform.** Manageability
  comes from the snippet at build time, not from application code.
* **The container is the firmware's supervisor.** It stays up, pipes MCU logs to
  stdio, and heartbeats; losing the board exits non-zero so restart policies fire.
* **No privileges, no device mappings.** The runtime holds the USB device from
  outside; nothing inside the container touches it.
* **Rollback is structural.** Confirmation is reachable only through the
  contract, so a broken image cannot confirm itself.

## If something goes wrong

| Symptom | Cause |
|---|---|
| `"/west.yml": not found` during build | building an app against the old repo-context Dockerfile; use the builder image as in §3 |
| `runtt-builder:v4.4.2: not found` | the builder image is not built, or is tagged under an older name — rebuild it as in §2. It is cheap: the expensive layers cache, only the module layer rebuilds |
| `/ws/runtt: no such file or directory` during an app build | the builder image predates a module path change. Rebuild it (§2); a stale builder carries the old layout |
| `Unable to acquire exclusive lock on serial port` | a leftover proxy holds the device — `pgrep -af runtt`, then `docker rm -f` the container |
| `could not resolve target usb:N-M` | wrong port path, or udev rules not installed |
| Deploy says `nothing to do` unexpectedly | same digest; bump `VERSION` |
| `failed to set up container networking` | use `--network none` |
| Board stops answering after a deploy | if you signed by hand, check the image with §7 before suspecting anything else |

---

*Co-authored with Claude*
