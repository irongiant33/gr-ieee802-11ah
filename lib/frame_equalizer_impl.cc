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
        
        //debug
        //dout << "d_current_symbol: " << d_current_symbol << " i: " << i << " o: " << o << std::endl;

        get_tags_in_window(tags, 0, i, i + 1, pmt::string_to_symbol("wifi_start"));

        // new frame
        if (tags.size()) {
            d_current_symbol = 0;
            d_frame_symbols = 0;
            d_sig = 0;
            d_frame_mod = d_bpsk;

            d_freq_offset_from_synclong =
                pmt::to_double(tags.front().value) * d_bw / (2 * M_PI);
            d_epsilon0 = pmt::to_double(tags.front().value) * d_bw / (2 * M_PI * d_freq);
            d_er = 0;

            dout << "epsilon: " << d_epsilon0 << std::endl;
        }

        // not interesting -> skip. Why is this not interesting? uncommenting for now because I think it terminates too early for HaLow
        /*
        if (d_current_symbol > (d_frame_symbols + 2)) {
            i++;
            continue;
        }*/

        std::memcpy(current_symbol, in + i * SAMPLES_PER_OFDM_SYMBOL, SAMPLES_PER_OFDM_SYMBOL * sizeof(gr_complex));

        //debug traveling pilots
        /*
        const int TRAVELING_PILOTS_POSITIONS = 13;
        int p_travel_0[TRAVELING_PILOTS_POSITIONS] = {-2,-10,-5,-13,-8,-3,-11,-6,-1,-9,-4,-12,-7};
        int p_travel_1[TRAVELING_PILOTS_POSITIONS] = {12,  4, 9,  1, 6,11,  3, 8,13, 5,10,  2, 7};

        for (int i = 0; i < TRAVELING_PILOTS_POSITIONS; i++){
            p_travel_0[i] += 16;
            p_travel_1[i] += 16;
        }
        */

        //we define pilot_mapping, which holds the values of the pilots iaw pilot mapping p.3253
        gr_complex pilot_mapping[NUM_PILOTS];

        if(d_current_symbol < NUM_OFDM_SYMBOLS_IN_LTF1){//pilot mapping is always {-1, -1} for all LTS inside LTF1
            pilot_mapping[0] = -1;
            pilot_mapping[1] = -1;
        }

        //TODO : add polarity computation for LTF2 (see Equation 23-41 p.3245)
        //do we have LTF2 if only one spatial stream is used ?
        
        else{
            //As from the end of LTF1 until end of SIG field and from the end of LTF2 until the end of the OFDM frame, pilot mapping is {1, -1} for all even symbols and {-1, 1} for all odd symbols.
            gr_complex first_pilot = d_current_symbol % 2 ? -1 : 1;

            //we mulitply the pilot values with a polarity factor as described in OFMD modulation p.3258
            gr_complex p_n = equalizer::base::POLARITY[(d_current_symbol - NUM_OFDM_SYMBOLS_IN_LTF1) % 127];

            pilot_mapping[0] = first_pilot * p_n;
            pilot_mapping[1] = - first_pilot * p_n;

        }

        //debug
        //dout << "Before CSO compensation \n";
        //dout << "Expected pilots polarity : " << p[0] << ", " << p[1] << ". Effective pilots polarity : " << current_symbol[PILOT1_INDEX] << ", " << current_symbol[PILOT2_INDEX] << "\n";


        // compensate sampling offset        
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            current_symbol[i] *= exp(gr_complex(0,
                                                2 * M_PI * d_current_symbol * (SAMPLES_PER_OFDM_SYMBOL + SAMPLES_PER_GI) * 
                                                    (d_epsilon0 + d_er) * (i - SAMPLES_PER_OFDM_SYMBOL / 2) / SAMPLES_PER_OFDM_SYMBOL));
            
            //@irongiant33 To answer your question "what is 32? Half of the 802.11a number of subcarriers?". The "32" seem indeed to come from the number of subcarriers in 802.11a. The sampling offset compensation performed is described in Equation 7 of paper "Frequency Offset Estimation and Correction in the IEEE 802.11a WLAN" (see https://openofdm.readthedocs.io/en/latest/_downloads/vtc04_freq_offset.pdf). The number (i - 32) actually corresponds to the k variable that ranges from -26 to 26. Consequently, you should replace "32" with "SAMPLES_PER_OFDM_SYMBOL/2" in your code.
            
        }
        

        //old
        //gr_complex p = equalizer::base::POLARITY[(d_current_symbol - 2) % 127]; // why subtract 2 here? does 127 have to do with polarity? anything to do with line 141 on sync long? 
        
        //debug
        //dout << "After CSO compensation \n";
        //dout << "Expected pilots polarity : " << p[0] << ", " << p[1] << ". Effective pilots polarity : " << current_symbol[PILOT1_INDEX] << ", " << current_symbol[PILOT2_INDEX] << "\n";
        
        /*
        Below you compute beta according to Equation (8).
        */

        //gr_complex beta_prev = p[0] * current_symbol[PILOT1_INDEX] * conj(d_Qi[0]) + p[1] * current_symbol[PILOT2_INDEX] * conj(d_Qi[1]);
        double beta = 0;
        if(d_current_symbol != 0){
            beta = arg( pilot_mapping[0] * current_symbol[PILOT1_INDEX] * conj(d_equalizer->get_csi_at(PILOT1_INDEX)) + 
                        pilot_mapping[1] * current_symbol[PILOT2_INDEX] * conj(d_equalizer->get_csi_at(PILOT2_INDEX)) );
        }

        //debug
        //dout << "Channel at i = 0 " <<  d_Qi[0] << ", at i = 1 " << d_Qi[1] << std::endl;
        //dout << "Pilot 0 is " << current_symbol[PILOT1_INDEX] / d_Qi[0] <<", pilot 1 is " << current_symbol[PILOT2_INDEX]/ d_Qi[1] << std::endl;
        //dout << "Polarity pilot 0 is " << p[0] << " , polarity pilot 1 is " << p[1] << std::endl;
        //dout << "Epsilon is " << epsilon << std::endl;
        //dout << "Beta is " << beta << std::endl;
        //dout << "Beta prev is " << beta_prev << std::endl;

        //old
        /*
        if (d_current_symbol < NUM_OFDM_SYMBOLS_IN_LTF1) {//if we are still in LTF1
            
            beta = arg(- current_symbol[PILOT1_INDEX]
                       - current_symbol[PILOT2_INDEX]);

        } else {//if we are at nth symbol after LTF1
            //use the pilots carriers corrected by polarity (supposing no travelling pilots)
            beta = arg((current_symbol[PILOT1_INDEX] * p) + 
                       (current_symbol[PILOT2_INDEX] * (-p) ));
        }
        */

        /*
        Below you compute epsilon_r using Formula (10)
        */
        double er = arg((conj(d_prev_pilots_with_corrected_polarity[0]) * pilot_mapping[0] * current_symbol[PILOT1_INDEX]) +
                        (conj(d_prev_pilots_with_corrected_polarity[1]) * pilot_mapping[1] * current_symbol[PILOT2_INDEX]));

        er *= d_bw / (2 * M_PI * d_freq * (SAMPLES_PER_OFDM_SYMBOL + SAMPLES_PER_GI));

        //old
        /*
        if (d_current_symbol < 8) { //used to be 2. assuming 8 b/c is 4+4 (4 symbols for STF, 4 symbols for LTF1)
            d_prev_pilots_with_corrected_polarity[0] = - current_symbol[PILOT1_INDEX];
            d_prev_pilots_with_corrected_polarity[1] = - current_symbol[PILOT2_INDEX];
        } else {
            d_prev_pilots_with_corrected_polarity[0] = current_symbol[PILOT1_INDEX] * p;
            d_prev_pilots_with_corrected_polarity[1] = current_symbol[PILOT2_INDEX] * (-p);
        }
        */

        // compensate residual frequency offset iaw Equation (9)
        for (int i = 0; i < SAMPLES_PER_OFDM_SYMBOL; i++) {
            current_symbol[i] *= exp(gr_complex(0, - beta));
        }
        
        //debug
        //dout << "Before RFO compensation \n";
        //dout << "Expected pilots polarity : " << p[0] << ", " << p[1] << ". Effective pilots polarity : " << current_symbol[PILOT1_INDEX] << ", " << current_symbol[PILOT2_INDEX] << "\n";

        // update estimate of residual frequency offset using exponential moving average
        if (d_current_symbol >= NUM_OFDM_SYMBOLS_IN_LTF1) {
            double alpha = 0.1;
            d_er = (1 - alpha) * d_er + alpha * er;
        }

        //update the previous pilots
        d_prev_pilots_with_corrected_polarity[0] = pilot_mapping[0] * current_symbol[PILOT1_INDEX];
        d_prev_pilots_with_corrected_polarity[1] = pilot_mapping[1] * current_symbol[PILOT2_INDEX];

        // do equalization. this is what sends bytes downstream to WiFi Decode MAC starting at the 0 offset relative to (out + o * CODED_BITS_PER_OFDM_SYMBOL), ending at CODED_BITS_PER_OFDM_SYMBOL offset index.
        //TODO : resolve why csi at position 1 is always close to zero
        d_equalizer->equalize(
            current_symbol, d_current_symbol, symbols, out + o * CODED_BITS_PER_OFDM_SYMBOL, d_frame_mod);

        //old
        /*
        // signal field, it takes 6 OFDM symbols to make the SIG field
        if (d_current_symbol >= 8 && d_current_symbol < 14) {
            o++;
        }
        */

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

        //if LTF2 or DATA
        //TODO : change for LTF2
        //debug
        if (d_current_symbol >= NUM_OFDM_SYMBOLS_IN_LTF1) {
        //if (d_current_symbol >= NUM_OFDM_SYMBOLS_IN_LTF1 && d_current_symbol < NUM_OFDM_SYMBOLS_IN_LTF1 + NUM_OFDM_SYMBOLS_IN_SIG_FIELD){
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

    /*
    dout << "Pre interleaver: ";
    for(int i = 0; i < 24; i++)
    {
        if (rx_bits[i] == 1){
            dout << "1";
        }
        else if(rx_bits[i] == 0){
            dout << "0";
        }
        else{
            dout << "ERROR";
        }
    }
    dout << std::endl;
    */
    
    //deinterleave
    deinterleave(rx_symbols);

    //unrepeat
    unrepeat(d_deinterleaved);
    
    /*
    dout << "Post deinterleaved: ";
    for(int i = 0; i < 24; i++)
    {
        if (d_deinterleaved[i] == 1){
            dout << "1";
        }
        else if(d_deinterleaved[i] == 0){
            dout << "0";
        }
        else{
            dout << "ERROR";
        }
    }
    dout << std::endl;
    */



    /*
    if(error_in_sig){
        dout << "ERROR detected in SIG repetition" << std::endl;
    }
    else{
        dout << "SIG repetition follows pattern" << std::endl;
    }
    */

    //add unrepeated bits into d_sig_field_bits
    /*
    for(int i = 0; i < NUM_BITS_UNREPEATED_SIG_SYMBOL; i++){
        d_sig_field_bits[d_sig * NUM_BITS_UNREPEATED_SIG_SYMBOL + i] = d_frame_mod->decision_maker(&d_unrepeated[i]);
    }
    */

    /*
    if (d_sig_field_bits[79] == 0 && d_sig_field_bits[78] == 0 && d_sig_field_bits[77] == 0 && d_sig_field_bits[76] == 0 && d_sig_field_bits[75] == 0 &&
    d_sig_field_bits[74] == 0 && d_sig_field_bits[73] == 0 && d_sig_field_bits[72] == 0){
        //dout << "All 8 sig field last bits to zero !\n";
    }
    */

    //increment the sig number
    d_sig++;

    //if we derepeated the whole sig field already
    if(d_sig == NUM_OFDM_SYMBOLS_IN_SIG_FIELD){

        /*
        dout << "Pre decode: ";
        for(int i = 0; i < 80; i++)
        {
            if (d_sig_field_bits[i] == 1){
                dout << "1";
            }
            else if(d_sig_field_bits[i] == 0){
                dout << "0";
            }
            else{
                dout << "E";
            }
        }
        dout << std::endl;
        */
        

        //decode
        uint8_t* decoded_bits = d_decoder.decode(&ofdm, &frame, d_sig_field_bits);
        
        //debug
        /*
        dout << "Post decode: ";
        for(int i = 0; i < 40; i++)
        {
            if (decoded_bits[i] == 1){
                dout << "1";
            }
            else if(decoded_bits[i] == 0){
                dout << "0";
            }
            else{
                dout << "E";
            }
        }
        dout << std::endl;
        */
        
        
        
        //parse the sig field
        return parse_signal(decoded_bits);
    }
    //otherwise, wait for remaining sig field symbols to be decoded
    else{
        return false;
    }
}

