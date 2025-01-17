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
#include <ieee802_11/decode_mac.h>

#include "utils.h"
#include "viterbi_decoder/viterbi_decoder.h"

#include <gnuradio/io_signature.h>
#include <boost/crc.hpp>
#include <iomanip>

using namespace gr::ieee802_11;

#define LINKTYPE_IEEE802_11 105 /* http://www.tcpdump.org/linktypes.html */
#define BYTE_SERVICE 1
#define BYTE_CRC32 4

class decode_mac_impl : public decode_mac
{

public:
    decode_mac_impl(bool log, bool debug)
        : block("decode_mac",
                gr::io_signature::make(1, 1, CODED_BITS_PER_OFDM_SYMBOL * sizeof(gr_complex)),
                gr::io_signature::make(0, 0, 0)),
          d_log(log),
          d_debug(debug),
          d_ofdm(BPSK_1_2),
          d_frame(d_ofdm, 0),
          d_frame_complete(true)
    {
        message_port_register_out(pmt::mp("out"));
    }

    int general_work(int noutput_items,
                     gr_vector_int& ninput_items,
                     gr_vector_const_void_star& input_items,
                     gr_vector_void_star& output_items)
    {

        const gr_complex* in = (const gr_complex*)input_items[0];

        int i = 0;

        std::vector<gr::tag_t> tags;
        const uint64_t nread = this->nitems_read(0);

        //TODO : uncomment this
        //dout << "Decode MAC: input " << ninput_items[0] << std::endl;

        while (i < ninput_items[0]) {

            get_tags_in_range(tags, 0, nread + i, nread + i + 1);

            if (tags.size()) {
                if (d_frame_complete == false) {
                    dout << "Warning: starting to receive new frame before old frame was "
                            "complete"
                         << std::endl;
                    dout << "Already copied " << copied << " out of " << d_frame.n_sym
                         << " symbols of last frame" << std::endl;
                }
                d_frame_complete = false;

                // Enter tags into metadata dictionary
                d_meta = pmt::make_dict();
                for (auto tag : tags)
                    d_meta = pmt::dict_add(d_meta, tag.key, tag.value);

                int len_data = pmt::to_uint64(pmt::dict_ref(
                    d_meta, pmt::mp("frame bytes"), pmt::from_uint64(MAX_PSDU_SIZE + 1)));
                int encoding = pmt::to_uint64(
                    pmt::dict_ref(d_meta, pmt::mp("encoding"), pmt::from_uint64(0)));

                ofdm_param ofdm = ofdm_param((Encoding)encoding);
                frame_param frame = frame_param(ofdm, len_data);

                // check for maximum frame size
                if (frame.n_sym <= MAX_SYM && frame.psdu_size <= MAX_PSDU_SIZE) {
                    d_ofdm = ofdm;
                    d_frame = frame;
                    copied = 0;
                    dout << "Decode MAC: frame start -- len " << len_data << "  symbols "
                         << frame.n_sym << "  encoding " << encoding << std::endl;
                } else {
                    dout << "Dropping frame which is too large (symbols or bits)"
                         << std::endl;
                }
            }

            if (copied < d_frame.n_sym) {

                //TODO : uncomment this
                //dout << "copy one symbol, copied " << copied << " out of "
                     //<< d_frame.n_sym << std::endl;
                
                //if MCS = 10
                if(d_ofdm.encoding == gr::ieee802_11::BPSK_1_2_REP){
                    
                    //deinterleave the complex symbols
                    gr_complex d_deinterleaved[CODED_BITS_PER_OFDM_SYMBOL];
                    deinterleave(d_deinterleaved, in);

                    //unrepeat the complex symbols
                    unrepeat(d_unrepeated, d_deinterleaved);

                    //bit decision last
                    for (int j = 0; j < d_ofdm.n_cbps; j++){
                        d_rx_bits[j] = d_ofdm.constellation->decision_maker(&d_unrepeated[j]);
                    }
                }

                //for any other MCS
                else{
                    
                    //bit decision first
                    for (int j = 0; j < CODED_BITS_PER_OFDM_SYMBOL; j++){
                        for(int k = 0; k < d_ofdm.n_bpsc; k++){
                            d_rx_bits[j * d_ofdm.n_bpsc + k] = !!(d_ofdm.constellation->decision_maker(&in[j]) &(1 << k));
                        }
                    }

                    //deinterleave the bits
                    uint8_t d_deinterleaved[MAX_BITS_PER_SYM];
                    deinterleave(d_rx_bits, d_deinterleaved, d_frame, d_ofdm);
                    
                    //copy deinterleaved bits back into d_rx_bits
                    memcpy(d_rx_bits, d_deinterleaved, d_ofdm.n_cbps * sizeof(uint8_t));
                }

                //copy d_rx_bits into d_encoded_bits for future conv decoding
                memcpy(d_encoded_bits + copied * d_ofdm.n_cbps, d_rx_bits, d_ofdm.n_cbps);

                /*
                //for all other MCS's
                else{
                    //add the decided bit into d_encoded_bits
                    //TODO : rename CODED_BITS_PER_OFDM_SYMBOL to SINGLE_CARRIER_PER_OFDM_SYMBOL
                    for (int j = 0; j < CODED_BITS_PER_OFDM_SYMBOL; j++){
                        //int k = 0;
                        for (int k = 0; k < d_ofdm.n_bpsc; k++){
                            d_encoded_bits[copied * d_ofdm.n_cbps + j * d_ofdm.n_bpsc + k] = !!(d_ofdm.constellation->decision_maker(&d_deinterleaved[j]) & (1 << (d_ofdm.n_bpsc-1 - k)));
                            
                        }
                    }
                }
                */

                //std::memcpy(d_rx_symbols + (copied * CODED_BITS_PER_OFDM_SYMBOL), in, CODED_BITS_PER_OFDM_SYMBOL * sizeof(gr_complex));
                copied++;

                if (copied == d_frame.n_sym) {
                    dout << "received complete frame - decoding" << std::endl;
                    decode();
                    in += CODED_BITS_PER_OFDM_SYMBOL;
                    i++;
                    d_frame_complete = true;
                    break;
                }
            }

            in += CODED_BITS_PER_OFDM_SYMBOL;
            i++;
        }

        consume(0, i);

        return 0;
    }

