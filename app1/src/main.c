/*
 * app1 -- a counter.
 *
 * Deliberately ordinary. Nothing here is aware of the runtime: the SMP server,
 * the two contract channels and the describe command all arrive with the
 * balena-mcu snippet at build time.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <app_version.h>

LOG_MODULE_REGISTER(app1, LOG_LEVEL_INF);

int main(void)
{
	uint32_t count = 0;

	LOG_INF("app1 %s up on %s -- counting", APP_VERSION_STRING, CONFIG_BOARD_TARGET);

	while (1) {
		LOG_INF("app1: count = %u", count++);
		k_sleep(K_SECONDS(2));
	}
	return 0;
}
