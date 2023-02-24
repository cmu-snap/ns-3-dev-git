/*
 * Copyright (c) 2023 Carnegie Mellon University
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4.h"

/*
 * Incast Topology
 *
 * [requester] --1-- [ToR switch] ==2== [ToR switch] --1-- [workers]
 *
 *                    Link 1          Link 2
 * Testing            10 Mbps         100 Mbps
 * Experimenting      10-100 Gbps     100-400 Gbps
 */

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Incast");

int
main(int argc, char* argv[])
{
    // Initialize variables
    bool verbose = true;
    uint32_t num_workers = 50;
    uint32_t request_bytes = 50;
    uint32_t response_bytes = 15000;
    uint64_t rtt_ms = 200;
    // uint32_t requests_per_second = 50;
    bool experimenting = false;
    bool tracing = true;

    // Define command line arguments
    CommandLine cmd(__FILE__);
    cmd.AddValue(
        "verbose",
        "Enable logging at the requester's switch (default: true)",
        verbose
    );
    cmd.AddValue(
        "num_workers",
        "Number of worker nodes (default: 50)",
        num_workers
    );
    cmd.AddValue(
        "request_bytes",
        "Number of bytes sent from the requester to all workers (default: 50)",
        request_bytes
    );
    cmd.AddValue(
        "response_bytes",
        "Number of bytes sent from each worker to the requester (default: 1500)",
        response_bytes
    );
    // cmd.AddValue(
    //     "requests_per_second",
    //     "Requests per second (default: 50)",
    //     requests_per_second
    // );
    cmd.AddValue(
        "experimenting",
        "Use experimentation link parameters (default: false)",
        experimenting
    );
    cmd.AddValue(
        "tracing",
        "Enable pcap tracing (default: true)",
        tracing
    );
    cmd.Parse(argc, argv);

    // Configure link parameters
    StringValue small_rate = StringValue("10Mbps");
    TimeValue small_delay = TimeValue(NanoSeconds(0)); // TODO: reconfigure
    StringValue large_rate = StringValue("100Mbps");
    TimeValue large_delay = TimeValue(NanoSeconds(0)); // TODO: reconfigure

    if (experimenting) {
        small_rate = StringValue("10Gbps");
        large_rate = StringValue("100Gbps");
    }

    NS_LOG_INFO("Building incast topology...");

    // Create nodes
    NodeContainer requester;
    requester.Create(1);

    NodeContainer switches;
    switches.Create(2);

    NodeContainer workers;
    workers.Create(num_workers);

    // Create links
    PointToPointHelper small_link;
    small_link.SetDeviceAttribute("DataRate", small_rate);
    small_link.SetChannelAttribute("Delay", small_delay);

    PointToPointHelper large_link;
    large_link.SetDeviceAttribute("DataRate", large_rate);
    large_link.SetChannelAttribute("Delay", large_delay);

    // Connect nodes
    NetDeviceContainer requester_switch;
    requester_switch = small_link.Install(requester.Get(0), switches.Get(0));

    NetDeviceContainer switch_switch;
    switch_switch = large_link.Install(switches.Get(0), switches.Get(1));

    std::vector<NetDeviceContainer> switch_worker_vector;
    switch_worker_vector.reserve(num_workers);

    for (uint32_t i = 0; i < num_workers; ++i) {
        Ptr<Node> worker = workers.Get(i);
        NetDeviceContainer switch_worker = small_link.Install(switches.Get(1), worker);
        switch_worker_vector.push_back(switch_worker);
    }

    // Install TCP stack to all nodes
    InternetStackHelper stack;
    stack.InstallAll();

    NS_LOG_INFO("Assigning IP addresses...");

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ip_requester_switch = address.Assign(requester_switch);

    address.SetBase("10.2.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ip_switch_switch = address.Assign(switch_switch);

    address.SetBase("10.1.0.0", "255.255.255.0");
    std::vector<Ipv4InterfaceContainer> ip_switch_worker_vector;

    for (uint32_t i = 0; i < num_workers; ++i) {
        NetDeviceContainer switch_worker = switch_worker_vector[i];
        Ipv4InterfaceContainer ip_switch_worker = address.Assign(switch_worker);
        ip_switch_worker_vector.push_back(ip_switch_worker);
        address.NewNetwork();
    }

    NS_LOG_INFO("Sending packets to all workers...");

    // Send packets from the requester to the workers
    ApplicationContainer requester_source_apps;
    ApplicationContainer worker_sink_apps;
    std::vector<Ptr<PacketSink>> worker_sinks;
    worker_sinks.reserve(num_workers);

    OnOffHelper requester_source_helper("ns3::TcpSocketFactory", Address());
    requester_source_helper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=.5]"));
    requester_source_helper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=.5]"));
    requester_source_helper.SetAttribute("PacketSize", UintegerValue(request_bytes / num_workers));
    requester_source_helper.SetAttribute("MaxBytes", UintegerValue(request_bytes / num_workers));

    for (uint32_t i = 0; i < num_workers; ++i) {
        uint16_t port = 5000 + i;
        Ipv4Address worker_address = ip_switch_worker_vector[i].GetAddress(1);

        AddressValue remote_address(InetSocketAddress(worker_address, port));
        requester_source_helper.SetAttribute("Remote", remote_address);
        requester_source_apps.Add(requester_source_helper.Install(requester.Get(0)));
        requester_source_apps.Start(MilliSeconds(100 + rand() % 5));

        Address sink_address = InetSocketAddress(Ipv4Address::GetAny(), port);
        PacketSinkHelper sink_helper("ns3::TcpSocketFactory", sink_address);
        worker_sink_apps.Add(sink_helper.Install(workers.Get(i)));

        Ptr<PacketSink> worker_sink = worker_sink_apps.Get(0)->GetObject<PacketSink>();
        worker_sinks.push_back(worker_sink);
    }

    requester_source_apps.Stop(MilliSeconds(5000)); // TODO: replace time
    worker_sink_apps.Start(MilliSeconds(100));
    worker_sink_apps.Stop(MilliSeconds(5000)); // TODO: replace time

    NS_LOG_INFO("Sending packets back to the requester...");

    // Send packets from the workers to the requester
    ApplicationContainer worker_source_apps;
    ApplicationContainer requester_sink_apps;
    std::vector<Ptr<PacketSink>> requester_sinks;
    requester_sinks.reserve(num_workers);

    OnOffHelper worker_source_helper("ns3::TcpSocketFactory", Address());
    worker_source_helper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=.5]"));
    worker_source_helper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=1.]"));
    worker_source_helper.SetAttribute("PacketSize", UintegerValue(500));
    worker_source_helper.SetAttribute("MaxBytes", UintegerValue(0));
    worker_source_helper.SetAttribute("DataRate", large_rate);

    for (uint32_t i = 0; i < num_workers; ++i) {
        uint16_t port = 5000 + i;

        Ipv4Address requester_address = ip_requester_switch.GetAddress(0);
        AddressValue remote_address(InetSocketAddress(requester_address, port));
        worker_source_helper.SetAttribute("Remote", remote_address);
        worker_source_apps.Add(worker_source_helper.Install(workers.Get(i)));
        worker_source_apps.Start(MilliSeconds(100 + rtt_ms/2 + rand() % 5));

        Address sink_address = InetSocketAddress(Ipv4Address::GetAny(), port);
        PacketSinkHelper sink_helper("ns3::TcpSocketFactory", sink_address);
        requester_sink_apps.Add(sink_helper.Install(requester));

        Ptr<PacketSink> requester_sink = requester_sink_apps.Get(0)->GetObject<PacketSink>();
        requester_sinks.push_back(requester_sink);
    }

    worker_source_apps.Stop(MilliSeconds(5000)); // TODO: replace time
    requester_sink_apps.Start(MilliSeconds(100));
    requester_sink_apps.Stop(MilliSeconds(5000)); // TODO: replace time

    // Enable logging for the requester's switch
    if (verbose) {
        NS_LOG_INFO("Enabling logging...");
        // TODO: add logging for left-most switch
        // Levels: LOG_LEVEL_INFO, LOG_PREFIX_FUNC, LOG_PREFIX_TIME
    }

    // Enable tracing across the middle link
    if (tracing) {
        NS_LOG_INFO("Enabling tracing...");
        large_link.EnablePcap("scratch/traces/incast-onoff", 1, 1);
        // AsciiTraceHelper ascii;
        // large_link.EnableAsciiAll(ascii.CreateFileStream ("traces/incast.tr"));
        // TODO: disable tracing for most nodes
    }

    NS_LOG_INFO("Enabling static global routing...");

    // Initialize routing tables for all nodes
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    NS_LOG_INFO("Running simulation...");

    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("Done.");

    return 0;
}