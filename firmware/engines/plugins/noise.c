/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* NOISE as a plugin (felucca_plugin.h): the engine of src/eng_noise.c, playing as Felucca's engine 11. */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_noise.c"

FE_PLUGIN(ENG_NOISE, FE_ID('N', 'O', 'I', 'S'), 1, 11, 0)
