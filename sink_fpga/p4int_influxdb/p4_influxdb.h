/**
 * @author Mario Kuka <kuka@cesnet.cz>
 * 
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 */

#ifndef _P4_INT_EXPORTER_H_
#define _P4_INT_EXPORTER_H_

#include "p4int.h"
#include "ringbuffer.h"

#define RING_BUFFER_SIZE 100000

/**
 * Sending int reports to the influxdb by udp datagrams.
 */
class IntExporter 
{
    public:
        /**
         * Constructor
         * \param num_of_threads
         * \param opt Program options
         */
        IntExporter(uint32_t num_of_threads, const options_t *opt);
        
        /**
         * Send int report 
         * \param telemetric
         * \return EXIT_SUCCESS on success and EXIT_FAILURE on error
         */
        bool sendData(const telemetric_hdr_t& telemetric);
    
    protected:
        // Number of threads 
        uint32_t m_th_num; 
        // Ring bufferes 
        std::vector<ringbuffer<telemetric_hdr_t, RING_BUFFER_SIZE>*> m_ring_buffs;
        // Rouind robin index
        uint32_t m_rr_index;
};

#endif // _P4_INFLUXDB_H_