void frame_equalizer_impl::deinterleave(gr_complex* rx_symbols)
{   
    //old
    /*
    //why does iteration 1 invoke undefined behavior?
    for(int j = 0; j < 6; j++){ //because there are 6 OFDM symbols in rx_bits (or at least there should be)
        for (int i = 0; i < CODED_BITS_PER_OFDM_SYMBOL; i++) {
            d_deinterleaved[j*CODED_BITS_PER_OFDM_SYMBOL + i] = rx_bits[j*CODED_BITS_PER_OFDM_SYMBOL + interleaver_pattern[i]];
        }
    }
    */

    for (int i = 0; i < CODED_BITS_PER_OFDM_SYMBOL; i++) {
        d_deinterleaved[i] = rx_symbols[interleaver_pattern[i]];
    }
}

void frame_equalizer_impl::unrepeat(gr_complex* rx_symbols){

    //Unrepeat using Maximum Ratio Combining
    //in this case the symbols have already been multiplied by the channel conjugate (see equalizer)
    //therefore all we still need to do is to peform an average of the signal repetitions

    uint8_t s[NUM_BITS_UNREPEATED_SIG_SYMBOL] = {1,0,0,0,0,1,0,1,0,1,1,1};

    for(int i = 0; i < NUM_BITS_UNREPEATED_SIG_SYMBOL; i++){

        //combine
        d_unrepeated[i] = d_deinterleaved[i] * gr_complex(0.5,0) + //first sample
                          d_deinterleaved[i + NUM_BITS_UNREPEATED_SIG_SYMBOL] * gr_complex((s[i] == 0 ? 0.5 : -0.5),0); //second sample, inverted in case s == 1

        //finally, add the decided bit into d_sig_field_bits
        d_sig_field_bits[d_sig * NUM_BITS_UNREPEATED_SIG_SYMBOL + i] = d_frame_mod->decision_maker(&d_unrepeated[i]);

        if((d_deinterleaved[i].real() < 0) != (d_deinterleaved[i + NUM_BITS_UNREPEATED_SIG_SYMBOL].real() * (s[i] == 0 ? 1 : -1) < 0 )){
            dout << "ERROR in unrepeat" << std::endl;
        }

    }
}

