/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

/*****************************************************************************/

#define CPUFREQ "/sys/devices/system/cpu/cpu0/cpufreq/"

static const char CPUFREQ_GOVERNOR[] = CPUFREQ "scaling_governor";
static const char CPUFREQ_SETSPEED[] = CPUFREQ "scaling_setspeed";

static const char LCD_BLANK[] = "/sys/class/graphics/fb0/blank";
static const char DISABLE_TS[] = "/sys/devices/platform/i2c_omap.2/i2c-2/2-004b/disable_ts";
static const char CPR_COEF[] = "/sys/devices/platform/omapdss/manager0/cpr_coef";
static const char CPR_ENABLE[] = "/sys/devices/platform/omapdss/manager0/cpr_enable";
static int fb = -1;

int main(int argc, char** argv) {
   if (fb == -1) {
       fb = open("/dev/graphics/fb0", O_RDWR, 0);
   }

   int x=0, y=0, width=854, height=480;
#define _IOC(dir,type,nr,size)                  \
    (((dir)  << _IOC_DIRSHIFT) |                \
     ((type) << _IOC_TYPESHIFT) |               \
     ((nr)   << _IOC_NRSHIFT) |                 \
     ((size) << _IOC_SIZESHIFT))
#define _IOW(type,nr,size)      _IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IO(type,nr)            _IOC(_IOC_NONE,(type),(nr),0)

#define OMAP_IOW(num, dtype)    _IOW('O', num, dtype)
#define OMAP_IO(num)            _IO('O', num)

    struct omapfb_update_window_old {
        __u32 x, y;
        __u32 width, height;
        __u32 format;
    };

    const unsigned int OMAPFB_UPDATE_WINDOW_OLD = OMAP_IOW(47, struct omapfb_update_window_old);
    struct omapfb_update_window_old update;
    memset(&update, 0, sizeof(omapfb_update_window_old));
    update.format = 0; /*OMAPFB_COLOR_RGB565*/
    update.x = x;
    update.y = y;
    update.width = width;
    update.height = height;
    while (true) {
        if (ioctl(fb, OMAPFB_UPDATE_WINDOW_OLD, &update) < 0) {
            printf("Could not ioctl(OMAPFB_UPDATE_WINDOW): %s\n", strerror(errno));
        }
        usleep(16666);
    }

    close(fb);
    return 0;
}
