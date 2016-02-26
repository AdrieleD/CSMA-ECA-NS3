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
//    
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
//              
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
  double wifiX;
  bool elevenAc;
  uint64_t simulationTime;
  std::vector<ApplicationContainer> servers;
  uint32_t payloadSize;
  uint32_t channelWidth;
  uint16_t channelNumber;
  double maxWifiRange;
  bool limitRange;

  /* Mobility */
  bool randomWalk;
  uint32_t defaultPositions;
  double xDistanceFromAp;

  /* Protocol specific */
  bool eca;
  bool hysteresis;
  bool fairShare;
  bool bitmap;
  bool dynStick;
  uint32_t stickiness;
};
struct sim_config config;

struct sim_results{
  int nStas;
  int nWifis;
  std::vector< std::vector<uint64_t> > failTx;
  std::vector< std::vector<uint64_t> > colTx;
  std::vector< std::vector<uint64_t> > sxTx;
  std::vector< std::vector<uint64_t> > txAttempts;
  std::vector< std::vector<uint64_t> > udpClientSentPackets;
  std::vector< std::vector<Time> > timeOfPrevSxTx;
  std::vector< std::vector<Time> > sumTimeBetweenSxTx;
  Ptr<OutputStreamWrapper> results_stream;

  uint64_t lastFailure;
  uint64_t lastCollision;
};
struct sim_results results;

double
GetJFI (int nStas, std::vector<uint64_t> &udpClientSentPackets)
{
  double jfi = 0.0;
  double num = 0.0;
  double denom = 0.0;
  for (uint32_t i = 0; i < nStas; i++)
    {
      if (udpClientSentPackets.at (i) > 0)
        {
          num += udpClientSentPackets.at (i);
          denom += nStas * std::pow (udpClientSentPackets.at (i), 2);
        }
      else
        {
          num = 0;
          denom = 1;
          std::cout << "Some clients did not sent packets." << std::endl;
          break;
        }
    }
  jfi = std::pow (num, 2) / denom;
  return jfi;
}

void
channelSetup (struct sim_config &config, std::vector<NetDeviceContainer> staDevices, std::vector<NetDeviceContainer> apDevices)
{
  /* Getting YansWifiChannel */
  NS_ASSERT (config.nWifis == staDevices.size ());
  NS_ASSERT (apDevices.size () == config.nWifis);

  for (uint32_t i = 0; i < apDevices.size (); i++)
    {
      Ptr<YansWifiChannel> channelAp;
      Ptr<YansWifiPhy> phyAp;

      /* apDevices have more than one device. We use an iterator
      *  and an Assert over channelAp to confirm that we chose the 
      *  correct channel pointer. */
      uint32_t nDevices = apDevices.at (i).GetN ();
      for (uint32_t n = 0; n < nDevices; n ++)
        {
          channelAp = apDevices.at (i).Get (n)->GetObject<WifiNetDevice> ()->GetChannel ()->GetObject<YansWifiChannel> ();
          phyAp = apDevices.at (i).Get (n)->GetObject<WifiNetDevice> ()->GetPhy ()->GetObject<YansWifiPhy> ();
          if (channelAp)
            break;
        }
      NS_ASSERT (channelAp);
      // Ptr<PropagationLossModel> lossModel = channelAp->GetPropagationLossModel ();

      phyAp->SetChannelWidth (config.channelWidth);
      phyAp->SetChannelNumber (config.channelNumber);


      std::cout << "\n###Channel###" << std::endl;
      std::cout << "- Ap-" << i << ": " << channelAp->GetNDevices () << " connected devices" << std::endl;
      std::cout << "\t- Wifi channel: " << phyAp->GetChannelNumber () << std::endl;
      std::cout << "\t- Frequency: " << phyAp->GetChannelFrequencyMhz () << " MHz" << std::endl;
      std::cout << "\t- Width: " << phyAp->GetChannelWidth () << " MHz" << std::endl;
      std::cout << "\t- Energy detection threshold: " << phyAp->GetEdThreshold () << " dBm" << std::endl;
      std::cout << "\t- Tx levels. Min: " << phyAp->GetTxPowerStart () << ", Max: " << phyAp->GetTxPowerEnd () << " dBm" << std::endl;
      if (config.limitRange) 
        std::cout << "\t- Max transmission range: " << config.maxWifiRange << " m" << std::endl;


      NS_ASSERT (staDevices.at (i).GetN () == config.nStas);
      uint32_t nStas = staDevices.at (i).GetN ();
      for (uint32_t j = 0; j < nStas; j++)
        {
          Ptr<YansWifiChannel> channelSta = staDevices.at (i).Get (j)->GetObject<WifiNetDevice> ()->GetChannel ()
                                            ->GetObject<YansWifiChannel> ();
          NS_ASSERT (channelSta);
          Ptr<YansWifiPhy> phySta = staDevices.at (i).Get (j)->GetObject<WifiNetDevice> ()->GetPhy ()->GetObject<YansWifiPhy> ();

          phySta->SetChannelWidth (config.channelWidth);
          phySta->SetChannelNumber (config.channelNumber);
        }
    }
}

