/*
 * This file is part of mcproxy.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * written by Sebastian Woelke, in cooperation with:
 * INET group, Hamburg University of Applied Sciences,
 * Website: http://mcproxy.realmv6.org/
 */

#include "include/hamcast_logging.h"
#include "include/proxy/simple_mc_proxy_routing.hpp"
#include "include/proxy/interfaces.hpp"
#include "include/utils/addr_storage.hpp"
#include "include/proxy/proxy_instance.hpp"
#include "include/proxy/querier.hpp"
#include "include/proxy/routing.hpp"
#include "include/proxy/interfaces.hpp"
#include "include/proxy/sender.hpp"
#include "include/proxy/timing.hpp"

#include <algorithm>

//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------

source_state::source_state()
    : m_mc_filter(INCLUDE_MODE)
{
    HC_LOG_TRACE("");
}

source_state::source_state(std::pair<mc_filter, source_list<source>> sstate)
    : m_mc_filter(sstate.first)
    , m_source_list(sstate.second)
{
    HC_LOG_TRACE("");
}

//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
interface_memberships::interface_memberships(const addr_storage& gaddr, const proxy_instance* pi)
{
    HC_LOG_TRACE("");


    std::list<std::pair<source_state, const std::shared_ptr<const interface>&>> init_sstate_list;
    for (auto & downs_e : pi->m_downstreams) {
        init_sstate_list.push_back(std::pair<source_state, const std::shared_ptr<const interface>&>(source_state(downs_e.second.m_querier->get_group_mebership_infos(gaddr)), downs_e.second.m_interface));
    }

    //init and fill database
    for (auto & upstr_e : pi->m_upstreams) {

        std::list<source_state> tmp_sstate_list;
        source_state tmp_sstate;
        
        for (auto & current_sstate : init_sstate_list) {

            //sort out all unwanted sources
            for (auto source_it = current_sstate.m_source_list.begin(); source_it != current_sstate.m_source_list.end(); ) {

                //downstream out
                if (!downs_e.second.m_interface->match_output_filter(interfaces::get_if_name(upstr_e.m_if_index), gaddr, source_it->saddr)) {
                    source_it = current_sstate.m_source_list.erase(source_it);
                    continue;
                }

                //upstream in
                if (!upstr_e.m_interface->match_input_filter(interfaces::get_if_name(upstr_e.m_if_index), gaddr, source_it->saddr)) {
                    tmp_sstate.m_source_list.insert(*source_it);
                    source_it = current_sstate.m_source_list.erase(source_it);
                    continue;
                }

                ++source_it;
            }





        }

        m_data.push_back(std::pair<unsigned int, std::list<source_state>>(upstr_e.m_if_index, init_sstate_list));
        init_sstate_list = std::move(tmp_sstate_list);
    }



}

void interface_memberships::merge_membership_infos(source_state& merge_to, const source_state& merge_from) const
{
    HC_LOG_TRACE("");

    if (merge_to.m_mc_filter == INCLUDE_MODE) {
        if (merge_from.m_mc_filter == INCLUDE_MODE) {
            merge_to.m_source_list += merge_from.m_source_list;
        } else if (merge_from.m_mc_filter == EXLCUDE_MODE) {
            merge_to.m_mc_filter = EXLCUDE_MODE;
            merge_to.m_source_list = merge_from.m_source_list - merge_to.m_source_list;
        } else {
            HC_LOG_ERROR("unknown filter mode in parameter merge_from");
        }
    } else if (merge_to.m_mc_filter == EXLCUDE_MODE) {
        if (merge_from.m_mc_filter == INCLUDE_MODE) {
            merge_to.m_source_list -= merge_from.m_source_list;
        } else if (merge_from.m_mc_filter == EXLCUDE_MODE) {
            merge_to.m_source_list *= merge_from.m_source_list;
        } else {
            HC_LOG_ERROR("unknown filter mode in parameter merge_from");
        }
    } else {
        HC_LOG_ERROR("unknown filter mode in parameter merge_to");
    }
}


source_state interface_memberships::get_group_memberships(unsigned int upstream_if_index)
{
    HC_LOG_TRACE("");

    source_state result;
    auto data_it = m_data.begin();
    if (data_it->first != upstream_if_index) {
        HC_LOG_ERROR("unexpected upstream interface " << interfaces::get_if_name(upstream_if_index));
        return result;
    }

    for (auto & e : data_it->second) {
        merge_membership_infos(result, e);
    }

    m_data.pop_front();
    return result;
}

//std::pair<mc_filter, source_list<source>> simple_mc_proxy_routing::collect_group_membership_infos(unsigned int input_if, const addr_storage& gaddr)
//{
//HC_LOG_TRACE("");

