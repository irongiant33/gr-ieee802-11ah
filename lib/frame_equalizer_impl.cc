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

#include "equalizer/base.h"
#include "equalizer/comb.h"
#include "equalizer/lms.h"
#include "equalizer/ls.h"
#include "equalizer/sta.h"
#include "frame_equalizer_impl.h"
#include "utils.h"
#include <gnuradio/io_signature.h>

namespace gr {
namespace ieee802_11 {

frame_equalizer::sptr
frame_equalizer::make(Equalizer algo, double freq, double bw, bool log, bool debug)
{
    return gnuradio::get_initial_sptr(
        new frame_equalizer_impl(algo, freq, bw, log, debug));
}


frame_equalizer_impl::frame_equalizer_impl(
    Equalizer algo, double freq, double bw, bool log, bool debug)
    : gr::block("frame_equalizer",
                gr::io_signature::make(1, 1, SAMPLES_PER_OFDM_SYMBOL * sizeof(gr_complex)),
                gr::io_signature::make(1, 1, CODED_BITS_PER_OFDM_SYMBOL)),
      d_current_symbol(0),
      d_log(log),
      d_debug(debug),
      d_equalizer(NULL),
      d_freq(freq),
      d_bw(bw),
      d_frame_bytes(0),
      d_frame_symbols(0),
      d_freq_offset_from_synclong(0.0)
{

    message_port_register_out(pmt::mp("symbols"));

    d_bpsk = constellation_bpsk::make();
    d_qpsk = constellation_qpsk::make();
    d_16qam = constellation_16qam::make();
    d_64qam = constellation_64qam::make();

    d_frame_mod = d_bpsk;

    set_tag_propagation_policy(block::TPP_DONT);
    set_algorithm(algo);
}

frame_equalizer_impl::~frame_equalizer_impl() {}


void frame_equalizer_impl::set_algorithm(Equalizer algo)
{
    gr::thread::scoped_lock lock(d_mutex);
    delete d_equalizer;

    switch (algo) {

    case COMB:
        dout << "Comb" << std::endl;
        d_equalizer = new equalizer::comb();
        break;
    case LS:
        dout << "LS" << std::endl;
        d_equalizer = new equalizer::ls();
        break;
    case LMS:
        dout << "LMS" << std::endl;
        d_equalizer = new equalizer::lms();
        break;
    case STA:
        dout << "STA" << std::endl;
        d_equalizer = new equalizer::sta();
        break;
    default:
        throw std::runtime_error("Algorithm not implemented");
    }
}

void frame_equalizer_impl::set_bandwidth(double bw)
{
    gr::thread::scoped_lock lock(d_mutex);
    d_bw = bw;
}

void frame_equalizer_impl::set_frequency(double freq)
{
    gr::thread::scoped_lock lock(d_mutex);
    d_freq = freq;
}

void frame_equalizer_impl::forecast(int noutput_items,
                                    gr_vector_int& ninput_items_required)
{
    ninput_items_required[0] = noutput_items;
}

int frame_equalizer_impl::general_work(int noutput_items,
                                       gr_vector_int& ninput_items,
                                       gr_vector_const_void_star& input_items,
                                       gr_vector_void_star& output_items)
{

    gr::thread::scoped_lock lock(d_mutex);

    const gr_complex* in = (const gr_complex*)input_items[0];
    uint8_t* out = (uint8_t*)output_items[0];

    int i = 0;
    int o = 0;
    gr_complex symbols[CODED_BITS_PER_OFDM_SYMBOL];
    gr_complex current_symbol[SAMPLES_PER_OFDM_SYMBOL];

    dout << "FRAME EQUALIZER: input " << ninput_items[0] << "  output " << noutput_items
         << std::endl;

    while ((i < ninput_items[0]) && (o < noutput_items)) {

        get_tags_in_window(tags, 0, i, i + 1, pmt::string_to_symbol("wifi_start"));

        // new frame
        if (tags.size()) {
            d_current_symbol = 0;
            d_frame_symbols = 0;
            d_frame_mod = d_bpsk;

            d_freq_offset_from_synclong =
                pmt::to_double(tags.front().value) * d_bw / (2 * M_PI);
            d_epsilon0 = pmt::to_double(tags.front().value) * d_bw / (2 * M_PI * d_freq);
            d_er = 0;

            dout << "epsilon: " << d_epsilon0 << std::endl;
        }

        // not interesting -> skip
        if (d_current_symbol > (d_frame_symbols + 2)) {
            i++;
            continue;
        }

        std::memcpy(current_symbol, in + i * SAMPLES_PER_OFDM_SYMBOL, SAMPLES_PER_OFDM_SYMBOL * sizeof(gr_complex));

        // compensate sampling offset
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            current_symbol[i] *= exp(gr_complex(0,
                                                2 * M_PI * d_current_symbol * (SAMPLES_PER_OFDM_SYMBOL + SAMPLES_PER_GI) * 
                                                    (d_epsilon0 + d_er) * (i - 32) / SAMPLES_PER_OFDM_SYMBOL)); //what is 32? Half of the 802.11a number of subcarriers?
        }

        gr_complex p = equalizer::base::POLARITY[(d_current_symbol - 2) % 127];

        double beta;
        if (d_current_symbol < 2) {
            beta = arg(current_symbol[PILOT2_INDEX]
                       - current_symbol[PILOT1_INDEX]); //unsure whether to add/subtract the pilot?

        } else {
            beta = arg((current_symbol[PILOT2_INDEX] * p) + 
                       (current_symbol[PILOT1_INDEX] * p)); //unsure whether to multiply by p or -p?
        }

        double er = arg((conj(d_prev_pilots[1]) * current_symbol[PILOT1_INDEX] * p) +
                        (conj(d_prev_pilots[2]) * current_symbol[PILOT2_INDEX] * p)); //unsure whether to multiply by p or -p?

        er *= d_bw / (2 * M_PI * d_freq * (SAMPLES_PER_OFDM_SYMBOL + SAMPLES_PER_GI));

        if (d_current_symbol < 2) {
            d_prev_pilots[0] = current_symbol[PILOT1_INDEX];
            d_prev_pilots[1] = -current_symbol[PILOT2_INDEX];
        } else {
            d_prev_pilots[0] = current_symbol[PILOT1_INDEX] * p; //unsure whether to multiply by p or -p?
            d_prev_pilots[1] = current_symbol[PILOT2_INDEX] * p;
        }

        // compensate residual frequency offset
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            current_symbol[i] *= exp(gr_complex(0, -beta));
        }

