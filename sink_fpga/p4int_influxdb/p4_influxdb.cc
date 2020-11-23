/**
 * @author Mario Kuka <kuka@cesnet.cz>
 * 
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 */

#include <string>
#include <iostream>
#include <thread> 

#include "p4_influxdb.h"
#include "UDP.h"

/**
 * Convert raw data to the int reports and send them to the influxdb
 * \param data Raw int data
 * \param opt Program options
 */
static void sender(ringbuffer<telemetric_hdr_t, 100000> *ring, const options_t* opt)
{
    // Prepare udp socket    
    INT_UDP udp_sock(std::string(opt->host), opt->port);

    std::string udp_data;
    udp_data.reserve(65527);
    char report[400];
    uint32_t it = 0;     

    while(true) {
        // read data
        telemetric_hdr_t telemetric; 
        while(!ring->pop(telemetric)) {
            delay_usecs(100); 
        }
        
        // prepare udp datagram and send it
        if(opt->hostValid) {
            sprintf(report,
                "int_telemetry,srcip=%s,dstip=%s,srcp=%u,dstp=%u origts=%lu,dstts=%lu,seq=%lu,delay=%lu,sink_jitter=%lu,reordering=%lu %lu\n",
                telemetric.srcIp, telemetric.dstIp, telemetric.srcPort, telemetric.dstPort, telemetric.origTs, telemetric.dstTs,
                telemetric.seqNum, telemetric.delay, telemetric.sink_jitter, telemetric.reordering, telemetric.dstTs);
            
            udp_data.append(report);
            it++;
               
            if(it == opt->batch) { 
                udp_sock.send(udp_data); 
                it = 0;
                udp_data.clear();
            }       
        }
    }
}

IntExporter::IntExporter(uint32_t num_of_threads, const options_t *opt)
{
    m_th_num = num_of_threads;
    m_rr_index = 0;

    for(uint32_t i = 0; i < m_th_num; i++) {
        // Prepare ring buffer and start sender
        m_ring_buffs.push_back(new ringbuffer<telemetric_hdr_t, RING_BUFFER_SIZE>());
        std::thread(sender, m_ring_buffs.back(), opt).detach();
    }
}

bool IntExporter::sendData(const telemetric_hdr_t& telemetric) 
{
    // round robin selection
    m_rr_index = (m_rr_index + 1) % m_th_num;   
    if(m_ring_buffs[m_rr_index]->push(telemetric)) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}