//std::pair<mc_filter, source_list<source>> rt_pair;
//rt_pair.first = INCLUDE_MODE;
//rt_pair.second = {};

//for (auto & e : m_p->m_downstreams) {
//std::pair<mc_filter, source_list<source>> merge_from = e.second.m_querier->get_group_mebership_infos(gaddr);
//source_list<source>& check_sources = merge_from.second;
//for (auto it = check_sources.begin(); it != check_sources.end();) {
//if (!check_interface(IT_DOWNSTREAM, ID_OUT, e.first, input_if, gaddr, it->saddr)) {
//it = check_sources.erase(it);
//continue;
//}
//++it;
//}

////merge_membership_infos(rt_pair, merge_from);
//}

//return rt_pair;
//}



//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
simple_mc_proxy_routing::simple_mc_proxy_routing(const proxy_instance* p)
    : routing_management(p)
    , m_data(p->m_group_mem_protocol, p->m_mrt_sock)
{
    HC_LOG_TRACE("");
}

std::chrono::seconds simple_mc_proxy_routing::get_source_life_time()
{
    HC_LOG_TRACE("");
    return std::chrono::seconds(20);
}

void simple_mc_proxy_routing::event_new_source(const std::shared_ptr<proxy_msg>& msg)
{
    HC_LOG_TRACE("");

    switch (msg->get_type()) {
    case proxy_msg::NEW_SOURCE_MSG: {
        auto sm = std::static_pointer_cast<new_source_msg>(msg);
        source s(sm->get_saddr());
        s.shared_source_timer = set_source_timer(sm->get_if_index(), sm->get_gaddr(), sm->get_saddr());

        //route calculation
        m_data.set_source(sm->get_if_index(), sm->get_gaddr(), s);
        set_routes(sm->get_gaddr(), collect_interested_interfaces(sm->get_if_index(), sm->get_gaddr(), {sm->get_saddr()}));
    }
    break;
    default:
        HC_LOG_ERROR("unknown message format");
        return;
    }
}


void simple_mc_proxy_routing::event_querier_state_change(unsigned int if_index, const addr_storage& gaddr, const source_list<source>& slist)
{
    HC_LOG_TRACE("");

    //route calculation
    auto available_sources = m_data.get_available_sources(gaddr, slist);
    set_routes(gaddr, collect_interested_interfaces(if_index, gaddr, available_sources));

    //membership agregation
    if (is_rule_matching_type(IT_UPSTREAM, ID_IN, RMT_FIRST)) {
        for (auto & e : m_p->m_upstreams) {
            auto mem_info = collect_group_membership_infos(e.m_if_index, gaddr);
            send_records({e.m_if_index}, gaddr, mem_info);
        }
    } else if (is_rule_matching_type(IT_UPSTREAM, ID_IN, RMT_MUTEX)) {

    } else {
        HC_LOG_ERROR("unkown rule matching type in this context");
    }
}

//bool simple_mc_proxy_routing::is_rule_matching_type(rb_interface_type interface_type, rb_interface_direction interface_direction, rb_rule_matching_type rule_matching_type) const

void simple_mc_proxy_routing::timer_triggerd_maintain_routing_table(const std::shared_ptr<proxy_msg>& msg)
{
    HC_LOG_TRACE("");

    std::shared_ptr<new_source_timer> tm;

    if (!msg.unique()) {
        switch (msg->get_type()) {
        case proxy_msg::NEW_SOURCE_TIMER_MSG: {
            tm = std::static_pointer_cast<new_source_timer>(msg);

            auto cmp_source_list = m_data.get_available_sources(tm->get_gaddr(), {tm->get_saddr()});
            if (!cmp_source_list.empty()) {
                if (tm.get() == cmp_source_list.begin()->shared_source_timer.get()) {
                    auto saddr_it = m_data.refresh_source_or_del_it_if_unused(tm->get_gaddr(), tm->get_saddr());
                    if (!saddr_it.second) {
                        del_route(tm->get_if_index(), tm->get_gaddr(), tm->get_saddr());
                    } else {
                        saddr_it.first->shared_source_timer = set_source_timer(tm->get_if_index(), tm->get_gaddr(), tm->get_saddr());
                    }

                } else {
                    HC_LOG_DEBUG("filter_timer is outdate");
                }
            }

        }
        break;
        default:
            HC_LOG_ERROR("unknown timer message format");
            return;
        }
    } else {
        HC_LOG_DEBUG("filter_timer is outdate");
        return;
    }
}

