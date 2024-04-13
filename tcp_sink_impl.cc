/* -*- c++ -*- */
/*
 * Copyright 2020 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "tcp_sink_impl.h"
#include <gnuradio/io_signature.h>

#include <chrono>
#include <sstream>
#include <thread>

namespace gr {
namespace network {

tcp_sink::sptr tcp_sink::make(
    size_t itemsize, size_t veclen, const std::string& host, int port, int sinkmode)
{
    return gnuradio::make_block_sptr<tcp_sink_impl>(
        itemsize, veclen, host, port, sinkmode);
}

/*
 * The private constructor
 */
tcp_sink_impl::tcp_sink_impl(
    size_t itemsize, size_t veclen, const std::string& host, int port, int sinkmode)
    : gr::sync_block("tcp_sink",
                     gr::io_signature::make(1, 1, itemsize * veclen),
                     gr::io_signature::make(0, 0, 0)),
      d_itemsize(itemsize),
      d_veclen(veclen),
      d_host(host),
      d_port(port),
      d_sinkmode(sinkmode),
      d_thread_running(false),
      d_stop_thread(false),
      d_listener_thread(NULL),
      d_start_new_listener(false),
      d_initial_connection(true)
{
    d_block_size = d_itemsize * d_veclen;
    freq_flag = 0;
    start_flag = 0;
    s_cycle=0;
    meanVec_flag = 0;
    ch_flag = 0;
    oneCount = 0;
    chCounter = 0;
    chCounter1 = 0;
    meanVecCount=0;
    one_last_flag = 0;
    single_time_freq_flag = 0;
    last_flag_for_meanvec = 0;
    last_flag_for_ch = 0;
    local_freq = 0;

    local_meanVec = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_idx = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_cen = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_mag = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    local_ch_bw = (gr_complex*) malloc (sizeof(gr_complex)*16384);
    
}

bool tcp_sink_impl::start()
{
    if (d_sinkmode == TCPSINKMODE_CLIENT) {
        // In this mode, we're connecting to a remote TCP service listener
        // as a client.
        d_logger->info("[TCP Sink] connecting to {:s} on port {:d}", d_host, d_port);

        asio::error_code err;
        d_tcpsocket = new asio::ip::tcp::socket(d_io_context);

        std::string s_port = std::to_string(d_port);
        asio::ip::tcp::resolver resolver(d_io_context);
        asio::ip::tcp::resolver::query query(
            d_host, s_port, asio::ip::resolver_query_base::passive);

        d_endpoint = *resolver.resolve(query, err);

        if (err) {
            throw std::runtime_error(
                std::string("[TCP Sink] Unable to resolve host/IP: ") + err.message());
        }

        if (d_host.find(":") != std::string::npos)
            d_is_ipv6 = true;
        else {
            // This block supports a check that a name rather than an IP is provided.
            // the endpoint is then checked after the resolver is done.
            if (d_endpoint.address().is_v6())
                d_is_ipv6 = true;
            else
                d_is_ipv6 = false;
        }

        d_tcpsocket->connect(d_endpoint, err);
        if (err) {
            throw std::runtime_error(std::string("[TCP Sink] Connection error: ") +
                                     err.message());
        }

        d_connected = true;

        asio::socket_base::keep_alive option(true);
        d_tcpsocket->set_option(option);
    } else {
        // In this mode, we're starting a local port listener and waiting
        // for inbound connections.
        d_start_new_listener = true;
        d_is_ipv6 = false;
        d_listener_thread = new std::thread([this] { run_listener(); });
    }

    return true;
}

