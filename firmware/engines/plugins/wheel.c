/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WHEEL as a plugin (felucca_plugin.h): the engine of src/eng_wheel.c, playing as Felucca's engine 7. */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_wheel.c"

static void wheel_reset(void)                   /* the rotors and bar gains as at power-on (no libc: a loop) */
{
    volatile uint8_t *p = (volatile uint8_t *)drw_t;
    uint32_t i;
    for (i = 0; i < sizeof drw_t; i++)
        p[i] = 0;
    p = (volatile uint8_t *)drw_v;
    for (i = 0; i < sizeof drw_v; i++)
        p[i] = 0;
}

FE_PLUGIN(ENG_WHEEL, FE_ID('W', 'H', 'E', 'L'), 1, 7, wheel_reset)
