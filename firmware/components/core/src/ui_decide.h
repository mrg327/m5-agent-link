/* The decision toys on glass (ui_decide.c over decide.c): the coin, the die and the oracle, one toy
 * in LABS whose B steps through the three (EXPERIMENTS.md "Toys", issue 20261126-74ee). */
#pragma once
#include <stdint.h>

#include "ui_toy.h"

extern const ui_toy_t ui_decide_toy;

/** @brief Restart the random source from @p seed and stop stirring it: repeatable throws. */
void ui_decide_seed(uint32_t seed);