void tcp_sink_impl::run_listener()
{
    d_thread_running = true;

    while (!d_stop_thread) {
        // this will block
        if (d_start_new_listener) {
            d_start_new_listener = false;
            connect(d_initial_connection);
            d_initial_connection = false;
        } else
            std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    d_thread_running = false;
}

void tcp_sink_impl::accept_handler(asio::ip::tcp::socket* new_connection,
                                   const asio::error_code& error)
{
    if (!error) {
        d_logger->info("Client connection received.");

        // Accept succeeded.
        d_tcpsocket = new_connection;

        asio::socket_base::keep_alive option(true);
        d_tcpsocket->set_option(option);
        d_connected = true;

    } else {
        d_logger->error("Error code {:s} accepting TCP session.", error.message());

        // Boost made a copy so we have to clean up
        delete new_connection;

        // safety settings.
        d_connected = false;
        d_tcpsocket = NULL;
    }
}

void tcp_sink_impl::connect(bool initial_connection)
{
    d_logger->info("Waiting for connection on port {:d}", d_port);

    if (initial_connection) {
        if (d_is_ipv6)
            d_acceptor = new asio::ip::tcp::acceptor(
                d_io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v6(), d_port));
        else
            d_acceptor = new asio::ip::tcp::acceptor(
                d_io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), d_port));
    } else {
        d_io_context.reset();
    }

    if (d_tcpsocket) {
        delete d_tcpsocket;
    }
    d_tcpsocket = NULL;
    d_connected = false;

    asio::ip::tcp::socket* tmpSocket = new asio::ip::tcp::socket(d_io_context);
    d_acceptor->async_accept(*tmpSocket,
                             [this, tmpSocket](const asio::error_code& error) {
                                 accept_handler(tmpSocket, error);
                             });

    d_io_context.run();
}

/*
 * Our virtual destructor.
 */
tcp_sink_impl::~tcp_sink_impl() { stop(); }

bool tcp_sink_impl::stop()
{
    if (d_thread_running) {
        d_stop_thread = true;
    }

    if (d_tcpsocket) {
        d_tcpsocket->close();
        delete d_tcpsocket;
        d_tcpsocket = NULL;
    }

    d_io_context.reset();
    d_io_context.stop();

    if (d_acceptor) {
        delete d_acceptor;
        d_acceptor = NULL;
    }

    if (d_listener_thread) {
        d_listener_thread->join();
        delete d_listener_thread;
        d_listener_thread = NULL;
    }

    return true;
}

