/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 * Authors: Luis Sanabria-Russo <luis.sanabria@upf.edu>
 */
#include <iomanip>
#include <iostream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
 #include "ns3/ipv4-global-routing-helper.h"

//Just a simple WLAN with address base 10.0.0.1/24 and one AP.

//Defining log codes for interesting metrics
 #define SXTX 1 //successes
 #define FAILTX 2 //failures
 #define TX 3 //attempts



using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("wlanFirstExample");

struct sim_config{
  uint32_t nWifi;
  Time lastReport;
  double totalSimtime;
  ApplicationContainer servers; //All the applications in the simulation
  uint32_t payload;
  NodeContainer *nodes;
  bool ECA;
  uint32_t CWmin;
  bool hysteresis;

};
struct sim_config config;


void
printResults(struct sim_config &config, Ptr<OutputStreamWrapper> stream, double &startClientApp)
{
  Time simulationDuration = Simulator::Now() - Seconds(startClientApp);

  // //Count all packets received by the server AP
  double rx_bits = 0.0;
  double total_rx_packets = 0.0;

  for(uint32_t i = 0; i < config.nWifi-1; i++){
    uint64_t rx_packets;
    rx_packets = DynamicCast<UdpServer>(config.servers.Get(i))->GetReceived();
    total_rx_packets += rx_packets;
    rx_bits += (rx_packets * config.payload * 8.0);
    std::cout << i << " " << rx_packets << " " << config.servers.GetN() << std::endl;
  }

  std::cout << "Total Throughput: " << rx_bits / simulationDuration.GetSeconds() << std::endl;
  std::cout << "Total received datagrams: " << total_rx_packets << std::endl;

  *stream->GetStream() << config.nWifi << " " << std::fixed << std::setprecision(6)
    << rx_bits / simulationDuration.GetSeconds() << std::endl;
}

void
processFinal(std::string &dataFileName)
{
  std::string prefix("(cd ~/Dropbox/PhD/Research/NS3/ns-allinone-3.24.1/bake/source/ns-3.24/tmp && ./process ");
  std::string command = prefix + dataFileName + ")";
  // std::cout << "Processing the output file: " << command << std::endl;
  system(command.c_str());
}

//Trace calbacks
void
TraceFailures(Ptr<OutputStreamWrapper> stream, std::string context, uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream() << m_now << " " << context << " " << FAILTX << " " << newValue << std::endl;
}

void
TraceSuccesses(Ptr<OutputStreamWrapper> stream, std::string context, uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream() << m_now << " " << context << " " << SXTX << " " << newValue << std::endl; 
}

void
TraceTxAttempts(Ptr<OutputStreamWrapper> stream, std::string context, uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream() << m_now << " " << context << " " << TX << " " << newValue << std::endl; 
}

/**
 * This is called 100ns before the actual simulation starts and it is used to
 * finalize the node setup and to reset the internal state of all the nodes.
 * In this way we are sure that the frames transmitted during the association
 * procedures will not affect simulation results
 */
void
finaliseSetup(struct sim_config &config)
{
  NodeContainer *allNodes (config.nodes);
  for(uint32_t i = 0; i < allNodes->GetN()-1; i++){
    for(uint32_t j = 0; j < allNodes->GetN()-1; j++){
      if(i == j)
        continue;

      /* Configuring the ARP entries to avoid control traffic */
      Address mac = allNodes->Get(j)->GetDevice(0)->GetAddress();
        /* Ipv4 is the forwarding table class */
      Ipv4Address ip = allNodes->Get(j)->GetObject<Ipv4>()->GetAddress(1,0).GetLocal();
      Ptr<ArpCache> arpCache = allNodes->Get(i)->GetObject<Ipv4L3Protocol>()->GetInterface(1)->GetArpCache();

      if(arpCache == NULL)
        arpCache = CreateObject<ArpCache>();
      arpCache->SetAliveTimeout(Seconds (config.totalSimtime + 1));
      ArpCache::Entry *entry = arpCache->Add(ip);
      entry->MarkWaitReply(0);
      entry->MarkAlive(mac);
    }

    /* Resetting node's state and stats */
    Ptr<DcfManager> dcfManager = allNodes->Get(i)->GetDevice(0)->GetObject<WifiNetDevice>()
      ->GetMac()->GetObject<RegularWifiMac>()->GetDcfManager();
    Ptr<DcaTxop> dca = allNodes->Get(i)->GetDevice(0)->GetObject<WifiNetDevice>()
      ->GetMac()->GetObject<RegularWifiMac>()->GetDcaTxop();
    dca->ResetStats();

    if(config.ECA){
      dcfManager->SetEnvironmentForECA(config.hysteresis);
            /* Contention windows */
      if(config.CWmin > 0)
        dca->SetMinCw(config.CWmin);
    }

    /* Setting the BER
    Ptr<YanWifiPhy> phy = allNodes->Get(i)->GetDevice(0)->GetObject<WifiNetDevice>()->
      GetPhy()->GetObject<YansWifiPhy>();
    phy->SetFrameMinBer(config.frameMinBer);*/
  }
}




