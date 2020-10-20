#define ETHERTYPE_IPV4 0x800
#define ETHERTYPE_ARP  0x806
#define ETHERTYPE_VLAN 0x8100
#define ETHERTYPE_NONE 0xFFFF

parser start {
    return parse_ethernet;
}

header ethernet_t ethernet;
parser parse_ethernet {
    extract(ethernet);
    return select(latest.etherType) {
        ETHERTYPE_IPV4 : parse_ip;
        ETHERTYPE_ARP  : ingress;
        ETHERTYPE_VLAN : ingress;
        ETHERTYPE_NONE : parse_influx;
        default        : ingress;
    }
}

header infux_t influx;
parser parse_influx {
    extract(influx);
    return ingress;
}

#define IP_PROTOCOLS_UDP 0x11

header ipv4_t ip;
parser parse_ip {
    extract(ip);
    return select(ip.protocol) {
        IP_PROTOCOLS_UDP : parse_udp;
        default          : ingress;
    }
}

header udp_t udp;
parser parse_udp {
    extract(udp);
    return parse_int_shim;
}

header intl4_shim_t int_shim;
parser parse_int_shim {
    extract(int_shim);
    return parse_int_hdr;
}

header int_hdr_t int_hdr;
parser parse_int_hdr {
    extract(int_hdr);
    return parse_int_switch_id;
}

header int_switch_id_t int_switch_id;
parser parse_int_switch_id {
    extract(int_switch_id);
    return parse_int_port_ids;
}

header int_port_ids_t int_port_ids;
parser parse_int_port_ids {
    extract(int_port_ids);
    return parse_int_ingress_tstamp;
}

header int_ingress_tstamp_t int_ingress_tstamp;
parser parse_int_ingress_tstamp {
    extract(int_ingress_tstamp);
    return parse_int_egress_tstamp;
}

header int_egress_tstamp_t int_egress_tstamp;
parser parse_int_egress_tstamp {
    extract(int_egress_tstamp);
    return parse_int_tail;
}

header intl4_tail_t int_tail;
parser parse_int_tail {
    extract(int_tail);
    return ingress;
}


