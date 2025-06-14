/*
 * Copyright (C) 2013 Bastian Bloessl <bloessl@ccs-labs.org>
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
#ifndef INCLUDED_IEEE802_11AH_ETHER_ENCAP_H
#define INCLUDED_IEEE802_11AH_ETHER_ENCAP_H

#include <gnuradio/block.h>
#include <ieee802_11ah/api.h>

namespace gr {
namespace ieee802_11ah {

class IEEE802_11AH_API ether_encap : virtual public block
{
public:
    typedef std::shared_ptr<ether_encap> sptr;
    static sptr make(bool debug);
};

} // namespace ieee802_11ah
} // namespace gr

#endif /* INCLUDED_IEEE802_11AH_ETHER_ENCAP_H */
