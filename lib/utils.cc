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
#include "utils.h"

#include <math.h>
#include <cassert>
#include <cstring>

using gr::ieee802_11::BPSK_1_2;
using gr::ieee802_11::QPSK_1_2;
using gr::ieee802_11::QPSK_3_4;
using gr::ieee802_11::QAM16_1_2;
using gr::ieee802_11::QAM16_3_4;
using gr::ieee802_11::QAM64_2_3;
using gr::ieee802_11::QAM64_3_4;
using gr::ieee802_11::QAM64_5_6;
using gr::ieee802_11::BPSK_1_2_REP;

ofdm_param::ofdm_param(Encoding e)
{
    encoding = e;

    switch (e) {
    case BPSK_1_2:
        n_bpsc = 1;
        n_cbps = 24;
        n_dbps = 12;
        //rate_field = 0x00; // i.e. MCS 0b00000000
        constellation =  gr::ieee802_11::constellation_bpsk::make();
        break;

    case QPSK_1_2:
        n_bpsc = 2;
        n_cbps = 48;
        n_dbps = 24;
        //rate_field = 0x05; // 0b00000101
        constellation =  gr::ieee802_11::constellation_qpsk::make();
        break;

    case QPSK_3_4:
        n_bpsc = 2;
        n_cbps = 48;
        n_dbps = 36;
        //rate_field = 0x07; // 0b00000111
        constellation =  gr::ieee802_11::constellation_qpsk::make();
        break;

    case QAM16_1_2:
        n_bpsc = 4;
        n_cbps = 96;
        n_dbps = 48;
        //rate_field = 0x09; // 0b00001001
        constellation =  gr::ieee802_11::constellation_16qam::make();
        break;

    case QAM16_3_4:
        n_bpsc = 4;
        n_cbps = 96;
        n_dbps = 72;
        //rate_field = 0x0B; // 0b00001011
        constellation =  gr::ieee802_11::constellation_16qam::make();
        break;

    case QAM64_2_3:
        n_bpsc = 6;
        n_cbps = 144;
        n_dbps = 96;
        //rate_field = 0x01; // 0b00000001
        constellation =  gr::ieee802_11::constellation_16qam::make();
        break;

    case QAM64_3_4:
        n_bpsc = 6;
        n_cbps = 144;
        n_dbps = 108;
        //rate_field = 0x03; // 0b00000011
        constellation =  gr::ieee802_11::constellation_64qam::make();
        break;

    case QAM64_5_6:
        n_bpsc = 6;
        n_cbps = 144;
        n_dbps = 120;
        //rate_field = 0x03; // 0b00000011
        constellation =  gr::ieee802_11::constellation_64qam::make();
        break;
    
    case BPSK_1_2_REP:
        n_bpsc = 1;
        n_cbps = 12;
        n_dbps = 6;
        //rate_field = 0x0a; // i.e. MCS 0b00001010
        constellation =  gr::ieee802_11::constellation_bpsk::make();
        break;

    defaut:
        assert(false);
        break;
    }
}


void ofdm_param::print()
{
    std::cout << "OFDM Parameters:" << std::endl;
    std::cout << "endcoding :" << encoding << std::endl;
    std::cout << "rate_field :" << (int)rate_field << std::endl;
    std::cout << "n_bpsc :" << n_bpsc << std::endl;
    std::cout << "n_cbps :" << n_cbps << std::endl;
    std::cout << "n_dbps :" << n_dbps << std::endl;
}

//constructor to be used for data frames
frame_param::frame_param(ofdm_param& ofdm, int psdu_length)
{

    psdu_size = psdu_length;

    // number of symbols p.3248 "Data Field" for HaLow OR EQN23-65 on p.3302 OR EQN 23-66 on p.3303
    n_sym = (int)ceil((8 * psdu_size + 8 + 6) / (double) ofdm.n_dbps);//see Equation 23-79

    n_data_bits = n_sym * ofdm.n_dbps;

    // number of symbols p.3248 "Data Field" for HaLow
    n_pad = n_data_bits - (8 * psdu_size + 8 + 6);

    n_encoded_bits = n_sym * ofdm.n_cbps;
}

