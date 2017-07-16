#include <sdkconfig.h>

#include <esp_log.h>
#include <driver/gpio.h>

#include "badge_pins.h"
#include "badge_base.h"
#include "badge_input.h"
#include "badge_button.h"
#include "badge_gpiobutton.h"
#include "badge_i2c.h"
#include "badge_portexp.h"
#include "badge_mpr121.h"
#include "badge_touch.h"
#include "badge_power.h"
#include "badge_leds.h"
#include "badge_vibrator.h"
#include "badge_sdcard.h"
#include "badge_eink.h"
#include "badge_nvs.h"

static const char *TAG = "badge";

#ifdef I2C_TOUCHPAD_ADDR
static void
touch_event_handler(int event)
{
	// convert into button queue event
	int event_type = (event >> 16) & 0x0f; // 0=touch, 1=release, 2=slider
	if (event_type == 0 || event_type == 1) {
		static const int conv[12] = {
			-1,
			-1,
			BADGE_BUTTON_LEFT,
			BADGE_BUTTON_UP,
			BADGE_BUTTON_RIGHT,
			BADGE_BUTTON_DOWN,
			-1,
			BADGE_BUTTON_SELECT,
			BADGE_BUTTON_START,
			BADGE_BUTTON_B,
			-1,
			BADGE_BUTTON_A,
		};
		if (((event >> 8) & 0xff) < 12) {
			int id = conv[(event >> 8) & 0xff];
			if (id != -1)
			{
				badge_input_add_event(id, event_type == 0 ? EVENT_BUTTON_PRESSED : EVENT_BUTTON_RELEASED, NOT_IN_ISR);
			}
		}
	}
}
#endif // I2C_TOUCHPAD_ADDR

#ifdef I2C_MPR121_ADDR
static void
mpr121_event_handler(void *b, bool pressed)
{
	badge_input_add_event((uint32_t) b, pressed ? EVENT_BUTTON_PRESSED : EVENT_BUTTON_RELEASED, NOT_IN_ISR);
}
#endif // I2C_MPR121_ADDR

#ifdef CONFIG_SHA_BADGE_MPR121_HARDCODE_BASELINE
// NVS helper
static esp_err_t
nvs_baseline_helper(uint8_t idx, uint32_t *value) {
	if (idx > 7) {
		ESP_LOGE(TAG, "NVS baseline index out of range: %d", idx);
		return -1;
	}
	char key[14];
	sprintf(key, "mpr121.base.%d", idx);
	uint16_t v;
	esp_err_t err = badge_nvs_get_u16("badge", key, &v);
	if (err == ESP_OK)
		*value = v;
	return err;
}
#endif // CONFIG_SHA_BADGE_MPR121_HARDCODE_BASELINE

void
badge_init(void)
{
	static bool badge_init_done = false;

	if (badge_init_done)
		return;

	// register isr service
	badge_base_init();

	// initialise nvs config store
	badge_nvs_init();

	// configure input queue
	badge_input_init();

	// configure buttons directly connected to gpio pins
#ifdef PIN_NUM_BUTTON_A
	badge_gpiobutton_add(PIN_NUM_BUTTON_A    , BADGE_BUTTON_A);
	badge_gpiobutton_add(PIN_NUM_BUTTON_B    , BADGE_BUTTON_B);
	badge_gpiobutton_add(PIN_NUM_BUTTON_START, BADGE_BUTTON_START); // 'mid'
	badge_gpiobutton_add(PIN_NUM_BUTTON_UP   , BADGE_BUTTON_UP);
	badge_gpiobutton_add(PIN_NUM_BUTTON_DOWN , BADGE_BUTTON_DOWN);
	badge_gpiobutton_add(PIN_NUM_BUTTON_LEFT , BADGE_BUTTON_LEFT);
	badge_gpiobutton_add(PIN_NUM_BUTTON_RIGHT, BADGE_BUTTON_RIGHT);
#elif defined( PIN_NUM_BUTTON_FLASH )
	badge_gpiobutton_add(PIN_NUM_BUTTON_FLASH, BADGE_BUTTON_FLASH);
#endif // ! PIN_NUM_BUTTON_A

	// configure the i2c bus to the port-expander and touch-controller or to the mpr121
#ifdef PIN_NUM_I2C_CLK
	badge_i2c_init();
#endif // PIN_NUM_I2C_CLK

#ifdef I2C_PORTEXP_ADDR
	badge_portexp_init();
#endif // I2C_PORTEXP_ADDR

#ifdef I2C_TOUCHPAD_ADDR
	badge_touch_init();
	badge_touch_set_event_handler(touch_event_handler);
#endif // I2C_TOUCHPAD_ADDR

#ifdef I2C_MPR121_ADDR
#ifdef CONFIG_SHA_BADGE_MPR121_HARDCODE_BASELINE
	uint32_t mpr121_baseline[8] = {
		0x0138,
		0x0144,
		0x0170,
		0x0174,
		0x00f0,
		0x0103,
		0x00ff,
		0x00ed,
	};
	int i;
	bool mpr121_strict = true;
	for (i=0; i<8; i++)
	{
		esp_err_t err = nvs_baseline_helper(i, &mpr121_baseline[i]);
		if (err != ESP_OK)
			mpr121_strict = false;
	}
	badge_mpr121_init();
	badge_mpr121_configure(mpr121_baseline, mpr121_strict);
#else
	badge_mpr121_configure(NULL, false);
#endif // CONFIG_SHA_BADGE_MPR121_HARDCODE_BASELINE
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_A     , mpr121_event_handler, (void*) (BADGE_BUTTON_A));
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_B     , mpr121_event_handler, (void*) (BADGE_BUTTON_B));
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_START , mpr121_event_handler, (void*) (BADGE_BUTTON_START));
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_SELECT, mpr121_event_handler, (void*) (BADGE_BUTTON_SELECT));
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_DOWN  , mpr121_event_handler, (void*) (BADGE_BUTTON_DOWN));
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_RIGHT , mpr121_event_handler, (void*) (BADGE_BUTTON_RIGHT));
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_UP    , mpr121_event_handler, (void*) (BADGE_BUTTON_UP));
	badge_mpr121_set_interrupt_handler(MPR121_PIN_NUM_LEFT  , mpr121_event_handler, (void*) (BADGE_BUTTON_LEFT));
#endif // I2C_MPR121_ADDR

	// configure the voltage measuring for charging-info feedback
	badge_power_init();

	// configure the led-strip on top of the badge
#ifdef PIN_NUM_LEDS
	badge_leds_init();
#endif // PIN_NUM_LEDS

#if defined(PORTEXP_PIN_NUM_VIBRATOR) || defined(MPR121_PIN_NUM_VIBRATOR)
	badge_vibrator_init();
#endif // defined(PORTEXP_PIN_NUM_VIBRATOR) || defined(MPR121_PIN_NUM_VIBRATOR)

	badge_sdcard_init();

	// configure eink display
	badge_eink_init();

	badge_init_done = true;
}
