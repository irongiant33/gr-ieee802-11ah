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
#include "utils.h"
#include <gnuradio/fft/fft.h>
#include <gnuradio/filter/fir_filter.h>
#include <gnuradio/io_signature.h>
#include <ieee802_11ah/sync_long.h>
#include <volk/volk.h>

#include <list>
#include <tuple>

using namespace gr::ieee802_11ah;
using namespace std;


bool compare_abs(const std::pair<gr_complex, int>& first,
                 const std::pair<gr_complex, int>& second)
{
    return abs(get<0>(first)) > abs(get<0>(second));
}

class sync_long_impl : public sync_long
{

public:
    sync_long_impl(unsigned int sync_length, bool log, bool debug)
        : block("sync_long",
                gr::io_signature::make2(2, 2, sizeof(gr_complex), sizeof(gr_complex)),
                gr::io_signature::make(1, 1, sizeof(gr_complex))),
          d_fir(gr::filter::kernel::fir_filter_ccc(LONG)),
          d_log(log),
          d_debug(debug),
          d_offset(0),
          d_state(SYNC),
          SYNC_LENGTH(sync_length)//sync_len is the number of samples from the preambule start (1st STS complex symbol) to the end of the second LTS (last complex symbol of the second LTS).
                                    //in a first instance, we want to avoid changing the algorithm for peak detection. Therefore we need to make sure only 2 LTS are contained in the 
                                    //sync_length. This means sync_length should be 240 (- min_plateau) samples long.
    {

        set_tag_propagation_policy(block::TPP_DONT);
        d_correlation = (gr_complex*)volk_malloc(sizeof(gr_complex) * 8192, volk_get_alignment());
    }

    ~sync_long_impl() {
        volk_free(d_correlation);
    }

    int general_work(int noutput,
                     gr_vector_int& ninput_items,
                     gr_vector_const_void_star& input_items,
                     gr_vector_void_star& output_items)
    {

        const gr_complex* in = (const gr_complex*)input_items[0];
        const gr_complex* in_delayed = (const gr_complex*)input_items[1];
        gr_complex* out = (gr_complex*)output_items[0];

        dout << "LONG ninput[0] " << ninput_items[0] << "   ninput[1] " << ninput_items[1]
             << "  noutput " << noutput << "   state " << d_state << std::endl;

        int ninput = std::min(std::min(ninput_items[0], ninput_items[1]), 8192);

        const uint64_t nread = nitems_read(0);
        get_tags_in_range(d_tags, 0, nread, nread + ninput);
        if (d_tags.size()) {
            std::sort(d_tags.begin(), d_tags.end(), gr::tag_t::offset_compare);

            const uint64_t offset = d_tags.front().offset;

            if (offset > nread) {
                ninput = offset - nread;
            } else {
                if (d_offset && (d_state == SYNC)) {
                    throw std::runtime_error("wtf");
                }
                if (d_state == COPY) {
                    d_state = RESET;
                }
                d_freq_offset_short = pmt::to_double(d_tags.front().value);
            }
        }


        int i = 0;
        int o = 0;

        switch (d_state) {

        case SYNC:
            d_fir.filterN(
                d_correlation, in, std::min(SYNC_LENGTH, std::max(ninput - (SAMPLES_PER_OFDM_SYMBOL - 1), 0)));

            while (i + (SAMPLES_PER_OFDM_SYMBOL - 1) < ninput) {

                d_cor.push_back(pair<gr_complex, int>(d_correlation[i], d_offset));

                i++;
                d_offset++;

                if (d_offset == SYNC_LENGTH) {
                    search_frame_start();
                    mylog("LONG: frame start at {}",d_frame_start);
                    d_offset = 0;
                    d_count = 0;
                    d_state = COPY;

                    break;
                }
            }

            break;

        case COPY:
            while (i < ninput && o < noutput) {

                int rel = d_offset - d_frame_start;

                if (!rel) {
                    add_item_tag(0,
                                 nitems_written(0),
                                 pmt::string_to_symbol("halow_start"),
                                 pmt::from_double(d_freq_offset_short - d_freq_offset),
                                 pmt::string_to_symbol(name()));
                }

                // send LTFs + SIG + DATA downstream with GIs filtered out
                if (rel >= 0 && (rel < 2*SAMPLES_PER_OFDM_SYMBOL || ((rel - 2*SAMPLES_PER_OFDM_SYMBOL) % (SAMPLES_PER_OFDM_SYMBOL + SAMPLES_PER_GI)) > (SAMPLES_PER_GI - 1))) {
                    //we mulitply by +d_freq_offset because we take conj of the second peak for freq offset computation (and not conj of first one)
                    out[o] = in_delayed[i] * exp(gr_complex(0, d_freq_offset * d_offset));
                    o++;
                }

                i++;
                d_offset++;
            }

            break;

        case RESET: {
            while (o < noutput) {
                if (((d_count + o) % SAMPLES_PER_OFDM_SYMBOL) == 0) {
                    d_offset = 0;
                    d_state = SYNC;
                    break;
                } else {
                    out[o] = 0;
                    o++;
                }
            }

            break;
        }
        }

        dout << "produced : " << o << " consumed: " << i << std::endl;

        d_count += o;
        consume(0, i);
        consume(1, i);
        return o;
    }