void
mobilitySetup (struct sim_config &config, std::vector<MobilityHelper> &mobility, NodeContainer &backboneNodes, 
  std::vector<NodeContainer> sta)
{
  NS_ASSERT (config.nWifis == backboneNodes.GetN ());
  NS_ASSERT (config.nWifis == mobility.size ());
  Ptr<ListPositionAllocator> positionAlloc;
  Vector apPos;
  Vector staPos;

  for (uint32_t i = 0; i < config.nWifis; i++)
    {
      NS_ASSERT (sta.at (i).GetN () == config.nStas);
      switch (config.defaultPositions)
        {
          //Squares surrounding the Ap
          case 1:
            NS_ASSERT (config.nStas == 4);
            goto setupPositions;
            break;
          //Three linear square networks, like case 1.
          case 2:
            NS_ASSERT (config.nStas == 4 && config.nWifis == 3);
            goto setupPositions;
            break;
          //Hundred linear square networks, like case 1.
          case 3:
            NS_ASSERT (config.nStas == 4 && config.nWifis == 100);
            goto setupPositions;
            break;
          default:
            mobility.at (i).SetPositionAllocator ("ns3::GridPositionAllocator",
                                                 "MinX", DoubleValue (config.wifiX),
                                                 "MinY", DoubleValue (0.0),
                                                 "DeltaX", DoubleValue (10),
                                                 "DeltaY", DoubleValue (10),
                                                 "GridWidth", UintegerValue (1),
                                                 "LayoutType", StringValue ("RowFirst"));

            mobility.at (i).SetMobilityModel ("ns3::ConstantPositionMobilityModel");
            mobility.at (i).Install (backboneNodes.Get (i));

            /* Sta mobility model */
            if(config.randomWalk)
              {
                mobility.at (i).SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                                                 "Mode", StringValue ("Time"),
                                                 "Time", StringValue ("2s"),
                                                 "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                                                 "Bounds", RectangleValue (Rectangle (config.wifiX, config.wifiX+5.0,0.0, (config.nStas+1)*5.0)));
              }
            mobility.at (i).Install (sta.at (i));
            goto showPositions;
            break;
        }

      setupPositions:
        positionAlloc = CreateObject<ListPositionAllocator> ();
        apPos = Vector (config.wifiX, 0.0, 0.0);
        positionAlloc->Add (apPos);

        for (uint32_t j = 0; j < config.nStas; j++)
          { 
            switch (j)
              {
                case 0:
                  staPos = Vector (config.wifiX + config.xDistanceFromAp, config.xDistanceFromAp, 0.0);
                  break;
                case 1:
                  staPos = Vector (config.wifiX - config.xDistanceFromAp, config.xDistanceFromAp, 0.0);
                  break;
                case 2:
                  staPos = Vector (config.wifiX - config.xDistanceFromAp, -1.0 * config.xDistanceFromAp, 0.0);
                  break;
                case 3:
                  staPos = Vector (config.wifiX + config.xDistanceFromAp, -1.0 * config.xDistanceFromAp, 0.0);
                  break;
              }
              positionAlloc->Add (staPos);
          }

        mobility.at (i).SetPositionAllocator (positionAlloc);
        mobility.at (i).SetMobilityModel ("ns3::ConstantPositionMobilityModel");
        mobility.at (i).Install (backboneNodes.Get (i));
        mobility.at (i).Install (sta.at (i));

      // /* Looking at each Sta's position */
      // AsciiTraceHelper asciiTraceHelper;
      // std::string filename = "position-";
      // std::stringstream ss;
      // ss << filename << i;
      // std::string positionLog = ss.str();
      // Ptr<OutputStreamWrapper> position_stream = asciiTraceHelper.CreateFileStream (positionLog);
      showPositions: 
        std::cout << "###Mobility###" << std::endl;
        std::cout << "- Wifi-" << i << std::endl;
        uint32_t n = 0;
        for (NodeContainer::Iterator j = sta.at (i).Begin (); j != sta.at (i).End (); j++, n++)
          {
            Ptr<Node> object = *j;
            Ptr<MobilityModel> position = object->GetObject<MobilityModel> ();
            NS_ASSERT (position != 0);
            Vector pos = position->GetPosition ();
            std::cout << "\t- Sta-" << n << ": x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z << std::endl;

            /* Printing position to a file for plotting */
            // *position_stream->GetStream () << pos.x << " " << pos.y << " " << 1 << std::endl;
          }
        config.wifiX += config.deltaWifiX;
    }
}

