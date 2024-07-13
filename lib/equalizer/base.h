/*
 * Copyright (C) 2015 Bastian Bloessl <bloessl@ccs-labs.org>
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

#ifndef INCLUDED_IEEE802_11_EQUALIZER_BASE_H
#define INCLUDED_IEEE802_11_EQUALIZER_BASE_H

#define SAMPLES_PER_OFDM_SYMBOL 32 //there are 32 HaLow subcarriers
#define CODED_BITS_PER_OFDM_SYMBOL 24 // there are 26 effective HaLow subcarriers. 2 are pilots, bringing the number of data subcarriers to 24 
#define NUM_PILOTS 2 //2 HaLow pilots. p.3253 of spec
#define PILOT1_INDEX 9 //technically -7 in the spec, but that is a range of [-16, 15). We're operating in range of [0, 31). p.3253 of spec
#define PILOT2_INDEX 23 //technically +7 in the spec, but see above ^. p.3253 of spec.
#define NUM_BITS_SIG_FIELD 36 //p.3246 of spec
#define NUM_SIG_FIELD_REPETITIONS 2 //p.3246 of spec
#define NUM_BITS_PER_REPETITION 6 //p.3246 of spec
#define MCS_FIRST_BIT_INDEX 7 //p.3246 of spec
#define MCS_LAST_BIT_INDEX 10 //p.3246 of spec, inclusive

#include <gnuradio/digital/constellation.h>
#include <gnuradio/gr_complex.h>

namespace gr {
namespace ieee802_11 {
namespace equalizer {

class base
{
public:
    virtual ~base(){};
    virtual void equalize(gr_complex* in,
                          int n,
                          gr_complex* symbols,
                          uint8_t* bits,
                          std::shared_ptr<gr::digital::constellation> mod) = 0;
    virtual double get_snr() = 0;

    static const gr_complex POLARITY[127];

    std::vector<gr_complex> get_csi();

protected:
    static const gr_complex LONG[SAMPLES_PER_OFDM_SYMBOL];

    gr_complex d_H[SAMPLES_PER_OFDM_SYMBOL];
};

} // namespace equalizer
} /* namespace ieee802_11 */
} /* namespace gr */

#endif /* INCLUDED_IEEE802_11_EQUALIZER_BASE_H */
