#ifndef MAYBE_GETS_H
#define MAYBE_GETS_H

#include <stdio.h>

#if __STDC_VERSION__ >= 202000L
#define MAYBE_GETS(buf) gets(buf)
#else
#define MAYBE_GETS(buf) ((void)(buf))
#endif

#endif