void
finishSetup (struct sim_config &config, std::vector<NodeContainer> allNodes)
{
  NS_ASSERT (config.nWifis == allNodes.size ());

  uint32_t CwMin, CwMax;
  std::cout << "\n###MAC and other parameters###" << std::endl;
  for (uint32_t i = 0; i < config.nWifis; i++)
    {
      NS_ASSERT (allNodes.at (i).GetN () == config.nStas);
      uint32_t device = 1; // device for stas
      /*
       * Navigating through stations for providing Arp entries.
       */
      uint32_t nStas = allNodes.at (i).GetN ();
      for (uint32_t j = 0; j < nStas; j++)
        {
          for (uint32_t k = 0; k < nStas; k++)
            {
              if (k == j)        
                continue;          
              /* Getting arpCache of node j */
              Ptr<NetDevice> netDeviceJ = allNodes.at (i).Get (j)->GetDevice (device)->GetObject<NetDevice> ();
              Ptr<ArpCache> arpCacheJ = allNodes.at (i).Get (j)->GetObject<ArpL3Protocol> ()->FindCache (netDeviceJ);
              if (arpCacheJ == NULL)
                arpCacheJ = CreateObject<ArpCache> ();

              /* Getting the addresses of node k */
              Ptr<WifiNetDevice> wifiNetDeviceK = allNodes.at (i).Get (k)->GetDevice (device)->GetObject<WifiNetDevice> ();
              Address mac = wifiNetDeviceK->GetAddress ();

              Ptr<ArpCache> arpCacheK = allNodes.at (i).Get (k)->GetObject<ArpL3Protocol> ()->FindCache (wifiNetDeviceK);
              Ptr<Ipv4Interface> ipv4InterfaceK = arpCacheK->GetInterface ();
              Ipv4Address ip = ipv4InterfaceK->GetAddress (0).GetLocal ();
              arpCacheJ->SetAliveTimeout (Seconds (config.simulationTime + 1));
              ArpCache::Entry *entry = arpCacheJ->Add (ip);
              entry->MarkWaitReply(0);
              entry->MarkAlive(mac);
            }

          Ptr<EdcaTxopN> edca = allNodes.at (i).Get (j)->GetDevice (device)->GetObject<WifiNetDevice> ()->GetMac ()
                                ->GetObject<RegularWifiMac> ()->GetBEQueue ();
          Ptr<DcfManager> manager = allNodes.at (i).Get (j)->GetDevice (device)->GetObject<WifiNetDevice> ()
                                    ->GetMac ()->GetObject<RegularWifiMac> ()->GetDcfManager ();
          if (config.eca)
            {
              edca->ResetStats ();
              manager->SetEnvironmentForECA (config.hysteresis, config.bitmap, config.stickiness, config.dynStick);
            }
          /* Universal variables. For visualization only */
          CwMin = edca->GetMinCw ();
          CwMax = edca->GetMaxCw ();

        }
        std::cout << "-Wlan-" << i << std::endl;
        std::cout << "\t- CSMA/ECA: " << config.eca << std::endl;
        std::cout << "\t- Hysteresis: " << config.hysteresis << std::endl;
        std::cout << "\t\t- Stickiness: " << config.stickiness << std::endl;
        std::cout << "\t- FairShare: " << config.fairShare << std::endl;
        std::cout << "\t- Schedule Reduction: " << config.bitmap << std::endl;

        std::cout << "\t- CwMin: " << CwMin << std::endl;
        std::cout << "\t- CwMax: " << CwMax << std::endl;
    }

}