        // update estimate of residual frequency offset
        if (d_current_symbol >= 2) {

            double alpha = 0.1;
            d_er = (1 - alpha) * d_er + alpha * er;
        }

        // do equalization
        d_equalizer->equalize(
            current_symbol, d_current_symbol, symbols, out + o * CODED_BITS_PER_OFDM_SYMBOL, d_frame_mod);

        // signal field, it takes 6 OFDM symbols to make the SIG field
        if (d_current_symbol >= 2 && d_current_symbol < 8) {
            o++;
        }
        if (d_current_symbol == 7){

            if (decode_signal_field(out + o * CODED_BITS_PER_OFDM_SYMBOL)) {

                pmt::pmt_t dict = pmt::make_dict();
                dict = pmt::dict_add(
                    dict, pmt::mp("frame bytes"), pmt::from_uint64(d_frame_bytes));
                dict = pmt::dict_add(
                    dict, pmt::mp("encoding"), pmt::from_uint64(d_frame_encoding));
                dict = pmt::dict_add(
                    dict, pmt::mp("snr"), pmt::from_double(d_equalizer->get_snr()));
                dict = pmt::dict_add(
                    dict, pmt::mp("nominal frequency"), pmt::from_double(d_freq));
                dict = pmt::dict_add(dict,
                                     pmt::mp("frequency offset"),
                                     pmt::from_double(d_freq_offset_from_synclong));
                dict = pmt::dict_add(dict, pmt::mp("beta"), pmt::from_double(beta));

                std::vector<gr_complex> csi = d_equalizer->get_csi();
                dict = pmt::dict_add(
                    dict, pmt::mp("csi"), pmt::init_c32vector(csi.size(), csi));

                pmt::pmt_t pairs = pmt::dict_items(dict);
                for (int i = 0; i < pmt::length(pairs); i++) {
                    pmt::pmt_t pair = pmt::nth(i, pairs);
                    add_item_tag(0,
                                 nitems_written(0) + o,
                                 pmt::car(pair),
                                 pmt::cdr(pair),
                                 alias_pmt());
                }
            }
        }

        // data
        if (d_current_symbol >= 8) {
            o++;
            pmt::pmt_t pdu = pmt::make_dict();
            message_port_pub(
                pmt::mp("symbols"),
                pmt::cons(pmt::make_dict(), pmt::init_c32vector(CODED_BITS_PER_OFDM_SYMBOL, symbols)));
        }

        i++;
        d_current_symbol++;
    }

