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
 #define BO 4 //assigned backoff
 #define txDUR 5 //last Tx duration



using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("wlanFirstExample");

struct sim_config{
  uint32_t nWifi;
  Time lastReport;
  bool tracing;
  bool verbose;
  double totalSimtime;
  double startClientApp;
  ApplicationContainer servers; //All the applications in the simulation
  uint32_t payload;
  NodeContainer *nodes;
  bool ECA;
  uint32_t CWmin;
  bool hysteresis;
  bool fairShare;
  bool bitmap;
  bool srConservative;
  uint32_t srActivationThreshold;
  bool srResetMode;
  uint32_t EIFSnoDIFS;
  uint32_t ackTimeout;
};
struct sim_config config;

struct sim_results{
  uint32_t stas;
  uint64_t sxTx;
  uint64_t failTx;
  uint64_t txAttempts;
  uint64_t lastCollision;
  uint32_t srAttempts;
  uint32_t srReductions;
  uint32_t srFails;
};
struct sim_results results;


void
printResults(struct sim_config &config, Ptr<OutputStreamWrapper> stream, double &startClientApp, struct sim_results *results)
{
  Time simulationDuration = Simulator::Now() - Seconds(startClientApp);

  // //Count all packets received by the server AP
  double rx_bits = 0.0;
  double total_rx_packets = 0.0;

  // Aggregated stats from the DcaTxop of each node
  double col = results->failTx; 
  double attempts = results->txAttempts;
  double sx = results->sxTx;

  for (uint32_t i = 0; i < config.nWifi-1; i++)
    {
    uint64_t rx_packets;
    rx_packets = DynamicCast<UdpServer> (config.servers.Get (i))->GetReceived ();
    total_rx_packets += rx_packets;
    rx_bits += (rx_packets * config.payload * 8.0);
    // std::cout << i << " " << rx_packets << " " << config.servers.GetN() << std::endl;
    }

  std::cout << "\n\n***RESULTS***" << std::endl;
  std::cout << "-Number of STAs: " << results->stas << std::endl;
  std::cout << "-Total Simulation time (s): " << simulationDuration.GetSeconds () << std::endl;
  std::cout << "-Total Throughput: " << rx_bits / simulationDuration.GetSeconds() << std::endl;
  std::cout << "-Total received datagrams: " << total_rx_packets << std::endl;
  double colFrac = 0;
  if (col > 0) colFrac = col/attempts;
  std::cout << "-Fraction of tx resulting in collisions: " << colFrac << ". " << attempts << " attempts" << std::endl;
  std::cout << "\t-Last collision: " << results->lastCollision * 1e-6 << " secs.\n" << std::endl;
  uint64_t index = attempts - (col + sx);
  if (index != 0)
    std::cout << "\tSomething is wrong with couting the results of tx attempts (" << index << "): failTx: " << results->failTx 
      << " " << std::endl;

  if (config.bitmap)
    {
      if (results->srReductions + results->srFails == results->srAttempts)
        std::cout << "-Schedule Reset reductions: " << results->srReductions << std::endl;
    }

  *stream->GetStream() << config.nWifi << " " << std::fixed << std::setprecision(6)
    << rx_bits / simulationDuration.GetSeconds() << " " << col/attempts << std::endl;
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
TraceFailures(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << FAILTX << " " << newValue << std::endl;
  results->failTx++;
  results->lastCollision = Simulator::Now().GetMicroSeconds();
}

void
TraceSuccesses(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << SXTX << " " << newValue << std::endl; 
  results->sxTx++;
}

void
TraceTxAttempts(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << TX << " " << newValue << std::endl; 
  results->txAttempts++;
}

void
TraceAssignedBackoff(Ptr<OutputStreamWrapper> stream, std::string context, uint32_t oldValue, uint32_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  if(newValue != 0xFFFFFFFF)
    *stream->GetStream () << m_now << " " << context << " " << BO << " " << newValue << std::endl; 
}

void
TraceLastTxDuration(Ptr<OutputStreamWrapper> stream, std::string context, uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  if(newValue != 0xFFFFFFFFFFFFFFFF)
    *stream->GetStream () << m_now << " " << context << " " << txDUR << " " << newValue << std::endl; 
}

void
TraceEcaBitmap(Ptr<OutputStreamWrapper> stream, std::string context, std::vector<bool> *bmold, std::vector<bool> *bmnew)
{
  uint64_t m_now = Simulator::Now().GetNanoSeconds();

  if(bmnew)
    {
      *stream->GetStream () << m_now << " " << context << " " << bmnew->size() << " ";
      for (std::vector<bool>::iterator i = bmnew->begin(); i != bmnew->end(); i++)
        {
          uint32_t slot = 0;
          if (*i == true)
            slot = 1;
          *stream->GetStream () << slot;
        }
      *stream->GetStream () << std::endl;
    }
}

void
TraceSrAttempts(Ptr<OutputStreamWrapper> stream, struct sim_results *results, 
  std::string context, uint32_t oldValue, uint32_t newValue)
{
  results->srAttempts++;
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << TX << " " << newValue << std::endl;
}

void
TraceSrReductions(Ptr<OutputStreamWrapper> stream, struct sim_results *results, 
  std::string context, uint32_t oldValue, uint32_t newValue)
{
  results->srReductions++;
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << SXTX << " " << newValue << std::endl;
}

void
TraceSrFails(Ptr<OutputStreamWrapper> stream, struct sim_results *results, 
  std::string context, uint32_t oldValue, uint32_t newValue)
{
  results->srFails++;
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << FAILTX << " " << newValue << std::endl;
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
  for(uint32_t i = 0; i < allNodes->GetN ()-1; i++){
    for(uint32_t j = 0; j < allNodes->GetN ()-1; j++){
      if(i == j)
        continue;

      /* Configuring the ARP entries to avoid control traffic */
      Address mac = allNodes->Get (j)->GetDevice (0)->GetAddress ();
        /* Ipv4 is the forwarding table class */
      Ipv4Address ip = allNodes->Get (j)->GetObject<Ipv4> ()->GetAddress (1,0).GetLocal ();
      Ptr<ArpCache> arpCache = allNodes->Get (i)->GetObject<Ipv4L3Protocol> ()->GetInterface (1)->GetArpCache ();

      if (arpCache == NULL)
        arpCache = CreateObject<ArpCache>( );
      arpCache->SetAliveTimeout (Seconds (config.totalSimtime + config.startClientApp));
      ArpCache::Entry *entry = arpCache->Add (ip);
      entry->MarkWaitReply(0);
      entry->MarkAlive(mac);
    }

    if (config.ECA == true)
      {
        /* Resetting node's state and stats */
        Ptr<DcfManager> dcfManager = allNodes->Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()
          ->GetMac ()->GetObject<RegularWifiMac> ()->GetDcfManager ();
        Ptr<DcaTxop> dca = allNodes->Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()
          ->GetMac ()->GetObject<RegularWifiMac> ()->GetDcaTxop ();
        Ptr<WifiMac> wifiMac = allNodes->Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()
          ->GetMac ();

        dca->ResetStats ();
        dcfManager->SetEnvironmentForECA (config.hysteresis, config.bitmap);      

        if (config.CWmin > 0)
          dca->SetMinCw(config.CWmin);

        /* Setting the Ack timeout and EIFS no DIFS to be equal to DIFS */
        if (config.EIFSnoDIFS > 0)
          dcfManager->SetEifsNoDifs (MicroSeconds (config.EIFSnoDIFS));
        if (config.ackTimeout > 0)
          wifiMac->SetAckTimeout (MicroSeconds (config.ackTimeout));

        
        /* Setting all the nasty stuff for Schedule Reset */      
        if (config.bitmap == true)
          {
            dca->SetScheduleResetActivationThreshold (config.srActivationThreshold);
            if (config.srConservative)
              dca->SetScheduleConservative ();
            if (config.srResetMode)
              dca->SetScheduleResetMode ();
          }
    }


    /* Setting the BER
    Ptr<YanWifiPhy> phy = allNodes->Get(i)->GetDevice(0)->GetObject<WifiNetDevice>()->
      GetPhy()->GetObject<YansWifiPhy>();
    phy->SetFrameMinBer(config.frameMinBer);*/
  }

  std::cout << "\n###Simulation parameters" << std::endl;
  std::cout << "-Number of STAs: " << config.nWifi-1 << std::endl;
  std::cout << "-Simulation time: " << config.totalSimtime << std::endl;
  std::cout << "-Tracing: " << config.tracing << std::endl;
  std::cout << "-Verbose: " << config.verbose << std::endl;
  std::cout << "-ECA: " << config.ECA << std::endl;
  std::cout << "-hysteresis: " << config.hysteresis << std::endl;
  std::cout << "-fairShare: " << config.fairShare << std::endl;
  std::cout << "-bitmap: " << config.bitmap << std::endl;
  std::cout << "-srConservative: " << config.srConservative << std::endl;
  std::cout << "-srActivationThreshold: " << config.srActivationThreshold << std::endl;
  std::cout << "-srResetMode: " << config.srResetMode << std::endl;

  std::cout << "\n**WiFi protocol details:" << std::endl;
  Ptr<WifiMac> apMac = allNodes->Get (0)->GetDevice (0)->GetObject<WifiNetDevice> ()
          ->GetMac ();
  Ptr<WifiMac> staMac = allNodes->Get (1)->GetDevice (0)->GetObject<WifiNetDevice> ()
          ->GetMac ();
  std::cout << "-Slot duration: " << staMac->GetSlot ().GetMicroSeconds () << std::endl;
  std::cout << "\t-For the Ap: " << apMac->GetSlot ().GetMicroSeconds () << std::endl;
  std::cout << "-SIFS: " << staMac->GetSifs ().GetMicroSeconds () << std::endl;
  std::cout << "\t-For the Ap: " << apMac->GetSifs ().GetMicroSeconds () << std::endl;
  std::cout << "-EIFSnoDIFS: " << staMac->GetEifsNoDifs ().GetMicroSeconds () << std::endl;
  std::cout << "\t-For the Ap: " << apMac->GetEifsNoDifs ().GetMicroSeconds () << std::endl;
  std::cout << "-Ack timeout: " << staMac->GetAckTimeout  ().GetMicroSeconds () << std::endl;
  std::cout << "\t-For the Ap: " << apMac->GetAckTimeout  ().GetMicroSeconds () << std::endl;



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
  std::string backoffLog ("detBackoff.log");
  std::string bitmapLog ("bitmap.log");
  std::string srLog ("sr.log");
  double totalSimtime = 10; //in seconds
  double startClientApp = 2;
  int32_t seed = -1;
  bool ECA = false;
  uint32_t CWmin = 0;
  bool hysteresis = false;
  bool fairShare = false;
  bool bitmap = false;
  bool srConservative = false;
  uint32_t srActivationThreshold = 1;
  bool srResetMode = false;
  uint32_t EIFSnoDIFS = -1; //µs
  uint32_t ackTimeout = -1; //µs


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
  cmd.AddValue ("fairShare", "Do we aggregate according to hysteresis?", fairShare);
  cmd.AddValue ("bitmap", "Are we using Schedule Reset?", bitmap);
  cmd.AddValue ("srConservative", "Adjusts the number of iterations for building Schedule Reset bitmap", srConservative);
  cmd.AddValue ("srActivationThreshold", "After this many consecutive successfull transmissions, SR is activated", srActivationThreshold);
  cmd.AddValue ("srResetMode", "By default, schedules will be halved. Set true for Schedule Reset", srResetMode);
  cmd.AddValue ("EIFSnoDIFS", "IFS before retransmitting a frame", EIFSnoDIFS);
  cmd.AddValue ("AckTimeout", "Time that will timeout the DATA+ACK exchange", ackTimeout);

  cmd.Parse (argc,argv);

  config.nWifi = nWifi;
  config.lastReport = ns3::Time (MicroSeconds (0));
  config.tracing = tracing;
  config.verbose = verbose;
  config.totalSimtime = totalSimtime;
  config.startClientApp = startClientApp;
  config.payload = payload;
  config.nodes = new NodeContainer();
  config.ECA = ECA;
  config.CWmin = CWmin;
  config.hysteresis = hysteresis;
  config.fairShare = fairShare;
  config.bitmap = bitmap;
  config.srConservative = srConservative;
  config.srActivationThreshold = srActivationThreshold;
  config.srResetMode = srResetMode;
  config.EIFSnoDIFS = EIFSnoDIFS;
  config.ackTimeout = ackTimeout;

  results.sxTx = 0;
  results.failTx = 0;
  results.txAttempts = 0;
  results.srAttempts = 0;
  results.srFails = 0;
  results.srReductions = 0;
  results.stas = nWifi - 1;


  // Check for valid number of csma or wifi nodes
  // 250 should be enough, otherwise IP addresses 
  // soon become an issue
  if (nWifi > 250)
  {
    std::cout << "Too many wifi csma nodes, no more than 250." << std::endl;
    return 1;
  }

  //Creating the log streams
  AsciiTraceHelper asciiTraceHelper;
  Ptr<OutputStreamWrapper> debug_log = asciiTraceHelper.CreateFileStream (logName);
  Ptr<OutputStreamWrapper> results_stream = asciiTraceHelper.CreateFileStream (resultsName, __gnu_cxx::ios_base::app);
  Ptr<OutputStreamWrapper> tx_stream = asciiTraceHelper.CreateFileStream (txLog);
  Ptr<OutputStreamWrapper> backoff_stream = asciiTraceHelper.CreateFileStream (backoffLog);
  Ptr<OutputStreamWrapper> bitmap_stream = asciiTraceHelper.CreateFileStream (bitmapLog);
  Ptr<OutputStreamWrapper> sr_stream = asciiTraceHelper.CreateFileStream (srLog);

  //Setting simulation seed
  if(seed >= 0)
    RngSeedManager::SetSeed (seed);

  NodeContainer wifiStaNodes;
  wifiStaNodes.Create (nWifi-1);	

  NodeContainer wifiApNode;
  wifiApNode.Create (1);

  //first node of allNodes container is the AP.

  NodeContainer allNodes;
  allNodes.Add (wifiStaNodes);
  allNodes.Add (wifiApNode);
  config.nodes->Add (allNodes);

   //Checking the containers have the same number of nodes
  if(wifiStaNodes.GetN() + wifiApNode.GetN() != allNodes.GetN())
    {
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
                                "DataMode", StringValue (dataRate),
                                "AckMode", StringValue (dataRate),
																"ControlMode", StringValue (controlRate));

  Ssid ssid = Ssid ("wlan-test");
  NqosWifiMacHelper clientMac = NqosWifiMacHelper::Default ();
  NqosWifiMacHelper apMac = NqosWifiMacHelper::Default ();
  
  clientMac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));

  apMac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid),
               "BeaconGeneration", BooleanValue (true));

  NetDeviceContainer staDevices;
  staDevices = wifi.Install (phy, clientMac, wifiStaNodes);

  NetDeviceContainer apDevices;
  apDevices = wifi.Install (phy, apMac, wifiApNode);

  NetDeviceContainer allDevices;
  allDevices.Add (apDevices);
  allDevices.Add (staDevices);
  

  MobilityHelper mobility;
  //Create a pointer to a list of positions, required by SetPositionAllocator(Ptr<positionAllocator> alloc)
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  /* AP in (0,0), all the clients in (2,0)
   * In this way we are sure the channel is perferct (no error)
   * Propagation delay between clients = 0
   */
  Vector apPos = Vector(0.0, 0.0, 0.0);
  Vector staPos = Vector(2.0, 0.0, 0.0);
  
  positionAlloc->Add (Vector(apPos));
  //Filling in the position of every sta node.
  //Remember node-0 is the AP was already added.
  for(uint32_t i = 1; i < nWifi; i++)
  {
		positionAlloc->Add (Vector (staPos));
  }
  mobility.SetPositionAllocator (positionAlloc);
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


  //Configuring tracing at the AP now that we configured L2 elements
  phy.SetPcapDataLinkType(YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 
  phy.EnablePcap("cap", apDevices.Get(0), true);


  //Configuring the Application
  Ipv4Address ipDestination = interfaces.GetAddress(0);   
  if(verbose == true){
    *debug_log->GetStream() << "Destination: " << ipDestination << std::endl;
  }
 
  for(uint32_t i = 0; i < allNodes.GetN () -1; i++){
		//Creating a server for each client
		UdpServerHelper server (destPort + i);
		ApplicationContainer serverApp = server.Install (wifiApNode.Get (0));
		serverApp.Start (Seconds (startClientApp-1.0));
		serverApp.Stop (Seconds (startClientApp + totalSimtime));
    //Adding serverApp for all clients into a container for processing
    config.servers.Add(serverApp);

		UdpClientHelper client (ipDestination, destPort + i);
    Time intervalTest = Seconds((payload * 8.0) / (txRate * 1e6));
    // uint64_t maxP = 4294967295;
		client.SetAttribute ("MaxPackets", UintegerValue (4294967295u));
    client.SetAttribute ("Interval", TimeValue (intervalTest));
		client.SetAttribute ("PacketSize", UintegerValue (payload));

		ApplicationContainer clientApp = client.Install (wifiStaNodes.Get (i));
		clientApp.Start(Seconds (startClientApp));
		clientApp.Stop(Seconds (startClientApp + totalSimtime));

    std::cout << "-Setting UDP flow " << i << "/" << nWifi-1 << " to AP " << ipDestination
      << ", node: " << wifiStaNodes.Get (i)->GetId () << std::endl;
  }


 //Creating the pointers to trace sources for each node
  for(uint32_t i = 0; i < allNodes.GetN () - 1; i++)
    {
      std::ostringstream n;
      Ptr<DcaTxop> dca = allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()->GetMac ()->
        GetObject<RegularWifiMac> ()->GetDcaTxop ();
      Ptr<DcfManager> dcfManager = allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()->
        GetMac ()->GetObject<RegularWifiMac> ()->GetDcfManager ();

      if(tracing == true)
        {
          n << i;
          dca->TraceConnect ("TxFailures",n.str (), MakeBoundCallback (&TraceFailures, tx_stream, &results));
          dca->TraceConnect ("TxSuccesses", n.str (), MakeBoundCallback (&TraceSuccesses, tx_stream, &results));
          dca->TraceConnect ("TxAttempts", n.str (), MakeBoundCallback (&TraceTxAttempts, tx_stream, &results));
          dca->TraceConnect ("BackoffCounter", n.str (), MakeBoundCallback (&TraceAssignedBackoff, backoff_stream));
          dca->TraceConnect ("Bitmap", n.str (), MakeBoundCallback (&TraceEcaBitmap, bitmap_stream));
          dca->TraceConnect ("SrReductionAttempts", n.str (), MakeBoundCallback (&TraceSrAttempts, sr_stream, &results));
          dca->TraceConnect ("SrReductions", n.str (), MakeBoundCallback (&TraceSrReductions, sr_stream, &results));
          dca->TraceConnect ("SrReductionFailed", n.str (), MakeBoundCallback (&TraceSrFails, sr_stream, &results));

          dcfManager->TraceConnect ("LastTxDuration", n.str (), MakeBoundCallback (&TraceLastTxDuration, tx_stream));
        }
    }

  if (verbose == true)
    {
      LogComponentEnable ("UdpClient", LOG_LEVEL_DEBUG);
      LogComponentEnable ("UdpServer", LOG_LEVEL_DEBUG);
      LogComponentEnable ("DcfManager", LOG_LEVEL_DEBUG);
      LogComponentEnable ("DcaTxop", LOG_LEVEL_DEBUG);
      LogComponentEnable ("WifiRemoteStationManager", LOG_LEVEL_DEBUG);
      LogComponentEnable ("RegularWifiMac", LOG_LEVEL_DEBUG);
      LogComponentEnable ("MacLow", LOG_LEVEL_DEBUG);
    }

  Simulator::Stop (Seconds (startClientApp + totalSimtime));

  //Finilise setup
  Simulator::Schedule(Seconds(startClientApp - 0.000009), finaliseSetup, config);

  //Last call to the printResults function
  Simulator::Schedule(Seconds(startClientApp  + totalSimtime - 0.000009), printResults, 
    config, results_stream, startClientApp, &results);
  Simulator::Schedule(Seconds(startClientApp  + totalSimtime - 0.000009), processFinal, resultsName);
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