void
finalResults (struct sim_config &config, Ptr<OutputStreamWrapper> stream, struct sim_results *results, 
  Ptr<OutputStreamWrapper> staStream, std::vector<NodeContainer> sta)
{
  NS_ASSERT (config.servers.size () == config.nWifis);
  NS_ASSERT (results->sxTx.at (0).size () == results->nStas + 1);

  std::vector< std::vector<Time> > timeBetweenSxTx;
  std::vector<Time> staTimesBetSxTx;
  staTimesBetSxTx.assign (results->nStas + 1, NanoSeconds (0));
  timeBetweenSxTx.assign (results->nWifis, staTimesBetSxTx);

  std::vector<double> overallTimeBetweenSxTx;
  overallTimeBetweenSxTx.assign (results->nWifis, 0.0);

  std::vector<double> topologyThroughput;
  std::vector<double> topologyFailedTx;
  std::vector<double> topologyJFI;

  topologyThroughput.assign (results->nWifis, 0.0);
  topologyFailedTx.assign (results->nWifis, 0.0);
  topologyJFI.assign (results->nWifis, 0.0);


  for (uint32_t i = 0; i < config.nWifis; i++)
    {
      double throughput = 0.0;
      double totalFails = 0.0;
      double col = 0;
      double attempts = 0;
      double sx = 0;

      // /* Looking at each Sta's position */
      AsciiTraceHelper asciiTraceHelper;
      std::string filename = "position-";
      std::stringstream ss;
      ss << filename << i;
      std::string positionLog = ss.str ();
      Ptr<OutputStreamWrapper> position_stream = asciiTraceHelper.CreateFileStream (positionLog);
      
      std::cout << "\nResults for Wifi: " << i << std::endl;
      std::cout << "\tThroughput from Udp servers:" << std::endl;
      
      /* Getting throughput from the Udp server */
      NS_ASSERT (config.servers.at (i).GetN () == sta.at (i).GetN ());
      for (uint32_t j = 0; j < config.servers.at (i).GetN (); j++)
        {
          uint32_t totalPacketsThrough = DynamicCast<UdpServer> (config.servers.at (i).Get (j))->GetReceived ();
          double addThroughput = totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0);
          throughput += addThroughput;
          std::cout << "\t-Sta-" << j << ": " << totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0) 
            << " Mbps" << std::endl;
          results->udpClientSentPackets.at (i).at (j) = totalPacketsThrough;

          /* Gathering per Sta information */
          *staStream->GetStream () << i << " " << j << " " << addThroughput << " " 
            << results->failTx.at (i).at (j+1) << " " << results->colTx.at (i).at (j+1) << std::endl;

          /* Printing the throughput and location information */
          std::stringstream nodeSs;
          std::string nodeId;
          nodeSs << i << "." <<  j;
          nodeId = nodeSs.str ();
          Ptr<MobilityModel> position = sta.at (i).Get (j)->GetObject<MobilityModel> ();
          NS_ASSERT (position != 0);
          Vector pos = position->GetPosition ();
          *position_stream->GetStream () << pos.x << " " << pos.y << " " << addThroughput << " " << nodeId << std::endl;
        }

      /* Looking at the traced values */
      std::cout << "\n-MAC and other details" << std::endl;
      for (uint32_t j = 0; j < results->nStas+1; j++)
        {
          std::cout << "\n\t-Sta-" << j << std::endl;
          totalFails += results->failTx.at (i).at (j);
          col += results->colTx.at (i).at (j);
          attempts += results->txAttempts.at (i).at (j);
          sx += results->sxTx.at (i).at (j);

          if (results->sxTx.at (i).at (j) > 0)
            timeBetweenSxTx.at (i).at (j) = MicroSeconds ((results->sumTimeBetweenSxTx.at (i).at (j).GetMicroSeconds ()) /
              results->sxTx.at (i).at (j) );
          overallTimeBetweenSxTx.at(i) += timeBetweenSxTx.at (i).at (j).GetSeconds ();

          std::cout << "\tTx Attmpts: " << results->txAttempts.at (i).at (j) << std::endl;
          std::cout << "\tSx Frames: " << results->sxTx.at (i).at (j) << std::endl;
          std::cout << "\tFailures: " << results->failTx.at (i).at (j) << std::endl;
          std::cout << "\tCollisions: " << results->colTx.at (i).at (j) << std::endl;
          std::cout << "\tAverage time between sx tx: " << timeBetweenSxTx.at (i).at (j).GetSeconds () << std::endl;
        }

        std::cout << "\n-Total Throughput: " << throughput << " Mbps" << std::endl;
        double failFrac = 0;
        if (col > 0 || totalFails > 0) failFrac = (totalFails + col) / (sx + col + totalFails);
        std::cout << "-Fraction of failed transmissions: " << failFrac << std::endl;
        double jfi = GetJFI (results->nStas, results->udpClientSentPackets.at (i));
        std::cout << "-JFI: " << jfi << std::endl;
        std::cout << "-Overall Time between sx tx: " << overallTimeBetweenSxTx.at (i) << ".0s" << std::endl;
        
        // double index = attempts - (col + totalFails + sx);
        // std::cout << "-Count index: " << index << std::endl;
        
        std::cout << "-Total Tx Attmpts: " << attempts << std::endl;
        std::cout << "-Total Sx Frames: " << sx << std::endl;
        std::cout << "-Total Failures: " << totalFails << std::endl;

        /* Global stats */
        topologyThroughput.at (i) = throughput;
        topologyJFI.at (i) = jfi;
        topologyFailedTx.at (i) = failFrac;
    }
    /* Global stats */
    bool multi = false;

    if (results->nWifis > 1) multi = true;
    if (multi) std::cout << "\n###Complete topology statistics###" << std::endl;
    for (uint32_t i = 0; i < results->nWifis; i++)
      {
        std::cout << "-Wlan-" << i << std::endl;
        std::cout << "\t-Throughput: " << topologyThroughput.at (i) << std::endl;
        std::cout << "\t-Fraction of failures: " << topologyFailedTx.at (i) << std::endl;
        std::cout << "\t-JFI: " << topologyJFI.at (i) << std::endl;

        /* Writing the results to file */
        *results->results_stream->GetStream () << i << " " << results->nStas << " " << topologyThroughput.at (i) << " "
          << topologyFailedTx.at (i) << " " << topologyJFI.at (i) << std::endl;
      }
}

