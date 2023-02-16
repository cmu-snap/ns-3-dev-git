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
 *   Left(i)            Left()             Right()          Right(i)
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

    // Create links
    PointToPointHelper small_link;
    small_link.SetDeviceAttribute("DataRate", small_rate);
    small_link.SetChannelAttribute("Delay", small_delay);

    PointToPointHelper large_link;
    large_link.SetDeviceAttribute("DataRate", large_rate);
    large_link.SetChannelAttribute("Delay", large_delay);

    PointToPointDumbbellHelper dumbbell_helper(1, small_link, num_workers, small_link, large_link);
    
    // Install TCP stack on all nodes
    InternetStackHelper stack;
    for (uint32_t i = 0; i < dumbbell_helper.LeftCount(); ++i)
    {
        stack.Install(dumbbell_helper.GetLeft(i));
    }
    for (uint32_t i = 0; i < dumbbell_helper.RightCount(); ++i)
    {
        stack.Install(dumbbell_helper.GetRight(i));
    }

    stack.Install(dumbbell_helper.GetLeft());
    stack.Install(dumbbell_helper.GetRight());

    NS_LOG_INFO("Assigning IP addresses...");

    // Assign IP Addresses
    dumbbell_helper.AssignIpv4Addresses(
        Ipv4AddressHelper("10.0.0.0", "255.255.255.0"),
        Ipv4AddressHelper("10.1.0.0", "255.255.255.0"),
        Ipv4AddressHelper("10.2.0.0", "255.255.255.0")
    );

    NS_LOG_INFO("Sending packets to all workers...");

    // Create sinks for all workers
    uint16_t port = 5000;
    Address sink_address(InetSocketAddress(Ipv4Address::GetAny(), port));
    PacketSinkHelper sink_helper("ns3::TcpSocketFactory", sink_address);
    ApplicationContainer sink_apps;

    for (uint32_t i = 0; i < dumbbell_helper.RightCount(); ++i) {
        sink_apps.Add(sink_helper.Install(dumbbell_helper.GetRight(i)));
    }
    sink_apps.Start(Seconds(0.0));
    sink_apps.Stop(Seconds(20.0)); // TODO: config burst time

    // Install on/off apps on the requester
    OnOffHelper source_helper("ns3::TcpSocketFactory", Address());
    source_helper.SetAttribute("OnTime", StringValue("ns3::UniformRandomVariable[Min=0.|Max=1.]"));
    source_helper.SetAttribute("OffTime", StringValue("ns3::UniformRandomVariable[Min=0.|Max=1.]"));
    ApplicationContainer source_apps;

    for (uint32_t i = 0; i < dumbbell_helper.RightCount(); ++i) {
        AddressValue remote_address(InetSocketAddress(dumbbell_helper.GetRightIpv4Address(i), port));
        source_helper.SetAttribute("Remote", remote_address);
        source_apps.Add(source_helper.Install(dumbbell_helper.GetLeft(0)));
        source_apps.Start(Seconds(1.0)); // TODO: add jitter
    }
    source_apps.Stop(Seconds(10.0)); // TODO: config burst time

    // TODO

    // Enable logging for the requester's switch
    if (verbose) {
        NS_LOG_INFO("Enabling logging...");
        // TODO: add logging for left switch 
        // Levels: LOG_LEVEL_INFO, LOG_PREFIX_FUNC, LOG_PREFIX_TIME
    }

    // Enable tracing across the middle link
    if (tracing) {
        NS_LOG_INFO("Enabling tracing...");
        large_link.EnablePcapAll("incast");
        AsciiTraceHelper ascii;
        large_link.EnableAsciiAll(ascii.CreateFileStream ("scratch/traces/incast.tr"));
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