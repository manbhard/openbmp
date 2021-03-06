/*
 * Copyright (c) 2013-2016 Cisco Systems, Inc. and others.  All rights reserved.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v1.0 which accompanies this distribution,
 * and is available at http://www.eclipse.org/legal/epl-v10.html
 *
 */

#ifndef TEMPLATE_CFG_H_
#define TEMPLATE_CFG_H_

#include <string>
#include <list>
#include <map>
#include "Logger.h"
#include "parseBgpLib.h"

namespace template_cfg {

    /**
     * Defines the template topics
     *
     *  \see http://www.iana.org/assignments/bgp-parameters/bgp-parameters.xhtml
     */
    enum TEMPLATE_TOPICS {
        BASE_ATTRIBUTES,
        UNICAST_PREFIX,
        LS_NODES,
        LS_LINKS,
        LS_PREFIXES,
        L3_VPN,
        EVPN,
        BMP_ROUTER,
        BMP_COLLECTOR,
        BMP_PEER,
        BMP_STATS
    };


    enum TEMPLATE_TYPES {
        CONTAINER,
        LOOP,
        REPLACE,
        END,
    };

    enum REPLACEMENT_LIST_TYPE {
        ATTR,
        NLRI,
        PEER,
        ROUTER,
        COLLECTOR,
        HEADER,
        STATS
    };

    enum TEMPLATE_FORMAT_TYPE {
        TSV,
        RAW
    };


    /**
     * \class   Template_cfg
     *
     * \brief   Template configuration class for openbmpd
     * \details
     *      Parses the template configuration file and loads value in this class instance.
     */
    class Template_cfg {
    public:
        /*********************************************************************//**
         * Constructor for class
         ***********************************************************************/
        Template_cfg(Logger *logPtr, bool enable_debug);

        virtual ~Template_cfg();

        static std::map<std::string, int> lookup_map;


        /*********************************************************************//**
         * Load configuration from file
         *
         * \param [in] template_filename     template filename
         ***********************************************************************/
        size_t
        create_container_loop(TEMPLATE_TYPES type, TEMPLATE_TOPICS topic, char *buf, std::string &prepend_string);

        bool
        create_container_loop_tsv(TEMPLATE_TYPES type, TEMPLATE_TOPICS topic, const YAML::Node &node);

        size_t create_replacement(char *buf, std::string &prepend_string);

        size_t execute_container(char *buf, size_t max_buf_length,
                               std::vector<parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri> &rib_list,
                               parse_bgp_lib::parseBgpLib::attr_map &attrs, parse_bgp_lib::parseBgpLib::peer_map &peer,
                                 parse_bgp_lib::parseBgpLib::router_map &router,
                                 parse_bgp_lib::parseBgpLib::collector_map &collector,
                                 parse_bgp_lib::parseBgpLib::header_map &header,
                                 parse_bgp_lib::parseBgpLib::stat_map &stats);

        size_t execute_loop(char *buf, size_t max_buf_length,
                                 std::vector<parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri> &rib_list,
                                 parse_bgp_lib::parseBgpLib::attr_map &attrs, parse_bgp_lib::parseBgpLib::peer_map &peer,
                            parse_bgp_lib::parseBgpLib::router_map &router,
                            parse_bgp_lib::parseBgpLib::collector_map &collector,
                            parse_bgp_lib::parseBgpLib::header_map &header,
                            parse_bgp_lib::parseBgpLib::stat_map &stats,
                            uint64_t &seq);

        size_t execute_replace(char *buf, size_t max_buf_length,
                            parse_bgp_lib::parseBgpLib::parse_bgp_lib_nlri &nlri,
                            parse_bgp_lib::parseBgpLib::attr_map &attrs, parse_bgp_lib::parseBgpLib::peer_map &peer,
                               parse_bgp_lib::parseBgpLib::router_map &router,
                               parse_bgp_lib::parseBgpLib::collector_map &collector,
                               parse_bgp_lib::parseBgpLib::header_map &header,
                               parse_bgp_lib::parseBgpLib::stat_map &stats);

        std::list<template_cfg::Template_cfg> template_children;

        TEMPLATE_TYPES type;
        TEMPLATE_TOPICS topic;
        TEMPLATE_FORMAT_TYPE format;

        REPLACEMENT_LIST_TYPE replacement_list_type;
        int replacement_var;
        uint64_t    seq;

