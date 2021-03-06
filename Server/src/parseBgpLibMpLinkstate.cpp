/*
* Copyright (c) 2013-2016 Cisco Systems, Inc. and others.  All rights reserved.
*
* This program and the accompanying materials are made available under the
* terms of the Eclipse Public License v1.0 which accompanies this distribution,
* and is available at http://www.eclipse.org/legal/epl-v10.html
*
*/

#include <arpa/inet.h>

#include "parseBgpLibMpLinkstate.h"
#include "parseBgpLibMpLinkstateAttr.h"
#include "netdb.h"

namespace parse_bgp_lib {
    /**
 * Constructor for class
 *
 * \details Handles bgp Extended Communities
 *
 * \param [in]     logPtr       Pointer to existing Logger for app logging
* \param [out]  parsed_update  Reference to parsed_update; will be updated with all parsed data
 * \param [in]     enable_debug Debug true to enable, false to disable
 */
    MPLinkState::MPLinkState(parseBgpLib *parse_lib, Logger *logPtr, parse_bgp_lib::parseBgpLib::parsed_update *update, bool enable_debug) {
        logger = logPtr;
        debug = enable_debug;
        this->update = update;
        caller = parse_lib;
    }

    MPLinkState::~MPLinkState() {
    }

    /**
     * MP Reach Link State NLRI parse
     *
     * \details Will handle parsing the link state NLRI
     *
     * \param [in]   nlri           Reference to parsed NLRI struct
     */
    void MPLinkState::parseReachLinkState(MPReachAttr::mp_reach_nlri &nlri) {

        nlri_list = &update->nlri_list;
        // Process the next hop
        // Next-hop is an IPv6 address - Change/set the next-hop attribute in parsed data to use this next-hop
        u_char ip_raw[16];
        char ip_char[40];

        if (nlri.nh_len == 4) {
            memcpy(ip_raw, nlri.next_hop, nlri.nh_len);
            inet_ntop(AF_INET, ip_raw, ip_char, sizeof(ip_char));
        } else if (nlri.nh_len > 4) {
            memcpy(ip_raw, nlri.next_hop, nlri.nh_len);
            inet_ntop(AF_INET6, ip_raw, ip_char, sizeof(ip_char));
        }

        update->attrs[LIB_ATTR_NEXT_HOP].official_type = ATTR_TYPE_NEXT_HOP;
        update->attrs[LIB_ATTR_NEXT_HOP].name = parse_bgp_lib::parse_bgp_lib_attr_names[LIB_ATTR_NEXT_HOP];
        update->attrs[LIB_ATTR_NEXT_HOP].value.push_back(std::string(ip_char));

        /*
         * Decode based on SAFI
         */
        switch (nlri.safi) {
            case parse_bgp_lib::BGP_SAFI_BGPLS: // Unicast BGP-LS
                SELF_DEBUG("%sREACH: bgp-ls: len=%d", caller->debug_prepend_string.c_str(), nlri.nlri_len);
                parseLinkStateNlriData(nlri.nlri_data, nlri.nlri_len);
                break;

            default :
                LOG_INFO("%sMP_UNREACH AFI=bgp-ls SAFI=%d is not implemented yet, skipping for now",
                         caller->debug_prepend_string.c_str(), nlri.afi, nlri.safi);
                return;
        }
    }


    /**
     * MP UnReach Link State NLRI parse
     *
     * \details Will handle parsing the unreach link state NLRI
     *
     * \param [in]   nlri           Reference to parsed NLRI struct
     */
    void MPLinkState::parseUnReachLinkState(MPUnReachAttr::mp_unreach_nlri &nlri) {
        nlri_list = &update->withdrawn_nlri_list;

        /*
         * Decode based on SAFI
         */
        switch (nlri.safi) {
            case parse_bgp_lib::BGP_SAFI_BGPLS: // Unicast BGP-LS
                SELF_DEBUG("%sUNREACH: bgp-ls: len=%d", caller->debug_prepend_string.c_str(), nlri.nlri_len);
                parseLinkStateNlriData(nlri.nlri_data, nlri.nlri_len);
                break;

            default :
                LOG_INFO("%sMP_UNREACH AFI=bgp-ls SAFI=%d is not implemented yet, skipping for now",
                         caller->debug_prepend_string.c_str(), nlri.afi, nlri.safi);
                return;
        }
    }

    /**********************************************************************************//*
     * Parses Link State NLRI data
     *
     * \details Will parse the link state NLRI's from MP_REACH or MP_UNREACH.
     *
     * \param [in]   data           Pointer to the NLRI data
     * \param [in]   len            Length of the NLRI data
     */
    void MPLinkState::parseLinkStateNlriData(u_char *data, uint16_t len) {
        uint16_t        nlri_type;
        uint16_t        nlri_len;
        uint16_t        nlri_len_read = 0;

        // Process the NLRI data
        while (nlri_len_read < len) {

            SELF_DEBUG("%sNLRI read=%d total = %d", caller->debug_prepend_string.c_str(), nlri_len_read, len);

            /*
             * Parse the NLRI TLV
             */
            memcpy(&nlri_type, data, 2);
            data += 2;
            parse_bgp_lib::SWAP_BYTES(&nlri_type);

            memcpy(&nlri_len, data, 2);
            data += 2;
            parse_bgp_lib::SWAP_BYTES(&nlri_len);

            nlri_len_read += 4;

            if (nlri_len > len) {
                LOG_NOTICE("%sbgp-ls: failed to parse link state NLRI; length is larger than available data",
                           caller->debug_prepend_string.c_str());
                return;
            }

            /*
             * Parse out the protocol and ID (present in each NLRI ypte
             */
            uint8_t          proto_id;
            uint64_t         id = 0;

            proto_id = *data;
            memcpy(&id, data + 1, sizeof(id));
            parse_bgp_lib::SWAP_BYTES(&id);

            // Update read NLRI attribute, current TLV length and data pointer
            nlri_len_read += 9; nlri_len -= 9; data += 9;

            /*
             * Decode based on bgp-ls NLRI type
             */
            switch (nlri_type) {
                case NLRI_TYPE_NODE:
                    SELF_DEBUG("%sbgp-ls: parsing NODE NLRI len=%d", caller->debug_prepend_string.c_str(), nlri_len);
                    parseNlriNode(data, nlri_len, id, proto_id);
                    break;

                case NLRI_TYPE_LINK:
                    SELF_DEBUG("%sbgp-ls: parsing LINK NLRI", caller->debug_prepend_string.c_str());
                    parseNlriLink(data, nlri_len, id, proto_id);
                    break;

                case NLRI_TYPE_IPV4_PREFIX:
                    SELF_DEBUG("%sbgp-ls: parsing IPv4 PREFIX NLRI", caller->debug_prepend_string.c_str());
                    parseNlriPrefix(data, nlri_len, id, proto_id, true);
                    break;

                case NLRI_TYPE_IPV6_PREFIX:
                    SELF_DEBUG("%sbgp-ls: parsing IPv6 PREFIX NLRI", caller->debug_prepend_string.c_str());
                    parseNlriPrefix(data, nlri_len, id, proto_id, false);
                    break;

                default :
                    LOG_INFO("%sbgp-ls NLRI Type %d is not implemented yet, skipping for now",
                             caller->debug_prepend_string.c_str(), nlri_type);
                    return;
            }

            // Move to next link state type
            data += nlri_len;
            nlri_len_read += nlri_len;
        }
    }

