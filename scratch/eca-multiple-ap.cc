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
 */

//
// Default network topology includes some number of AP nodes specified by
// the variable nWifis (defaults to two).  Off of each AP node, there are some
// number of STA nodes specified by the variable nStas (defaults to two).
// Each AP talks to its associated STA nodes.  There are bridge net devices
// on each AP node that bridge the whole thing into one network.
//
//      +-----+      +-----+            +-----+      +-----+
//      | STA |      | STA |            | STA |      | STA | 
//      +-----+      +-----+            +-----+      +-----+
//    192.168.0.2  192.168.0.3        192.168.0.5  192.168.0.6
//      --------     --------           --------     --------
//      WIFI STA     WIFI STA           WIFI STA     WIFI STA
//      --------     --------           --------     --------
//        ((*))       ((*))       |      ((*))        ((*))
//                                |
//              ((*))             |             ((*))
//             -------                         -------
//             WIFI AP   CSMA ========= CSMA   WIFI AP 
//             -------   ----           ----   -------
//             ##############           ##############
//                 BRIDGE                   BRIDGE
//             ##############           ############## 
//               192.168.0.1              192.168.0.4
//               +---------+              +---------+
//               | AP Node |              | AP Node |
//               +---------+              +---------+
//
// * Authors: Luis Sanabria-Russo <luis.sanabria@upf.edu>

#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/bridge-helper.h"
#include "ns3/assert.h"
#include <vector>
#include <stdint.h>
#include <sstream>
#include <fstream>


//Defining log codes for interesting metrics
#define SXTX 1 //successes
#define FAILTX 2 //failures
#define TX 3 //attempts
#define BO 4 //assigned backoff
#define txDUR 5 //last Tx duration
#define FSAGG 6 //# of frames aggregated
#define COLTX 7 //# Tx while channel busy

using namespace ns3;

struct sim_config
{
  uint32_t nWifis;
  uint32_t nStas;
  double deltaWifiX;
  bool elevenAc;
  uint64_t simulationTime;
  std::vector<ApplicationContainer> servers;
  uint32_t payloadSize;
  uint32_t channelWidth;
};

struct sim_config config;

struct sim_results{
  int nStas;
  int nWifis;
  std::vector< std::vector<uint64_t> > failTx;
  uint64_t lastFailure;
};
struct sim_results results;

void
finalResults (struct sim_config &config, Ptr<OutputStreamWrapper> stream, struct sim_results *results)
{
  NS_ASSERT (config.servers.size () == config.nWifis);
  for (uint32_t i = 0; i < config.nWifis; i++)
    {
      double throughput = 0.0;
      double totalFails = 0.0;
      std::cout << "\nResults for Wifi: " << i << std::endl;
      for (uint32_t j = 0; j < config.servers.at (i).GetN (); j++)
        {
          uint32_t totalPacketsThrough = DynamicCast<UdpServer> (config.servers.at (i).Get (j))->GetReceived ();
          throughput += totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0);
          totalFails += results->failTx.at (i).at (j);

          std::cout << "\tSta-" << j << ": " << totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0) 
            << " Mbps" << std::endl;
          std::cout << "\tFailures: " << results->failTx.at (i).at (j) << std::endl;
        }
        std::cout << "-Total Throughput: " << throughput << " Mbps" << std::endl;
        std::cout << "-Total Failures: " << totalFails << std::endl;
    }
}

void
TraceFailures(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue)
{
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos+2, context.length ());

  *stream->GetStream () << m_now << " " << wlan << " " << node << " " << FAILTX << " " << newValue << std::endl;
  
  results->failTx.at (std::stoi(wlan)).at (std::stoi (node)) ++;
  results->lastFailure = m_now;
}

