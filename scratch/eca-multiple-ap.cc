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
};

struct sim_config config;


void
finalResults (struct sim_config &config, Ptr<OutputStreamWrapper> stream)
{
  NS_ASSERT (config.servers.size () == config.nWifis);
  for (uint32_t i = 0; i < config.nWifis; i++)
    {
      double throughput = 0.0;
      std::cout << "\nResults for Wifi: " << i << std::endl;
      for (uint32_t j = 0; j < config.servers.at (i).GetN (); j++)
        {
          uint32_t totalPacketsThrough = DynamicCast<UdpServer> (config.servers.at (i).Get (j))->GetReceived ();
          throughput += totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0);
          std::cout << "Sta-" << j << ": " << totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0) << std::endl;
        }
        std::cout << "-Throughput: " << throughput << std::endl;
    }
}

int main (int argc, char *argv[])
{
  uint32_t nWifis = 2;
  uint32_t nStas = 2;
  bool sendIp = true;
  bool writeMobility = false;
  double deltaWifiX = 2.0;
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
  bool udpTest = true;
  uint32_t destPort = 1000;
  uint64_t simulationTime = 3; //seconds
  uint32_t txRate = 83;
  Time dataGenerationRate = Seconds ((payloadSize*8) / (txRate * 1e6));

  std::string resultsName ("results3.log");

  CommandLine cmd;
  cmd.AddValue ("nWifis", "Number of wifi networks", nWifis);
  cmd.AddValue ("nStas", "Number of stations per wifi network", nStas);
  cmd.AddValue ("SendIp", "Send Ipv4 or raw packets", sendIp);
  cmd.AddValue ("writeMobility", "Write mobility trace", writeMobility);
  cmd.AddValue ("deltaWifiX", "Separation between two nWifis", deltaWifiX);
  cmd.AddValue ("randomWalk", "Random walk of Stas", randomWalk);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
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

  NodeContainer backboneNodes;
  NetDeviceContainer backboneDevices;
  Ipv4InterfaceContainer backboneInterfaces;
  std::vector<NodeContainer> staNodes;
  std::vector<NetDeviceContainer> staDevices;
  std::vector<NetDeviceContainer> apDevices;
  std::vector<Ipv4InterfaceContainer> staInterfaces;
  std::vector<Ipv4InterfaceContainer> apInterfaces;
  std::vector<Ipv4InterfaceContainer> apBridgeInterfaces;



  config.simulationTime = simulationTime;
  config.nWifis = nWifis;
  config.nStas = nStas;
  config.deltaWifiX = deltaWifiX;
  config.payloadSize = payloadSize;

  InternetStackHelper stack;
  CsmaHelper csma;
  Ipv4AddressHelper ip;
  ip.SetBase ("192.168.0.0", "255.255.255.0");

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
                                     "DeltaX", DoubleValue (5.0),
                                     "DeltaY", DoubleValue (5.0),
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

      if (udpTest)
        {
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
        }

      // save everything in containers.
      staNodes.push_back (sta);
      apDevices.push_back (apDev);
      apInterfaces.push_back (apInterface);
      staDevices.push_back (staDev);
      staInterfaces.push_back (staInterface);
      config.servers.push_back (servers);

      wifiX += deltaWifiX;
    }

    if (!udpTest)
      {
        Address dest;
        std::string protocol;
        if (sendIp)
          {
            dest = InetSocketAddress (staInterfaces[1].GetAddress (1), 1025);
            protocol = "ns3::UdpSocketFactory";
          }
        else
          {
            PacketSocketAddress tmp;
            tmp.SetSingleDevice (staDevices[0].Get (0)->GetIfIndex ());
            tmp.SetPhysicalAddress (staDevices[1].Get (0)->GetAddress ());
            tmp.SetProtocol (0x807);
            dest = tmp;
            protocol = "ns3::PacketSocketFactory";
          }

        OnOffHelper onoff = OnOffHelper (protocol, dest);
        onoff.SetConstantRate (DataRate ("500kb/s"));
        ApplicationContainer apps = onoff.Install (staNodes[0].Get (0));
        apps.Start (Seconds (0.5));
        apps.Stop (Seconds (3.0));
      }

  wifiPhy.EnablePcap ("wifi-wired-bridging", apDevices[0]);
  wifiPhy.EnablePcap ("wifi-wired-bridging", apDevices[1]);

  if (writeMobility)
    {
      AsciiTraceHelper ascii;
      MobilityHelper::EnableAsciiAll (ascii.CreateFileStream ("wifi-wired-bridging.mob"));
    }

  Simulator::Stop (Seconds (simulationTime + 1));
  Simulator::Schedule (Seconds (simulationTime + 0.999999), finalResults, config, results_stream);

  Simulator::Run ();
  Simulator::Destroy ();
}
