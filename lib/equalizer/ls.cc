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
    /*
    if (n == 0) {
        std::memcpy(d_H, in, SAMPLES_PER_OFDM_SYMBOL * sizeof(gr_complex));

    }
    else if( n <= 2){

        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            if ((i == 16) || (i < 3) || (i > 29)) { //dividing by two gets you the dead DC pilot subcarrier, the other indices correspond to dead lower and upper subcarriers. 3 dead lower ones, 2 dead upper ones
                continue;
            }
            d_H[i] += in[i];
        }
    }
    else if (n == 3) {
        //debug
        //std::cout << "H : ";

        double signal = 0;
        double noise = 0;
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            if ((i == 16) || (i < 3) || (i > 29)) { //dividing by two gets you the dead DC pilot subcarrier, the other indices correspond to dead lower and upper subcarriers. 3 dead lower ones, 2 dead upper ones
                continue;
            }
            //TODO : add snr computation
            //noise += std::pow(std::abs(d_H[i] - in[i]), 2);
            //signal += std::pow(std::abs(d_H[i] + in[i]), 2);
            d_H[i] += in[i];
            d_H[i] /= LONG[i] * gr_complex(4, 0);
            
            //debug
            //std::cout << abs(d_H[i])  << std::endl;
        }

        //d_snr = 10 * std::log10(signal / noise / 2);

    }
    */
    if(n < NUM_OFDM_SYMBOLS_IN_LTF1){//if we are in LTF1
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            if ((i == 16) || (i < 3) || (i > 29)) {
                continue;//skip if dead subcarrier
            }
            d_H[i] *= gr_complex((float) n / (n + 1), 0);//rescale current mean iaw previous and current mean sizes
            d_H[i] += (in[i] / LONG[i]) / gr_complex(n + 1, 0);//add latest channel estimate, scaled to current mean size
        }
        
        //debug
        /*
        std::cout << "From Equalizer LS : In at 0 is " << abs(in[0]) << std::endl;
        std::cout << "From Equalizer LS : In at 1 is " << abs(in[1]) << std::endl;
        std::cout << "From Equalizer LS : In at 2 is " << abs(in[2]) << std::endl;
        std::cout << "From Equalizer LS : In at 3 is " << abs(in[3]) << std::endl;
        std::cout << "From Equalizer LS : In at 15 is " << abs(in[15]) << std::endl;
        std::cout << "From Equalizer LS : In at 16 is " << abs(in[16]) << std::endl;
        std::cout << "From Equalizer LS : In at 17 is " << abs(in[17]) << std::endl;
        std::cout << "From Equalizer LS : In at 29 is " << abs(in[29]) << std::endl;
        std::cout << "From Equalizer LS : In at 30 is " << abs(in[30]) << std::endl;
        std::cout << "From Equalizer LS : In at 31 is " << abs(in[31]) << std::endl;
        */
    }
    else {
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
