/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* PHASE as a plugin (felucca_plugin.h): the engine of src/eng_phase.c, playing as Felucca's engine 2. */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_phase.c"

FE_PLUGIN(ENG_PHASE, FE_ID('P', 'H', 'A', 'S'), 1, 2, 0)