void
process (std::string &dataFileName)
{
  std::string prefix("(cd ~/Dropbox/PhD/Research/NS3/ns-allinone-3.24.1/bake/source/ns-3.24/tmp3 && ./process3 ");
  std::string command = prefix + dataFileName + ")";
  system(command.c_str());
}

void
TraceFailures(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue)
{
  uint64_t m_now = Simulator::Now().GetNanoSeconds ();
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos + 2, context.length ());

  *stream->GetStream () << m_now << " " << wlan << " " << node << " " << FAILTX << " " << newValue << std::endl;

  results->failTx.at (std::stoi (wlan)).at (std::stoi (node)) ++;
  results->lastFailure = m_now;
}

void
TraceCollisions(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue)
{
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos + 2, context.length ());

  *stream->GetStream () << m_now << " " << wlan << " " << node << " " << COLTX << " " << newValue << std::endl;

  results->colTx.at (std::stoi (wlan)).at (std::stoi (node)) ++;
  results->lastCollision = m_now;
}

void
TraceSuccesses(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue)
{
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  uint64_t delta = 0;
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos + 2, context.length ());

  *stream->GetStream () << m_now << " " << wlan << " " << node <<  " " << SXTX << " " << newValue << std::endl; 

  results->sxTx.at (std::stoi (wlan)).at (std::stoi (node)) ++;

  if (newValue == 1)
    results->timeOfPrevSxTx.at (std::stoi (wlan)).at (std::stoi (node)) = NanoSeconds (m_now);
  delta = (m_now - results->timeOfPrevSxTx.at (std::stoi (wlan)).at (std::stoi (node)).GetNanoSeconds ());
  results->sumTimeBetweenSxTx.at (std::stoi (wlan)).at (std::stoi (node)) += NanoSeconds (delta);
  results->timeOfPrevSxTx.at (std::stoi (wlan)).at (std::stoi (node)) = NanoSeconds (m_now);
}