//constructor to be used for SIG field
frame_param::frame_param(ofdm_param& ofdm){
    //sig field is always NUM_OFDM_SYMBOLS_IN_SIG_FIELD symbols long
    n_sym = NUM_OFDM_SYMBOLS_IN_SIG_FIELD;
    //viterbi decoder processes bytes per bytes. n_encoded_bits needs to be the nearest mulitple of 8 capable of holding the number of encoded bits in the sig field
    n_encoded_bits = (n_sym * ofdm.n_cbps) + (8 - (n_sym * ofdm.n_cbps) % 8);
    //sig field is bpsk 1/2 coded
    n_data_bits = n_encoded_bits/2;

}

void frame_param::print()
{
    std::cout << "FRAME Parameters:" << std::endl;
    std::cout << "psdu_size: " << psdu_size << std::endl;
    std::cout << "n_sym: " << n_sym << std::endl;
    std::cout << "n_pad: " << n_pad << std::endl;
    std::cout << "n_encoded_bits: " << n_encoded_bits << std::endl;
    std::cout << "n_data_bits: " << n_data_bits << std::endl;
}


void scramble(const char* in, char* out, frame_param& frame, char initial_state)
{

    int state = initial_state;
    int feedback;

    for (int i = 0; i < frame.n_data_bits; i++) {

        feedback = (!!(state & 64)) ^ (!!(state & 8));
        out[i] = feedback ^ in[i];
        state = ((state << 1) & 0x7e) | feedback;
    }
}


void reset_tail_bits(char* scrambled_data, frame_param& frame)
{
    memset(scrambled_data + frame.n_data_bits - frame.n_pad - 6, 0, 6 * sizeof(char));
}


int ones(int n)
{
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        if (n & (1 << i)) {
            sum++;
        }
    }
    return sum;
}


void convolutional_encoding(const char* in, char* out, frame_param& frame)
{

    int state = 0;

    for (int i = 0; i < frame.n_data_bits; i++) {
        assert(in[i] == 0 || in[i] == 1);
        state = ((state << 1) & 0x7e) | in[i];
        out[i * 2] = ones(state & 0155) % 2;
        out[i * 2 + 1] = ones(state & 0117) % 2;
    }
}


void puncturing(const char* in, char* out, frame_param& frame, ofdm_param& ofdm)
{

    int mod;

    for (int i = 0; i < frame.n_data_bits * 2; i++) {
        switch (ofdm.encoding) {
        case BPSK_1_2:
        case QPSK_1_2:
        case QAM16_1_2:
        case BPSK_1_2_REP:
            *out = in[i];
            out++;
            break;

        case QAM64_2_3:
            if (i % 4 != 3) {
                *out = in[i];
                out++;
            }
            break;

        case QPSK_3_4:
        case QAM16_3_4:
        case QAM64_3_4:
            mod = i % 6;
            if (!(mod == 3 || mod == 4)) {
                *out = in[i];
                out++;
            }
            break;
        //TODO : Add QAM64_5_6
        defaut:
            assert(false);
            break;
        }
    }
}

void repeat(const char* in, char* out, frame_param& frame, ofdm_param& ofdm)
{
    char s[NUM_BITS_UNREPEATED_SIG_SYMBOL] = {1,0,0,0,0,1,0,1,0,1,1,1};

    for (int i = 0; i < frame.n_sym; i++) {
        for(int j = 0; j < CODED_BITS_PER_OFDM_SYMBOL/2; j++){
            
            //simple repeat
            out[i * CODED_BITS_PER_OFDM_SYMBOL + j] = in[i * CODED_BITS_PER_OFDM_SYMBOL/2 + j];
            //XORed repeat
            out[i * CODED_BITS_PER_OFDM_SYMBOL + j + CODED_BITS_PER_OFDM_SYMBOL/2] = in[i * CODED_BITS_PER_OFDM_SYMBOL/2 + j] ^ s[j];

        }
    }

}

void interleave(const char* in, char* out, frame_param& frame, ofdm_param& ofdm, bool reverse)
{

    int n_cbps = ofdm.n_cbps >= CODED_BITS_PER_OFDM_SYMBOL ? ofdm.n_cbps : CODED_BITS_PER_OFDM_SYMBOL;

    int first[MAX_BITS_PER_SYM];
    int second[MAX_BITS_PER_SYM];
    int s = std::max(ofdm.n_bpsc / 2, 1);
    int ncol = 8;
    int nrow = 3 * ofdm.n_bpsc;

    for (int j = 0; j < n_cbps; j++) {
        first[j] = s * (j / s) + ((j + int(floor(ncol * j / n_cbps))) % s); // Eq. 21-82 p. 3078
    }

    for (int i = 0; i < n_cbps; i++) {
        second[i] = ncol * i - (n_cbps - 1) * int(floor(i / nrow)); // Eq. 21-83 p. 3078
    }

    for (int i = 0; i < frame.n_sym; i++) {
        for (int k = 0; k < n_cbps; k++) {
            if (reverse) {
                out[i * n_cbps + second[first[k]]] = in[i * n_cbps + k];
            } else {
                out[i * n_cbps + k] = in[i * n_cbps + second[first[k]]];
            }
        }
    }
}

