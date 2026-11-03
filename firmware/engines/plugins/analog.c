/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ANALOG as a plugin (felucca_plugin.h): the engine of src/eng_analog.c, playing as Felucca's engine 0. */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_analog.c"

FE_PLUGIN(ENG_ANALOG, FE_ID('A', 'N', 'L', 'G'), 1, 0, 0)