bool frame_equalizer_impl::parse_signal(uint8_t* decoded_bits)
{   

    //debug
    /*
    dout << "Post decode: ";
    for(int i = 0; i < 36; i++)
    {   
        dout << "B" << i << " : ";
        if (decoded_bits[i] == 1){
            dout << "1";
        }
        else if(decoded_bits[i] == 0){
            dout << "0";
        }
        else{
            dout << "E";
        }
        dout << " ,";
    }
    dout << std::endl;
    */
    
    

    //number of spatial streams
    uint8_t nsts = decoded_bits[0] * 0x2 + decoded_bits[1] * 0x1;

    //short GI
    //TODO : foresee in sync long short guard intervals
    bool short_gi = decoded_bits[2] == 1 ? true : false;

    //coding
    frame_coding coding = decoded_bits[3] == 1 ? frame_coding::LDPC : frame_coding::BCC;
    
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
    bool traveling_pilots = decoded_bits[24] == 1 ? true : false;

    //crc received
    uint8_t rx_crc4 = decoded_bits[26] * 0x8 + decoded_bits[27] * 0x4 + decoded_bits[28] * 0x2 + decoded_bits[29] * 0x1;

    //debug
    dout << "mcs : " << unsigned(mcs) << std::endl;
    dout << "Short GI : " << short_gi << std::endl;
    dout << "Aggregation : " << aggregation << std::endl;
    dout << "length : " << unsigned(length) << std::endl;
    dout << "CRC-4 bit received : " << unsigned(rx_crc4) << std::endl;
    dout << "CRC-4 bit computed : " << unsigned(compute_crc(decoded_bits)) << std::endl;

    //CRC-4

    /*
    dout << "Pre Formating: ";
    for(int i = 0; i < 26; i++)
    {   
        //dout << "B" << i << " : ";
        if (decoded_bits[i] == 1){
            dout << "1";
        }
        else if(decoded_bits[i] == 0){
            dout << "0";
        }
        else{
            dout << "E";
        }
        //dout << " ,";
    }
    dout << std::endl;
    */

    if(rx_crc4 == compute_crc(decoded_bits)){
        dout << "SIG field read with success" << std::endl;
    }
    else{
        dout << "ERROR while reading SIG field : bad crc" << std::endl;

        return false;
    }


    /*
    int mcs = 0;
    int frame_bit_index = 0;
    d_frame_bytes = 0;
    bool parity = false;
    std::unique_ptr<char[]> sig_field(new char[(NUM_BITS_IN_HALOW_SIG_FIELD * NUM_SIG_FIELD_REPETITIONS)/8]); //8 bits per byte
    char byte = '\0';
    if(decoded_bits[0])
    {
        byte = 1 << 7;
    }
    int byte_counter = 0;

    //each SIG field is repeated before the next is sent. So first 6 bits should be equal to the next 6, repeated 6 times to yield 72 bits
    for (int i = 0; i < NUM_BITS_IN_HALOW_SIG_FIELD * NUM_SIG_FIELD_REPETITIONS; i++) {
        if(i % 8 == 0 && i != 0) //8 bits per byte
        {
            sig_field[byte_counter] = byte;
            byte = '\0';
            if(decoded_bits[i])
            {
                byte = 1 << 7;
            }
            byte_counter++;
        }
        else
        {
            if(decoded_bits[i])
            {
                byte = byte | (1 << (7 - (i % 8)));
            }
        }
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
    /*
    }
    sig_field[byte_counter] = byte;
    byte_counter++;
    byte = '\0';

    //print out the hex representation of the signal field
    for(int i = 0; i < byte_counter; i++)
    {
        byte = sig_field[i];
        dout << std::hex << ((byte & 0xF0) >> 4) << " ";
        dout << std::hex << ((byte & 0x0F)) << " ";
    }
    //dout << std::endl;

    /* unused with HaLow, CRC instead
    if (parity != decoded_bits[17]) {
        dout << "SIGNAL: wrong parity" << std::endl;
        return false;
    }*/
    
    switch (mcs) {//table 23-41
    case 0:
        d_frame_encoding = 0;
        //d_frame_symbols = (int)ceil((8 * d_frame_bytes + 8 + 6) / (double) 12);//see Equation 23-79
        //d_frame_mod = d_bpsk;
        dout << "Encoding: 300 kbit/s   ";
        break;
    case 1:
        d_frame_encoding = 1;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)36);//TODO
        d_frame_mod = d_qpsk;
        dout << "Encoding: 600 kbit/s   ";
        break;
    case 2:
        d_frame_encoding = 2;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)48);//TODO
        d_frame_mod = d_qpsk;
        dout << "Encoding: 900 kbit/s   ";
        break;
    case 3:
        d_frame_encoding = 3;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)72);//TODO
        d_frame_mod = d_16qam;
        dout << "Encoding: 1200 kbit/s   ";
        break;
    case 4:
        d_frame_encoding = 4;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)96);//TODO
        d_frame_mod = d_16qam;
        dout << "Encoding: 1800 kbit/s   ";
        break;
    case 5:
        d_frame_encoding = 5;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)144);//TODO
        d_frame_mod = d_64qam;
        dout << "Encoding: 2400 kbit/s   ";
        break;
    case 6:
        d_frame_encoding = 6;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)192);//TODO
        d_frame_mod = d_64qam;
        dout << "Encoding: 2700 kbit/s   ";
        break;
    case 7:
        d_frame_encoding = 7;
        d_frame_symbols = (int)ceil((16 + 8 * d_frame_bytes + 6) / (double)216);//TODO
        d_frame_mod = d_64qam;
        dout << "Encoding: 3000 kbit/s   ";
        break;
    case 10:
        d_frame_encoding = 10;
        //d_frame_symbols = (int)ceil((8 * d_frame_bytes + 8 + 6) / (double) 6);//see Equation 23-79
        //d_frame_mod = d_bpsk;
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

