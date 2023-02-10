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
    uint32_t requests_per_second = 50;
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
        "requests_per_second", 
        "Requests per second (default: 50)", 
        requests_per_second
    );
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

    address.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ip_switch_switch = address.Assign(switch_switch);

    address.SetBase("10.0.1.0", "255.255.255.0");
    std::vector<Ipv4InterfaceContainer> ip_switch_worker_vector;

    for (uint32_t i = 0; i < num_workers; ++i) {
        NetDeviceContainer switch_worker = switch_worker_vector[i];
        Ipv4InterfaceContainer ip_switch_worker = address.Assign(switch_worker);
        ip_switch_worker_vector.push_back(ip_switch_worker);
    }

    NS_LOG_INFO("Routing packets from the requester to the workers...");
    // TODO

    // Enable logging for the requester's switch
    if (verbose) {
        NS_LOG_INFO("Enabling logging...");
        // TODO: add logging for left-most switch 
        // Levels: LOG_LEVEL_INFO, LOG_PREFIX_FUNC, LOG_PREFIX_TIME
    }

    // Enable tracing across the middle link
    if (tracing) {
        NS_LOG_INFO("Enabling tracing...");
        large_link.EnablePcapAll("incast");
        // AsciiTraceHelper ascii;
        // large_link.EnableAsciiAll(ascii.CreateFileStream ("traces/incast.tr"));
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