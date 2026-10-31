/* SPDX-License-Identifier: MIT
 * Portable layout sanity check, NOT a substitute for building the driver.
 */
#include <stdio.h>
#include "protocol.h"

int main(void)
{
    if (RL_IOCTL_START != 0x8000E000u || RL_IOCTL_READ != 0x8000E004u ||
        RL_IOCTL_STOP != 0x8000E008u) return 1;
    printf("protocol=%u event=%zu batch_header=%u batch=%zu\n",
           RL_PROTOCOL_VERSION, sizeof(RL_EVENT), RL_BATCH_HEADER_SIZE, sizeof(RL_BATCH));
    return 0;
}
