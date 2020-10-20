#include "header.p4"
#include "parser.p4"

#define ETH_EGRESS_PORT -128
#define DMA_EGRESS_PORT 128

action map_to_chan() {
    add_to_field(intrinsic_metadata.egress_port, DMA_EGRESS_PORT);
}

action map_to_eth() {
    add_to_field(intrinsic_metadata.egress_port, ETH_EGRESS_PORT);
}

action fill_influx() {
    modify_field(influx.srcAddr, ip.srcAddr);
    modify_field(influx.dstAddr, ip.dstAddr);
    modify_field(influx.ingress_port_id, udp.sprt);
    modify_field(influx.egress_port_id, udp.dprt);
    modify_field(influx.ingress_tstamp, int_ingress_tstamp.ingress_tstamp);
    modify_field(influx.egress_tstamp, int_egress_tstamp.egress_tstamp); 
    modify_field(influx.ndk_tstamp, intrinsic_metadata.ingress_timestamp);
    modify_field(influx.delay, intrinsic_metadata.ingress_timestamp);

    remove_header(ethernet);   
    remove_header(ip);   
    remove_header(udp);   
    remove_header(int_shim);   
    remove_header(int_hdr);   
    remove_header(int_switch_id);   
    remove_header(int_port_ids);   
    remove_header(int_ingress_tstamp);   
    remove_header(int_egress_tstamp);   
    remove_header(int_tail);   

    add_header(influx); 
    truncate(64);
}

action pkt_drop() {
    drop();
}

table table_cha_map {
    actions {
        map_to_chan;
    }
}

table table_eth_map {
    actions {
        map_to_eth;
    }
}

table table_influx {
    reads {
        ip.srcAddr : exact;			
        udp.dprt   : exact;			
    }
    actions {
        fill_influx;
        pkt_drop;
    }
    size : 64;
}

control ingress {
    if(intrinsic_metadata.ingress_port  < DMA_EGRESS_PORT) {
        // Data come from the ethernet
        apply(table_cha_map);
    } else {
        // Data come from the DMA
        apply(table_eth_map);
    }
    
    apply(table_influx);
}