    /**********************************************************************************//*
     * Decode Protocol ID
     *
     * \details will decode and return string representation of protocol (matches DB enum)
     *
     * \param [in]   proto_id       NLRI protocol type id
     *
     * \return string representation for the protocol that matches the DB enum string value
     *          empty will be returned if invalid/unknown.
     */
    std::string MPLinkState::decodeNlriProtocolId(uint8_t proto_id) {
        std::string value = "";

        switch (proto_id) {
            case NLRI_PROTO_DIRECT:
                value = "Direct";
                break;

            case NLRI_PROTO_STATIC:
                value = "Static";
                break;

            case NLRI_PROTO_ISIS_L1:
                value = "IS-IS_L1";
                break;

            case NLRI_PROTO_ISIS_L2:
                value = "IS-IS_L2";
                break;

            case NLRI_PROTO_OSPFV2:
                value = "OSPFv2";
                break;

            case NLRI_PROTO_OSPFV3:
                value = "OSPFv3";
                break;

            case NLRI_PROTO_EPE:
                value = "EPE";
                break;

            default:
                break;
        }

        return value;
    }

    /**********************************************************************************//*
     * Parse a Local or Remote Descriptor sub-tlv's
     *
     * \details will parse a local/remote descriptor
     *
     * \param [in]   data           Pointer to the start of the node NLRI data
     * \param [in]   data_len       Length of the data
     * \param [out]  parsed_nlri    Parsed NLRI filled with node descriptor fields
     *
     * \returns number of bytes read
     */
    int MPLinkState::parseDescrLocalRemoteNode(u_char *data, int data_len, parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri &parsed_nlri,
                                               bool local, MD5 &hash) {
        uint16_t        type;
        uint16_t        len;
        int             data_read = 0;

        uint32_t    asn;                           ///< BGP ASN
        uint32_t    bgp_ls_id;                     ///< BGP-LS Identifier
        uint8_t     igp_router_id[8];              ///< IGP router ID
        char        igp_router_id_buf[46];
        char        dr[16];
        uint8_t     ospf_area_Id[4];               ///< OSPF area ID
        uint32_t    bgp_router_id;                 ///< BGP router ID (draft-ietf-idr-bgpls-segment-routing-epe)

        std::stringstream   val_ss;

        if (data_len < 4) {
            LOG_NOTICE("%sbgp-ls: failed to parse node descriptor; too short", caller->debug_prepend_string.c_str());
            return data_len;
        }

        memcpy(&type, data, 2);
        parse_bgp_lib::SWAP_BYTES(&type);

        memcpy(&len, data+2, 2);
        parse_bgp_lib::SWAP_BYTES(&len);

        //SELF_DEBUG("%s: bgp-ls: Parsing node descriptor type %d len %d", peer_addr.c_str(), type, len);

        if (len > data_len - 4) {
            LOG_NOTICE("%sbgp-ls: failed to parse node descriptor; type length is larger than available data %d>=%d",
                       caller->debug_prepend_string.c_str(), len, data_len);
            return data_len;
        }

        data += 4; data_read += 4;
        switch (type) {
            case NODE_DESCR_AS:
            {
                if (len != 4) {
                    LOG_NOTICE("%sbgp-ls: failed to parse node descriptor AS sub-tlv; too short",
                    caller->debug_prepend_string.c_str());
                    data_read += len;
                    break;
                }

                memcpy(&asn, data, 4);
                parse_bgp_lib::SWAP_BYTES(&asn);
                val_ss.str(std::string());  // Clear
                val_ss << asn;

                if (local) {
                    parsed_nlri.nlri[LIB_NLRI_LS_ASN_LOCAL].official_type = NODE_DESCR_AS;
                    parsed_nlri.nlri[LIB_NLRI_LS_ASN_LOCAL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_ASN_LOCAL];
                    parsed_nlri.nlri[LIB_NLRI_LS_ASN_LOCAL].value.push_back(val_ss.str());
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_ASN_LOCAL].value, &hash);
                } else {
                    parsed_nlri.nlri[LIB_NLRI_LS_ASN_REMOTE].official_type = NODE_DESCR_AS;
                    parsed_nlri.nlri[LIB_NLRI_LS_ASN_REMOTE].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_ASN_REMOTE];
                    parsed_nlri.nlri[LIB_NLRI_LS_ASN_REMOTE].value.push_back(val_ss.str());
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_ASN_REMOTE].value, &hash);
                }

                data_read += 4;

                SELF_DEBUG("%sbgp-ls: Node descriptor AS = %u", caller->debug_prepend_string.c_str(), asn);

                break;
            }

            case NODE_DESCR_BGP_LS_ID:
            {
                if (len != 4) {
                    LOG_NOTICE("%sbgp-ls: failed to parse node descriptor BGP-LS ID sub-tlv; too short",
                    caller->debug_prepend_string.c_str());
                    data_read += len;
                    break;
                }

                memcpy(&bgp_ls_id, data, 4);
                parse_bgp_lib::SWAP_BYTES(&bgp_ls_id);

                char numstring[100] = {0};
                snprintf(&numstring[0], sizeof(numstring), "%" PRIx32, bgp_ls_id);

                if (local) {
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_LOCAL].official_type = NODE_DESCR_BGP_LS_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_LOCAL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_BGP_LS_ID_LOCAL];
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_LOCAL].value.push_back(string(numstring));
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_LOCAL].value, &hash);
                } else {
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_REMOTE].official_type = NODE_DESCR_BGP_LS_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_REMOTE].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_BGP_LS_ID_REMOTE];
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_REMOTE].value.push_back(string(numstring));
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_BGP_LS_ID_REMOTE].value, &hash);
                }

                data_read += 4;

                SELF_DEBUG("%sbgp-ls: Node descriptor BGP-LS ID = %08X", caller->debug_prepend_string.c_str(), bgp_ls_id);
                break;
            }

            case NODE_DESCR_OSPF_AREA_ID:
            {
                if (len != 4) {
                    LOG_NOTICE("%sbgp-ls: failed to parse node descriptor OSPF Area ID sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len <= data_len ? len : data_len;
                    break;
                }

                char    ipv4_char[16];
                memcpy(ospf_area_Id, data, 4);
                inet_ntop(AF_INET, ospf_area_Id, ipv4_char, sizeof(ipv4_char));
                if (local) {
                    parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_LOCAL].official_type = NODE_DESCR_OSPF_AREA_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_LOCAL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_OSPF_AREA_ID_LOCAL];
                    parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_LOCAL].value.push_back(ipv4_char);
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_LOCAL].value, &hash);
                } else {
                    parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_REMOTE].official_type = NODE_DESCR_OSPF_AREA_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_REMOTE].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_OSPF_AREA_ID_REMOTE];
                    parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_REMOTE].value.push_back(ipv4_char);
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_OSPF_AREA_ID_REMOTE].value, &hash);
                }

                data_read += 4;

                SELF_DEBUG("%sbgp-ls: Node descriptor OSPF Area ID = %s", caller->debug_prepend_string.c_str(), ipv4_char);
                break;
            }

            case NODE_DESCR_IGP_ROUTER_ID:
            {
                if (len > data_len or len > 8) {
                    LOG_NOTICE("%sbgp-ls: failed to parse node descriptor IGP Router ID sub-tlv; len (%d) is invalid",
                               caller->debug_prepend_string.c_str(), len);
                    data_read += len;
                    break;
                }

                bzero(igp_router_id, sizeof(igp_router_id));
                memcpy(igp_router_id, data, len);

                if (!strcmp(parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.front().c_str(), "OSPFv3") or
                        !strcmp(parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.front().c_str(), "OSPFv2")) {
                    bzero(igp_router_id, sizeof(igp_router_id));

                    // The first 4 octets are the router ID and the second 4 are the DR or ZERO if no DR
                    inet_ntop(PF_INET, igp_router_id, igp_router_id_buf, sizeof(igp_router_id_buf));

                    string hostname;
                    resolveIp(igp_router_id_buf, hostname);
                    update->attrs[LIB_ATTR_LS_NODE_NAME].official_type = parse_bgp_lib::ATTR_NODE_NAME;
                    update->attrs[LIB_ATTR_LS_NODE_NAME].name = parse_bgp_lib::parse_bgp_lib_attr_names[LIB_ATTR_LS_NODE_NAME];
                    update->attrs[LIB_ATTR_LS_NODE_NAME].value.push_back(std::string((char *)data, (char *)(data + len)));

                    if ((uint32_t) *(igp_router_id+4) != 0) {
                        inet_ntop(PF_INET, igp_router_id+4, dr, sizeof(dr));
                        strncat(igp_router_id_buf, "[", 1);
                        strncat(igp_router_id_buf, dr, sizeof(dr));
                        strncat(igp_router_id_buf, "]", 1);
                        LOG_INFO("%sigp router id includes DR: %s %s", caller->debug_prepend_string.c_str(), igp_router_id_buf, dr);
                    }
                } else {
                    snprintf(igp_router_id_buf, sizeof(igp_router_id_buf),
                             "%02hhX%02hhX.%02hhX%02hhX.%02hhX%02hhX.%02hhX%02hhX",
                             igp_router_id[0], igp_router_id[1], igp_router_id[2], igp_router_id[3],
                             igp_router_id[4], igp_router_id[5], igp_router_id[6], igp_router_id[7]);
                }


                if (local) {
                    parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_LOCAL].official_type = NODE_DESCR_IGP_ROUTER_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_LOCAL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_IGP_ROUTER_ID_LOCAL];
                    parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_LOCAL].value.push_back(igp_router_id_buf);
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_LOCAL].value, &hash);
                } else {
                    parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_REMOTE].official_type = NODE_DESCR_IGP_ROUTER_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_REMOTE].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_IGP_ROUTER_ID_REMOTE];
                    parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_REMOTE].value.push_back(igp_router_id_buf);
                    update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_IGP_ROUTER_ID_REMOTE].value, &hash);
                }

                data_read += len;

                SELF_DEBUG("%sbgp-ls: Node descriptor IGP Router ID %d = %d.%d.%d.%d (%02x%02x.%02x%02x.%02x%02x.%02x %02x)",
                           caller->debug_prepend_string.c_str(), data_read,
                           igp_router_id[0], igp_router_id[1], igp_router_id[2], igp_router_id[3],
                           igp_router_id[0], igp_router_id[1], igp_router_id[2], igp_router_id[3],
                           igp_router_id[4], igp_router_id[5], igp_router_id[6], igp_router_id[7]);
                break;
            }

            case NODE_DESCR_BGP_ROUTER_ID:
            {
                if (len != 4) {
                    LOG_NOTICE("%sbgp-ls: failed to parse node descriptor BGP Router ID sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len <= data_len ? len : data_len;
                    break;
                }


                char    ipv4_char[16];
                memcpy(&bgp_router_id, data, 4);
                inet_ntop(AF_INET, &bgp_router_id, ipv4_char, sizeof(ipv4_char));
                if (local) {
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_ROUTER_ID_LOCAL].official_type = NODE_DESCR_BGP_ROUTER_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_ROUTER_ID_LOCAL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_BGP_ROUTER_ID_LOCAL];
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_ROUTER_ID_LOCAL].value.push_back(ipv4_char);
                } else {
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_ROUTER_ID_REMOTE].official_type = NODE_DESCR_BGP_ROUTER_ID;
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_ROUTER_ID_REMOTE].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_BGP_ROUTER_ID_REMOTE];
                    parsed_nlri.nlri[LIB_NLRI_LS_BGP_ROUTER_ID_REMOTE].value.push_back(ipv4_char);
                }

                data_read += 4;

                SELF_DEBUG("%sbgp-ls: Node descriptor BGP Router-ID = %s", caller->debug_prepend_string.c_str(), ipv4_char);
                break;
            }

            default:
                LOG_NOTICE("%sbgp-ls: node descriptor sub-tlv %d not yet implemented, skipping.", caller->debug_prepend_string.c_str(), type);
                data_read += len;
                break;
        }

        return data_read;
    }


    /**********************************************************************************//*
     * Parse NODE NLRI
     *
     * \details will parse the node NLRI type. Data starts at local node descriptor.
     *
     * \param [in]   data           Pointer to the start of the node NLRI data
     * \param [in]   data_len       Length of the data
     * \param [in]   id             NLRI/type identifier
     * \param [in]   proto_id       NLRI protocol type id
     */
    void MPLinkState::parseNlriNode(u_char *data, int data_len, uint64_t id, uint8_t proto_id) {
        parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri parsed_nlri;
        parsed_nlri.afi = parse_bgp_lib::BGP_AFI_BGPLS;
        parsed_nlri.safi = parse_bgp_lib::BGP_SAFI_BGPLS;
        parsed_nlri.type = parse_bgp_lib::LIB_NLRI_TYPE_LS_NODE;

        std::stringstream   val_ss;

        if (data_len < 4) {
            LOG_WARN("%sbgp-ls: Unable to parse node NLRI since it's too short (invalid)", caller->debug_prepend_string.c_str());
            return;
        }

        char numstring[100] = {0};
        snprintf(&numstring[0], sizeof(numstring), "%" PRIx64, id);

        parsed_nlri.nlri[LIB_NLRI_LS_ROUTING_ID].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_ROUTING_ID];
        parsed_nlri.nlri[LIB_NLRI_LS_ROUTING_ID].value.push_back(string(numstring));

        parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_PROTOCOL];
        parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.push_back(decodeNlriProtocolId(proto_id));

        SELF_DEBUG("%sbgp-ls: ID = %x Protocol = %s", caller->debug_prepend_string.c_str(),
                   id, parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.front().c_str());

        /*
         * Parse the local node descriptor sub-tlv
         */
        uint16_t type;
        uint16_t len;

        memcpy(&type, data, 2);
        parse_bgp_lib::SWAP_BYTES(&type);
        memcpy(&len, data + 2, 2);
        parse_bgp_lib::SWAP_BYTES(&len);
        data_len -= 4;
        data += 4;

        if (len > data_len) {
            LOG_WARN("%sbgp-ls: failed to parse node descriptor; type length is larger than available data %d>=%d",
                     caller->debug_prepend_string.c_str(), len, data_len);
            return;
        }

        if (type != NODE_DESCR_LOCAL_DESCR) {
            LOG_WARN("%sbgp-ls: failed to parse node descriptor; Type (%d) is not local descriptor",
                     caller->debug_prepend_string.c_str(), type);
            return;
        }
        MD5 hash;

        // Parse the local descriptor sub-tlv's
        int data_read;
        while (len > 0) {
            data_read = parseDescrLocalRemoteNode(data, len, parsed_nlri, true, hash);
            len -= data_read;

            // Update the nlri data pointer and remaining length after processing the local descriptor sub-tlv
            data += data_read;
            data_len -= data_read;
        }

        //Update hash with peer hash if it exists
        if (caller->p_info)
            hash.update((unsigned char *) caller->p_info->peer_hash_str.c_str(), caller->p_info->peer_hash_str.length());

        hash.finalize();

        // Save the hash
        unsigned char *hash_raw = hash.raw_digest();
        parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_LOCAL_NODE_HASH];
        parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].value.push_back(parse_bgp_lib::hash_toStr(hash_raw));
        delete[] hash_raw;
