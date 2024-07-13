/*
 * Copyright (C) 2016 Bastian Bloessl <bloessl@ccs-labs.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ls.h"
#include <cstring>
#include <iostream>

using namespace gr::ieee802_11::equalizer;

void ls::equalize(gr_complex* in,
                  int n,
                  gr_complex* symbols,
                  uint8_t* bits,
                  std::shared_ptr<gr::digital::constellation> mod)
{

    if (n == 0) {
        std::memcpy(d_H, in, SAMPLES_PER_OFDM_SYMBOL * sizeof(gr_complex));

    } else if (n == 1) {
        double signal = 0;
        double noise = 0;
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            if ((i == 16) || (i < 3) || (i > 29)) { //dividing by two gets you the dead DC pilot subcarrier, the other indices correspond to dead lower and upper subcarriers. 3 dead lower ones, 2 dead upper ones
                continue;
            }
            noise += std::pow(std::abs(d_H[i] - in[i]), 2);
            signal += std::pow(std::abs(d_H[i] + in[i]), 2);
            d_H[i] += in[i];
            d_H[i] /= LONG[i] * gr_complex(2, 0);
        }

        d_snr = 10 * std::log10(signal / noise / 2);

    } else {

        int c = 0;
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            if ((i == PILOT1_INDEX) || (i == 16) || (i == PILOT2_INDEX) ||
                (i < 3) || (i > 29)) {
                continue; //ignore all pilots and dead subcarriers
            } else {
                symbols[c] = in[i] / d_H[i];
                bits[c] = mod->decision_maker(&symbols[c]);
                c++;
            }
        }
    }
}

double ls::get_snr() { return d_snr; }
