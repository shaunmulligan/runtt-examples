# A firmware service. Stage one builds it, stage two ships nothing but the
# signed image and an entrypoint naming it -- which is all the runtime reads.
#
#   docker build -t my-app:v1 .
#
# The build environment (Zephyr, MCUboot, the runtt module) comes from the
# builder image, so this directory is self-contained and the build context is
# just the application.
ARG BUILDER=runtt-builder:v4.4.2
FROM ${BUILDER} AS builder
ARG BOARD=rpi_pico/rp2040/mcuboot

COPY . /ws/app
# -Dapp_SNIPPET rather than -S: under sysbuild a top-level snippet applies to
# every image, including the bootloader.
RUN west build -b "${BOARD}" --sysbuild /ws/app -d /ws/build -- \
      -DZEPHYR_EXTRA_MODULES=/ws/runtt \
      -Dapp_SNIPPET=runtt

# sysbuild signs the image itself, correctly. Do not re-sign this by hand with
# imgtool --pad-header: the app already reserves its header space via
# CONFIG_ROM_START_OFFSET, so padding it again produces an image that passes
# `imgtool verify` and then locks the board up. See docs/WALKTHROUGH.md.
FROM scratch
COPY --from=builder /ws/build/app/zephyr/zephyr.signed.bin /app.signed.bin
ENTRYPOINT ["app.signed.bin"]