int main (int argc, char *argv[])
{
  uint32_t nWifis = 1;
  uint32_t nStas = 2;
  bool sendIp = false;
  uint32_t channelWidth = 20;
  bool writeMobility = false;
  double deltaWifiX = 20.0;
  bool elevenAc = false;
  bool shortGuardInterval = true;
  uint32_t dataRateAc  = 8; // Vht mcs
  uint32_t controlRateAc = 8; // Vht mcs
  std::string dataRate ("HtMcs7");
  std::string controlRate ("HtMcs7");
  uint32_t nMsdus = 1;
  bool randomWalk = false;
  uint32_t payloadSize = 1470; //bytes
  bool enableRts = false;
  int32_t seed = -1;
  uint32_t destPort = 1000;
  uint64_t simulationTime = 3; //seconds
  uint32_t txRate = 83;
  Time dataGenerationRate = Seconds ((payloadSize*8) / (txRate * 1e6));
  bool verbose = false;

  std::string resultsName ("results3.log");
  std::string txLog ("tx.log");
  std::string backoffLog ("backoff.log");

  CommandLine cmd;
  cmd.AddValue ("nWifis", "Number of wifi networks", nWifis);
  cmd.AddValue ("nStas", "Number of stations per wifi network", nStas);
  cmd.AddValue ("SendIp", "Send Ipv4 or raw packets", sendIp);
  cmd.AddValue ("writeMobility", "Write mobility trace", writeMobility);
  cmd.AddValue ("deltaWifiX", "Separation between two nWifis", deltaWifiX);
  cmd.AddValue ("randomWalk", "Random walk of Stas", randomWalk);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("channelWidth", "channelWidth", channelWidth);
  cmd.AddValue ("elevenAc", "802.elevenAc MCS", elevenAc);
  cmd.AddValue ("seed", "RNG simulation seed", seed);
  cmd.AddValue ("verbose", "Logging", verbose);
  cmd.Parse (argc, argv);

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
  Ptr<OutputStreamWrapper> tx_stream = asciiTraceHelper.CreateFileStream (txLog);
  Ptr<OutputStreamWrapper> backoff_stream = asciiTraceHelper.CreateFileStream (backoffLog);

  NodeContainer backboneNodes;
  NetDeviceContainer backboneDevices;
  Ipv4InterfaceContainer backboneInterfaces;
  std::vector<NodeContainer> staNodes;
  std::vector<NetDeviceContainer> staDevices;
  std::vector<NetDeviceContainer> apDevices;
  std::vector<Ipv4InterfaceContainer> staInterfaces;
  std::vector<Ipv4InterfaceContainer> apInterfaces;
  std::vector<Ipv4InterfaceContainer> apBridgeInterfaces;
  std::vector<NodeContainer> allNodes;

  config.simulationTime = simulationTime;
  config.nWifis = nWifis;
  config.nStas = nStas;
  config.deltaWifiX = deltaWifiX;
  config.payloadSize = payloadSize;
  config.channelWidth = channelWidth;

  std::vector<uint64_t> zeroth;
  zeroth.assign (nStas+1, 0); // a zero vector for statistics. Ap + Stas

  results.failTx.assign (nWifis, zeroth);
  results.nWifis = nWifis;
  results.nStas = nStas;

  InternetStackHelper stack;
  CsmaHelper csma;
  Ipv4AddressHelper ip;
  ip.SetBase ("10.0.0.0", "255.0.0.0");

  /* Installing Csma capability on the bridges */
  backboneNodes.Create (nWifis);
  stack.Install (backboneNodes);

  backboneDevices = csma.Install (backboneNodes);

  double wifiX = 0.0;

  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();
  wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 

  //Setting up routing, given that is a type of csma network.
  //Helper creates tables and populates them. Forget about routing.
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();  

  for (uint32_t i = 0; i < nWifis; ++i)
    {
      // calculate ssid for wifi subnetwork
      std::ostringstream oss;
      oss << "wifi-default-" << i;
      Ssid ssid = Ssid (oss.str ());

      NodeContainer sta;
      NetDeviceContainer staDev;
      NetDeviceContainer apDev;
      Ipv4InterfaceContainer staInterface;
      Ipv4InterfaceContainer apInterface;
      Ipv4InterfaceContainer apBridgeInterface;
      ApplicationContainer servers;

      MobilityHelper mobility;
      BridgeHelper bridge;
      WifiHelper wifi = WifiHelper::Default ();

      /* PHY */
      std::cout << "Setting PHY" << std::endl;
      wifi.SetStandard (WIFI_PHY_STANDARD_80211n_5GHZ);
      if (elevenAc)
        {
          /* Set guard interval, PHY details and Vht rates */
          wifiPhy.Set ("ShortGuardEnabled", BooleanValue (shortGuardInterval));
          wifi.SetStandard (WIFI_PHY_STANDARD_80211ac);
          StringValue dataRate = VhtWifiMacHelper::DataRateForMcs (dataRateAc);
          StringValue controlRate = VhtWifiMacHelper::DataRateForMcs (controlRateAc);
        }
      
      /* MAC */
      wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (dataRate), 
                                                                    "ControlMode", StringValue (controlRate),
                                                                    "AckMode", StringValue (dataRate));

      VhtWifiMacHelper wifiMac = VhtWifiMacHelper::Default ();
      if (!elevenAc)
        HtWifiMacHelper wifiMac = HtWifiMacHelper::Default ();

      YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
      wifiPhy.SetChannel (wifiChannel.Create ());

      /* Mobility */
      mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                     "MinX", DoubleValue (wifiX),
                                     "MinY", DoubleValue (0.0),
                                     "DeltaX", DoubleValue (0.0),
                                     "DeltaY", DoubleValue (0.0),
                                     "GridWidth", UintegerValue (1),
                                     "LayoutType", StringValue ("RowFirst"));

      mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
      mobility.Install (backboneNodes.Get (i));

      /* Setting the Wifi Ap */
      wifiMac.SetType ("ns3::ApWifiMac",
                       "Ssid", SsidValue (ssid));

      wifiMac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator",
                                                  "MaxAmsduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MSDU aggregation for AC_BE with a maximum aggregated size of nMsdus*(payloadSize+100) bytes, i.e. nMsdus aggregated packets in an A-MSDU

      apDev = wifi.Install (wifiPhy, wifiMac, backboneNodes.Get (i));
      apInterface = ip.Assign (apDev);

      /* Setting the Bridge */
      NetDeviceContainer bridgeDev;
      bridgeDev = bridge.Install (backboneNodes.Get (i), NetDeviceContainer (apDev, backboneDevices.Get (i)));

      // assign AP IP address to bridge, not wifi
      apBridgeInterface = ip.Assign (bridgeDev);


      /*/////////////////////*/
      /* setup the wifi STAs */
      sta.Create (nStas);
      stack.Install (sta);
      
      /* Sta mobility model */
      if(randomWalk)
        {
          mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                                           "Mode", StringValue ("Time"),
                                           "Time", StringValue ("2s"),
                                           "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                                           "Bounds", RectangleValue (Rectangle (wifiX, wifiX+5.0,0.0, (nStas+1)*5.0)));
        }
      mobility.Install (sta);

      /* Looking at each Sta's position */
      std::cout << "\n***Wifi: " << i << std::endl;
      uint32_t n = 0;
      for (NodeContainer::Iterator j = sta.Begin (); j != sta.End (); j++)
        {
          Ptr<Node> object = *j;
          Ptr<MobilityModel> position = object->GetObject<MobilityModel> ();
          NS_ASSERT (position != 0);
          Vector pos = position->GetPosition ();
          std::cout << "Sta-" << n << ": x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z << std::endl;
          n++;
        }

      wifiMac.SetType ("ns3::StaWifiMac",
                       "Ssid", SsidValue (ssid),
                       "ActiveProbing", BooleanValue (false));

      wifiMac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator",
                                                  "MaxAmsduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MSDU aggregation for AC_BE with a maximum aggregated size of nMsdus*(payloadSize+100) bytes, i.e. nMsdus aggregated packets in an A-MSDU

      staDev = wifi.Install (wifiPhy, wifiMac, sta);
      staInterface = ip.Assign (staDev);

      // getting the Ips
      Ipv4InterfaceContainer ApDestAddress;
      ApDestAddress = ip.Assign (apDev);

      for (uint32_t j = 0; j < sta.GetN (); j++)
        {
          uint16_t port =  ((i * destPort) + j) + 1;
          UdpServerHelper myServer (port);
          ApplicationContainer serverApp = myServer.Install (backboneNodes.Get (i));
          serverApp.Start (Seconds (0.0));
          serverApp.Stop (Seconds (simulationTime + 1));
          servers.Add (serverApp);

          UdpClientHelper myClient (ApDestAddress.GetAddress (0), port);
          myClient.SetAttribute ("MaxPackets", UintegerValue (4294967295u));
          myClient.SetAttribute ("Interval", TimeValue (dataGenerationRate)); //packets/s
          myClient.SetAttribute ("PacketSize", UintegerValue (payloadSize));

          ApplicationContainer clientApp = myClient.Install (sta.Get (j));
          clientApp.Start (Seconds (1.0));
          clientApp.Stop (Seconds (simulationTime + 1));

          std::cout << "-Setting UDP flow " << j << "/" << sta.GetN () - 1 << " from ip: " << staInterface.GetAddress (j)
            << ", to: " << ApDestAddress.GetAddress (0) << std::endl;
        }

      // save everything in containers.
      std::cout << "Saving all in containers: " << i << std::endl;
      staNodes.push_back (sta);
      apDevices.push_back (apDev);
      apInterfaces.push_back (apInterface);
      staDevices.push_back (staDev);
      staInterfaces.push_back (staInterface);
      config.servers.push_back (servers);

      allNodes.push_back (backboneNodes.Get (i));
      allNodes.at (i).Add (sta);
      NS_ASSERT (allNodes.at (i).GetN () == (1 + nStas));
  
      wifiPhy.EnablePcap ("wifi-wired-bridging", apDevices[i]);

      wifiX += deltaWifiX;
    }

    // Set channel width
    Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/ChannelWidth", UintegerValue (channelWidth));


  /* Logging and Tracing artifacts */
  if (verbose)
    {
      LogComponentEnable ("EdcaTxopN", LOG_LEVEL_DEBUG);
      LogComponentEnable ("DcfManager", LOG_LEVEL_DEBUG);
      LogComponentEnable ("MsduStandardAggregator", LOG_LEVEL_DEBUG);
      LogComponentEnable ("MsduAggregator", LOG_LEVEL_DEBUG);
      LogComponentEnable ("YansWifiPhy", LOG_LEVEL_DEBUG);
    }

  // Plugging the trace sources to all nodes in each network.
  NS_ASSERT (nWifis == allNodes.size ());
  for (uint32_t i = 0; i < nWifis; i++)
    {
      NS_ASSERT (allNodes.at (i).GetN () == (nStas + 1));
      for (uint32_t j = 0; j <= nStas; j++)
        {
          std::ostringstream n;
          uint32_t device = 1; // device for stas
          if (j == 0)
            device = 2; // device for backbone nodes
          n << i << "->" << j;
  
          Ptr<EdcaTxopN> edca = allNodes.at(i).Get (j)->GetDevice (device)->GetObject<WifiNetDevice> ()->GetMac ()
                                ->GetObject<RegularWifiMac> ()->GetBEQueue ();
  
          edca->TraceConnect ("TxFailures",n.str (), MakeBoundCallback (&TraceFailures, tx_stream, &results)); 
        }
    }



  Simulator::Stop (Seconds (simulationTime + 1));
  Simulator::Schedule (Seconds (simulationTime + 0.999999), finalResults, config, results_stream, &results);

  Simulator::Run ();
  Simulator::Destroy ();
}
