/*
 * Copyright (C) 2013, 2016 Bastian Bloessl <bloessl@ccs-labs.org>
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
#ifndef INCLUDED_IEEE802_11AH_MAPPER_H
#define INCLUDED_IEEE802_11AH_MAPPER_H

#include <cstdint>
#include <gnuradio/block.h>
#include <ieee802_11ah/api.h>

namespace gr {
namespace ieee802_11ah {

enum Encoding {
    BPSK_1_2 = 0,
    QPSK_1_2 = 1,
    QPSK_3_4 = 2,
    QAM16_1_2 = 3,
    QAM16_3_4 = 4,
    QAM64_2_3 = 5,
    QAM64_3_4 = 6,
    QAM64_5_6 = 7,
    BPSK_1_2_REP = 10,
};

// Required for fmt 10
inline uint8_t format_as(Encoding e) {
  return static_cast<uint8_t>(e);
}

class IEEE802_11AH_API mapper : virtual public block
{
public:
    typedef std::shared_ptr<mapper> sptr;
    static sptr make(Encoding mcs, bool debug = false);
    virtual void set_encoding(Encoding mcs) = 0;
};

} // namespace ieee802_11ah
} // namespace gr

#endif /* INCLUDED_IEEE802_11AH_MAPPER_H */
