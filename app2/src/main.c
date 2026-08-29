/*
 * app2 -- a different application, so switching between releases is visible in
 * `docker logs` rather than inferred from a version string.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <app_version.h>

LOG_MODULE_REGISTER(app2, LOG_LEVEL_INF);

static const char *const phases[] = {"idle", "sensing", "reporting"};

int main(void)
{
	uint32_t tick = 0;

	LOG_INF("app2 %s up on %s -- cycling phases", APP_VERSION_STRING, CONFIG_BOARD_TARGET);

	while (1) {
		LOG_INF("app2: phase = %s", phases[tick++ % ARRAY_SIZE(phases)]);
		k_sleep(K_SECONDS(2));
	}
	return 0;
}
