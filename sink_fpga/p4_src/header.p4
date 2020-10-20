header_type ethernet_t {
    fields {
        dstAddr   : 48;
        srcAddr   : 48;
        etherType : 16;
    }
}

header_type ipv4_t {
    fields {
        version        : 4;
        ihl            : 4;
        dscp           : 6;
        enc            : 2;
        totalLen       : 16;
        identification : 16;
        flags          : 3;
        fragOffset     : 13;
        ttl            : 8;
        protocol       : 8;
        checksum       : 16;
        srcAddr        : 32;
        dstAddr        : 32;
    }
}

header_type udp_t {
    fields {
        sprt        : 16;
        dprt        : 16;
        totall      : 16;
        checksum    : 16;
    }
}

header_type intl4_shim_t {
    fields {
        inttype  : 8;
        rsvd1    : 8;
        len      : 8;
        dscp     : 6;
        rsvd2    : 2;
    }
}

header_type int_hdr_t {
    fields {
        ver         : 2;
        rep         : 2;
        c           : 1;
        e           : 1;
        rsvd1       : 5;
        ins_cnt     : 5;
        max_hops    : 8;
        total_hops  : 8;
        inst_mask   : 16;
        rsvd2       : 16;
    }
}

header_type int_switch_id_t {
    fields {
        switch_id : 32;
    }
}

header_type int_port_ids_t {
    fields {
        ingress_port_id : 16;
        egress_port_id  : 16;
    }
}

header_type int_ingress_tstamp_t {
    fields {
        ingress_tstamp : 64;
    }
}

header_type int_egress_tstamp_t {
    fields {
        egress_tstamp : 64;
    }
}

header_type intl4_tail_t {
    fields {
        next_proto : 8;
        dest_port  : 16;
        padding    : 2;
        dscp       : 6;
    }
}

header_type infux_t {
    fields {
        srcAddr         : 32;
        dstAddr         : 32;
        ingress_port_id : 16;
        egress_port_id  : 16;
        ingress_tstamp  : 64;
        egress_tstamp   : 64;
        ndk_tstamp      : 64;
        delay           : 64;
        padding         : 160;        
    }
}