    private:
        bool debug;                             ///< debug flag to indicate debugging
        Logger *logger;                         ///< Logging class pointer
        std::string prepend_string;
        void printWarning(const std::string msg, const YAML::Node &node);
    };
}

    class Template_map {
    public:
        /*********************************************************************//**
         * Constructor for class
         ***********************************************************************/
        Template_map(Logger *logPtr, bool enable_debug);

        virtual ~Template_map();

        std::map<template_cfg::TEMPLATE_TOPICS, template_cfg::Template_cfg> template_map;

        /*********************************************************************//**
         * Load configuration from file
         *
         * \param [in] template_filename     template filename
         ***********************************************************************/
        bool load(const char *template_filename);


    private:
        bool debug;                             ///< debug flag to indicate debugging
        Logger *logger;                         ///< Logging class pointer

    };

static void strip_last_newline (std::string &s) {

    std::string str2;
    str2.push_back('\\');
    str2.push_back(s[s.length()]);

    size_t tmp = 0, begin;
    while (tmp != std::string::npos) {
        begin = tmp;
        tmp = s.find_first_of('\n', tmp + 1);
    }

    size_t end = s.find_first_not_of(' ', begin + 1);
    if ((end == std::string::npos) or (end == (s.length() - 1))) {
         s.erase(begin, end);
    }
}

static void print_template (template_cfg::Template_cfg &template_cfg_print, size_t iteration) {
    std::string append_str(iteration, 32);
    switch (template_cfg_print.type) {
        case template_cfg::CONTAINER : {
            cout << append_str << "Container: ";
            switch (template_cfg_print.topic) {
                case template_cfg::UNICAST_PREFIX : {
                    cout << "unicast_prefix" << endl;
                    break;
                }
                case template_cfg::LS_NODES : {
                    cout << "ls_node" << endl;
                    break;
                }
                case template_cfg::LS_LINKS: {
                    cout << "ls_links" << endl;
                    break;
                }
                case template_cfg::LS_PREFIXES : {
                    cout << "ls_prefixes" << endl;
                    break;
                }
                case template_cfg::L3_VPN : {
                    cout << "l3vpn" << endl;
                    break;
                }
                case template_cfg::EVPN : {
                    cout << "evpn" << endl;
                    break;
                }
                case template_cfg::BMP_ROUTER : {
                    cout << "router" << endl;
                    break;
                }
                case template_cfg::BMP_COLLECTOR : {
                    cout << "collector" << endl;
                    break;
                }
                case template_cfg::BMP_PEER : {
                    cout << "peer" << endl;
                    break;
                }
                case template_cfg::BASE_ATTRIBUTES : {
                    cout << "base_attributes" << endl;
                    break;
                }
                case template_cfg::BMP_STATS : {
                    cout << "stats" << endl;
                    break;
                }
                    default:
                    break;
            }
            for (std::list<template_cfg::Template_cfg>::iterator it = template_cfg_print.template_children.begin();
                 it != template_cfg_print.template_children.end(); it++) {
                print_template(*it, iteration + 4);
            }
            break;
        }
        case template_cfg::LOOP : {
            cout << append_str.c_str() << "Loop: " << endl;
            for (std::list<template_cfg::Template_cfg>::iterator it = template_cfg_print.template_children.begin();
                 it != template_cfg_print.template_children.end(); it++) {
                print_template(*it, iteration + 4);
            }
            break;
        }
        case template_cfg::END : {
            cout << append_str.c_str() << "End " << endl;
            break;
        }
        case template_cfg::REPLACE : {
            cout << append_str.c_str() << "Replacement: List: ";
            switch (template_cfg_print.replacement_list_type) {
                case template_cfg::ATTR :
                    cout << "ATTR, replacement variable: " << template_cfg_print.replacement_var << endl;
                    break;
                case template_cfg::NLRI :
                    cout << "NLRI, replacement variable: " << template_cfg_print.replacement_var << endl;
                    break;
                case template_cfg::PEER :
                    cout << "PEER, replacement variable: " << template_cfg_print.replacement_var << endl;
                    break;
                case template_cfg::ROUTER :
                    cout << "ROUTER, replacement variable: " << template_cfg_print.replacement_var << endl;
                    break;
                case template_cfg::COLLECTOR :
                    cout << "COLLECTOR, replacement variable: " << template_cfg_print.replacement_var << endl;
                    break;
                case template_cfg::HEADER :
                    cout << "HEADER, replacement variable: " << template_cfg_print.replacement_var << endl;
                    break;
                case template_cfg::STATS :
                    cout << "STATS, replacement variable: " << template_cfg_print.replacement_var << endl;
                    break;

                default:
                    break;
            }
        }
        default:
            break;
    }
}


#endif /* TEMPLATE_CFG_H_ */