int tcp_sink_impl::work(int noutput_items,
                        gr_vector_const_void_star& input_items,
                        gr_vector_void_star& output_items)
{
    gr::thread::scoped_lock guard(d_setlock);

    if (!d_connected)
        return noutput_items;

    d_block_size = 4;
    // unsigned int noi = noutput_items * d_block_size;
    unsigned int noi = noutput_items * d_block_size;
    int bytes_written;
    int bytes_remaining = noi;
    int hasnonzero = 0;
    
    
    ec.clear();
    s_cycle++;

    char* p_buff;
    p_buff = (char*)input_items[0];

    const float* buff2;
    buff2 = reinterpret_cast<const float*>(input_items[0]);
    const gr_complex* in = (const gr_complex*)input_items[0];
    // std::cout<< buff2[0]<<" "<< buff2[1]<< " "<<buff2[2]<< " : " <<sizeof(buff2[0])<<" "<< d_veclen<<" "<<"\n";

    float* real_parts_buffer = new float[noutput_items];

    for (int i = 0; i < noutput_items; i++) {
        real_parts_buffer[i] = in[i].real();
    }
    // std::cout<<d_block_size<< real_parts_buffer[0] <<" "<< real_parts_buffer[1]<< " " << noi <<" " <<"\n";
  
    
    // std::cout<<noutput_items<<"\n";
    
    // if (in[0].real() > 0) { std::cout<< s_cycle<<" "<< in[]}
    // std::cout<<s_cycle<<" "<<in[0]<<"\n";

    for (int i=0; i < noutput_items; i++){
        if ( start_flag == 0 && in[0] == gr_complex(1, 0)) {
            // std::cout<<" first zero  "<<in[i]<<"\n";
            start_flag = 1;
            oneCount++;
            // hasnonzero = 1;
        }

        // else if(start_flag == 1 && oneCount == 1){
        //     oneCount++;
        //     if (oneCount == 16382){
        //         std::cout<<"next is freq: "<<"\n";
        //         freq_flag=1;
        //         oneCount = 0;
        //         start_flag = 0;
        //     }
        // }
        else if(start_flag==1 && in[i]==gr_complex(1,0)) {
        	oneCount++;
        	// out[i] = in[i];
            
        	if(oneCount==16383){
                // std::cout<< in[i]<<"\n";
        		// std::cout<<"new_freq"<<std::endl;
                freq_flag=1;
                hasnonzero = 1;
        	}
        }
        
        else if(freq_flag==1 && start_flag==1){
            local_freq = in[i];
            // out[i] = in[i];
            // std::cout<< "freq "<<in[i]<<"\n";
            single_time_freq_flag = 1;
        	meanVec_flag=1;
        	freq_flag=0;
            hasnonzero = 1;
        }
        
        else if(meanVec_flag==1 && start_flag==1){
        	local_meanVec[meanVecCount]=in[i];
        	// out[i] = in[i];
            // std::cout<< "meanvec"<<in[i]<<"\n";
        	meanVecCount++;
        	if(meanVecCount==16384){
        		ch_flag=1;
        		meanVec_flag=0;
                last_flag_for_meanvec = 1;
                hasnonzero = 1;
        	}
        }
        else if (ch_flag == 1 && start_flag == 1 && chCounter == 0){
            total_chs = in[i].real();
            // out[i] = in[i];
            // std::cout<< "channel data"<<in[i]<<"\n";
            chCounter++;
            
            if(total_chs==0){
                ch_flag = 0;
                start_flag = 0;
                oneCount = 0;
                meanVecCount= 0;
                chCounter = 0;
                one_last_flag = 1;
            
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
                hasnonzero = 1;
                if(chCounter == total_chs){
                    ch_flag = 0;
                    last_flag_for_ch = 1;
                    start_flag = 0;
                    oneCount = 0;
                    meanVecCount= 0;
                    chCounter = 0;
                    one_last_flag = 1;
                }
            }
            // out[i] = in[i];
            // std::cout<<"actual channels: "<< in[i]<<"\n";
        }
        else{
            hasnonzero = 0;
        }
        // else out[i] = in[i];
    }


        // if( hasnonzero == 1)
        // {
        //      bytes_remaining = noi;
        // }


    // for ( int i = 0; i<noutput_items; i++){
    //     if(in[i] != gr_complex(0, 0)){
    //         hasnonzero = 1;
    //     }
    // }
    // if (hasnonzero == 1){
    //     bytes_remaining = noi;
    // } 
    // else{
    //     bytes_remaining = 0;
    // }

    // if (bytes_remaining > 0 && freq_flag==1) 
    // if (freq_flag==1 && start_flag==1) 
    // std::cout<<single_time_freq_flag<<": "<<meanVec_flag+last_flag_for_meanvec<< " : "<< last_flag_for_ch<< " : " << in[0] << in[1] << " " << noutput_items<< " " << in[noutput_items-1] << " \n";
    // if(single_time_freq_flag == 1) {
        
    // }
    if (last_flag_for_ch == 1) {
        total_chs = in[0].real();
        // std::cout<< total_chs<< "\n";
        bytes_remaining = total_chs * 3 *  d_block_size;

    }
    // std::cout<< bytes_remaining<<" \n";
    
    // while ( (bytes_remaining > 0) && (!ec) && (start_flag + one_last_flag == 1) ){
    while ( (bytes_remaining > 0) && (!ec) && ((single_time_freq_flag + (meanVec_flag+last_flag_for_meanvec) + last_flag_for_ch) > 0)) {
        bytes_written = asio::write(
            *d_tcpsocket, asio::buffer((const void*)real_parts_buffer, bytes_remaining), ec);
        
        bytes_remaining -= bytes_written;
        // std::cout<<s_cycle<<" "<<c1<<" "<<bytes_written<<" bytes written"<<" :: "<< "bytes remaining:"<<bytes_remaining<<" "<<noi<<"\n";
        p_buff += bytes_written;
        // c1++;

        if (ec == asio::error::connection_reset || ec == asio::error::broken_pipe) {

            // Connection was reset
            d_connected = false;
            bytes_remaining = 0;

            if (d_sinkmode == TCPSINKMODE_CLIENT) {
                d_logger->warn("Server closed the connection. Stopping processing.");

                return WORK_DONE;
            } else {
                d_logger->info("Client disconnected. Waiting for new connection.");

                // start waiting for another connection
                d_start_new_listener = true;
            }
        }
    }

    if (one_last_flag == 1) one_last_flag = 0;
    if (single_time_freq_flag == 1) single_time_freq_flag = 0;
    if(last_flag_for_meanvec == 1) last_flag_for_meanvec = 0;
    if(last_flag_for_ch == 1) last_flag_for_ch = 0;

    return noutput_items;
}
} /* namespace network */
} /* namespace gr */