/*

        // Update node table entry and add to parsed data list
        node_tbl.isIPv4 = true;
        memcpy(node_tbl.hash_id, info.hash_bin, sizeof(node_tbl.hash_id));
        node_tbl.asn = info.asn;
        memcpy(node_tbl.ospf_area_Id, info.ospf_area_Id, sizeof(node_tbl.ospf_area_Id));
        node_tbl.bgp_ls_id = info.bgp_ls_id;
        memcpy(node_tbl.igp_router_id, info.igp_router_id, sizeof(node_tbl.igp_router_id));
         */

        // Save the parsed data
        nlri_list->push_back(parsed_nlri);
    }

    /**********************************************************************************//*
     * Parse Link Descriptor sub-tlvs
     *
     * \details will parse a link descriptor (series of sub-tlv's)
     *
     * \param [in]   data           Pointer to the start of the node NLRI data
     * \param [in]   data_len       Length of the data
     * \param [out]  parsed_nlri    Parsed NLRI filled with node descriptor fields
     *
     * \returns number of bytes read
     */
    int MPLinkState::parseDescrLink(u_char *data, int data_len, parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri &parsed_nlri, MD5 &hash) {
        uint16_t        type;
        uint16_t        len;
        int             data_read = 0;

        uint32_t    local_id;                           ///< Link Local ID
        uint32_t    remote_id;                          ///< Link Remote ID
        uint8_t     intf_addr[16];                      ///< Interface binary address
        uint8_t     nei_addr[16];                       ///< Neighbor binary address
        uint32_t    mt_id;                              ///< Multi-Topology ID

        std::stringstream   val_ss;

        if (data_len < 4) {
            LOG_NOTICE("%sbgp-ls: failed to parse link descriptor; too short", caller->debug_prepend_string.c_str());
            return data_len;
        }

        memcpy(&type, data, 2);
        parse_bgp_lib::SWAP_BYTES(&type);

        memcpy(&len, data+2, 2);
        parse_bgp_lib::SWAP_BYTES(&len);

        if (len > data_len - 4) {
            LOG_NOTICE("%sbgp-ls: failed to parse link descriptor; type length is larger than available data %d>=%d",
                       caller->debug_prepend_string.c_str(), len, data_len);
            return data_len;
        }

        data += 4; data_read += 4;

        switch (type) {
            case LINK_DESCR_ID:
            {
                if (len != 8) {
                    LOG_NOTICE("%sbgp-ls: failed to parse link ID descriptor sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len;
                    break;
                }

                memcpy(&local_id, data, 4); parse_bgp_lib::SWAP_BYTES(&local_id);
                memcpy(&remote_id, data+4, 4); parse_bgp_lib::SWAP_BYTES(&remote_id);

                val_ss.str(std::string());  // Clear
                val_ss << local_id;

                parsed_nlri.nlri[LIB_NLRI_LS_LINK_LOCAL_ID].official_type = LINK_DESCR_ID;
                parsed_nlri.nlri[LIB_NLRI_LS_LINK_LOCAL_ID].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_LINK_LOCAL_ID];
                parsed_nlri.nlri[LIB_NLRI_LS_LINK_LOCAL_ID].value.push_back(val_ss.str());

                val_ss.str(std::string());  // Clear
                val_ss << remote_id;

                parsed_nlri.nlri[LIB_NLRI_LS_LINK_REMOTE_ID].official_type = LINK_DESCR_ID;
                parsed_nlri.nlri[LIB_NLRI_LS_LINK_REMOTE_ID].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_LINK_REMOTE_ID];
                parsed_nlri.nlri[LIB_NLRI_LS_LINK_REMOTE_ID].value.push_back(val_ss.str());

                //Update the hash
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_LINK_LOCAL_ID].value, &hash);
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_LINK_REMOTE_ID].value, &hash);

                data_read += 8;

                SELF_DEBUG("%sbgp-ls: Link descriptor ID local = %08x remote = %08x", caller->debug_prepend_string.c_str(),
                           local_id, remote_id);

                break;
            }

            case LINK_DESCR_MT_ID:
            {
                if (len < 2) {
                    LOG_NOTICE("%sbgp-ls: failed to parse link MT-ID descriptor sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len;
                    break;
                }

                if (len > 4) {
                    SELF_DEBUG("%sbgp-ls: failed to parse link MT-ID descriptor sub-tlv; too long %d", caller->debug_prepend_string.c_str(),
                               len);
                    mt_id = 0;
                    data_read += len;
                    break;
                }

                memcpy(&mt_id, data, len); parse_bgp_lib::SWAP_BYTES(&mt_id);
                mt_id >>= 16;          // MT ID is 16 bits
                char numstring[100] = {0};
                snprintf(&numstring[0], sizeof(numstring), "%" PRIx32, mt_id);
                parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].official_type = LINK_DESCR_MT_ID;
                parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_MT_ID];
                parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].value.push_back(string(numstring));
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].value, &hash);

                data_read += len;

                SELF_DEBUG("%sbgp-ls: Link descriptor MT-ID = %08x ", caller->debug_prepend_string.c_str(), mt_id);

                break;
            }

            case LINK_DESCR_IPV4_INTF_ADDR:
            {
                if (len != 4) {
                    LOG_NOTICE("%sbgp-ls: failed to parse link descriptor interface IPv4 sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len <= data_len ? len : data_len;
                    break;
                }

                char ip_char[46];
                memcpy(intf_addr, data, 4);
                inet_ntop(AF_INET, intf_addr, ip_char, sizeof(ip_char));

                parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].official_type = LINK_DESCR_IPV4_INTF_ADDR;
                parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_INTF_ADDR];
                parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].value.push_back(ip_char);
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].value, &hash);

                data_read += 4;

                SELF_DEBUG("%sbgp-ls: Link descriptor Interface Address = %s", caller->debug_prepend_string.c_str(), ip_char);
                break;
            }

            case LINK_DESCR_IPV6_INTF_ADDR:
            {
                if (len != 16) {
                    LOG_NOTICE("%sbgp-ls: failed to parse link descriptor interface IPv6 sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len <= data_len ? len : data_len;
                    break;
                }

                char ip_char[46];
                memcpy(intf_addr, data, 16);
                inet_ntop(AF_INET6, intf_addr, ip_char, sizeof(ip_char));
                parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].official_type = LINK_DESCR_IPV6_INTF_ADDR;
                parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_INTF_ADDR];
                parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].value.push_back(ip_char);
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_INTF_ADDR].value, &hash);

                data_read += 16;

                SELF_DEBUG("%sbgp-ls: Link descriptor interface address = %s", caller->debug_prepend_string.c_str(), ip_char);
                break;
            }

            case LINK_DESCR_IPV4_NEI_ADDR:
            {
                if (len != 4) {
                    LOG_NOTICE("%sbgp-ls: failed to parse link descriptor neighbor IPv4 sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len <= data_len ? len : data_len;
                    break;
                }

                char ip_char[46];
                memcpy(nei_addr, data, 4);
                inet_ntop(AF_INET, nei_addr, ip_char, sizeof(ip_char));
                parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].official_type = LINK_DESCR_IPV4_NEI_ADDR;
                parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_NEIGHBOR_ADDR];
                parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].value.push_back(ip_char);
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].value, &hash);

                data_read += 4;

                SELF_DEBUG("%sbgp-ls: Link descriptor neighbor address = %s", caller->debug_prepend_string.c_str(), ip_char);
                break;
            }

            case LINK_DESCR_IPV6_NEI_ADDR:
            {
                if (len != 16) {
                    LOG_NOTICE("%sbgp-ls: failed to parse link descriptor neighbor IPv6 sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len <= data_len ? len : data_len;
                    break;
                }

                char ip_char[46];
                memcpy(nei_addr, data, 16);
                inet_ntop(AF_INET6, nei_addr, ip_char, sizeof(ip_char));
                parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].official_type = LINK_DESCR_IPV6_NEI_ADDR;
                parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_NEIGHBOR_ADDR];
                parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].value.push_back(ip_char);
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_NEIGHBOR_ADDR].value, &hash);

                data_read += 16;

                SELF_DEBUG("%sbgp-ls: Link descriptor neighbor address = %s", caller->debug_prepend_string.c_str(), ip_char);
                break;
            }


            default:
                LOG_NOTICE("%sbgp-ls: link descriptor sub-tlv %d not yet implemented, skipping", caller->debug_prepend_string.c_str(), type);
                data_read += len;
                break;
        }

        return data_read;
    }


    /**********************************************************************************//*
     * Parse LINK NLRI
     *
     * \details will parse the LINK NLRI type.  Data starts at local node descriptor.
     *
     * \param [in]   data           Pointer to the start of the node NLRI data
     * \param [in]   data_len       Length of the data
     * \param [in]   id             NLRI/type identifier
     * \param [in]   proto_id       NLRI protocol type id
     */
    void MPLinkState::parseNlriLink(u_char *data, int data_len, uint64_t id, uint8_t proto_id) {
        parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri parsed_nlri;
        parsed_nlri.afi = parse_bgp_lib::BGP_AFI_BGPLS;
        parsed_nlri.safi = parse_bgp_lib::BGP_SAFI_BGPLS;
        parsed_nlri.type = parse_bgp_lib::LIB_NLRI_TYPE_LS_LINK;

        std::stringstream   val_ss;

        if (data_len < 4) {
            LOG_WARN("%sbgp-ls: Unable to parse link NLRI since it's too short (invalid)", caller->debug_prepend_string.c_str());
            return;
        }
        char numstring[100] = {0};
        snprintf(&numstring[0], sizeof(numstring), "%" PRIx64, id);

        parsed_nlri.nlri[LIB_NLRI_LS_ROUTING_ID].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_ROUTING_ID];
        parsed_nlri.nlri[LIB_NLRI_LS_ROUTING_ID].value.push_back(string(numstring));

        parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_PROTOCOL];
        parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.push_back(decodeNlriProtocolId(proto_id));

        SELF_DEBUG("%sbgp-ls: ID = %x Protocol = %s", caller->debug_prepend_string.c_str(),
                   id, parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.front().c_str());

        /*
         * Parse local and remote node descriptors (expect both)
         */
        uint16_t type;
        uint16_t len;

        for (char i = 0; i < 2; i++) {
            memcpy(&type, data, 2);
            parse_bgp_lib::SWAP_BYTES(&type);
            memcpy(&len, data + 2, 2);
            parse_bgp_lib::SWAP_BYTES(&len);
            data_len -= 4;
            data += 4;

            if (len > data_len) {
                LOG_WARN("%sbgp-ls: failed to parse node descriptor; type length is larger than available data %d>=%d",
                         caller->debug_prepend_string.c_str(), len, data_len);
                return;
            }

            // Parse the local descriptor sub-tlv's
            int data_read;
            MD5 hash;
            while (len > 0) {
                data_read = parseDescrLocalRemoteNode(data, len, parsed_nlri, (type == NODE_DESCR_LOCAL_DESCR), hash);
                len -= data_read;

                // Update the nlri data pointer and remaining length after processing the local descriptor sub-tlv
                data += data_read;
                data_len -= data_read;
            }
            hash.finalize();

            // Save the hash
            unsigned char *hash_raw = hash.raw_digest();
            if (type == NODE_DESCR_LOCAL_DESCR) {
                parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_LOCAL_NODE_HASH];
                parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].value.push_back(parse_bgp_lib::hash_toStr(hash_raw));
            } else {
                parsed_nlri.nlri[LIB_NLRI_LS_REMOTE_NODE_HASH].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_REMOTE_NODE_HASH];
                parsed_nlri.nlri[LIB_NLRI_LS_REMOTE_NODE_HASH].value.push_back(parse_bgp_lib::hash_toStr(hash_raw));
            }
            delete[] hash_raw;
        }

        MD5 hash;
        /*
         * Remaining data is the link descriptor sub-tlv's
         */
        int data_read;
        while (data_len > 0) {
            data_read = parseDescrLink(data, data_len, parsed_nlri, hash);

            // Update the nlri data pointer and remaining length after processing the local descriptor sub-tlv
            data += data_read;
            data_len -= data_read;
        }
        update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value, &hash);
        update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].value, &hash);
        update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_REMOTE_NODE_HASH].value, &hash);

        //Update hash with peer hash if it exists
        if (caller->p_info)
            hash.update((unsigned char *) caller->p_info->peer_hash_str.c_str(), caller->p_info->peer_hash_str.length());

        hash.finalize();

        unsigned char *hash_raw = hash.raw_digest();
        parsed_nlri.nlri[LIB_NLRI_LS_LINK_HASH].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_LINK_HASH];
        parsed_nlri.nlri[LIB_NLRI_LS_LINK_HASH].value.push_back(parse_bgp_lib::hash_toStr(hash_raw));
        delete[] hash_raw;

        // Save link to parsed data
        /*
        SELF_DEBUG("MT-ID = %u/%u", link_tbl.mt_id, info.mt_id);
        link_tbl.isIPv4             = info.isIPv4;
        link_tbl.mt_id              = info.mt_id;
        link_tbl.local_link_id      = info.local_id;
        link_tbl.remote_link_id     = info.remote_id;
        memcpy(link_tbl.intf_addr, info.intf_addr, sizeof(link_tbl.intf_addr));
        memcpy(link_tbl.nei_addr, info.nei_addr, sizeof(link_tbl.nei_addr));
         */

        nlri_list->push_back(parsed_nlri);
    }

    /**********************************************************************************//*
     * Parse PREFIX NLRI
     *
     * \details will parse the PREFIX NLRI type.  Data starts at local node descriptor.
     *
     * \param [in]   data           Pointer to the start of the node NLRI data
     * \param [in]   data_len       Length of the data
     * \param [in]   id             NLRI/type identifier
     * \param [in]   proto_id       NLRI protocol type id
     * \param [in]   isIPv4         Bool value to indicate IPv4(true) or IPv6(false)
     */
    void MPLinkState::parseNlriPrefix(u_char *data, int data_len, uint64_t id, uint8_t proto_id, bool isIPv4) {
        parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri parsed_nlri;
        parsed_nlri.afi = parse_bgp_lib::BGP_AFI_BGPLS;
        parsed_nlri.safi = parse_bgp_lib::BGP_SAFI_BGPLS;
        parsed_nlri.type = parse_bgp_lib::LIB_NLRI_TYPE_LS_PREFIX;

        std::stringstream   val_ss;

        if (data_len < 4) {
            LOG_WARN("%sbgp-ls: Unable to parse prefix NLRI since it's too short (invalid)", caller->debug_prepend_string.c_str());
            return;
        }

        char numstring[100] = {0};
        snprintf(&numstring[0], sizeof(numstring), "%" PRIx64, id);

        parsed_nlri.nlri[LIB_NLRI_LS_ROUTING_ID].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_ROUTING_ID];
        parsed_nlri.nlri[LIB_NLRI_LS_ROUTING_ID].value.push_back(string(numstring));

        parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_PROTOCOL];
        parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.push_back(decodeNlriProtocolId(proto_id));

        SELF_DEBUG("%sbgp-ls: ID = %x Protocol = %s", caller->debug_prepend_string.c_str(),
                   id, parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value.front().c_str());

        /*
         * Parse the local node descriptor sub-tlv
         */
        uint16_t type;
        uint16_t len;

        memcpy(&type, data, 2);
        parse_bgp_lib::SWAP_BYTES(&type);
        memcpy(&len, data + 2, 2);
        parse_bgp_lib::SWAP_BYTES(&len);
        data_len -= 4;
        data += 4;

        if (len > data_len) {
            LOG_WARN("%sbgp-ls: failed to parse node descriptor; type length is larger than available data %d>=%d",
                     caller->debug_prepend_string.c_str(), len, data_len);
            return;
        }

        if (type != NODE_DESCR_LOCAL_DESCR) {
            LOG_WARN("%sbgp-ls: failed to parse node descriptor; Type (%d) is not local descriptor",
                     caller->debug_prepend_string.c_str(), type);
            return;
        }

        // Parse the local descriptor sub-tlv's
        int data_read;
        MD5 node_hash;
        while (len > 0) {
            data_read = parseDescrLocalRemoteNode(data, len, parsed_nlri, true, node_hash);
            len -= data_read;

            // Update the nlri data pointer and remaining length after processing the local descriptor sub-tlv
            data += data_read;
            data_len -= data_read;
        }
        node_hash.finalize();

        // Save the hash
        unsigned char *node_hash_raw = node_hash.raw_digest();
        parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_LOCAL_NODE_HASH];
        parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].value.push_back(parse_bgp_lib::hash_toStr(node_hash_raw));
        delete[] node_hash_raw;

        MD5 hash;
        /*
         * Remaining data is the link descriptor sub-tlv's
         */
        while (data_len > 0) {
            data_read = parseDescrPrefix(data, data_len, parsed_nlri, isIPv4, hash);

            // Update the nlri data pointer and remaining length after processing the local descriptor sub-tlv
            data += data_read;
            data_len -= data_read;
        }

        update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_PROTOCOL].value, &hash);
        update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_LOCAL_NODE_HASH].value, &hash);

        //Update hash with peer hash if it exists
        if (caller->p_info)
            hash.update((unsigned char *) caller->p_info->peer_hash_str.c_str(), caller->p_info->peer_hash_str.length());

        hash.finalize();

        unsigned char *hash_raw = hash.raw_digest();
        parsed_nlri.nlri[LIB_NLRI_LS_PREFIX_HASH].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_PREFIX_HASH];
        parsed_nlri.nlri[LIB_NLRI_LS_PREFIX_HASH].value.push_back(parse_bgp_lib::hash_toStr(hash_raw));
        delete[] hash_raw;

        nlri_list->push_back(parsed_nlri);
    }

    /**********************************************************************************//*
     * Parse Prefix Descriptor sub-tlvs
     *
     * \details will parse a prefix descriptor (series of sub-tlv's)
     *
     * \param [in]   data           Pointer to the start of the node NLRI data
     * \param [in]   data_len       Length of the data
     * \param [out]  info           prefix descriptor information returned/updated
     * \param [in]   isIPv4         Bool value to indicate IPv4(true) or IPv6(false)
     * \returns number of bytes read
     */
    int MPLinkState::parseDescrPrefix(u_char *data, int data_len, parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri &parsed_nlri,
                                      bool isIPv4, MD5 &hash) {
        uint16_t type;
        uint16_t len;
        int data_read = 0;

        char        ospf_route_type[32];                ///< OSPF Route type in string form for DB enum
        uint32_t    mt_id;                              ///< Multi-Topology ID
        uint8_t     prefix[16];                         ///< Prefix binary address
        uint8_t     prefix_bcast[16];                   ///< Prefix broadcast/ending binary address
        uint8_t     prefix_len;                         ///< Length of prefix in bits

        std::stringstream   val_ss;

        if (data_len < 4) {
            LOG_NOTICE("%sbgp-ls: failed to parse link descriptor; too short", caller->debug_prepend_string.c_str());
            return data_len;
        }

        memcpy(&type, data, 2);
        parse_bgp_lib::SWAP_BYTES(&type);

        memcpy(&len, data + 2, 2);
        parse_bgp_lib::SWAP_BYTES(&len);

        if (len > data_len - 4) {
            LOG_NOTICE("%sbgp-ls: failed to parse prefix descriptor; type length is larger than available data %d>=%d",
                       caller->debug_prepend_string.c_str(), len, data_len);
            return data_len;
        }

        data += 4; data_read += 4;
        char numstring[100] = {0};

        switch (type) {
            case PREFIX_DESCR_IP_REACH_INFO: {
                uint64_t    value_64bit;
                uint32_t    value_32bit;

                if (len < 1) {
                    LOG_INFO("%sbgp-ls: Not parsing prefix ip_reach_info sub-tlv; too short at len=%d",
                             caller->debug_prepend_string.c_str(), len);
                    data_read += len;
                    break;
                }

                prefix_len = *data;
                val_ss.str();
                val_ss << static_cast<unsigned>(prefix_len);
                parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX_LENGTH].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_IP_REACH_PREFIX_LENGTH];
                parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX_LENGTH].value.push_back(val_ss.str());

                data_read++; data++;

                char ip_char[46];
                char ip_char_bcast[46];
                bzero(prefix, sizeof(prefix));
                bzero(prefix, sizeof(prefix_bcast));

                // If length is greater than 1 then parse the prefix (default/zero prefix will not have prefix bytes)
                if (len > 1) {
                    memcpy(prefix, data, len - 1);
                    data_read += len - 1;
                }

                if (isIPv4) {
                    inet_ntop(AF_INET, prefix, ip_char, sizeof(ip_char));

                    // Get the broadcast/ending IP address
                    if (prefix_len < 32) {
                        memcpy(&value_32bit, prefix, 4);
                        parse_bgp_lib::SWAP_BYTES(&value_32bit);

                        value_32bit |= 0xFFFFFFFF >> prefix_len;
                        parse_bgp_lib::SWAP_BYTES(&value_32bit);
                        memcpy(prefix_bcast, &value_32bit, 4);

                    } else
                        memcpy(prefix_bcast, prefix, sizeof(prefix_bcast));
                    inet_ntop(AF_INET, prefix_bcast, ip_char_bcast, sizeof(ip_char_bcast));


                } else {
                    inet_ntop(AF_INET6, prefix, ip_char, sizeof(ip_char));

                    // Get the broadcast/ending IP address
                    if (prefix_len < 128) {
                        if (prefix_len >= 64) {
                            // High order bytes are left alone
                            memcpy(prefix_bcast, prefix, 8);

                            // Low order bytes are updated
                            memcpy(&value_64bit, &prefix[8], 8);
                            parse_bgp_lib::SWAP_BYTES(&value_64bit);

                            value_64bit |= 0xFFFFFFFFFFFFFFFF >> (prefix_len - 64);
                            parse_bgp_lib::SWAP_BYTES(&value_64bit);
                            memcpy(&prefix_bcast[8], &value_64bit, 8);

                        } else {
                            // Low order types are all ones
                            value_64bit = 0xFFFFFFFFFFFFFFFF;
                            memcpy(&prefix_bcast[8], &value_64bit, 8);

                            // High order bypes are updated
                            memcpy(&value_64bit, prefix, 8);
                            parse_bgp_lib::SWAP_BYTES(&value_64bit);

                            value_64bit |= 0xFFFFFFFFFFFFFFFF >> prefix_len;
                            parse_bgp_lib::SWAP_BYTES(&value_64bit);
                            memcpy(prefix_bcast, &value_64bit, 8);
                        }
                    } else
                        memcpy(prefix_bcast, prefix, sizeof(prefix_bcast));
                }

                parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX].official_type = PREFIX_DESCR_IP_REACH_INFO;
                parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_IP_REACH_PREFIX];
                parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX].value.push_back(ip_char);

                parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX_BCAST].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_IP_REACH_PREFIX_BCAST];
                parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX_BCAST].value.push_back(ip_char_bcast);

                //Update hash
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX].value, &hash);
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_IP_REACH_PREFIX_LENGTH].value, &hash);

                SELF_DEBUG("%sbgp-ls: prefix ip_reach_info: prefix = %s/%d", caller->debug_prepend_string.c_str(), ip_char, prefix_len);
                break;
            }
            case PREFIX_DESCR_MT_ID:
                if (len < 2) {
                    LOG_NOTICE("%sbgp-ls: failed to parse prefix MT-ID descriptor sub-tlv; too short", caller->debug_prepend_string.c_str());
                    data_read += len;
                    break;
                }

                if (len > 4) {
                    SELF_DEBUG("%sbgp-ls: failed to parse link MT-ID descriptor sub-tlv; too long %d", caller->debug_prepend_string.c_str(), len);
                    mt_id = 0;
                    data_read += len;
                    break;
                }

                memcpy(&mt_id, data, len); parse_bgp_lib::SWAP_BYTES(&mt_id);
                mt_id >>= 16;          // MT ID is 16 bits
                snprintf(&numstring[0], sizeof(numstring), "%" PRIx32, mt_id);

                parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].official_type = PREFIX_DESCR_MT_ID;
                parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_MT_ID];
                parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].value.push_back(string(numstring));
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_MT_ID].value, &hash);

                data_read += len;

                SELF_DEBUG("%sbgp-ls: Link descriptor MT-ID = %08x ", caller->debug_prepend_string.c_str(), mt_id);

                break;

            case PREFIX_DESCR_OSPF_ROUTE_TYPE: {
                data_read++;
                switch (*data) {
                    case OSPF_RT_EXTERNAL_1:
                        snprintf(ospf_route_type, sizeof(ospf_route_type), "Ext-1");
                        break;

                    case OSPF_RT_EXTERNAL_2:
                        snprintf(ospf_route_type, sizeof(ospf_route_type),"Ext-2");
                        break;

                    case OSPF_RT_INTER_AREA:
                        snprintf(ospf_route_type, sizeof(ospf_route_type),"Inter");
                        break;

                    case OSPF_RT_INTRA_AREA:
                        snprintf(ospf_route_type, sizeof(ospf_route_type),"Intra");
                        break;

                    case OSPF_RT_NSSA_1:
                        snprintf(ospf_route_type, sizeof(ospf_route_type),"NSSA-1");
                        break;

                    case OSPF_RT_NSSA_2:
                        snprintf(ospf_route_type, sizeof(ospf_route_type),"NSSA-2");
                        break;

                    default:
                        snprintf(ospf_route_type, sizeof(ospf_route_type),"Intra");

                }

                parsed_nlri.nlri[LIB_NLRI_LS_OSPF_ROUTE_TYPE].official_type = PREFIX_DESCR_OSPF_ROUTE_TYPE;
                parsed_nlri.nlri[LIB_NLRI_LS_OSPF_ROUTE_TYPE].name = parse_bgp_lib::parse_bgp_lib_nlri_names[LIB_NLRI_LS_OSPF_ROUTE_TYPE];
                parsed_nlri.nlri[LIB_NLRI_LS_OSPF_ROUTE_TYPE].value.push_back(val_ss.str());
                update_hash(&parsed_nlri.nlri[LIB_NLRI_LS_OSPF_ROUTE_TYPE].value, &hash);

                SELF_DEBUG("%sbgp-ls: prefix ospf route type is %s", caller->debug_prepend_string.c_str(), ospf_route_type);
                break;
            }

            default:
                LOG_NOTICE("%sbgp-ls: Prefix descriptor sub-tlv %d not yet implemented, skipping.", caller->debug_prepend_string.c_str(), type);
                data_read += len;
                break;
        }


        return data_read;
    }

/**
* \brief Method to resolve the IP address to a hostname
*
*  \param [in]   name      String name (ip address)
*  \param [out]  hostname  String reference for hostname
*
*  \returns true if error, false if no error
*/
    bool MPLinkState::resolveIp(string name, string &hostname) {
        addrinfo *ai;
        char host[255];

        if (!getaddrinfo(name.c_str(), NULL, NULL, &ai)) {

            if (!getnameinfo(ai->ai_addr,ai->ai_addrlen, host, sizeof(host), NULL, 0, NI_NAMEREQD)) {
                hostname.assign(host);
                LOG_INFO("%sresolve: %s to %s", caller->debug_prepend_string.c_str(),
                         name.c_str(), hostname.c_str());
            }

            freeaddrinfo(ai);
            return false;
        }

        return true;
    }

} /* namespace parse_bgp_lib */
