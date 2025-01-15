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

#ifndef INCLUDED_IEEE802_11_FRAME_EQUALIZER_IMPL_H
#define INCLUDED_IEEE802_11_FRAME_EQUALIZER_IMPL_H

#include "equalizer/base.h"
#include "viterbi_decoder/viterbi_decoder.h"
#include <ieee802_11/constellations.h>
#include <ieee802_11/frame_equalizer.h>

namespace gr {
namespace ieee802_11 {

enum frame_coding{
    BCC,
    LDPC
};

class frame_equalizer_impl : virtual public frame_equalizer
{

public:
    frame_equalizer_impl(Equalizer algo, double freq, double bw, bool log, bool debug);
    ~frame_equalizer_impl();

    void set_algorithm(Equalizer algo);
    void set_bandwidth(double bw);
    void set_frequency(double freq);

    void forecast(int noutput_items, gr_vector_int& ninput_items_required);
    int general_work(int noutput_items,
                     gr_vector_int& ninput_items,
                     gr_vector_const_void_star& input_items,
                     gr_vector_void_star& output_items);

private:
    bool parse_signal(uint8_t* signal);
    bool decode_signal_field(gr_complex* rx_bits);
    void print_coding(frame_coding coding);
    //void deinterleave(gr_complex* rx_symbols);
    //void unrepeat(gr_complex* rx_symbols);
    uint8_t compute_crc(uint8_t* crc_input);
    uint8_t crc4HaLoW_byte(uint8_t crc, void const *mem, size_t len);

    equalizer::base* d_equalizer;
    gr::thread::mutex d_mutex;
    std::vector<gr::tag_t> tags;
    bool d_debug;
    bool d_log;
    int d_current_symbol;
    int d_sig;//the current sig field number
    uint8_t d_sig_field_bits[200] = {0};//the bits contained in the sig field before decoding
    uint8_t d_crc4_input_bytes[4];//the input bytes to the crc computer
    /*
    80 should be enough. However, the viterbi algorithm needs to be able to read further than 80 (because of the traceback). For the sig field it has to access up until index 152). If value at index > 80 is !=0
    then the whole decoding breaks and leads to different decoded values at each run.
    */
    viterbi_decoder d_decoder;

    // freq offset
    double d_freq;                      // Hz
    double d_freq_offset_from_synclong; // Hz, estimation from "sync_long" block
    double d_bw;                        // Hz
    double d_er;
    double d_epsilon0;
    gr_complex d_prev_pilots_with_corrected_polarity[NUM_PILOTS] = {gr_complex(0,0)}; //initiate to 0

    const gr_complex LONG[SAMPLES_PER_OFDM_SYMBOL] = { 0,  0,  0,  1, -1,  1, -1, -1,  1, -1, 1, 1, -1, 1, 1, 1, 0, -1, -1, -1, 1, -1, -1, -1, 1, -1, 1, 1, 1, -1, 0, 0};
    
    //traveling pilots
    const int TRAVEL_PILOT1[TRAVELING_PILOT_POSITIONS] = {14, 6, 11, 3, 8, 13, 5, 10, 15, 7, 12, 4, 9};
    const int TRAVEL_PILOT2[TRAVELING_PILOT_POSITIONS] = {28,20, 25,17,22, 27,19, 24, 29,21, 26,18,23};
    bool d_travel_pilots = false;

    int d_frame_bytes;
    int d_frame_symbols;
    int d_frame_encoding;

    gr_complex d_deinterleaved[CODED_BITS_PER_OFDM_SYMBOL];
    gr_complex d_unrepeated[NUM_BITS_UNREPEATED_SIG_SYMBOL];
    gr_complex symbols[CODED_BITS_PER_OFDM_SYMBOL];

    std::shared_ptr<gr::digital::constellation> d_frame_mod;
    constellation_bpsk::sptr d_bpsk;
    constellation_qpsk::sptr d_qpsk;
    constellation_16qam::sptr d_16qam;
    constellation_64qam::sptr d_64qam;

};

} // namespace ieee802_11
} // namespace gr

#endif /* INCLUDED_IEEE802_11_FRAME_EQUALIZER_IMPL_H */
