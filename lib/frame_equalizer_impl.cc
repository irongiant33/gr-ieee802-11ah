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
namespace ieee802_11ah {

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
                gr::io_signature::make(1, 1, CODED_BITS_PER_OFDM_SYMBOL * sizeof(gr_complex))),
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
    gr_complex* out = (gr_complex*)output_items[0];

    int i = 0;
    int o = 0;
    gr_complex symbols[CODED_BITS_PER_OFDM_SYMBOL];
    gr_complex current_symbol[SAMPLES_PER_OFDM_SYMBOL];

    dout << "FRAME EQUALIZER: input " << ninput_items[0] << "  output " << noutput_items
         << std::endl;

    while ((i < ninput_items[0]) && (o < noutput_items)) {
        
        dout << "d_current_symbol: " << d_current_symbol << " i: " << i << " o: " << o << std::endl;

        get_tags_in_window(tags, 0, i, i + 1, pmt::string_to_symbol("wifi_start"));

        // new frame
        if (tags.size()) {
            d_current_symbol = 0;
            d_frame_symbols = 0;
            d_sig = 0;
            d_travel_pilots = false;
            d_frame_mod = d_bpsk;

            d_freq_offset_from_synclong =
                pmt::to_double(tags.front().value) * d_bw / (2 * M_PI);
            d_epsilon0 = pmt::to_double(tags.front().value) * d_bw / (2 * M_PI * d_freq);
            d_er = 0;

            dout << "epsilon: " << d_epsilon0 << std::endl;
        }

        // if we reached the end of the frame, drop remaining samples
        if (d_current_symbol > (d_frame_symbols + NUM_OFDM_SYMBOLS_IN_LTF1 + NUM_OFDM_SYMBOLS_IN_SIG_FIELD)) {
            i++;
            continue;
        }
        

        std::memcpy(current_symbol, in + i * SAMPLES_PER_OFDM_SYMBOL, SAMPLES_PER_OFDM_SYMBOL * sizeof(gr_complex));

        //we define pilot_mapping, which holds the values of the pilots iaw pilot mapping p.3253
        gr_complex pilot_mapping[NUM_PILOTS];

        if(d_current_symbol < NUM_OFDM_SYMBOLS_IN_LTF1){//pilot mapping is always {-1, -1} for all LTS inside LTF1
            pilot_mapping[0] = -1;
            pilot_mapping[1] = -1;
        }

        else{
            //As from the end of LTF1 until end of DATA field, pilot mapping is {1, -1} for all even symbols and {-1, 1} for all odd symbols.
            gr_complex first_pilot = d_current_symbol % 2 ? -1 : 1;                     
            //we mulitply the pilot values with a polarity factor as described in OFMD modulation p.3258
            gr_complex p_n = equalizer::base::POLARITY[(d_current_symbol - NUM_OFDM_SYMBOLS_IN_LTF1) % 127];

            pilot_mapping[0] = first_pilot * p_n;
            pilot_mapping[1] = - first_pilot * p_n;

        }

        //compute the pilot indexes
        uint8_t pilot1_index = PILOT1_INDEX;
        uint8_t pilot2_index = PILOT2_INDEX;

        if(d_travel_pilots){//in the case of traveling pilots

            //compute the indexes iaw table 23-21 (p. 3254)
            uint8_t m = (d_current_symbol - NUM_OFDM_SYMBOLS_IN_LTF1 - NUM_OFDM_SYMBOLS_IN_SIG_FIELD) % TRAVELING_PILOT_POSITIONS;

            pilot1_index = TRAVEL_PILOT1[m];
            pilot2_index = TRAVEL_PILOT2[m];
        }

        // compensate sampling offset        
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            current_symbol[i] *= exp(gr_complex(0,
                                                - 2 * M_PI * d_current_symbol * (SAMPLES_PER_OFDM_SYMBOL + SAMPLES_PER_GI) * 
                                                    (d_epsilon0 + d_er) * (i - SAMPLES_PER_OFDM_SYMBOL / 2) / SAMPLES_PER_OFDM_SYMBOL));
            
            //@irongiant33 To answer your question "what is 32? Half of the 802.11a number of subcarriers?". The "32" seem indeed to come from the number of subcarriers in 802.11a. The sampling offset compensation performed is described in Equation 7 of paper "Frequency Offset Estimation and Correction in the IEEE 802.11a WLAN" (see https://openofdm.readthedocs.io/en/latest/_downloads/vtc04_freq_offset.pdf). The number (i - 32) actually corresponds to the k variable that ranges from -26 to 26. Consequently, you should replace "32" with "SAMPLES_PER_OFDM_SYMBOL/2" in your code.
            
        }
        
        /*
        Below you compute beta according to Equation (8) from paper.
        */

        double beta = 0;
        if(d_current_symbol != 0){
            beta = arg( pilot_mapping[0] * current_symbol[pilot1_index] * conj(d_equalizer->get_csi_at(pilot1_index)) + 
                        pilot_mapping[1] * current_symbol[pilot2_index] * conj(d_equalizer->get_csi_at(pilot2_index)) );
        }

        //debug
        //dout << "Pilot 0 is " << current_symbol[pilot1_index] / d_equalizer->get_csi_at(pilot1_index) <<", pilot 1 is " << current_symbol[pilot2_index]/ d_equalizer->get_csi_at(pilot2_index) << std::endl;
        //dout << "Polarity pilot 0 should be " << pilot_mapping[0] << " , polarity pilot 1 should be " << pilot_mapping[1] << std::endl;
        //dout << "Epsilon is " << epsilon << std::endl;
        //dout << "Beta is " << beta << std::endl;

        /*
        Below you compute epsilon_r using Formula (10) from paper.
        */

        double er = arg((conj(d_prev_pilots_with_corrected_polarity[0]) * pilot_mapping[0] * current_symbol[pilot1_index]) +
                        (conj(d_prev_pilots_with_corrected_polarity[1]) * pilot_mapping[1] * current_symbol[pilot2_index]));

        er *= d_bw / (2 * M_PI * d_freq * (SAMPLES_PER_OFDM_SYMBOL + SAMPLES_PER_GI));

        // compensate residual frequency offset iaw Equation (9)
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            current_symbol[i] *= exp(gr_complex(0, - beta));
        }
        
        // update estimate of residual frequency offset using exponential moving average
        if (d_current_symbol >= NUM_OFDM_SYMBOLS_IN_LTF1) {
            double alpha = 0.1;
            d_er = (1 - alpha) * d_er + alpha * er;
        }

        //update the previous pilots
        d_prev_pilots_with_corrected_polarity[0] = pilot_mapping[0] * current_symbol[pilot1_index];
        d_prev_pilots_with_corrected_polarity[1] = pilot_mapping[1] * current_symbol[pilot2_index];

        // do equalization. this is what sends bytes downstream to HaLow Decode MAC starting at the 0 offset relative to (out + o * CODED_BITS_PER_OFDM_SYMBOL), ending at CODED_BITS_PER_OFDM_SYMBOL offset index.
        d_equalizer->equalize(
            current_symbol, d_current_symbol, symbols, out + o * CODED_BITS_PER_OFDM_SYMBOL, pilot1_index, pilot2_index, d_frame_mod);

        //if in SIG field
        if (d_current_symbol >= NUM_OFDM_SYMBOLS_IN_LTF1 && d_current_symbol < NUM_OFDM_SYMBOLS_IN_LTF1 + NUM_OFDM_SYMBOLS_IN_SIG_FIELD){
            dout << "o: " << o << std::endl;

            if (decode_signal_field(symbols)) {

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

        //if DATA
        if (d_current_symbol >= NUM_OFDM_SYMBOLS_IN_LTF1 + NUM_OFDM_SYMBOLS_IN_SIG_FIELD) {
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

bool frame_equalizer_impl::decode_signal_field(gr_complex* rx_symbols)
{
    static ofdm_param ofdm(BPSK_1_2_REP);
    static frame_param frame(ofdm);
    
    //deinterleave
    deinterleave(d_deinterleaved, rx_symbols);

    //unrepeat
    unrepeat(d_unrepeated, d_deinterleaved);

    //add the decided bit into d_sig_field_bits
    for (int i = 0; i < NUM_BITS_UNREPEATED_SIG_SYMBOL; i++){
        d_sig_field_bits[d_sig * NUM_BITS_UNREPEATED_SIG_SYMBOL + i] = ofdm.constellation->decision_maker(&d_unrepeated[i]);
    }

    //increment the sig number
    d_sig++;

    //if we derepeated the whole sig field already
    if(d_sig == NUM_OFDM_SYMBOLS_IN_SIG_FIELD){

        //decode
        uint8_t* decoded_bits = d_decoder.decode(&ofdm, &frame, d_sig_field_bits);
        
        //parse the sig field
        return parse_signal(decoded_bits);
    }
    //otherwise, wait for remaining sig field symbols to be decoded
    else{
        return false;
    }
}

void frame_equalizer_impl::print_coding(frame_coding coding){
    switch (coding)
    {
    case BCC:
        dout << "BCC";
        break;
    case LDPC:
        dout << "LDPC";
        break;
    default:
        break;
    }
}


bool frame_equalizer_impl::parse_signal(uint8_t* decoded_bits)
{    

    //number of spatial streams
    uint8_t nsts = decoded_bits[0] * 0x1 + decoded_bits[1] * 0x2 + 1;

    //short GI
    bool short_gi = decoded_bits[2] == 1 ? true : false;

    //coding
    frame_coding coding = decoded_bits[3] == 1 ? frame_coding::LDPC : frame_coding::BCC;
    if(coding == frame_coding::LDPC){
        dout << "ERROR : frame coding (LDPC) unsupported" << std::endl;
    }
    
    //mcs
    uint8_t mcs = decoded_bits[7] * 0x1 + decoded_bits[8] * 0x2 + decoded_bits[9] * 0x4 + decoded_bits[10] * 0x8;
    d_frame_encoding = mcs;

    //aggregation
    bool aggregation = decoded_bits[11] == 1 ? true : false;

    //length
    uint16_t length = 0;
    for (int i = 0; i < 9; i++){
        //length += decoded_bits[i] * pow(2, (20 - i));
        length += decoded_bits[12 + i] * pow(2, i);
    }
    if(!aggregation){
        //when Aggregation is set to OFF, length is the number of octets in the PSDU (Table 23-18)
        d_frame_bytes = length;
    }
    else{
        //TODO
    }

    //travelling pilots
    d_travel_pilots = decoded_bits[24] == 1 ? true : false;

    //NDP indication
    bool ndp = !!(decoded_bits[25]);

    //crc received
    uint8_t rx_crc4 = decoded_bits[26] * 0x8 + decoded_bits[27] * 0x4 + decoded_bits[28] * 0x2 + decoded_bits[29] * 0x1;

    //debug
    dout << "sts : " << unsigned(nsts) << std::endl;
    dout << "Short GI : " << short_gi << std::endl;
    dout << "Coding : ";
    print_coding(coding);
    dout << std::endl;
    dout << "mcs : " << unsigned(mcs) << std::endl;
    dout << "Aggregation : " << aggregation << std::endl;
    dout << "length : " << unsigned(length) << std::endl;
    dout << "Travelling Pilots " << d_travel_pilots << std::endl;
    dout << "NDP Indication " << ndp << std::endl;
    dout << "CRC-4 bit received : " << unsigned(rx_crc4) << std::endl;
    dout << "CRC-4 bit computed : " << unsigned(compute_crc(decoded_bits)) << std::endl;

    if(rx_crc4 == compute_crc(decoded_bits)){
        dout << "SIG field read with success" << std::endl;
    }
    else{
        dout << "ERROR while reading SIG field : bad crc" << std::endl;

        return false;
    }

    //compute frame symbols, encoding and bytes members
    ofdm_param ofdm((gr::ieee802_11ah::Encoding) mcs);
    frame_param frame(ofdm, (int)length);

    d_frame_symbols = frame.n_sym;
    d_frame_encoding = (int) mcs;
    d_frame_bytes = (int)length;

    
    switch (mcs) {//table 23-41
    case 0:
        dout << "Encoding: 300 kbit/s   ";
        break;
    case 1:
        dout << "Encoding: 600 kbit/s   ";
        break;
    case 2:
        dout << "Encoding: 900 kbit/s   ";
        break;
    case 3:
        dout << "Encoding: 1200 kbit/s   ";
        break;
    case 4:
        dout << "Encoding: 1800 kbit/s   ";
        break;
    case 5:
        dout << "Encoding: 2400 kbit/s   ";
        break;
    case 6:
        dout << "Encoding: 2700 kbit/s   ";
        break;
    case 7:
        dout << "Encoding: 3000 kbit/s   ";
        break;
    case 10:
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

} /* namespace ieee802_11ah */
} /* namespace gr */
