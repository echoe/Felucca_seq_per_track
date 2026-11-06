/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* LOFI as a plugin (felucca_plugin.h): the engine of src/eng_lofi.c, playing as Felucca's engine 3. */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"
#include "eng_lofi.c"

FE_PLUGIN(ENG_LOFI, FE_ID('L', 'O', 'F', 'I'), 1, 3, 0)
