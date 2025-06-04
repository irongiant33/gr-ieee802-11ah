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

#include "base.h"
#include <cstring>
#include <iostream>

using namespace gr::ieee802_11::equalizer;

const gr_complex base::LONG[] = { 0,  0,  0,  1, -1,  1, -1, -1,  1, -1, 1, 1, -1, 1, 1, 1, 0, -1, -1, -1, 1, -1, -1, -1, 1, -1, 1, 1, 1, -1, 0, 0};
    //old (and incorrect) 
    //0,  0,  0,  1,  1, -1, -1,  1,  1, -1, 1, -1,  1,  1,  1,  1,  0,  1, -1, -1, 1, -1,  1,  1,  1,  1,  1,  1, -1, -1, 0,  0};//maybe this is the first 48 (24 in the case of HaLow) values from the polarity field below, after the SIG

const gr_complex base::POLARITY[127] = {
    1,  1,  1,  1,  -1, -1, -1, 1,  -1, -1, -1, -1, 1,  1,  -1, 1,  -1, -1, 1, 1,  -1, 1,
    1,  -1, 1,  1,  1,  1,  1,  1,  -1, 1,  1,  1,  -1, 1,  1,  -1, -1, 1,  1, 1,  -1, 1,
    -1, -1, -1, 1,  -1, 1,  -1, -1, 1,  -1, -1, 1,  1,  1,  1,  1,  -1, -1, 1, 1,  -1, -1,
    1,  -1, 1,  -1, 1,  1,  -1, -1, -1, 1,  1,  -1, -1, -1, -1, 1,  -1, -1, 1, -1, 1,  1,
    1,  1,  -1, 1,  -1, 1,  -1, 1,  -1, -1, -1, -1, -1, 1,  -1, 1,  1,  -1, 1, -1, 1,  1,
    1,  -1, -1, 1,  -1, -1, -1, 1,  1,  1,  -1, -1, -1, -1, -1, -1, -1
}; //original from p.2826

/*
    1,  1,  1,  1,  -1, -1, -1, 1,  -1, -1, -1, -1, 1,  1,  -1, 1,  -1, -1, 1, 1,  -1, 1,
    1,  -1, 1,  1,  1,  1,  1,  1,  -1, 1,  1,  1,  -1, 1,  1,  -1, -1, 1,  1, 1,  -1, 1,
    -1, -1, -1, 1,  -1, 1,  -1, -1, 1,  -1, -1, 1,  1,  1,  1,  1,  -1, -1, 1, 1,  -1, -1,
    1,  -1, 1,  -1, 1,  1,  -1, -1, -1, 1,  1,  -1, -1, -1, -1, 1,  -1, -1, 1, -1, 1,  1,
    1,  1,  -1, 1,  -1, 1,  -1, 1,  -1, -1, -1, -1, -1, 1,  -1, 1,  1,  -1, 1, -1, 1,  1,
    1,  -1, -1, 1,  -1, -1, -1, 1,  1,  1,  -1, -1, -1, -1, -1, -1, -1
*/  //sequence I copied (trusted) and is the same as above ==> no errors

std::vector<gr_complex> base::get_csi()
{
    std::vector<gr_complex> csi;
    csi.reserve(26);
    for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
        if ((i == 16) || (i < 3) || (i > 29)) {
            continue;
        }
        csi.push_back(d_H[i]);
    }
    return csi;
}

gr_complex base::get_csi_at(int subcarrier_index)
{
    if ((subcarrier_index == 16) || (subcarrier_index < 3) || (subcarrier_index > 29)) {
        return gr_complex(0,0);
    }
    else{
        return d_H[subcarrier_index];
    }
}
