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
    add_header(influx); 
    modify_field(influx.srcAddr, ip.srcAddr);
    modify_field(influx.dstAddr, ip.dstAddr);
    modify_field(influx.node_cnt, int_hdr.total_hops);

    add_header(inf_node);
    modify_field(inf_node.ingress_port_id, int_port_ids.ingress_port_id);
    modify_field(inf_node.egress_port_id, int_port_ids.egress_port_id);

    modify_field(inf_node.ingress_tstamp, int_ingress_tstamp.ingress_tstamp);
    modify_field(inf_node.egress_tstamp, int_egress_tstamp.egress_tstamp);

    add_header(inf_node2);
    modify_field(inf_node2.ingress_port_id, int_port_ids2.ingress_port_id);
    modify_field(inf_node2.egress_port_id, int_port_ids2.egress_port_id);

    modify_field(inf_node2.ingress_tstamp, int_ingress_tstamp2.ingress_tstamp);
    modify_field(inf_node2.egress_tstamp, int_egress_tstamp2.egress_tstamp);



    modify_field(inf_node.ndk_tstamp, intrinsic_metadata.ingress_timestamp);
    modify_field(inf_node.delay, intrinsic_metadata.ingress_timestamp);

    modify_field(inf_node2.ndk_tstamp, intrinsic_metadata.ingress_timestamp);
    modify_field(inf_node2.delay, intrinsic_metadata.ingress_timestamp);


    remove_header(ethernet);   
    remove_header(ip);   
    remove_header(udp);   
    remove_header(int_shim);   
    remove_header(int_hdr);   

remove_header(int_switch_id);   
    remove_header(int_port_ids);   
    remove_header(int_ingress_tstamp);   
    remove_header(int_egress_tstamp);   

remove_header(int_switch_id2);   
    remove_header(int_port_ids2);   
    remove_header(int_ingress_tstamp2);   
    remove_header(int_egress_tstamp2);

    remove_header(int_tail);   

   // truncate(64);
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
//    if(intrinsic_metadata.ingress_port  < DMA_EGRESS_PORT) {
        // Data come from the ethernet
//        apply(table_cha_map);
//    } else {
        // Data come from the DMA
 //       apply(table_eth_map);
 //   }
    
    apply(table_influx);
}