bool simple_mc_proxy_routing::is_upstream(unsigned int if_index) const
{
    HC_LOG_TRACE("");

    for (auto & e : m_p->m_upstreams) {
        if (e.m_if_index == if_index) {
            return true;
        }
    }

    return false;
}

bool simple_mc_proxy_routing::is_downstream(unsigned int if_index) const
{
    HC_LOG_TRACE("");

    return m_p->m_downstreams.find(if_index) != m_p->m_downstreams.end();
}

bool simple_mc_proxy_routing::is_rule_matching_type(rb_interface_type interface_type, rb_interface_direction interface_direction, rb_rule_matching_type rule_matching_type) const
{
    HC_LOG_TRACE("");

    if (interface_type == IT_UPSTREAM) {
        if (interface_direction == ID_IN) {
            if (m_p->m_upstream_input_rule != nullptr) {
                return m_p->m_upstream_input_rule->get_rule_matching_type() == rule_matching_type;
            } else {
                HC_LOG_ERROR("upstream input rule is null");
                return false;
            }
        } else if (interface_direction == ID_OUT) {
            if (m_p->m_upstream_output_rule != nullptr) {
                return m_p->m_upstream_output_rule->get_rule_matching_type() == rule_matching_type;
            } else {
                HC_LOG_ERROR("upstream output rule is null");
                return false;
            }
        } else {
            HC_LOG_ERROR("interface direction not supported");
            return false;
        }
    } else {
        HC_LOG_ERROR("interface type not supported");
        return false;
    }
}

std::list<std::pair<source, std::list<unsigned int>>> simple_mc_proxy_routing::collect_interested_interfaces(unsigned int event_if_index, const addr_storage& gaddr, const source_list<source>& slist) const
{
    HC_LOG_TRACE("");

    const std::map<addr_storage, unsigned int>& input_if_index_map = m_data.get_interface_map(gaddr);

    //add upstream interfaces
    std::list<std::pair<source, std::list<unsigned int>>> rt_list;
    for (auto & s : slist) {
        if (is_downstream(event_if_index)) {
            auto input_if_it = input_if_index_map.find(s.saddr);
            if (input_if_it == input_if_index_map.end()) {
                HC_LOG_ERROR("input interface of multicast source " << s.saddr << " not found");
                continue;
            }

            std::list<unsigned int> up_if_list;
            for (auto ui : m_p->m_upstreams) {
                if (check_interface(IT_UPSTREAM, ID_OUT, ui.m_if_index, input_if_it->second, gaddr, s.saddr)) {

                    if (is_rule_matching_type(IT_UPSTREAM, ID_OUT, RMT_ALL)) {
                        up_if_list.push_back(ui.m_if_index);
                    } else if (is_rule_matching_type(IT_UPSTREAM, ID_OUT, RMT_FIRST)) {
                        up_if_list.push_back(ui.m_if_index);
                        break;
                    } else {
                        HC_LOG_ERROR("unknown rule matching type");
                    }

                }
            }
            rt_list.push_back(std::pair<source, std::list<unsigned int>>(s, up_if_list));

        } else { //data from an upstream are not forwarded to an other upstream interface
            rt_list.push_back(std::pair<source, std::list<unsigned int>>(s, {}));
        }
    }

    //add downstream interfaces
    std::function<bool(unsigned int, const addr_storage&)> filter_fun = [&](unsigned int output_if_index, const addr_storage & saddr) {
        auto input_if_it = input_if_index_map.find(saddr);
        if (input_if_it == input_if_index_map.end()) {
            HC_LOG_ERROR("input interface of multicast source " << saddr << " not found");
            return false;
        }

        return check_interface(IT_DOWNSTREAM, ID_OUT, output_if_index, input_if_it->second, gaddr, saddr);
    };

    for (auto & dif : m_p->m_downstreams) {
        if (dif.first != event_if_index) {
            dif.second.m_querier->suggest_to_forward_traffic(gaddr, rt_list, std::bind(filter_fun, dif.first, std::placeholders::_1));
        }
    }

    return rt_list;
}

