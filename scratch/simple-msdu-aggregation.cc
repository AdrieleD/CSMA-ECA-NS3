/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 MIRKO BANCHI
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
 * Authors: Mirko Banchi <mk.banchi@gmail.com>
 *          Sébastien Deronne <sebastien.deronne@gmail.com>
 *          Luis Sanabria-Russo <luis.sanabria@upf.edu>
 */
#include <iomanip>
#include <iostream>
#include "ns3/assert.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/internet-module.h"

// This is a simple example in order to show how 802.11n MSDU aggregation feature works.
// The throughput is obtained for a given number of aggregated MSDUs.
//
// The number of aggregated MSDUs can be chosen by the user through the nMsdus attibute.
// A value of 1 means that no MSDU aggregation is performed.
//
// Example: ./waf --run "simple-msdu-aggregation --nMsdus=5"
//
// Network topology:
//
//   Wifi 192.168.1.0
//
//        AP
//   *    *
//   |    |
//   n1   n2
//
// Packets in this simulation aren't marked with a QosTag so they are considered
// belonging to BestEffort Access Class (AC_BE).

using namespace ns3;

struct sim_config
{
  uint32_t nWifi;
  uint64_t simulationTime;
  // uint32_t nMsdus;
  uint32_t payloadSize;
  NodeContainer staNodes;
  NodeContainer apNodes;
  NodeContainer allNodes;
  ApplicationContainer servers;
  bool eca;
  bool hysteresis;
  uint32_t stickiness;
  bool dynStick;
  bool bitmap;

  bool fairShare;
};
struct sim_config config;

void
finishSetup (struct sim_config &config)
{
  NodeContainer allNodes (config.allNodes);
  uint32_t Cwmin, Cwmax;
  Cwmin = 0;
  Cwmax = Cwmin;
  /* Last index in allNodes container is the AP */
  for(uint32_t i = 0; i < allNodes.GetN ()-1; i++){
    for(uint32_t j = 0; j < allNodes.GetN ()-1; j++){
      if(i == j)
        continue;

      /* Configuring the ARP entries to avoid control traffic */
      Address mac = allNodes.Get (j)->GetDevice (0)->GetAddress ();
      /* Ipv4 is the forwarding table class */
      Ipv4Address ip = allNodes.Get (j)->GetObject<Ipv4> ()->GetAddress (1,0).GetLocal ();
      Ptr<ArpCache> arpCache = allNodes.Get (i)->GetObject<Ipv4L3Protocol> ()->GetInterface (1)->GetArpCache ();

      if (arpCache == NULL)
        arpCache = CreateObject<ArpCache>( );
      arpCache->SetAliveTimeout (Seconds (config.simulationTime + 1));
      ArpCache::Entry *entry = arpCache->Add (ip);
      entry->MarkWaitReply(0);
      entry->MarkAlive(mac);
    }

    /* Configuring the clients */
    Ptr<EdcaTxopN> dca = allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()->
      GetMac ()->GetObject<RegularWifiMac> ()->GetBEQueue ();
    Ptr<DcfManager> manager = allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()->
      GetMac ()->GetObject<RegularWifiMac> ()->GetDcfManager ();

    Cwmin = dca->GetMinCw ();
    Cwmax = dca->GetMaxCw ();

    if (config.eca)
    {
      dca->ResetStats ();
      manager->SetEnvironmentForECA (config.hysteresis, config.bitmap,
        config.stickiness, config.dynStick);
      if (config.fairShare)
        dca->SetFairShare ();
    }

  }
  std::cout << "- Cwmin: " << Cwmin << std::endl;
  std::cout << "- Cwmax: " << Cwmax << std::endl;
  std::cout << "- FairShare: " << config.fairShare << std::endl;
}

void
results (struct sim_config &config, Ptr<OutputStreamWrapper> stream)
{
  double throughput = 0.0;
  NS_ASSERT (config.servers.GetN () == config.nWifi);

  std::cout << "- Results for " << config.servers.GetN () << " clients." << std::endl;

  for (uint32_t i = 0; i < config.servers.GetN (); i++)
    {
      uint32_t totalPacketsThrough = DynamicCast<UdpServer> (config.servers.Get (i))->GetReceived ();
      throughput += totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0);
    }
  std::cout << "Throughput: " << throughput << " Mbit/s" << '\n';

  /* Writing to file */
  *stream->GetStream() << config.nWifi << " " << std::fixed << std::setprecision(6) << throughput << std::endl;
}

void
process (std::string &dataFileName)
{
  std::string prefix("(cd ~/Dropbox/PhD/Research/NS3/ns-allinone-3.24.1/bake/source/ns-3.24/tmp2 && ./process ");
  std::string command = prefix + dataFileName + ")";
  system(command.c_str());
}



NS_LOG_COMPONENT_DEFINE ("SimpleMsduAggregation");

