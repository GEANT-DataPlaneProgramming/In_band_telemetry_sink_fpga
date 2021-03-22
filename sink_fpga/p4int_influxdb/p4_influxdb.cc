/**
 * @author Mario Kuka <kuka@cesnet.cz>
 */ 

#include <string>
#include <iostream>
#include <thread> 
#include <sstream>

#include "p4_influxdb.h"
#include "UDP.h"
#include "HTTP.h"

#define POP_THRESHOLD 10
#define RECORD_SIZE 210

/**
 * Read records from ring buffer and send them to the database by HTTP protocol.
 * \param ring Selected ring buffer
 * \param opt Program options
 * \param id Sender ID
 */
static void http_sender(ringbuffer<telemetric_hdr_t, RING_BUFFER_SIZE> *ring, const options_t* opt, uint32_t id)
{
    // Prepare udp socket
    std::string url = std::string(opt->protocol) + "://" + std::string(opt->host) + ":" + std::to_string(opt->port) + "?db=int_telemetry_db";
    HTTP http_sock(url);
    http_sock.enableBasicAuth(std::string(opt->username) + ":" + std::string(opt->password));

    std::string data;
    data.reserve(RECORD_SIZE * opt->batch);
    char report[400];
    uint32_t it = 0;     

    while(true) {
        telemetric_hdr_t telemetric; 
        
        // If the buffer is empty, all processed records are flushed to the database.
        uint32_t flush = 0;
        while(!ring->pop(telemetric)) {
            delay_usecs(100);
            flush++; 
        
            if(flush == POP_THRESHOLD and !data.empty() and opt->hostValid) {
                try {
                    http_sock.send(data);
                } catch (std::runtime_error& e) {
                    std::stringstream msg;
                    std::cerr << msg.str();
                }
                it = 0;
                data.clear();
            }
        }
        
        // Prepare http datagram and send it
        if(opt->hostValid) {
            sprintf(report,
                "int_telemetry,srcip=%s,dstip=%s,srcp=%u,dstp=%u origts=%lu,dstts=%lu,seq=%lu,delay=%lu,sink_jitter=%lu,reordering=%ld %lu\n",
                telemetric.srcIp, telemetric.dstIp, telemetric.srcPort, telemetric.dstPort, telemetric.origTs, telemetric.dstTs,
                telemetric.seqNum, telemetric.delay, telemetric.sink_jitter, telemetric.reordering, telemetric.dstTs);
            data.append(report);
            it++;
            
            // Chekc Batch threshold
            if(it == opt->batch) {
                try {
                    http_sock.send(data);
                } catch (std::runtime_error& e) {
                    std::stringstream msg;
                    std::cerr << msg.str();
                }
                it = 0;
                data.clear();
            }       
        }
    }
}

/**
 * Read records from ring buffer and send them to the database by UDP piotocol.
 * \param ring Selected ring buffer
 * \param opt Program options
 * \param id Sender ID
 */
static void udp_sender(ringbuffer<telemetric_hdr_t, RING_BUFFER_SIZE> *ring, const options_t* opt, uint32_t id)
{
    // Prepare udp socket
    INT_UDP udp_sock(std::string(opt->host), opt->port);

    std::string data;
    data.reserve(65527);
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
            
            data.append(report);
            it++;
               
            if(it == opt->batch) { 
                try {
                    udp_sock.send(data); 
                } catch (std::runtime_error& e) {
                    std::stringstream msg;
                    msg << "ID:" << id << ", error: "  << e.what() << std::endl;
                    std::cerr << msg.str();
                }
                it = 0;
                data.clear();
            }       
        }
    }
}

IntExporter::IntExporter(const options_t *opt)
{
    m_th_num = opt->raw_buffer;
    m_rr_index = 0;

    for(uint32_t i = 0; i < m_th_num; i++) {
        // Prepare ring buffer and start sender
        m_ring_buffs.push_back(new ringbuffer<telemetric_hdr_t, RING_BUFFER_SIZE>());
        
        if(std::string(opt->protocol) == "udp") {
            std::thread(udp_sender, m_ring_buffs.back(), opt, i).detach();
        } else if(std::string(opt->protocol) == "http" || std::string(opt->protocol) == "https") {
            std::thread(http_sender, m_ring_buffs.back(), opt, i).detach();
        } else {
            throw std::runtime_error("Unknown protocol");
        }
        delay_usecs(1000); 
    }
}

bool IntExporter::sendData(const telemetric_hdr_t& telemetric) 
{
    // round robin selection
    for(uint32_t i = 0; i < m_th_num; i++) {
        m_rr_index = (m_rr_index + 1) % m_th_num;   
        if(m_ring_buffs[m_rr_index]->push(telemetric)) {
            return EXIT_SUCCESS;
        } else {
            continue;
        }
    }
    return EXIT_FAILURE;
}