    void forecast(int noutput_items, gr_vector_int& ninput_items_required)
    {

        // in sync state we need at least a symbol to correlate
        // with the pattern
        if (d_state == SYNC) {
            ninput_items_required[0] = SAMPLES_PER_OFDM_SYMBOL;
            ninput_items_required[1] = SAMPLES_PER_OFDM_SYMBOL;

        } else {
            ninput_items_required[0] = noutput_items;
            ninput_items_required[1] = noutput_items;
        }
    }

    void search_frame_start()
    {

        // sort list (highest correlation first)
        assert(d_cor.size() == SYNC_LENGTH);
        d_cor.sort(compare_abs);

        // copy list in vector for nicer access
        vector<pair<gr_complex, int>> vec(d_cor.begin(), d_cor.end());
        d_cor.clear();

        // in case we don't find anything use SYNC_LENGTH
        d_frame_start = SYNC_LENGTH;

        for (int i = 0; i < 3; i++) {
            for (int k = i + 1; k < 4; k++) {
                gr_complex first;
                gr_complex second;
                if (get<1>(vec[i]) > get<1>(vec[k])) {
                    first = get<0>(vec[k]);
                    second = get<0>(vec[i]);
                } else {
                    first = get<0>(vec[i]);
                    second = get<0>(vec[k]);
                }
                int diff = abs(get<1>(vec[i]) - get<1>(vec[k]));
                if (diff == SAMPLES_PER_OFDM_SYMBOL) {
                    d_frame_start = min(get<1>(vec[i]), get<1>(vec[k]));
                    d_freq_offset = arg(first * conj(second)) / SAMPLES_PER_OFDM_SYMBOL;
                    // nice match found, return immediately
                    return;

                } else if (diff == (SAMPLES_PER_OFDM_SYMBOL - 1)) {
                    d_frame_start = min(get<1>(vec[i]), get<1>(vec[k]));
                    d_freq_offset = arg(first * conj(second)) / (SAMPLES_PER_OFDM_SYMBOL - 1);
                } else if (diff == (SAMPLES_PER_OFDM_SYMBOL + 1)) {
                    d_frame_start = min(get<1>(vec[i]), get<1>(vec[k]));
                    d_freq_offset = arg(first * conj(second)) / (SAMPLES_PER_OFDM_SYMBOL + 1);
                }
            }
        }
    }

private:
    enum { SYNC, COPY, RESET } d_state;
    int d_count;
    int d_offset;
    int d_frame_start;
    float d_freq_offset;
    double d_freq_offset_short;

    gr_complex* d_correlation;
    list<pair<gr_complex, int>> d_cor;
    std::vector<gr::tag_t> d_tags;
    gr::filter::kernel::fir_filter_ccc d_fir;

    const bool d_log;
    const bool d_debug;
    const int SYNC_LENGTH;

    static const std::vector<gr_complex> LONG;
};

sync_long::sptr sync_long::make(unsigned int sync_length, bool log, bool debug)
{
    return gnuradio::get_initial_sptr(new sync_long_impl(sync_length, log, debug));
}

// from root of project directory:
// Rscript utils/create_long_halow.R
const std::vector<gr_complex> sync_long_impl::LONG = {
    gr_complex(-0.2179, -0.4339), gr_complex( 0.1824, -1.3445), gr_complex( 0.3847, -0.8715), gr_complex(-0.4398,  0.3922),
    gr_complex(-0.0765, -1.0116), gr_complex( 0.2703, -1.0443), gr_complex(-0.3261, -0.6552), gr_complex( 0.3922, -0.7845),
    gr_complex( 0.3261,  0.7545), gr_complex(-1.0548,  0.6198), gr_complex( 0.0765,  0.8226), gr_complex( 1.2243, -0.3922),
    gr_complex(-0.3847, -1.2561), gr_complex(-0.9669,  0.3196), gr_complex( 0.2179, -1.2431), gr_complex( 0.7845, -0.0000),
    gr_complex( 0.2179,  1.2431), gr_complex(-0.9669, -0.3196), gr_complex(-0.3847,  1.2561), gr_complex( 1.2243,  0.3922),
    gr_complex( 0.0765, -0.8226), gr_complex(-1.0548, -0.6198), gr_complex( 0.3261, -0.7545), gr_complex( 0.3922,  0.7845),
    gr_complex(-0.3261,  0.6552), gr_complex( 0.2703,  1.0443), gr_complex(-0.0765,  1.0116), gr_complex(-0.4398, -0.3922),
    gr_complex( 0.3847,  0.8715), gr_complex( 0.1824,  1.3445), gr_complex(-0.2179,  0.4339), gr_complex( 0.0000, -0.0000)
};