void simple_mc_proxy_routing::set_routes(const addr_storage& gaddr, const std::list<std::pair<source, std::list<unsigned int>>>& output_if_index) const
{
    HC_LOG_TRACE("");

    const std::map<addr_storage, unsigned int>& input_if_index_map = m_data.get_interface_map(gaddr);
    unsigned int input_if_index;

    for (auto & e : output_if_index) {
        if (e.second.empty()) {

            auto input_if_it = input_if_index_map.find(e.first.saddr);
            if (input_if_it != std::end(input_if_index_map)) {
                input_if_index = input_if_it->second;
            } else {
                HC_LOG_ERROR("failed to find input interface of  (" << gaddr << ", " << e.first.saddr);
                continue;
            }

            del_route(input_if_index, gaddr, e.first.saddr);
        } else {
            std::list<int> vif_out;

            for (auto outif : e.second) {
                vif_out.push_back(m_p->m_interfaces->get_virtual_if_index(outif));
            }

            auto input_if_it = input_if_index_map.find(e.first.saddr);
            if (input_if_it != std::end(input_if_index_map)) {
                input_if_index = input_if_it->second;


                bool use_this_interface = false;
                if (is_upstream(input_if_index)) {
                    if (!check_interface(IT_UPSTREAM, ID_IN, input_if_index, input_if_index, gaddr, e.first.saddr)) {
                        use_this_interface = true;
                    }
                }

                if (!use_this_interface && is_downstream(input_if_index)) {
                    if (!check_interface(IT_DOWNSTREAM, ID_IN, input_if_index, input_if_index, gaddr, e.first.saddr)) {
                        use_this_interface = true;
                    }
                }

                if (!use_this_interface) {
                    continue;
                }

            } else {
                HC_LOG_ERROR("failed to find input interface of  (" << gaddr << ", " << e.first.saddr);
                continue;
            }

            m_p->m_routing->add_route(m_p->m_interfaces->get_virtual_if_index(input_if_index), gaddr, e.first.saddr, vif_out);
        }
    }
}

void simple_mc_proxy_routing::send_records(unsigned int upstream_if_index, const addr_storage& gaddr, const source_state& sstate) const
{
    HC_LOG_TRACE("");
    m_p->m_sender->send_report(upstream_if_index, sstate.m_mc_filter, gaddr, sstate.m_source_list);
}

void simple_mc_proxy_routing::del_route(unsigned int if_index, const addr_storage& gaddr, const addr_storage& saddr) const
{
    HC_LOG_TRACE("");
    m_p->m_routing->del_route(m_p->m_interfaces->get_virtual_if_index(if_index), gaddr, saddr);
}

std::shared_ptr<new_source_timer> simple_mc_proxy_routing::set_source_timer(unsigned int if_index, const addr_storage& gaddr, const addr_storage& saddr)
{
    HC_LOG_TRACE("");
    auto nst = std::make_shared<new_source_timer>(if_index, gaddr, saddr, get_source_life_time());
    m_p->m_timing->add_time(get_source_life_time(), m_p, nst);

    return nst;
}

bool simple_mc_proxy_routing::check_interface(rb_interface_type interface_type, rb_interface_direction interface_direction, unsigned int checking_if_index, unsigned int input_if_index, const addr_storage& gaddr, const addr_storage& saddr) const
{
    HC_LOG_TRACE("");

    std::shared_ptr<interface> interf;
    if (interface_type == IT_UPSTREAM) {
        auto uinfo_it = std::find_if(m_p->m_upstreams.begin(), m_p->m_upstreams.end(), [&](const proxy_instance::upstream_infos & ui) {
            return ui.m_if_index == checking_if_index;
        });

        if (uinfo_it != m_p->m_upstreams.end()) {
            interf = uinfo_it->m_interface;
        } else {
            HC_LOG_ERROR("upstream interface " << interfaces::get_if_name(checking_if_index) << " not found");
            return false;
        }
    } else if (interface_type == IT_DOWNSTREAM) {
        auto dinfo_it = m_p->m_downstreams.find(checking_if_index);
        if (dinfo_it != m_p->m_downstreams.end()) {
            interf = dinfo_it->second.m_interface;
        } else {
            HC_LOG_ERROR("downstream interface " << interfaces::get_if_name(checking_if_index) << " not found");
            return false;
        }
    } else {
        HC_LOG_ERROR("unkown interface type");
        return false;
    }

    if (interf == nullptr) {
        HC_LOG_ERROR("interface rule_binding of interface " << interfaces::get_if_name(checking_if_index) << " not found");
        return false;
    }

    std::string input_if_index_name = interfaces::get_if_name(input_if_index);
    if (!input_if_index_name.empty()) {
        if (interface_direction == ID_IN) {
            return interf->match_input_filter(input_if_index_name, gaddr, saddr);
        } else if (interface_direction == ID_OUT) {
            return interf->match_output_filter(input_if_index_name, gaddr, saddr);
        } else {
            HC_LOG_ERROR("unkown interface direction");
            return false;
        }
    } else {
        HC_LOG_ERROR("failed to map interface index " << input_if_index << " to interface name");
        return false;
    }
}

std::string simple_mc_proxy_routing::to_string() const
{
    HC_LOG_TRACE("");
    return m_data.to_string();
}

