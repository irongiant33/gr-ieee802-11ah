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

#ifndef INCLUDED_IEEE802_11AH_EQUALIZER_STA_H
#define INCLUDED_IEEE802_11AH_EQUALIZER_STA_H

#include "base.h"
#include <vector>

namespace gr {
namespace ieee802_11ah {
namespace equalizer {

class sta : public base
{
public:
    virtual void equalize(gr_complex* in,
                          int n,
                          gr_complex* symbols,
                          gr_complex* bits,
                          uint8_t pilot1_index,
                          uint8_t pilot2_index,
                          std::shared_ptr<gr::digital::constellation> mod);
    double get_snr();

private:
    double d_snr;

    const double alpha = 0.5;
    const int beta = 2;
};

} // namespace equalizer
} /* namespace ieee802_11ah */
} /* namespace gr */

#endif /* INCLUDED_IEEE802_11AH_EQUALIZER_STA_H */