    void decode()
    {   
        //TODO
        /*
        for (int i = 0; i < d_frame.n_sym * CODED_BITS_PER_OFDM_SYMBOL; i++) {
            for (int k = 0; k < d_ofdm.n_bpsc; k++) {
                d_rx_bits[i * d_ofdm.n_bpsc + k] = !!(d_rx_symbols[i] & (1 << k));
            }
        }
        */
        
        //debug
        //gr_complex sum0 = gr_complex(0,0);
        /*
        //loop over all symbols
        for (int i = 0; i < d_frame.n_sym; i++){

            deinterleave(d_deinterleaved, d_rx_symbols + (i * CODED_BITS_PER_OFDM_SYMBOL));


            //if MCS = 10
            if(d_ofdm.encoding == gr::ieee802_11::BPSK_1_2_REP){
                //unrepeat the symbols
                //dout << "Current symbol" << i << std::endl;

                unrepeat(d_unrepeated, d_deinterleaved);

                //add the decided bit into d_encoded_bits
                for (int j = 0; j < CODED_BITS_PER_OFDM_SYMBOL/2; j++){
                    d_encoded_bits[i * CODED_BITS_PER_OFDM_SYMBOL/2 + j] = d_ofdm.constellation->decision_maker(&d_unrepeated[j]);

                    //debug
                    //sum0 += d_unrepeated[j];
                }
            }

            //for all other MCS's
            else{

                //add the decided bit into d_encoded_bits
                for (int j = 0; j < CODED_BITS_PER_OFDM_SYMBOL; j++){
                    int k = 0;
                    //for (int k = 0; k < d_ofdm.n_bpsc; k++){
                        //TODO : rename CODED_BITS_PER_OFDM_SYMBOL to SINGLE_CARRIER_PER_OFDM_SYMBOL
                        d_encoded_bits[i * CODED_BITS_PER_OFDM_SYMBOL * d_ofdm.n_bpsc + j * d_ofdm.n_bpsc + k] = !!(d_ofdm.constellation->decision_maker(&d_deinterleaved[j]) & (1 << (d_ofdm.n_bpsc-1 - k)));

                        
                        if(i = 0 && j == 0){
                            dout << "Constellation point : " << d_deinterleaved[j] << std::endl;
                            dout << "k" << std::endl;
                            dout << !!(d_ofdm.constellation->decision_maker(&d_deinterleaved[j]) & (1 << (d_ofdm.n_bpsc-1 - k)));
                }
                        
                    //}
                }
            }
        }

        */

        

        //debug
        //std::cout << "Frame Sym : " << d_frame.n_sym << std::endl;
        //std::cout << "SUM 0 : " << sum0 << std::endl;
        

        /*
        dout << "Pre decode: ";
        for(int i = 0; i < 3024; i++)
        {
            if (d_encoded_bits[i] == 1){
                dout << "1";
            }
            else if(d_encoded_bits[i] == 0){
                dout << "0";
            }
            else{
                dout << "E";
            }

            dout << ",";
        }
        dout << std::endl;
        */
        
        
        
        
        
    
        /*
        int sum1 = 0;
        for(int i = 0; i < 81760; i++){
            sum1 += d_encoded_bits[i];
        }
        std::cout << "SUM 1 : " << sum1 << std::endl;
        */
       
        //d_frame.print();
        //d_ofdm.print();

        //debug
        dout << "Frame data bits : " << d_frame.n_data_bits << std::endl;

        //round n_data_bits to superior and closest multiple of 8
        d_frame.n_data_bits  += 7;
        d_frame.n_data_bits  &= 0xfff8;

        dout << "Frame data bits now : " << d_frame.n_data_bits << std::endl;
        

        if(d_frame.n_data_bits % 8 != 0){
            dout << "ERROR : n data bits should be a multiple of 8 ! " << std::endl;
        }
        
        uint8_t* decoded = d_decoder.decode(&d_ofdm, &d_frame, d_encoded_bits);


        /*
        dout << "Post decode: ";
        for(int i = 0; i < 264; i++)
        {
            if (decoded[i] == 1){
                dout << "1";
            }
            else if(decoded[i] == 0){
                dout << "0";
            }
            else{
                dout << "E";
            }

            dout << ",";
        }
        dout << std::endl;
        */
        

        descramble(decoded);


        print_output();

        // skip service field
        boost::crc_32_type result;
        result.process_bytes(out_bytes + BYTE_SERVICE, d_frame.psdu_size);
        if (result.checksum() != 558161692) {
            dout << "checksum wrong -- dropping. expected 558161692 got: " << result.checksum() << std::endl;
            return;
        }

        mylog("encoding: {} - length: {} - symbols: {}",
              d_ofdm.encoding,
              d_frame.psdu_size,
              d_frame.n_sym);

        // create PDU
        pmt::pmt_t blob = pmt::make_blob(out_bytes + BYTE_SERVICE, d_frame.psdu_size - BYTE_CRC32);
        d_meta =
            pmt::dict_add(d_meta, pmt::mp("dlt"), pmt::from_long(LINKTYPE_IEEE802_11));

        message_port_pub(pmt::mp("out"), pmt::cons(d_meta, blob));

    }

