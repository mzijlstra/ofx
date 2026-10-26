// The vendored code on its own, for the GUI mutants' build (Makefile, build/ofx-mut): Clay's and
// nanosvg's (2026-10-26, about 0.5 s a build), compiled once per run instead of once per mutant.
// The everyday builds compile them in main.c.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CLAY_IMPLEMENTATION
#include "clay.h"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
