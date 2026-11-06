/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* TRIO as a plugin (felucca_plugin.h): the engine of src/eng_trio.c, playing as Felucca's engine 6. */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_trio.c"

FE_PLUGIN(ENG_TRIO, FE_ID('T', 'R', 'I', 'O'), 1, 6, 0)