int 
main (int argc, char *argv[])
{
  bool verbose = false;
  uint32_t nWifi = 2;
  bool tracing = false;
  std::string dataRate("ErpOfdmRate54Mbps");
  std::string controlRate("ErpOfdmRate54Mbps");
  uint16_t destPort = 10000;
  uint32_t payload = 1470; //in bytes
  double txRate = 54; //in Mbps
  std::string logName ("debug.log");
  std::string resultsName ("results.log");
  std::string txLog ("tx.log");
  double totalSimtime = 10; //in seconds
  double startClientApp = 2;
  int32_t seed = -1;
  bool ECA = false;
  uint32_t CWmin = 0;
  bool hysteresis = false;


  CommandLine cmd;
  cmd.AddValue ("nWifi", "Number of wifi STA devices", nWifi);
  cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);
  cmd.AddValue ("tracing", "Enable pcap tracing", tracing);
  cmd.AddValue ("payload", "Length of UDP load in bytes", payload);
  cmd.AddValue ("txRate", "Transmission rate in Mbps", txRate);
  cmd.AddValue ("totalSimtime", "Simulation time in seconds", totalSimtime);
  cmd.AddValue ("seed", "RNG simulation seed", seed);
  cmd.AddValue ("ECA", "CSMA/ECA", ECA);
  cmd.AddValue ("CWmin", "Minimum contention window", CWmin);
  cmd.AddValue ("hysteresis", "Do we keep the CurCW after a sxTx?", hysteresis);

  cmd.Parse (argc,argv);

  config.nWifi = nWifi;
  config.lastReport = ns3::Time(MicroSeconds(0));
  config.totalSimtime = totalSimtime;
  config.payload = payload;
  config.nodes = new NodeContainer();
  config.ECA = ECA;
  config.CWmin = CWmin;
  config.hysteresis = hysteresis;


  // Check for valid number of csma or wifi nodes
  // 250 should be enough, otherwise IP addresses 
  // soon become an issue
  if (nWifi > 250)
  {
    std::cout << "Too many wifi csma nodes, no more than 250." << std::endl;
    return 1;
  }

  if (verbose == true)
  {
    // LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
    // LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("DcfManager", LOG_LEVEL_DEBUG);
    LogComponentEnable("DcaTxop", LOG_LEVEL_FUNCTION);
  }

  //Creating the log streams
  AsciiTraceHelper asciiTraceHelper;
  Ptr<OutputStreamWrapper> debug_log = asciiTraceHelper.CreateFileStream (logName);
  Ptr<OutputStreamWrapper> results_stream = asciiTraceHelper.CreateFileStream (resultsName, __gnu_cxx::ios_base::app);
  Ptr<OutputStreamWrapper> tx_stream = asciiTraceHelper.CreateFileStream(txLog);

  //Setting simulation seed
  if(seed >= 0)
    RngSeedManager::SetSeed(seed);

  NodeContainer wifiStaNodes;
  wifiStaNodes.Create (nWifi-1);	
  //Setting node-0 as the AP
  NodeContainer wifiApNode;
  wifiApNode.Create(1);
  NodeContainer allNodes;
  allNodes.Add(wifiStaNodes);
  allNodes.Add(wifiApNode);
  config.nodes->Add(allNodes);

   //Checking the containers have the same number of nodes
  if(wifiStaNodes.GetN() + wifiApNode.GetN() != allNodes.GetN()){
    std::cout << "Emergency exit" << std::endl;
    return 1;
  }


  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy = YansWifiPhyHelper::Default ();
  phy.SetChannel (channel.Create ());

  WifiHelper wifi = WifiHelper::Default ();
  wifi.SetStandard (WIFI_PHY_STANDARD_80211g);
  //this wifi station manager uses the same rate for all packets. Even RTS
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
																"DataMode", StringValue(dataRate),
                                "AckMode", StringValue(dataRate),
																"ControlMode", StringValue(controlRate));


  Ssid ssid = Ssid("wlan-test");
  NqosWifiMacHelper clientMac = NqosWifiMacHelper::Default ();
  NqosWifiMacHelper apMac = NqosWifiMacHelper::Default ();
  clientMac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));

  NetDeviceContainer staDevices;
  staDevices = wifi.Install (phy, clientMac, wifiStaNodes);

  apMac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid),
               "BeaconGeneration", BooleanValue (true));

  NetDeviceContainer apDevices;
  apDevices = wifi.Install (phy, apMac, wifiApNode);

  NetDeviceContainer allDevices;
  allDevices.Add(apDevices);
  allDevices.Add(staDevices);
  

  MobilityHelper mobility;
  //Create a pointer to a list of positions, required by SetPositionAllocator(Ptr<positionAllocator> alloc)
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  /* AP in (0,0), all the clients in (2,0)
   * In this way we are sure the channel is perferct (no error)
   * Propagation delay between clients = 0
   */
  Vector apPos = Vector(0.0, 0.0, 0.0);
  Vector staPos = Vector(2.0, 0.0, 0.0);
  positionAlloc->Add(Vector(apPos));
  //Filling in the position of every sta node.
  //Remember node-0 is the AP was already added.
  for(uint32_t i = 1; i < nWifi; i++){
		positionAlloc->Add(Vector(staPos));
  }
  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);
  mobility.Install (wifiStaNodes);
  
  InternetStackHelper stack;
  stack.Install (wifiApNode);
  stack.Install (wifiStaNodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0", "0.0.0.1");
  Ipv4InterfaceContainer interfaces = address.Assign (allDevices);
 
  //Setting up routing, given that is a type of csma network.
  //Helper creates tables and populates them. Forget about routing.
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


  //Configuring tracing at the AP
  phy.SetPcapDataLinkType(YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 
  phy.EnablePcap("cap", apDevices.Get(0), true);


  //Configuring the Application
  //Ipv4Address ipDestination = apDevices.Get(0)->GetObject<Ipv4>()->GetAddress(1,0).GetLocal();
  Ipv4Address ipDestination = interfaces.GetAddress(0);   
  if(verbose == true){
    *debug_log->GetStream() << "Destination: " << ipDestination << std::endl;
  }
 
  for(uint32_t i = 0; i < nWifi-1; i++){
		//Creating a server for each client
		UdpServerHelper server (destPort + i);
		ApplicationContainer serverApp = server.Install(wifiApNode.Get(0));
		serverApp.Start(Seconds (startClientApp-1.0));
		serverApp.Stop(Seconds (totalSimtime + 1));
    //Adding serverApps for all clients into a container for processing
    config.servers.Add(serverApp);

		UdpClientHelper client (ipDestination, destPort + i);
    Time intervalTest = Seconds((payload * 8.0) / (txRate * 1e6));
    // uint64_t maxP = 4294967295;
		client.SetAttribute("MaxPackets", UintegerValue (1));
    client.SetAttribute("Interval", TimeValue(intervalTest));
		client.SetAttribute("PacketSize", UintegerValue(payload));

		ApplicationContainer clientApp = client.Install(wifiStaNodes.Get(i));
		clientApp.Start(Seconds (startClientApp));
		clientApp.Stop(Seconds (totalSimtime + 1));

    std::cout << "-Setting UDP flow " << i << "/" << nWifi-1 << " to AP " << ipDestination
      << ", node: " << wifiStaNodes.Get(i)->GetId() << std::endl;
  }


 //Creating the pointers to trace sources for each node
  for(uint32_t i = 0; i < allNodes.GetN()-1; i++){
    std::ostringstream n;
    Ptr<DcaTxop> dca = allNodes.Get(i)->GetDevice(0)->GetObject<WifiNetDevice>()->GetMac()->
      GetObject<RegularWifiMac>()->GetDcaTxop();
    if(tracing == true){
      n << i;
      dca->TraceConnect ("TxFailures",n.str(), MakeBoundCallback(&TraceFailures,tx_stream));
      dca->TraceConnect ("TxSuccesses", n.str(), MakeBoundCallback(&TraceSuccesses,tx_stream));
      dca->TraceConnect ("TxAttempts", n.str(), MakeBoundCallback(&TraceTxAttempts, tx_stream));
    }
  }


  //Configuring the MAC protocol
  // if(ECA){
  //   for(uint32_t i = 0; i < allNodes.GetN()-1; i++){
  //     allNodes.Get(i)->GetDevice(0)->GetObject<WifiNetDevice>()->GetMac()->
  //       GetObject<RegularWifiMac>()->ConfigureCw();
  //   }
  // }


  Simulator::Stop (Seconds (totalSimtime+1));
  //Last call to the printResults function
  Simulator::Schedule(Seconds(totalSimtime+0.999999), printResults, config, results_stream, startClientApp);
  Simulator::Schedule(Seconds(totalSimtime+0.999999), processFinal, txLog);
  Simulator::Schedule(Seconds(startClientApp-1.000001), finaliseSetup, config);

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