void deinterleave(const uint8_t* in, uint8_t* out, frame_param& frame, ofdm_param& ofdm, bool reverse)
{

    int n_cbps = ofdm.n_cbps >= CODED_BITS_PER_OFDM_SYMBOL ? ofdm.n_cbps : CODED_BITS_PER_OFDM_SYMBOL;

    int first[MAX_BITS_PER_SYM];
    int second[MAX_BITS_PER_SYM];
    int s = std::max(ofdm.n_bpsc / 2, 1);
    int ncol = 8;
    int nrow = 3 * ofdm.n_bpsc;

    for (int j = 0; j < n_cbps; j++) {
        first[j] = s * (j / s) + ((j + int(floor(ncol * j / n_cbps))) % s); // Eq. 21-82 p. 3078
    }

    for (int i = 0; i < n_cbps; i++) {
        second[i] = ncol * i - (n_cbps - 1) * int(floor(i / nrow)); // Eq. 21-83 p. 3078
    }

    for (int k = 0; k < n_cbps; k++) {
        if (reverse) {
            out[second[first[k]]] = in[k];
        } else {
            out[k] = in[second[first[k]]];
        }
    }
}


void split_symbols(const char* in, char* out, frame_param& frame, ofdm_param& ofdm)
{

    int symbols = frame.n_sym * CODED_BITS_PER_OFDM_SYMBOL;

    for (int i = 0; i < symbols; i++) {
        out[i] = 0;
        for (int k = 0; k < ofdm.n_bpsc; k++) {
            assert(*in == 1 || *in == 0);
            out[i] |= (*in << k);
            in++;
        }
    }
}


void generate_bits(const char* psdu, char* data_bits, frame_param& frame)
{

    // first 8 bits are zero (SERVICE field) (see p. 3248)
    memset(data_bits, 0, 8);
    data_bits += 8;

    for (int i = 0; i < frame.psdu_size; i++) {
        for (int b = 0; b < 8; b++) {
            data_bits[i * 8 + b] = !!(psdu[i] & (1 << b));
        }
    }
}


void deinterleave(gr_complex* deinterleaved, const gr_complex* rx_symbols)
{   
    for (int i = 0; i < CODED_BITS_PER_OFDM_SYMBOL; i++) {
        deinterleaved[i] = rx_symbols[interleaver_pattern[i]];
    }
}

void unrepeat(gr_complex* unrepeated, gr_complex* deinterleaved){

    //Unrepeat using Maximum Ratio Combining
    //in this case the symbols have already been multiplied by the channel conjugate (see equalizer)
    //therefore all we still need to do is to peform an average of the signal repetitions

    uint8_t s[NUM_BITS_UNREPEATED_SIG_SYMBOL] = {1,0,0,0,0,1,0,1,0,1,1,1};

    for(int i = 0; i < NUM_BITS_UNREPEATED_SIG_SYMBOL; i++){

        //combine
        unrepeated[i] = deinterleaved[i] * gr_complex(0.5,0) + //first sample
                          deinterleaved[i + NUM_BITS_UNREPEATED_SIG_SYMBOL] * gr_complex((s[i] == 0 ? 0.5 : -0.5),0); //second sample, inverted in case s == 1

        /*
        if((deinterleaved[i].real() < 0) != (deinterleaved[i + NUM_BITS_UNREPEATED_SIG_SYMBOL].real() * (s[i] == 0 ? 1 : -1) < 0 )){
            std::cout << "ERROR in unrepeat" << std::endl;
        }
        */
        
    }
}

// Compute the crc-4bit, a byte at a time using the table approach
// This code was partially generated from the crcany program of Mark Adler (see https://github.com/madler/crcany)
uint8_t crc4HaLoW_byte(uint8_t crc, void const *mem, size_t len) {
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

uint8_t compute_crc(uint8_t* decoded_bits){

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