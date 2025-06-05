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

#include "signal_field_impl.h"
#include "utils.h"
#include <gnuradio/digital/lfsr.h>

using namespace gr::ieee802_11ah;

signal_field::sptr signal_field::make()
{
    return signal_field::sptr(new signal_field_impl());
}

signal_field::signal_field() : packet_header_default(NUM_BITS_DECODED_SIG_SYMBOL * NUM_OFDM_SYMBOLS_IN_SIG_FIELD * 4, "packet_len"){};


signal_field_impl::signal_field_impl() : packet_header_default(NUM_BITS_DECODED_SIG_SYMBOL * NUM_OFDM_SYMBOLS_IN_SIG_FIELD * 4, "packet_len") {}


signal_field_impl::~signal_field_impl() {}


int signal_field_impl::get_bit(int b, int i) { return (b & (1 << i) ? 1 : 0); }


void signal_field_impl::generate_signal_field(char* out,
                                              frame_param& frame,
                                              ofdm_param& ofdm)
{

    // data bits of the signal header
    char* signal_header = (char*)malloc(sizeof(char) * NUM_BITS_DECODED_SIG_SYMBOL * NUM_OFDM_SYMBOLS_IN_SIG_FIELD);

    // signal header after...
    // convolutional encoding
    char* encoded_signal_header = (char*)malloc(sizeof(char) * NUM_BITS_DECODED_SIG_SYMBOL * NUM_OFDM_SYMBOLS_IN_SIG_FIELD * 2);
    // repeated
    char* repeated_signal_header = (char*)malloc(sizeof(char) * NUM_BITS_DECODED_SIG_SYMBOL * NUM_OFDM_SYMBOLS_IN_SIG_FIELD * 4);
    // interleaving
    char* interleaved_signal_header = (char*)malloc(sizeof(char) * NUM_BITS_DECODED_SIG_SYMBOL * NUM_OFDM_SYMBOLS_IN_SIG_FIELD * 4);

    int length = frame.psdu_size;

    // B0-B1 NSTS
    signal_header[0] = 0;//1 spatial stream
    signal_header[1] = 0;

    // B2 Short GI
    signal_header[2] = 0;//short GI

    // B3 Coding
    signal_header[3] = 0;//BCC

    // B4 LDPC Extra
    signal_header[4] = 1;//If Coding field is 0, this field is set to 1.

    // B5 STBC 
    signal_header[5] = 0;//1 spatial stream so no space time block coding

    // B6 
    signal_header[6] = 1;//Reserved

    // B7-B10 MCS
    signal_header[7] = get_bit(ofdm.encoding, 0);//MCS LSB first, MSB last
    signal_header[8] = get_bit(ofdm.encoding, 1);
    signal_header[9] = get_bit(ofdm.encoding, 2);
    signal_header[10] = get_bit(ofdm.encoding, 3);

    // B11 Aggregation
    signal_header[11] = 0;//length field represents number of bytes in ofdm frame

    // B12-B20 Length
    signal_header[12] = get_bit(length, 0);
    signal_header[13] = get_bit(length, 1);
    signal_header[14] = get_bit(length, 2);
    signal_header[15] = get_bit(length, 3);
    signal_header[16] = get_bit(length, 4);
    signal_header[17] = get_bit(length, 5);
    signal_header[18] = get_bit(length, 6);
    signal_header[19] = get_bit(length, 7);
    signal_header[20] = get_bit(length, 8);

    // B21-B22 Response Indication
    signal_header[21] = 0;// 0 for the moment
    signal_header[22] = 0;

    // B23 Smoothing
    signal_header[23] = 0;

    // B24 Travelling Pilots
    signal_header[24] = 0;//no travelling pilots for the moment

    // B25 NDP Indication
    signal_header[25] = 0;//no ndp for the moment

    // B26-B29 CRC
    uint8_t crc = compute_crc((uint8_t *) signal_header);

    signal_header[26] = get_bit(crc, 3);//MCS LSB first, MSB last
    signal_header[27] = get_bit(crc, 2);
    signal_header[28] = get_bit(crc, 1);
    signal_header[29] = get_bit(crc, 0);

    // B30-B35 reset conv coder
    signal_header[30] = 0;
    signal_header[31] = 0;
    signal_header[32] = 0;
    signal_header[33] = 0;
    signal_header[34] = 0;
    signal_header[35] = 0;


    ofdm_param signal_ofdm(BPSK_1_2_REP);
    frame_param signal_param(signal_ofdm);

    // convolutional encoding (scrambling is not needed)
    convolutional_encoding(signal_header, encoded_signal_header, signal_param);
    // repeating
    repeat(encoded_signal_header, repeated_signal_header, signal_param, signal_ofdm);
    // interleaving
    interleave(repeated_signal_header, out, signal_param, signal_ofdm);
    //TODO add p_n multiplyer here

    free(signal_header);
    free(encoded_signal_header);
    free(interleaved_signal_header);
}

bool signal_field_impl::header_formatter(long packet_len,
                                         unsigned char* out,
                                         const std::vector<tag_t>& tags)
{

    bool encoding_found = false;
    bool len_found = false;
    int encoding = 0;
    int len = 0;

    // read tags
    for (int i = 0; i < tags.size(); i++) {
        if (pmt::eq(tags[i].key, pmt::mp("encoding"))) {
            encoding_found = true;
            encoding = pmt::to_long(tags[i].value);
        } else if (pmt::eq(tags[i].key, pmt::mp("psdu_len"))) {
            len_found = true;
            len = pmt::to_long(tags[i].value);
        }
    }

    // check if all tags are present
    if ((!encoding_found) || (!len_found)) {
        return false;
    }

    ofdm_param ofdm((Encoding)encoding);
    frame_param frame(ofdm, len);

    generate_signal_field((char*)out, frame, ofdm);
    return true;
}

bool signal_field_impl::header_parser(const unsigned char* in, std::vector<tag_t>& tags)
{

    throw std::runtime_error("not implemented yet");
    return false;
}
