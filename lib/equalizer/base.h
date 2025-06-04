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

#include <gnuradio/digital/constellation.h>
#include <gnuradio/gr_complex.h>
#include "../utils.h"

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
                          gr_complex* bits,
                          uint8_t pilot1_index,
                          uint8_t pilot2_index,
                          std::shared_ptr<gr::digital::constellation> mod) = 0;

    virtual double get_snr() = 0;

    static const gr_complex POLARITY[127];

    std::vector<gr_complex> get_csi();

    gr_complex get_csi_at(int subcarrier_index);

protected:
    static const gr_complex LONG[SAMPLES_PER_OFDM_SYMBOL];

    gr_complex d_H[SAMPLES_PER_OFDM_SYMBOL] = {gr_complex(0, 0)};
};

} // namespace equalizer
} /* namespace ieee802_11 */
} /* namespace gr */

#endif /* INCLUDED_IEEE802_11_EQUALIZER_BASE_H */