int main (int argc, char *argv[])
{
  uint32_t nWifi = 2;
  uint32_t payloadSize = 1470; //bytes
  uint64_t simulationTime = 3; //seconds
  bool enableRts = false;
  uint32_t txRate = 260;
  uint32_t destPort = 10000;
  Time dataGenerationRate = Seconds ((payloadSize*8) / (txRate * 1e6));
  std::string dataRate ("HtMcs7");
  std::string controlRate ("HtMcs7");
  std::string ackMode ("HtMcs7");
  bool verbose = false;
  bool eca = true;
  bool hysteresis = false;
  uint32_t stickiness = 0;
  bool fairShare = false;
  bool bitmap = false;
  bool dynStick = false;
  int32_t seed = -1;

  std::string resultsName ("results2.log");

  CommandLine cmd;
  // cmd.AddValue ("nMsdus", "Number of aggregated MSDUs", nMsdus); //number of aggregated MSDUs specified by the user
  cmd.AddValue ("payloadSize", "Payload size in bytes", payloadSize);
  cmd.AddValue ("enableRts", "Enable RTS/CTS", enableRts);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("eca", "CSMA/ECA", eca);
  cmd.AddValue ("hysteresis", "Hysteresis", hysteresis);
  cmd.AddValue ("stickiness", "Stickiness", stickiness);
  cmd.AddValue ("fairShare", "Fair Share", fairShare);
  cmd.AddValue ("bitmap", "Bitmap activation", bitmap);
  cmd.AddValue ("dynStick", "Dynamic stickiness", dynStick);
  cmd.AddValue ("verbose", "Logging", verbose);
  cmd.AddValue ("seed", "RNG simulation seed", seed);
  cmd.AddValue ("nWifi", "Number of Wifi clients", nWifi);

  cmd.Parse (argc, argv);

  config.nWifi = nWifi;
  config.simulationTime = simulationTime;
  config.payloadSize = payloadSize;
  config.eca = eca;
  config.hysteresis = hysteresis;
  config.stickiness = stickiness;
  config.fairShare = fairShare;
  config.bitmap = bitmap;
  config.dynStick = dynStick;

  if (!enableRts)
    {
      Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("999999"));
    }
  else
    {
      Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("0"));
    }

  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("990000"));

  //Setting simulation seed
  if(seed >= 0)
    RngSeedManager::SetSeed (seed);

  /* Creating the log streams */
  AsciiTraceHelper asciiTraceHelper;
  Ptr<OutputStreamWrapper> results_stream = asciiTraceHelper.CreateFileStream (resultsName, __gnu_cxx::ios_base::app);

  NodeContainer wifiStaNode;
  wifiStaNode.Create (nWifi);

  NodeContainer wifiApNode;
  wifiApNode.Create (1);

  config.staNodes.Add (wifiStaNode);
  config.apNodes.Add (wifiApNode);
  config.allNodes.Add (wifiStaNode);
  config.allNodes.Add (wifiApNode);


  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy = YansWifiPhyHelper::Default ();
  phy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO);
  phy.SetChannel (channel.Create ());

  WifiHelper wifi = WifiHelper::Default ();
  wifi.SetStandard (WIFI_PHY_STANDARD_80211n_5GHZ);
  
  /*
   * According to wifi-phy.cc
   * HtMcs7: 20 MHz, 72.2  Mbps
   * HtMcs0: 20 MHz, 7.2 Mbps
  */

  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (dataRate), "ControlMode", StringValue (controlRate));
  HtWifiMacHelper mac = HtWifiMacHelper::Default ();

  Ssid ssid = Ssid ("simple-msdu-aggregation");
  mac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));

  mac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator");

  NetDeviceContainer staDevice;
  staDevice = wifi.Install (phy, mac, wifiStaNode);

  mac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid));

  mac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator");

  NetDeviceContainer apDevice;
  apDevice = wifi.Install (phy, mac, wifiApNode);
  NetDeviceContainer allDevices;
  allDevices.Add (apDevice);
  allDevices.Add (staDevice);

  /* Setting mobility model */
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();

  Vector apPos = Vector (0.0, 0.0, 0.0);
  Vector staPos = Vector (1.0, 0.0, 0.0);
  positionAlloc->Add (apPos);
  for (uint32_t i = 0; i < wifiStaNode.GetN(); i++)
    {
      positionAlloc->Add (staPos);
    }
  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);
  mobility.Install (wifiStaNode);

  /* Internet stack*/
  InternetStackHelper stack;
  stack.Install (wifiApNode);
  stack.Install (wifiStaNode);

  Ipv4AddressHelper address;

  address.SetBase ("192.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ApInterface;
  ApInterface = address.Assign (apDevice);
  Ipv4InterfaceContainer StaInterface;
  StaInterface = address.Assign (staDevice);
  //Setting up routing, given that is a type of csma network.
  //Helper creates tables and populates them. Forget about routing.
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  /* Setting applications */
  for (uint32_t i = 0; i < wifiStaNode.GetN (); i++)
    {
      UdpServerHelper myServer (destPort + i);
      ApplicationContainer serverApp = myServer.Install (wifiApNode.Get (0));
      serverApp.Start (Seconds (0.0));
      serverApp.Stop (Seconds (simulationTime + 1));
      config.servers.Add (serverApp);

      UdpClientHelper myClient (ApInterface.GetAddress (0), destPort + i);
      myClient.SetAttribute ("MaxPackets", UintegerValue (4294967295u));
      myClient.SetAttribute ("Interval", TimeValue (dataGenerationRate)); //packets/s
      myClient.SetAttribute ("PacketSize", UintegerValue (payloadSize));

      ApplicationContainer clientApp = myClient.Install (wifiStaNode.Get (i));
      clientApp.Start (Seconds (1.0));
      clientApp.Stop (Seconds (simulationTime + 1));
    }

  if (verbose)
    {
      LogComponentEnable ("EdcaTxopN", LOG_LEVEL_DEBUG);
    }

  Simulator::Stop (Seconds (simulationTime + 1));

  Simulator::Schedule (Seconds (0.999999), finishSetup, config);
  Simulator::Schedule (Seconds (simulationTime + 0.999999), results, config, results_stream);
  Simulator::Schedule (Seconds (simulationTime + 0.999999), process, resultsName);

  Simulator::Run ();
  Simulator::Destroy ();

  

  return 0;
}