uint8_t frame_equalizer_impl::compute_crc(uint8_t* decoded_bits){

    //copy first 26 bits, inverting the first four
    uint8_t num_crc_input_bytes = 4;
    uint32_t crc4_input_bits = 0;
    for(int i = 0; i < 26; i++){
        crc4_input_bits += (i < 4 ? (1 - decoded_bits[i]) : (decoded_bits[i])) * pow(2, (31 - i));
    }

    /*
    std::cout << "To 32: ";
    std::cout << std::bitset<32>(crc4_input_bits);
    std::cout << std::endl;
    */

    //right shift by 6 (32 - 26) bits
    crc4_input_bits = crc4_input_bits >> 6;

    //debug
    /*
    std::cout << "Right shift: ";
    std::cout << std::bitset<32>(crc4_input_bits);
    std::cout << std::endl;
    */
    

    //split into 4 bytes (to hold the 32 bits)
    uint8_t crc4_input_bytes[num_crc_input_bytes];
    crc4_input_bytes[0] = (crc4_input_bits & 0xff000000) >> 24;
    crc4_input_bytes[1] = (crc4_input_bits & 0x00ff0000) >> 16;
    crc4_input_bytes[2] = (crc4_input_bits & 0x0000ff00) >> 8;
    crc4_input_bytes[3] = (crc4_input_bits & 0x000000ff);

    //debug
    /*
    dout << "Post Formating: ";
    for(int i = 0; i < 4; i++){
        std::cout << std::bitset<8>(crc4_input_bytes[i]);
    }
    dout << std::endl;
    */
    

    uint8_t computed_crc = 0;
    return crc4HaLoW_byte(computed_crc, crc4_input_bytes, num_crc_input_bytes);
}

const int frame_equalizer_impl::interleaver_pattern[CODED_BITS_PER_OFDM_SYMBOL] = {
    0, 3, 6, 9,  12, 15, 18, 21,
    1, 4, 7, 10, 13, 16, 19, 22,
    2, 5, 8, 11, 14, 17, 20, 23
}; //table 23-20 and table 23-41

// Table for CRC4 computation
// This code was partially generated from the crcany program of Mark Adler (see https://github.com/madler/crcany)
#define table_byte table_word[0]