    consume(0, i);
    return o;
}

bool frame_equalizer_impl::decode_signal_field(uint8_t* rx_bits)
{

    static ofdm_param ofdm(BPSK_1_2);
    static frame_param frame(ofdm, 0);

    deinterleave(rx_bits);
    uint8_t* decoded_bits = d_decoder.decode(&ofdm, &frame, d_deinterleaved);

    return parse_signal(decoded_bits);
}

void frame_equalizer_impl::deinterleave(uint8_t* rx_bits)
{
    for (int i = 0; i < CODED_BITS_PER_OFDM_SYMBOL; i++) {
        d_deinterleaved[i] = rx_bits[interleaver_pattern[i]];
    }
}

//p.3246 wifi spec
bool frame_equalizer_impl::parse_signal(uint8_t* decoded_bits)
{

    int mcs = 0;
    int frame_bit_index = 0;
    d_frame_bytes = 0;
    bool parity = false;
    for (int i = 0; i < NUM_BITS_SIG_FIELD * NUM_SIG_FIELD_REPETITIONS; i++) {
        parity ^= decoded_bits[i];

        //only accounts for the first repetition
        if ((i >= MCS_FIRST_BIT_INDEX * NUM_SIG_FIELD_REPETITIONS) && (i <= MCS_LAST_BIT_INDEX * NUM_SIG_FIELD_REPETITIONS) && decoded_bits[i]) {
            mcs = mcs | (1 << (MCS_LAST_BIT_INDEX * NUM_SIG_FIELD_REPETITIONS - i));
        }

        // separate if statement because d_frame_bytes is int and can only store 32 bits
        /*need to account for repetition
        if (decoded_bits[i] && (i < MCS_FIRST_BIT_INDEX) && (i > MCS_LAST_BIT_INDEX)) {
            d_frame_bytes = d_frame_bytes | (1 << (32 - frame_bit_index));
            frame_bit_index++;
        }*/
    }

    /* unused with HaLow, CRC instead
    if (parity != decoded_bits[17]) {
        dout << "SIGNAL: wrong parity" << std::endl;
        return false;
    }*/

    switch (mcs) { //table 23-41
    case 0:
        d_frame_encoding = 0;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)24); //idk what this is doing
        d_frame_mod = d_bpsk;
        dout << "Encoding: 300 kbit/s   ";
        break;
    case 1:
        d_frame_encoding = 1;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)36); //idk what this is doing
        d_frame_mod = d_qpsk;
        dout << "Encoding: 600 kbit/s   ";
        break;
    case 2:
        d_frame_encoding = 2;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)48); //idk what this is doing
        d_frame_mod = d_qpsk;
        dout << "Encoding: 900 kbit/s   ";
        break;
    case 3:
        d_frame_encoding = 3;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)72); //idk what this is doing
        d_frame_mod = d_16qam;
        dout << "Encoding: 1200 kbit/s   ";
        break;
    case 4:
        d_frame_encoding = 4;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)96); //idk what this is doing
        d_frame_mod = d_16qam;
        dout << "Encoding: 1800 kbit/s   ";
        break;
    case 5:
        d_frame_encoding = 5;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)144); //idk what this is doing
        d_frame_mod = d_64qam;
        dout << "Encoding: 2400 kbit/s   ";
        break;
    case 6:
        d_frame_encoding = 6;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)192); //idk what this is doing
        d_frame_mod = d_64qam;
        dout << "Encoding: 2700 kbit/s   ";
        break;
    case 7:
        d_frame_encoding = 7;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)216); //idk what this is doing
        d_frame_mod = d_64qam;
        dout << "Encoding: 3000 kbit/s   ";
        break;
    case 10:
        d_frame_encoding = 10;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)240); //idk what this is doing, just added 24 to case 7
        d_frame_mod = d_bpsk;
        dout << "Encoding: 150 kbit/s   ";
        break;
    default:
        dout << "unsupported encoding" << std::endl;
        return false;
    }

    mylog("encoding: {} - length: {} - symbols: {}",
          d_frame_encoding,
          d_frame_bytes,
          d_frame_symbols);
    return true;
}

const int frame_equalizer_impl::interleaver_pattern[CODED_BITS_PER_OFDM_SYMBOL] = {
    0, 3, 6, 9,  12, 15, 18, 21,
    1, 4, 7, 10, 13, 16, 19, 22,
    2, 5, 8, 11, 14, 17, 20, 23
}; //table 23-20 and table 23-41

} /* namespace ieee802_11 */
} /* namespace gr */
