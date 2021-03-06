/*
 * Copyright (C) 2010-2012 The NITDroid Project
 * Author: Alexey Roslyakov <alexey.roslyakov@newsycat.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "lights"

#include <cutils/log.h>
#include <cutils/properties.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

// number of keyboard leds on Nokia n950
#define NUM_KEYBOARD_LEDS 6

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static struct light_state_t g_notification = {
	.color = 0,
};
static struct light_state_t g_battery = {
	.color = 0,
};

static int g_attention = 0;


static const char LCD_FILE[]
= "/sys/devices/omapdss/display0/backlight/display0/brightness";

static const char LCD_BLANK_FILE[]
= "/sys/devices/platform/omapfb/graphics/fb0/blank";

static const char KEYBOARD_FILE_TEMPLATE[]
= "/sys/class/leds/lp5523:channel%d/brightness";

static const char LED_BRIGHTNESS_FILE[]
= "/sys/devices/platform/i2c_omap.2/i2c-2/2-0032/leds/lp5521:channel0/brightness";

static const char ENGINE1_MODE_FILE[]
= "/sys/devices/platform/i2c_omap.2/i2c-2/2-0032/engine1_mode";

static const char ENGINE1_LOAD_FILE[]
= "/sys/devices/platform/i2c_omap.2/i2c-2/2-0032/engine1_load";

/**
 * device methods
 */

static int
write_string(char const *file, const char const *value)
{
	int fd;

	fd = open(file, O_WRONLY);
	if (fd >= 0)
	{
		char buffer[128];
		int bytes = snprintf(buffer, sizeof(buffer), "%s", value);
		int amt = write(fd, buffer, bytes);
		close(fd);
		return amt == -1 ? -errno : 0;
	}
	else {
		LOGE("%s failed to open %s\n", __func__, file);
		return -errno;
	}
}

static int
write_int(char const* file, int value)
{
	char buffer[20];
	int bytes = sprintf(buffer, "%d\n", value);
	return write_string(file, buffer);
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const* state)
{
    int color = state->color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))
            + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

static int
set_light_backlight(struct light_device_t* dev,
                    struct light_state_t const* state)
{
	static int lcdOff = 0;
    int err = 0;
    int brightness = rgb_to_brightness(state);
    pthread_mutex_lock(&g_lock);
    err = write_int(LCD_FILE, brightness);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int
set_light_keyboard(struct light_device_t* dev,
                   struct light_state_t const* state)
{
    int err = 0;
    int i = 0;
    char file[100];
    int brightness = rgb_to_brightness(state);
    pthread_mutex_lock(&g_lock);
    for(i = 0; i < NUM_KEYBOARD_LEDS; i++) {
        snprintf(file, sizeof(file), KEYBOARD_FILE_TEMPLATE, i);
        err = write_int(file, brightness);
        if (err != 0)
            break;
    }

    pthread_mutex_unlock(&g_lock);
    return err;
}

/*
// led
cd /sys/devices/platform/i2c_omap.2/i2c-2/2-0032
echo load > engine1_mode
echo 9d804000427f0d7f7f007f0042000000 > engine1_load
# - or -
echo 9d8040ff7f0040007f000000 > engine1_load
echo run > engine1_mode 
*/
static int
set_led_state_locked(struct light_device_t* dev,
                     struct light_state_t const* state)
{
    int len;
    int onMS, offMS;
	char enginePattern[35];

    switch (state->flashMode) {
      case LIGHT_FLASH_TIMED:
          onMS = state->flashOnMS;
          offMS = state->flashOffMS;
          break;
      case LIGHT_FLASH_NONE:
      default:
          onMS = 0;
          offMS = 0;
          break;
    }

    LOGV("set_led_state color=%08X, onMS=%d, offMS=%d\n",
         state->color, onMS, offMS);

    if (onMS > 0 && offMS > 0) {
        write_int(LED_BRIGHTNESS_FILE, 0);

        snprintf(enginePattern, sizeof(enginePattern),
                 "9d8040ff%02x004000%02x000000\n", 0x42+onMS/67, 0x42+offMS/67);
        //snprintf(enginePattern, sizeof(enginePattern), "9d8040ff7f0040007f000000\n"); // TODO: remove this

        LOGD("enginePattern: %s", enginePattern);

        write_string(ENGINE1_MODE_FILE, "load\n");
        write_string(ENGINE1_LOAD_FILE, enginePattern);
        write_string(ENGINE1_MODE_FILE, "run\n");
    } else {
		write_string(ENGINE1_MODE_FILE, "disabled\n");
		write_int(LED_BRIGHTNESS_FILE, rgb_to_brightness(state));
    }

    return 0;
}

static void
handle_speaker_battery_locked(struct light_device_t* dev)
{
    if (is_lit(&g_battery) && LIGHT_FLASH_TIMED == g_battery.flashMode) {
        set_led_state_locked(dev, &g_battery);
    } else {
        set_led_state_locked(dev, &g_notification);
    }
}

static int
set_light_battery(struct light_device_t* dev,
                  struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_battery = *state;
	LOGD("set_light_battery color=0x%08x", state->color);
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_notifications(struct light_device_t* dev,
                        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    LOGD("set_light_notifications color=0x%08x", state->color);
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_attention(struct light_device_t* dev,
                    struct light_state_t const* state)
{
#if 0
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    LOGD("set_light_attention color=0x%08x", state->color);
    if (state->flashMode == LIGHT_FLASH_HARDWARE) {
        g_attention = state->flashOnMS;
    } else if (state->flashMode == LIGHT_FLASH_NONE) {
        g_attention = 0;
    }
	handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
#endif
    return 0;
}

static int isKeyboardPresent()
{
    char propValue[PROPERTY_VALUE_MAX];
    if ( property_get("ro.hardware", propValue, NULL) &&
         strcmp(propValue, "nokiarm-696board") == 0 ) {
        LOGW("Keyboard isn't present on %s", propValue);
        return 0;
    }

    return 1;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t* module, char const* name,
                       struct hw_device_t** device)
{
    LOGD("open_lights %s", name);
    int (*set_light)(struct light_device_t* dev,
                     struct light_state_t const* state);
    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
    } else if (0 == strcmp(LIGHT_ID_KEYBOARD, name) && isKeyboardPresent()) {
        set_light = set_light_keyboard;
    }
#if 1
    else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
    }
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        set_light = set_light_notifications;
    }
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
    }
#endif
    else {
        return -EINVAL;
    }

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}


static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
const struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Nokia N9 lights Module",
    .author = "Alexey Roslyakov (e-yes)",
    .methods = &lights_module_methods,
};