static uint8_t const table_word[][256] = {
   {0x00, 0x30, 0x60, 0x50, 0xc0, 0xf0, 0xa0, 0x90, 0xb0, 0x80, 0xd0, 0xe0, 0x70,
    0x40, 0x10, 0x20, 0x50, 0x60, 0x30, 0x00, 0x90, 0xa0, 0xf0, 0xc0, 0xe0, 0xd0,
    0x80, 0xb0, 0x20, 0x10, 0x40, 0x70, 0xa0, 0x90, 0xc0, 0xf0, 0x60, 0x50, 0x00,
    0x30, 0x10, 0x20, 0x70, 0x40, 0xd0, 0xe0, 0xb0, 0x80, 0xf0, 0xc0, 0x90, 0xa0,
    0x30, 0x00, 0x50, 0x60, 0x40, 0x70, 0x20, 0x10, 0x80, 0xb0, 0xe0, 0xd0, 0x70,
    0x40, 0x10, 0x20, 0xb0, 0x80, 0xd0, 0xe0, 0xc0, 0xf0, 0xa0, 0x90, 0x00, 0x30,
    0x60, 0x50, 0x20, 0x10, 0x40, 0x70, 0xe0, 0xd0, 0x80, 0xb0, 0x90, 0xa0, 0xf0,
    0xc0, 0x50, 0x60, 0x30, 0x00, 0xd0, 0xe0, 0xb0, 0x80, 0x10, 0x20, 0x70, 0x40,
    0x60, 0x50, 0x00, 0x30, 0xa0, 0x90, 0xc0, 0xf0, 0x80, 0xb0, 0xe0, 0xd0, 0x40,
    0x70, 0x20, 0x10, 0x30, 0x00, 0x50, 0x60, 0xf0, 0xc0, 0x90, 0xa0, 0xe0, 0xd0,
    0x80, 0xb0, 0x20, 0x10, 0x40, 0x70, 0x50, 0x60, 0x30, 0x00, 0x90, 0xa0, 0xf0,
    0xc0, 0xb0, 0x80, 0xd0, 0xe0, 0x70, 0x40, 0x10, 0x20, 0x00, 0x30, 0x60, 0x50,
    0xc0, 0xf0, 0xa0, 0x90, 0x40, 0x70, 0x20, 0x10, 0x80, 0xb0, 0xe0, 0xd0, 0xf0,
    0xc0, 0x90, 0xa0, 0x30, 0x00, 0x50, 0x60, 0x10, 0x20, 0x70, 0x40, 0xd0, 0xe0,
    0xb0, 0x80, 0xa0, 0x90, 0xc0, 0xf0, 0x60, 0x50, 0x00, 0x30, 0x90, 0xa0, 0xf0,
    0xc0, 0x50, 0x60, 0x30, 0x00, 0x20, 0x10, 0x40, 0x70, 0xe0, 0xd0, 0x80, 0xb0,
    0xc0, 0xf0, 0xa0, 0x90, 0x00, 0x30, 0x60, 0x50, 0x70, 0x40, 0x10, 0x20, 0xb0,
    0x80, 0xd0, 0xe0, 0x30, 0x00, 0x50, 0x60, 0xf0, 0xc0, 0x90, 0xa0, 0x80, 0xb0,
    0xe0, 0xd0, 0x40, 0x70, 0x20, 0x10, 0x60, 0x50, 0x00, 0x30, 0xa0, 0x90, 0xc0,
    0xf0, 0xd0, 0xe0, 0xb0, 0x80, 0x10, 0x20, 0x70, 0x40},
   {0x00, 0xf0, 0xd0, 0x20, 0x90, 0x60, 0x40, 0xb0, 0x10, 0xe0, 0xc0, 0x30, 0x80,
    0x70, 0x50, 0xa0, 0x20, 0xd0, 0xf0, 0x00, 0xb0, 0x40, 0x60, 0x90, 0x30, 0xc0,
    0xe0, 0x10, 0xa0, 0x50, 0x70, 0x80, 0x40, 0xb0, 0x90, 0x60, 0xd0, 0x20, 0x00,
    0xf0, 0x50, 0xa0, 0x80, 0x70, 0xc0, 0x30, 0x10, 0xe0, 0x60, 0x90, 0xb0, 0x40,
    0xf0, 0x00, 0x20, 0xd0, 0x70, 0x80, 0xa0, 0x50, 0xe0, 0x10, 0x30, 0xc0, 0x80,
    0x70, 0x50, 0xa0, 0x10, 0xe0, 0xc0, 0x30, 0x90, 0x60, 0x40, 0xb0, 0x00, 0xf0,
    0xd0, 0x20, 0xa0, 0x50, 0x70, 0x80, 0x30, 0xc0, 0xe0, 0x10, 0xb0, 0x40, 0x60,
    0x90, 0x20, 0xd0, 0xf0, 0x00, 0xc0, 0x30, 0x10, 0xe0, 0x50, 0xa0, 0x80, 0x70,
    0xd0, 0x20, 0x00, 0xf0, 0x40, 0xb0, 0x90, 0x60, 0xe0, 0x10, 0x30, 0xc0, 0x70,
    0x80, 0xa0, 0x50, 0xf0, 0x00, 0x20, 0xd0, 0x60, 0x90, 0xb0, 0x40, 0x30, 0xc0,
    0xe0, 0x10, 0xa0, 0x50, 0x70, 0x80, 0x20, 0xd0, 0xf0, 0x00, 0xb0, 0x40, 0x60,
    0x90, 0x10, 0xe0, 0xc0, 0x30, 0x80, 0x70, 0x50, 0xa0, 0x00, 0xf0, 0xd0, 0x20,
    0x90, 0x60, 0x40, 0xb0, 0x70, 0x80, 0xa0, 0x50, 0xe0, 0x10, 0x30, 0xc0, 0x60,
    0x90, 0xb0, 0x40, 0xf0, 0x00, 0x20, 0xd0, 0x50, 0xa0, 0x80, 0x70, 0xc0, 0x30,
    0x10, 0xe0, 0x40, 0xb0, 0x90, 0x60, 0xd0, 0x20, 0x00, 0xf0, 0xb0, 0x40, 0x60,
    0x90, 0x20, 0xd0, 0xf0, 0x00, 0xa0, 0x50, 0x70, 0x80, 0x30, 0xc0, 0xe0, 0x10,
    0x90, 0x60, 0x40, 0xb0, 0x00, 0xf0, 0xd0, 0x20, 0x80, 0x70, 0x50, 0xa0, 0x10,
    0xe0, 0xc0, 0x30, 0xf0, 0x00, 0x20, 0xd0, 0x60, 0x90, 0xb0, 0x40, 0xe0, 0x10,
    0x30, 0xc0, 0x70, 0x80, 0xa0, 0x50, 0xd0, 0x20, 0x00, 0xf0, 0x40, 0xb0, 0x90,
    0x60, 0xc0, 0x30, 0x10, 0xe0, 0x50, 0xa0, 0x80, 0x70},
   {0x00, 0x60, 0xc0, 0xa0, 0xb0, 0xd0, 0x70, 0x10, 0x50, 0x30, 0x90, 0xf0, 0xe0,
    0x80, 0x20, 0x40, 0xa0, 0xc0, 0x60, 0x00, 0x10, 0x70, 0xd0, 0xb0, 0xf0, 0x90,
    0x30, 0x50, 0x40, 0x20, 0x80, 0xe0, 0x70, 0x10, 0xb0, 0xd0, 0xc0, 0xa0, 0x00,
    0x60, 0x20, 0x40, 0xe0, 0x80, 0x90, 0xf0, 0x50, 0x30, 0xd0, 0xb0, 0x10, 0x70,
    0x60, 0x00, 0xa0, 0xc0, 0x80, 0xe0, 0x40, 0x20, 0x30, 0x50, 0xf0, 0x90, 0xe0,
    0x80, 0x20, 0x40, 0x50, 0x30, 0x90, 0xf0, 0xb0, 0xd0, 0x70, 0x10, 0x00, 0x60,
    0xc0, 0xa0, 0x40, 0x20, 0x80, 0xe0, 0xf0, 0x90, 0x30, 0x50, 0x10, 0x70, 0xd0,
    0xb0, 0xa0, 0xc0, 0x60, 0x00, 0x90, 0xf0, 0x50, 0x30, 0x20, 0x40, 0xe0, 0x80,
    0xc0, 0xa0, 0x00, 0x60, 0x70, 0x10, 0xb0, 0xd0, 0x30, 0x50, 0xf0, 0x90, 0x80,
    0xe0, 0x40, 0x20, 0x60, 0x00, 0xa0, 0xc0, 0xd0, 0xb0, 0x10, 0x70, 0xf0, 0x90,
    0x30, 0x50, 0x40, 0x20, 0x80, 0xe0, 0xa0, 0xc0, 0x60, 0x00, 0x10, 0x70, 0xd0,
    0xb0, 0x50, 0x30, 0x90, 0xf0, 0xe0, 0x80, 0x20, 0x40, 0x00, 0x60, 0xc0, 0xa0,
    0xb0, 0xd0, 0x70, 0x10, 0x80, 0xe0, 0x40, 0x20, 0x30, 0x50, 0xf0, 0x90, 0xd0,
    0xb0, 0x10, 0x70, 0x60, 0x00, 0xa0, 0xc0, 0x20, 0x40, 0xe0, 0x80, 0x90, 0xf0,
    0x50, 0x30, 0x70, 0x10, 0xb0, 0xd0, 0xc0, 0xa0, 0x00, 0x60, 0x10, 0x70, 0xd0,
    0xb0, 0xa0, 0xc0, 0x60, 0x00, 0x40, 0x20, 0x80, 0xe0, 0xf0, 0x90, 0x30, 0x50,
    0xb0, 0xd0, 0x70, 0x10, 0x00, 0x60, 0xc0, 0xa0, 0xe0, 0x80, 0x20, 0x40, 0x50,
    0x30, 0x90, 0xf0, 0x60, 0x00, 0xa0, 0xc0, 0xd0, 0xb0, 0x10, 0x70, 0x30, 0x50,
    0xf0, 0x90, 0x80, 0xe0, 0x40, 0x20, 0xc0, 0xa0, 0x00, 0x60, 0x70, 0x10, 0xb0,
    0xd0, 0x90, 0xf0, 0x50, 0x30, 0x20, 0x40, 0xe0, 0x80},
   {0x00, 0xd0, 0x90, 0x40, 0x10, 0xc0, 0x80, 0x50, 0x20, 0xf0, 0xb0, 0x60, 0x30,
    0xe0, 0xa0, 0x70, 0x40, 0x90, 0xd0, 0x00, 0x50, 0x80, 0xc0, 0x10, 0x60, 0xb0,
    0xf0, 0x20, 0x70, 0xa0, 0xe0, 0x30, 0x80, 0x50, 0x10, 0xc0, 0x90, 0x40, 0x00,
    0xd0, 0xa0, 0x70, 0x30, 0xe0, 0xb0, 0x60, 0x20, 0xf0, 0xc0, 0x10, 0x50, 0x80,
    0xd0, 0x00, 0x40, 0x90, 0xe0, 0x30, 0x70, 0xa0, 0xf0, 0x20, 0x60, 0xb0, 0x30,
    0xe0, 0xa0, 0x70, 0x20, 0xf0, 0xb0, 0x60, 0x10, 0xc0, 0x80, 0x50, 0x00, 0xd0,
    0x90, 0x40, 0x70, 0xa0, 0xe0, 0x30, 0x60, 0xb0, 0xf0, 0x20, 0x50, 0x80, 0xc0,
    0x10, 0x40, 0x90, 0xd0, 0x00, 0xb0, 0x60, 0x20, 0xf0, 0xa0, 0x70, 0x30, 0xe0,
    0x90, 0x40, 0x00, 0xd0, 0x80, 0x50, 0x10, 0xc0, 0xf0, 0x20, 0x60, 0xb0, 0xe0,
    0x30, 0x70, 0xa0, 0xd0, 0x00, 0x40, 0x90, 0xc0, 0x10, 0x50, 0x80, 0x60, 0xb0,
    0xf0, 0x20, 0x70, 0xa0, 0xe0, 0x30, 0x40, 0x90, 0xd0, 0x00, 0x50, 0x80, 0xc0,
    0x10, 0x20, 0xf0, 0xb0, 0x60, 0x30, 0xe0, 0xa0, 0x70, 0x00, 0xd0, 0x90, 0x40,
    0x10, 0xc0, 0x80, 0x50, 0xe0, 0x30, 0x70, 0xa0, 0xf0, 0x20, 0x60, 0xb0, 0xc0,
    0x10, 0x50, 0x80, 0xd0, 0x00, 0x40, 0x90, 0xa0, 0x70, 0x30, 0xe0, 0xb0, 0x60,
    0x20, 0xf0, 0x80, 0x50, 0x10, 0xc0, 0x90, 0x40, 0x00, 0xd0, 0x50, 0x80, 0xc0,
    0x10, 0x40, 0x90, 0xd0, 0x00, 0x70, 0xa0, 0xe0, 0x30, 0x60, 0xb0, 0xf0, 0x20,
    0x10, 0xc0, 0x80, 0x50, 0x00, 0xd0, 0x90, 0x40, 0x30, 0xe0, 0xa0, 0x70, 0x20,
    0xf0, 0xb0, 0x60, 0xd0, 0x00, 0x40, 0x90, 0xc0, 0x10, 0x50, 0x80, 0xf0, 0x20,
    0x60, 0xb0, 0xe0, 0x30, 0x70, 0xa0, 0x90, 0x40, 0x00, 0xd0, 0x80, 0x50, 0x10,
    0xc0, 0xb0, 0x60, 0x20, 0xf0, 0xa0, 0x70, 0x30, 0xe0},
   {0x00, 0xc0, 0xb0, 0x70, 0x50, 0x90, 0xe0, 0x20, 0xa0, 0x60, 0x10, 0xd0, 0xf0,
    0x30, 0x40, 0x80, 0x70, 0xb0, 0xc0, 0x00, 0x20, 0xe0, 0x90, 0x50, 0xd0, 0x10,
    0x60, 0xa0, 0x80, 0x40, 0x30, 0xf0, 0xe0, 0x20, 0x50, 0x90, 0xb0, 0x70, 0x00,
    0xc0, 0x40, 0x80, 0xf0, 0x30, 0x10, 0xd0, 0xa0, 0x60, 0x90, 0x50, 0x20, 0xe0,
    0xc0, 0x00, 0x70, 0xb0, 0x30, 0xf0, 0x80, 0x40, 0x60, 0xa0, 0xd0, 0x10, 0xf0,
    0x30, 0x40, 0x80, 0xa0, 0x60, 0x10, 0xd0, 0x50, 0x90, 0xe0, 0x20, 0x00, 0xc0,
    0xb0, 0x70, 0x80, 0x40, 0x30, 0xf0, 0xd0, 0x10, 0x60, 0xa0, 0x20, 0xe0, 0x90,
    0x50, 0x70, 0xb0, 0xc0, 0x00, 0x10, 0xd0, 0xa0, 0x60, 0x40, 0x80, 0xf0, 0x30,
    0xb0, 0x70, 0x00, 0xc0, 0xe0, 0x20, 0x50, 0x90, 0x60, 0xa0, 0xd0, 0x10, 0x30,
    0xf0, 0x80, 0x40, 0xc0, 0x00, 0x70, 0xb0, 0x90, 0x50, 0x20, 0xe0, 0xd0, 0x10,
    0x60, 0xa0, 0x80, 0x40, 0x30, 0xf0, 0x70, 0xb0, 0xc0, 0x00, 0x20, 0xe0, 0x90,
    0x50, 0xa0, 0x60, 0x10, 0xd0, 0xf0, 0x30, 0x40, 0x80, 0x00, 0xc0, 0xb0, 0x70,
    0x50, 0x90, 0xe0, 0x20, 0x30, 0xf0, 0x80, 0x40, 0x60, 0xa0, 0xd0, 0x10, 0x90,
    0x50, 0x20, 0xe0, 0xc0, 0x00, 0x70, 0xb0, 0x40, 0x80, 0xf0, 0x30, 0x10, 0xd0,
    0xa0, 0x60, 0xe0, 0x20, 0x50, 0x90, 0xb0, 0x70, 0x00, 0xc0, 0x20, 0xe0, 0x90,
    0x50, 0x70, 0xb0, 0xc0, 0x00, 0x80, 0x40, 0x30, 0xf0, 0xd0, 0x10, 0x60, 0xa0,
    0x50, 0x90, 0xe0, 0x20, 0x00, 0xc0, 0xb0, 0x70, 0xf0, 0x30, 0x40, 0x80, 0xa0,
    0x60, 0x10, 0xd0, 0xc0, 0x00, 0x70, 0xb0, 0x90, 0x50, 0x20, 0xe0, 0x60, 0xa0,
    0xd0, 0x10, 0x30, 0xf0, 0x80, 0x40, 0xb0, 0x70, 0x00, 0xc0, 0xe0, 0x20, 0x50,
    0x90, 0x10, 0xd0, 0xa0, 0x60, 0x40, 0x80, 0xf0, 0x30},
   {0x00, 0x90, 0x10, 0x80, 0x20, 0xb0, 0x30, 0xa0, 0x40, 0xd0, 0x50, 0xc0, 0x60,
    0xf0, 0x70, 0xe0, 0x80, 0x10, 0x90, 0x00, 0xa0, 0x30, 0xb0, 0x20, 0xc0, 0x50,
    0xd0, 0x40, 0xe0, 0x70, 0xf0, 0x60, 0x30, 0xa0, 0x20, 0xb0, 0x10, 0x80, 0x00,
    0x90, 0x70, 0xe0, 0x60, 0xf0, 0x50, 0xc0, 0x40, 0xd0, 0xb0, 0x20, 0xa0, 0x30,
    0x90, 0x00, 0x80, 0x10, 0xf0, 0x60, 0xe0, 0x70, 0xd0, 0x40, 0xc0, 0x50, 0x60,
    0xf0, 0x70, 0xe0, 0x40, 0xd0, 0x50, 0xc0, 0x20, 0xb0, 0x30, 0xa0, 0x00, 0x90,
    0x10, 0x80, 0xe0, 0x70, 0xf0, 0x60, 0xc0, 0x50, 0xd0, 0x40, 0xa0, 0x30, 0xb0,
    0x20, 0x80, 0x10, 0x90, 0x00, 0x50, 0xc0, 0x40, 0xd0, 0x70, 0xe0, 0x60, 0xf0,
    0x10, 0x80, 0x00, 0x90, 0x30, 0xa0, 0x20, 0xb0, 0xd0, 0x40, 0xc0, 0x50, 0xf0,
    0x60, 0xe0, 0x70, 0x90, 0x00, 0x80, 0x10, 0xb0, 0x20, 0xa0, 0x30, 0xc0, 0x50,
    0xd0, 0x40, 0xe0, 0x70, 0xf0, 0x60, 0x80, 0x10, 0x90, 0x00, 0xa0, 0x30, 0xb0,
    0x20, 0x40, 0xd0, 0x50, 0xc0, 0x60, 0xf0, 0x70, 0xe0, 0x00, 0x90, 0x10, 0x80,
    0x20, 0xb0, 0x30, 0xa0, 0xf0, 0x60, 0xe0, 0x70, 0xd0, 0x40, 0xc0, 0x50, 0xb0,
    0x20, 0xa0, 0x30, 0x90, 0x00, 0x80, 0x10, 0x70, 0xe0, 0x60, 0xf0, 0x50, 0xc0,
    0x40, 0xd0, 0x30, 0xa0, 0x20, 0xb0, 0x10, 0x80, 0x00, 0x90, 0xa0, 0x30, 0xb0,
    0x20, 0x80, 0x10, 0x90, 0x00, 0xe0, 0x70, 0xf0, 0x60, 0xc0, 0x50, 0xd0, 0x40,
    0x20, 0xb0, 0x30, 0xa0, 0x00, 0x90, 0x10, 0x80, 0x60, 0xf0, 0x70, 0xe0, 0x40,
    0xd0, 0x50, 0xc0, 0x90, 0x00, 0x80, 0x10, 0xb0, 0x20, 0xa0, 0x30, 0xd0, 0x40,
    0xc0, 0x50, 0xf0, 0x60, 0xe0, 0x70, 0x10, 0x80, 0x00, 0x90, 0x30, 0xa0, 0x20,
    0xb0, 0x50, 0xc0, 0x40, 0xd0, 0x70, 0xe0, 0x60, 0xf0},
   {0x00, 0xb0, 0x50, 0xe0, 0xa0, 0x10, 0xf0, 0x40, 0x70, 0xc0, 0x20, 0x90, 0xd0,
    0x60, 0x80, 0x30, 0xe0, 0x50, 0xb0, 0x00, 0x40, 0xf0, 0x10, 0xa0, 0x90, 0x20,
    0xc0, 0x70, 0x30, 0x80, 0x60, 0xd0, 0xf0, 0x40, 0xa0, 0x10, 0x50, 0xe0, 0x00,
    0xb0, 0x80, 0x30, 0xd0, 0x60, 0x20, 0x90, 0x70, 0xc0, 0x10, 0xa0, 0x40, 0xf0,
    0xb0, 0x00, 0xe0, 0x50, 0x60, 0xd0, 0x30, 0x80, 0xc0, 0x70, 0x90, 0x20, 0xd0,
    0x60, 0x80, 0x30, 0x70, 0xc0, 0x20, 0x90, 0xa0, 0x10, 0xf0, 0x40, 0x00, 0xb0,
    0x50, 0xe0, 0x30, 0x80, 0x60, 0xd0, 0x90, 0x20, 0xc0, 0x70, 0x40, 0xf0, 0x10,
    0xa0, 0xe0, 0x50, 0xb0, 0x00, 0x20, 0x90, 0x70, 0xc0, 0x80, 0x30, 0xd0, 0x60,
    0x50, 0xe0, 0x00, 0xb0, 0xf0, 0x40, 0xa0, 0x10, 0xc0, 0x70, 0x90, 0x20, 0x60,
    0xd0, 0x30, 0x80, 0xb0, 0x00, 0xe0, 0x50, 0x10, 0xa0, 0x40, 0xf0, 0x90, 0x20,
    0xc0, 0x70, 0x30, 0x80, 0x60, 0xd0, 0xe0, 0x50, 0xb0, 0x00, 0x40, 0xf0, 0x10,
    0xa0, 0x70, 0xc0, 0x20, 0x90, 0xd0, 0x60, 0x80, 0x30, 0x00, 0xb0, 0x50, 0xe0,
    0xa0, 0x10, 0xf0, 0x40, 0x60, 0xd0, 0x30, 0x80, 0xc0, 0x70, 0x90, 0x20, 0x10,
    0xa0, 0x40, 0xf0, 0xb0, 0x00, 0xe0, 0x50, 0x80, 0x30, 0xd0, 0x60, 0x20, 0x90,
    0x70, 0xc0, 0xf0, 0x40, 0xa0, 0x10, 0x50, 0xe0, 0x00, 0xb0, 0x40, 0xf0, 0x10,
    0xa0, 0xe0, 0x50, 0xb0, 0x00, 0x30, 0x80, 0x60, 0xd0, 0x90, 0x20, 0xc0, 0x70,
    0xa0, 0x10, 0xf0, 0x40, 0x00, 0xb0, 0x50, 0xe0, 0xd0, 0x60, 0x80, 0x30, 0x70,
    0xc0, 0x20, 0x90, 0xb0, 0x00, 0xe0, 0x50, 0x10, 0xa0, 0x40, 0xf0, 0xc0, 0x70,
    0x90, 0x20, 0x60, 0xd0, 0x30, 0x80, 0x50, 0xe0, 0x00, 0xb0, 0xf0, 0x40, 0xa0,
    0x10, 0x20, 0x90, 0x70, 0xc0, 0x80, 0x30, 0xd0, 0x60},
   {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0,
    0xd0, 0xe0, 0xf0, 0x30, 0x20, 0x10, 0x00, 0x70, 0x60, 0x50, 0x40, 0xb0, 0xa0,
    0x90, 0x80, 0xf0, 0xe0, 0xd0, 0xc0, 0x60, 0x70, 0x40, 0x50, 0x20, 0x30, 0x00,
    0x10, 0xe0, 0xf0, 0xc0, 0xd0, 0xa0, 0xb0, 0x80, 0x90, 0x50, 0x40, 0x70, 0x60,
    0x10, 0x00, 0x30, 0x20, 0xd0, 0xc0, 0xf0, 0xe0, 0x90, 0x80, 0xb0, 0xa0, 0xc0,
    0xd0, 0xe0, 0xf0, 0x80, 0x90, 0xa0, 0xb0, 0x40, 0x50, 0x60, 0x70, 0x00, 0x10,
    0x20, 0x30, 0xf0, 0xe0, 0xd0, 0xc0, 0xb0, 0xa0, 0x90, 0x80, 0x70, 0x60, 0x50,
    0x40, 0x30, 0x20, 0x10, 0x00, 0xa0, 0xb0, 0x80, 0x90, 0xe0, 0xf0, 0xc0, 0xd0,
    0x20, 0x30, 0x00, 0x10, 0x60, 0x70, 0x40, 0x50, 0x90, 0x80, 0xb0, 0xa0, 0xd0,
    0xc0, 0xf0, 0xe0, 0x10, 0x00, 0x30, 0x20, 0x50, 0x40, 0x70, 0x60, 0xb0, 0xa0,
    0x90, 0x80, 0xf0, 0xe0, 0xd0, 0xc0, 0x30, 0x20, 0x10, 0x00, 0x70, 0x60, 0x50,
    0x40, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0x00, 0x10, 0x20, 0x30,
    0x40, 0x50, 0x60, 0x70, 0xd0, 0xc0, 0xf0, 0xe0, 0x90, 0x80, 0xb0, 0xa0, 0x50,
    0x40, 0x70, 0x60, 0x10, 0x00, 0x30, 0x20, 0xe0, 0xf0, 0xc0, 0xd0, 0xa0, 0xb0,
    0x80, 0x90, 0x60, 0x70, 0x40, 0x50, 0x20, 0x30, 0x00, 0x10, 0x70, 0x60, 0x50,
    0x40, 0x30, 0x20, 0x10, 0x00, 0xf0, 0xe0, 0xd0, 0xc0, 0xb0, 0xa0, 0x90, 0x80,
    0x40, 0x50, 0x60, 0x70, 0x00, 0x10, 0x20, 0x30, 0xc0, 0xd0, 0xe0, 0xf0, 0x80,
    0x90, 0xa0, 0xb0, 0x10, 0x00, 0x30, 0x20, 0x50, 0x40, 0x70, 0x60, 0x90, 0x80,
    0xb0, 0xa0, 0xd0, 0xc0, 0xf0, 0xe0, 0x20, 0x30, 0x00, 0x10, 0x60, 0x70, 0x40,
    0x50, 0xa0, 0xb0, 0x80, 0x90, 0xe0, 0xf0, 0xc0, 0xd0}
};

// Compute the crc-4bit, a byte at a time using the table approach
// This code was partially generated from the crcany program of Mark Adler (see https://github.com/madler/crcany)
uint8_t frame_equalizer_impl::crc4HaLoW_byte(uint8_t crc, void const *mem, size_t len) {
    unsigned char const *data = static_cast<unsigned char const *>(mem);
    if (data == nullptr)
        return 0;
    crc <<= 4;
    for (size_t i = 0; i < len; i++) {
        crc = table_byte[crc ^ data[i]];
    }
    crc >>= 4;

    return crc ^ 0xf;
}

} /* namespace ieee802_11 */
} /* namespace gr */
