/* -*- c++ -*- */
/*
 * Copyright 2020 gr-iridium author.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "iuchar_to_complex_impl.h"
#include <iostream>
#include <boost/asio.hpp>
#include <gnuradio/io_signature.h>

namespace gr {
namespace iridium {

iuchar_to_complex::sptr iuchar_to_complex::make()
{
    return gnuradio::get_initial_sptr(new iuchar_to_complex_impl());
}


/*
 * The private constructor
 */
iuchar_to_complex_impl::iuchar_to_complex_impl()
    : gr::sync_decimator("iuchar_to_complex",
                         gr::io_signature::make(1, 1, sizeof(gr_complex )),
                         gr::io_signature::make(1, 1, sizeof(gr_complex)),
                         1)
{
    //set_output_multiple(4096);
    start_flag = 0; s_cycle=0;
    freq_flag = 0;
    meanVec_flag = 0;
    ch_flag = 0;
    oneCount = 0;
    chCounter = 0;
    chCounter1 = 0;
    meanVecCount=0;
    
    local_meanVec = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_idx = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_cen = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_mag = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_bw = (gr_complex*) malloc (sizeof(gr_complex)*16384);
}

/*
 * Our virtual destructor.
 */
iuchar_to_complex_impl::~iuchar_to_complex_impl() {
	free (local_meanVec);
    free (local_ch_idx);
    free (local_ch_cen);
    free (local_ch_mag);
    free (local_ch_bw);
}

int iuchar_to_complex_impl::work(int noutput_items,
                                 gr_vector_const_void_star& input_items,
                                 gr_vector_void_star& output_items)
{
    const gr_complex* in = (const gr_complex*)input_items[0];
    gr_complex* out = (gr_complex*)output_items[0];
    s_cycle++; 
    
    for (int i = 0; i <= noutput_items-1; ++i){ 
    
           if(start_flag==0 && in[i]==gr_complex(1,0)) {
        	out[i] = in[i];
        	start_flag=1;
        	oneCount++;        	
        }
        
        else if(start_flag==1 && in[i]==gr_complex(1,0)) {
        	oneCount++;
        	out[i] = in[i];
        	if(oneCount==16383){
        		//std::cout<<"reached"<<std::endl;
                freq_flag=1;
        	}
        }
        
        else if(freq_flag==1 && start_flag==1){
            local_freq = in[i];
            out[i] = in[i];
        	meanVec_flag=1;
        	freq_flag=0;
        }
        
        else if(meanVec_flag==1 && start_flag==1){
        	local_meanVec[meanVecCount]=in[i];
        	out[i] = in[i];
        	meanVecCount++;
        	if(meanVecCount==16384){
        		ch_flag=1;
        		meanVec_flag=0;
        	}
        }
        else if (ch_flag == 1 && start_flag == 1 && chCounter == 0){
            total_chs = in[i].real();
            out[i] = in[i];
            chCounter++;
            if(total_chs==0){
                ch_flag = 0;
                start_flag = 0;
                oneCount = 0;
                meanVecCount= 0;
                chCounter = 0;
            
            }
        }

        else if (ch_flag == 1 && start_flag == 1 && chCounter > 0){
            if(chCounter1==0) { local_ch_idx[chCounter-1] = in[i]; chCounter1++;}
            else if(chCounter1==1) { local_ch_cen[chCounter-1] = in[i]; chCounter1++;}
            else if(chCounter1==2) { local_ch_mag[chCounter-1] = in[i]; chCounter1++;}
            else if(chCounter1==3) {
                local_ch_mag[chCounter-1] = in[i];
                chCounter1 = 0;
                chCounter++;
                if(chCounter == total_chs){
                    ch_flag = 0;
                    start_flag = 0;
                    oneCount = 0;
                    meanVecCount= 0;
                    chCounter = 0;
                }
            }
            out[i] = in[i];
        }
        else out[i] = in[i];
        }   
    //    std::cout<<out[0]<<" "<<out[1]<<" "<< out[noutput_items-1] << " " <<noutput_items<<"\n";
    // Tell runtime system how many output items we produced.
    return noutput_items;
}

} /* namespace iridium */
} /* namespace gr */