void
TraceTxAttempts(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue)
{
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos + 2, context.length ());

  *stream->GetStream () << m_now << " " << wlan << " " << node << " " << TX << " " << newValue << std::endl; 
  results->txAttempts.at (std::stoi (wlan)).at (std::stoi (node)) ++;
}

void
TraceAssignedBackoff(Ptr<OutputStreamWrapper> stream, std::string context, uint32_t oldValue, uint32_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  if(newValue != 0xFFFFFFFF)
    *stream->GetStream () << m_now << " " << context << " " << BO << " " << newValue << std::endl; 
}


int main (int argc, char *argv[])
{
  uint32_t nWifis = 1;
  uint32_t nStas = 2;
  bool sendIp = false;
  uint32_t channelWidth = 20;
  uint16_t channelNumber = 48;
  bool writeMobility = false;
  double deltaWifiX = 30.0;
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
  uint32_t defaultPositions = 0;

  /* Mobility */
  double xDistanceFromAp = 10; // x component of maxWifiRange calculation
  double maxWifiRange = 0.0;
  bool limitRange = false;

  /* Protocol specific */
  bool eca = false;
  bool hysteresis = false;
  bool fairShare = false;
  bool bitmap = false;
  bool dynStick = false;
  uint32_t stickiness = 0;

  std::string resultsName ("results3.log");
  std::string staResultsName ("staResults3.log");
  std::string txLog ("tx.log");
  std::string backoffLog ("backoff.log");


  CommandLine cmd;
  cmd.AddValue ("nWifis", "Number of wifi networks", nWifis);
  cmd.AddValue ("nStas", "Number of stations per wifi network", nStas);
  cmd.AddValue ("SendIp", "Send Ipv4 or raw packets", sendIp);
  cmd.AddValue ("writeMobility", "Write mobility trace", writeMobility);
  cmd.AddValue ("xDistanceFromAp", "X component of maxWifiRange calculations", xDistanceFromAp);
  cmd.AddValue ("limitRange", "Limit the transmission range", limitRange);
  cmd.AddValue ("defaultPositions", "Positions of different experiments", defaultPositions);
  cmd.AddValue ("randomWalk", "Random walk of Stas", randomWalk);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("channelWidth", "channelWidth", channelWidth);
  cmd.AddValue ("channelNumber", "channelWidth", channelNumber);
  cmd.AddValue ("elevenAc", "802.elevenAc MCS", elevenAc);
  cmd.AddValue ("seed", "RNG simulation seed", seed);
  cmd.AddValue ("verbose", "Logging", verbose);
  cmd.AddValue ("eca", "Activation of a deterministic backoff after sxTx", eca);
  cmd.AddValue ("hyst", "Hysteresis", hysteresis);
  cmd.AddValue ("stickiness", "Stickiness", stickiness);
  cmd.AddValue ("fairShare", "Fair Share", fairShare);
  cmd.AddValue ("bitmap", "Bitmap activation", bitmap);
  cmd.AddValue ("dynStick", "Dynamic stickiness", dynStick);
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
  Ptr<OutputStreamWrapper> sta_stream = asciiTraceHelper.CreateFileStream (staResultsName);

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

  std::vector<MobilityHelper> allMobility;
  config.wifiX = 0.0; // initial x position of first Ap
  double yDistanceFromAp = xDistanceFromAp;
  maxWifiRange = std::sqrt (std::pow (xDistanceFromAp, 2) + std::pow (yDistanceFromAp, 2));
  config.maxWifiRange = maxWifiRange;
  // separation coefficient
  uint16_t beta = 30;
  deltaWifiX = beta * maxWifiRange;

  config.limitRange = limitRange;
  // Set the maximum wireless range to maxWifiRange meters in order to reproduce a hidden nodes scenario
  // i.e. the distance between hidden stations is larger than maxWifiRange meters
  if (limitRange)
    Config::SetDefault ("ns3::RangePropagationLossModel::MaxRange", DoubleValue (maxWifiRange));

  config.simulationTime = simulationTime;
  config.nWifis = nWifis;
  config.nStas = nStas;
  config.payloadSize = payloadSize;
  config.channelWidth = channelWidth;
  config.channelNumber = channelNumber;

  config.randomWalk = randomWalk;
  if (defaultPositions > 0)
    NS_ASSERT (nStas >= 4 && nStas % 4 == 0);
  config.deltaWifiX = deltaWifiX;
  config.defaultPositions = defaultPositions;
  config.xDistanceFromAp = xDistanceFromAp;

  config.eca = eca;
  config.hysteresis = hysteresis;
  config.stickiness = stickiness;
  config.fairShare = fairShare;
  config.bitmap = bitmap;
  config.dynStick = dynStick;

  std::vector<uint64_t> zeroth;
  std::vector<Time> zerothTime;
  zeroth.assign (nStas+1, 0); // a zero vector for statistics. Ap + Stas
  zerothTime.assign (nStas+1, NanoSeconds (0));

  results.failTx.assign (nWifis, zeroth);
  results.colTx.assign (nWifis, zeroth);
  results.sxTx.assign (nWifis, zeroth);
  results.txAttempts.assign (nWifis, zeroth);
  results.udpClientSentPackets.assign (nWifis, zeroth);
  results.timeOfPrevSxTx.assign (nWifis, zerothTime);
  results.sumTimeBetweenSxTx.assign (nWifis, zerothTime);
  results.results_stream = results_stream;

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
      wifiChannel.AddPropagationLoss ("ns3::RangePropagationLossModel"); //wireless range limited to xDistanceFromAp meters!
      wifiPhy.SetChannel (wifiChannel.Create ());


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

          // std::cout << "-Setting UDP flow " << j << "/" << sta.GetN () - 1 << " from ip: " << staInterface.GetAddress (j)
          //   << ", to: " << ApDestAddress.GetAddress (0) << std::endl;
        }

      // save everything in containers.
      staNodes.push_back (sta);
      apDevices.push_back (apDev);
      apInterfaces.push_back (apInterface);
      staDevices.push_back (staDev);
      staInterfaces.push_back (staInterface);
      config.servers.push_back (servers);
      allMobility.push_back (mobility);

      allNodes.push_back (backboneNodes.Get (i));
      allNodes.at (i).Add (sta);
      NS_ASSERT (allNodes.at (i).GetN () == (1 + nStas));
  
      wifiPhy.EnablePcap ("wifi-wired-bridging", apDevices[i]);
    }

    /* Mobility */
    mobilitySetup (config, allMobility, backboneNodes, staNodes);


  /* Logging and Tracing artifacts */
  if (verbose)
    {
      LogComponentEnable ("EdcaTxopN", LOG_LEVEL_DEBUG);
      LogComponentEnable ("DcfManager", LOG_LEVEL_DEBUG);
      LogComponentEnable ("MsduStandardAggregator", LOG_LEVEL_DEBUG);
      LogComponentEnable ("MsduAggregator", LOG_LEVEL_DEBUG);
      LogComponentEnable ("YansWifiPhy", LOG_LEVEL_DEBUG);
      LogComponentEnable ("InterferenceHelper", LOG_LEVEL_DEBUG);
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
          edca->TraceConnect ("TxCollisions",n.str (), MakeBoundCallback (&TraceCollisions, tx_stream, &results));
          edca->TraceConnect ("TxSuccesses", n.str (), MakeBoundCallback (&TraceSuccesses, tx_stream, &results));
          edca->TraceConnect ("TxAttempts", n.str (), MakeBoundCallback (&TraceTxAttempts, tx_stream, &results));
          edca->TraceConnect ("BackoffCounter", n.str (), MakeBoundCallback (&TraceAssignedBackoff, backoff_stream));  
        }
    }



  Simulator::Stop (Seconds (simulationTime + 1));
  Simulator::Schedule (Seconds (0.5), channelSetup, config, staDevices, apDevices);
  Simulator::Schedule (Seconds (0.5), finishSetup, config, staNodes);
  Simulator::Schedule (Seconds (simulationTime + 0.999999), finalResults, config, results_stream, &results, sta_stream, staNodes);
  Simulator::Schedule (Seconds (simulationTime + 0.999999), process, resultsName);

  


  Simulator::Run ();
  Simulator::Destroy ();
}