    void descramble(uint8_t* decoded_bits)
    {

        int state = 0;
        std::memset(out_bytes, 0, d_frame.psdu_size + BYTE_SERVICE);

        for (int i = 0; i < 7; i++) {
            if (decoded_bits[i]) {
                state |= 1 << (6 - i);
            }
        }
        out_bytes[0] = state;

        int feedback;
        int bit;

        for (int i = 7; i < d_frame.psdu_size * 8 + 8; i++) {
            feedback = ((!!(state & 64))) ^ (!!(state & 8));
            bit = feedback ^ (decoded_bits[i] & 0x1);
            out_bytes[i / 8] |= bit << (i % 8);
            state = ((state << 1) & 0x7e) | feedback;
        }
    }

    void print_output()
    {

        dout << std::endl;
        dout << "psdu size" << d_frame.psdu_size << std::endl;
        for (int i = 0; i < d_frame.psdu_size + BYTE_SERVICE; i++) {
            dout << std::setfill('0') << std::setw(2) << std::hex
                 << ((unsigned int)out_bytes[i] & 0xFF) << std::dec << " ";
            if (i % 16 == 15) {
                dout << std::endl;
            }
        }
        dout << std::endl;
        for (int i = 0; i < d_frame.psdu_size + BYTE_SERVICE; i++) {
            if ((out_bytes[i] > 31) && (out_bytes[i] < 127)) {
                dout << ((char)out_bytes[i]);
            } else {
                dout << ".";
            }
        }
        dout << std::endl;
    }

private:
    bool d_debug;
    bool d_log;

    pmt::pmt_t d_meta;

    frame_param d_frame;
    ofdm_param d_ofdm;

    viterbi_decoder d_decoder;

    gr_complex *d_rx_symbols;
    uint8_t d_rx_bits[MAX_ENCODED_BITS];

    uint8_t out_bytes[MAX_PSDU_SIZE + 6]; // 2 for signal field

    //gr_complex d_deinterleaved[CODED_BITS_PER_OFDM_SYMBOL];
    gr_complex d_unrepeated[NUM_BITS_UNREPEATED_SIG_SYMBOL];
    uint8_t d_encoded_bits[MAX_ENCODED_BITS] = {0};

    int copied;
    bool d_frame_complete;
};

decode_mac::sptr decode_mac::make(bool log, bool debug)
{
    return gnuradio::get_initial_sptr(new decode_mac_impl(log, debug));
}
