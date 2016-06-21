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
#include <ns3/buildings-helper.h>
#include <ns3/node-list.h>
#include <ns3/building.h>
#include <ns3/mobility-building-info.h>
#include <ns3/hybrid-buildings-propagation-loss-model.h>
#include <ns3/constant-position-mobility-model.h>
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/bridge-helper.h"
#include "ns3/assert.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/random-variable-stream.h"
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
  bool channelAllocation;
  double freq;
  double maxWifiRange;
  bool limitRange;

  /* Mobility */
  bool randomWalk;
  uint32_t defaultPositions;
  double xDistanceFromAp;
  double yDistanceFromAp;
  double zDistanceFromAp;
  uint16_t beta;

  /* Channel and CCA */
  double edTheshold;
  double cca1Threshold;

  /* Protocol specific */
  bool eca;
  bool hysteresis;
  bool fairShare;
  bool fairShareAMPDU;
  bool bitmap;
  bool dynStick;
  bool srResetMode;
  bool srConservative;
  uint32_t stickiness;
  uint32_t maxMsdus;

  /* Traffic specific */
  bool saturation;
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

  std::vector< std::vector<uint64_t> > srAttempts;
  std::vector< std::vector<uint64_t> > srReductions;
  std::vector< std::vector<uint64_t> > srFails;

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
        }
    }
  jfi = std::pow (num, 2) / denom;
  return jfi;
}

uint16_t
GetChannelForWifi (uint32_t ap, struct sim_config &config)
{
  NS_ASSERT (config.defaultPositions >= 2);
  /* We may need to generate a more efficient way to do this */
  uint16_t ch = config.channelNumber;
  static const int arrA[] = {36,40,52,56,36 ,40,52,56,36,40 ,48,44,64,60,48 ,44,64,60,48,44};
  static const int arrB[] = {52,56,36,40,52 ,56,36,40,52,56 ,64,60,48,44,64 ,60,48,44,64,60};
  std::vector<uint16_t> typeA (arrA, arrA + sizeof (arrA) / sizeof (arrA[0]));
  std::vector<uint16_t> typeB (arrB, arrB + sizeof (arrB) / sizeof (arrB[0]));

  NS_ASSERT (typeA.size () == 20);
  NS_ASSERT (typeB.size () == 20);

  switch (config.defaultPositions)
    {
      case 2:
        break;
      case 3:
        break;
      case 4:
        break;
      case 5:
        goto setupChannel;
        break;
      default:
        break;
    }
  return ch;

  setupChannel:
    uint16_t z = 0;
    int c = ap;
    do
      {
        c = c - 20;
        z++;
      } while (c >= 0);  
    z = z - 1;
    NS_ASSERT ( (z >= 0) && (z < 5) );

    int mappedAp = ap - (z * 20);
    NS_ASSERT ( (mappedAp >= 0) && (mappedAp < 20) ); 
    if (z % 2 == 0)
      {
        goto typeA;
      }
    else
      {
        goto typeB;
      }

    typeA:
      ch = typeA.at (mappedAp);
      return ch;

    typeB:
      ch = typeB.at (mappedAp);
      return ch;   
}

void
channelSetup (struct sim_config &config, std::vector<NetDeviceContainer> staDevices, 
  std::vector<NetDeviceContainer> apDevices)
{
  /* Getting YansWifiChannel */
  NS_ASSERT (config.nWifis == staDevices.size ());
  NS_ASSERT (apDevices.size () == config.nWifis);

  uint16_t nonOverlapCh = config.channelNumber;
  uint16_t plus = 0;

  for (uint32_t i = 0; i < apDevices.size (); i++)
    {
      //Navigating every WLAN

      Ptr<YansWifiChannel> channelAp;
      Ptr<YansWifiPhy> phyAp;

      /* apDevices have more than one net device. We use an iterator
      *  and an Assert over channelAp to confirm that we chose the 
      *  correct channel pointer. */
      uint32_t nDevices = apDevices.at (i).GetN ();
      for (uint32_t n = 0; n < nDevices; n ++)
        {
          channelAp = apDevices.at (i).Get (n)->GetObject<WifiNetDevice> ()->
            GetChannel ()->GetObject<YansWifiChannel> ();
          phyAp = apDevices.at (i).Get (n)->GetObject<WifiNetDevice> ()->
            GetPhy ()->GetObject<YansWifiPhy> ();
          if (channelAp)
            break;
        }
      NS_ASSERT (channelAp); // GetObject<YansWifiChannel> () can return false

      phyAp->SetChannelWidth (config.channelWidth);
      
      if (config.channelAllocation)
        {
          nonOverlapCh = GetChannelForWifi (i, config);
          phyAp->SetChannelNumber (nonOverlapCh);
        }
      else
        {
          phyAp->SetChannelNumber (config.channelNumber + plus);
        }


      /* Setting the CCA parameters for AP */
      phyAp->SetEdThreshold (config.edTheshold);
      phyAp->SetCcaMode1Threshold (config.cca1Threshold);

      /* Setting tx powers according TGax */
      double txPowerDb = 15;

      phyAp->SetTxPowerStart (txPowerDb);
      phyAp->SetTxPowerEnd (txPowerDb);

      std::cout << "\n###Channel###" << std::endl;
      std::cout << "- Ap-" << i << ": " << channelAp->GetNDevices () << " connected devices" << std::endl;
      std::cout << "\t- Wifi channel: " << phyAp->GetChannelNumber () << std::endl;
      std::cout << "\t- Frequency: " << phyAp->GetChannelFrequencyMhz () << " MHz" << std::endl;
      std::cout << "\t- Width: " << phyAp->GetChannelWidth () << " MHz" << std::endl;
      std::cout << "\t- Cca threshold : " << phyAp->GetCcaMode1Threshold () << " dBm" << std::endl;
      std::cout << "\t- Energy detection threshold: " << phyAp->GetEdThreshold () << " dBm" << std::endl;
      std::cout << "\t- Tx levels. Min: " << phyAp->GetTxPowerStart () << ", Max: " << phyAp->GetTxPowerEnd () << " dBm" << std::endl;
      if (config.limitRange) 
        {
          std::cout << "\t- Max transmission range: " << config.maxWifiRange << " m" << std::endl;
        }


      /* Checking STAs for this WLAN */
      NS_ASSERT (staDevices.at (i).GetN () == config.nStas);
      uint32_t nStas = staDevices.at (i).GetN ();
      std::vector<double> staEdThreshold;
      staEdThreshold.assign (nStas, 0);
      for (uint32_t j = 0; j < nStas; j++)
        {
          Ptr<YansWifiChannel> channelSta = staDevices.at (i).Get (j)->GetObject<WifiNetDevice> ()->GetChannel ()
                                            ->GetObject<YansWifiChannel> ();
          NS_ASSERT (channelSta);
          Ptr<YansWifiPhy> phySta = staDevices.at (i).Get (j)->GetObject<WifiNetDevice> ()->GetPhy ()->GetObject<YansWifiPhy> ();

          phySta->SetChannelWidth (config.channelWidth);
         
          if (config.channelAllocation)
            {
              phySta->SetChannelNumber (nonOverlapCh);
            }
          else
            {
              phySta->SetChannelNumber (config.channelNumber + plus);
            }

          phySta->SetEdThreshold (config.edTheshold);
          phySta->SetCcaMode1Threshold (config.cca1Threshold);

          phySta->SetTxPowerStart (txPowerDb);
          phySta->SetTxPowerEnd (txPowerDb);

          staEdThreshold.at (j) = phySta->GetEdThreshold ();

          /* Checking that AP and STAs PHY settings are the same */
          NS_ASSERT (phySta->GetChannelNumber () == phyAp->GetChannelNumber ());
          NS_ASSERT (phySta->GetChannelWidth () == phyAp->GetChannelWidth ());
          NS_ASSERT (phySta->GetEdThreshold () == phyAp->GetEdThreshold ());
        }
      std::cout << "\t- Stations's energy detection threshold: " << staEdThreshold.at (0) << " dBm" << std::endl;
      plus += 4;
    }
}

void
mobilityUsingBuildings (struct sim_config &config, std::vector<MobilityHelper> &mobility, 
  NodeContainer &backboneNodes, std::vector<NodeContainer> &sta, std::vector<NetDeviceContainer> &staDevices, 
  std::vector<NetDeviceContainer> &apDevices)
{
  NS_ASSERT (config.defaultPositions == 5);
  NS_ASSERT (config.nWifis == staDevices.size ());
  NS_ASSERT (apDevices.size () == config.nWifis);

  bool superC = true;

  double wifiX = 0.0;
  double distanceX = config.xDistanceFromAp;
  double bottomX = wifiX;
  uint32_t columnsOfRooms = 5;
  if (superC)
    columnsOfRooms *= 2;
  double topX = (columnsOfRooms * 2 * distanceX);

  double wifiY = 0.0;
  double distanceY = config.yDistanceFromAp;
  double bottomY = wifiY;
  uint32_t rowsOfRooms = 2;
  double topY = (rowsOfRooms * 2 * distanceY);

  double wifiZ = 1.5;
  double distanceZ = config.zDistanceFromAp;
  double bottomZ = wifiZ;
  uint32_t numOfFloors = 5;

  double topZ = (2 * distanceZ * numOfFloors);


  Ptr<Building> building1  = CreateObject<Building> ();
  building1->SetBoundaries (Box (bottomX, topX, bottomY, topY, bottomZ, topZ));
  building1->SetNRoomsX (columnsOfRooms);
  building1->SetNRoomsY (rowsOfRooms);
  building1->SetNFloors (numOfFloors);

  building1->SetBuildingType (Building::Residential);
  building1->SetExtWallsType (Building::ConcreteWithWindows);

  uint16_t roomZ = 1;
  uint16_t roomX = 1;
  uint16_t roomY = 1;

  for (uint32_t i = 0; i < config.nWifis; i ++)
    {
      Vector apPos;
      
      Ptr<UniformRandomVariable> rX = CreateObject<UniformRandomVariable> ();
      Ptr<UniformRandomVariable> rY = CreateObject<UniformRandomVariable> ();

      rX->SetAttribute ("Min", DoubleValue (wifiX));
      rX->SetAttribute ("Max", DoubleValue (wifiX + (2 * config.xDistanceFromAp)));
      rY->SetAttribute ("Min", DoubleValue (wifiY));
      rY->SetAttribute ("Max", DoubleValue (wifiY + (2 * config.yDistanceFromAp)));

      apPos = Vector (rX->GetValue (), rY->GetValue (), wifiZ);
   
      mobility.at (i).Install (backboneNodes.Get (i));
      mobility.at (i).Install (sta.at (i));
      mobility.at (i).SetMobilityModel ("ns3::ConstantPositionMobilityModel");  


      Ptr<Object> object = backboneNodes.Get (i);
      Ptr<MobilityModel> mm = object->GetObject<MobilityModel> ();
      NS_ASSERT (mm != 0);
      mm->SetPosition (apPos);

      /* Creating the MobilityBuildingInfo with the help of BuildingsHelper */
      BuildingsHelper::Install (backboneNodes.Get (i));

      Ptr<MobilityBuildingInfo> apBuildingInfo = mm->GetObject<MobilityBuildingInfo> ();
      NS_ASSERT (apBuildingInfo != 0);
      apBuildingInfo->SetBuilding (building1);

      apBuildingInfo->SetIndoor (building1, roomZ, roomX, roomY);

      BuildingsHelper::MakeConsistent (mm);

      Ptr<YansWifiChannel> channelAp;
      uint32_t nDevices = apDevices.at (i).GetN ();
      for (uint32_t n = 0; n < nDevices; n ++)
        {
          channelAp = apDevices.at (i).Get (n)->GetObject<WifiNetDevice> ()->
            GetChannel ()->GetObject<YansWifiChannel> ();
          if (channelAp)
            break;
        }
      NS_ASSERT (channelAp); // GetObject<YansWifiChannel> () can return false
      Ptr<HybridBuildingsPropagationLossModel> buildingLoss = channelAp->GetPropagationLossModel ()->GetNext ()
        ->GetObject<HybridBuildingsPropagationLossModel> ();
      NS_ASSERT (buildingLoss);

      buildingLoss->SetFrequency (config.freq);
      buildingLoss->SetRooftopHeight (topZ);
      buildingLoss->SetAttribute ("ShadowSigmaOutdoor", DoubleValue (0.0));
      buildingLoss->SetAttribute ("ShadowSigmaIndoor", DoubleValue (0.0));
      buildingLoss->SetAttribute ("ShadowSigmaExtWalls", DoubleValue (0.0));

      //pippo
      uint32_t k = 0;
      for (NodeContainer::Iterator j = sta.at (i).Begin (); j != sta.at (i).End (); j++, k++)
        {
          Vector staPos = Vector (rX->GetValue (), rY->GetValue (), wifiZ);

          Ptr<Object> object = *j;
          Ptr<MobilityModel> mmStas = object->GetObject<MobilityModel> ();
          NS_ASSERT (mmStas != 0);
          mmStas->SetPosition (staPos);

          BuildingsHelper::Install (*j);

          Ptr<MobilityBuildingInfo> stasBuildingInfo = mmStas->GetObject<MobilityBuildingInfo> ();
          NS_ASSERT (stasBuildingInfo != 0);
          stasBuildingInfo->SetBuilding (building1);
          stasBuildingInfo->SetIndoor (building1, roomZ, roomX, roomY);
          
          BuildingsHelper::MakeConsistent (mmStas);

          Ptr<YansWifiChannel> channelSta = staDevices.at (i).Get (k)->GetObject<WifiNetDevice> ()->GetChannel ()
            ->GetObject<YansWifiChannel> ();
          NS_ASSERT (channelSta);
          Ptr<HybridBuildingsPropagationLossModel> buildingLoss = channelSta->GetPropagationLossModel ()->GetNext ()
          ->GetObject<HybridBuildingsPropagationLossModel> ();
          NS_ASSERT (buildingLoss);

          buildingLoss->SetFrequency (config.freq);
          buildingLoss->SetRooftopHeight (topZ);
          buildingLoss->SetAttribute ("ShadowSigmaOutdoor", DoubleValue (0.0));
          buildingLoss->SetAttribute ("ShadowSigmaIndoor", DoubleValue (0.0));
          buildingLoss->SetAttribute ("ShadowSigmaExtWalls", DoubleValue (0.0));
        }

      std::cout << "###Mobility###" << std::endl;
      std::cout << "- Wifi-" << i << std::endl;
      std::cout << "\t- Center of cell is at: (" << wifiX << ", " << wifiY << ", " << wifiZ << ")" << std::endl;
      uint32_t n = 0;
      for (NodeContainer::Iterator j = sta.at (i).Begin (); j != sta.at (i).End (); j++, n++)
        {
          Ptr<Node> object = *j;
          Ptr<MobilityModel> position = object->GetObject<MobilityModel> ();
          NS_ASSERT (position != 0);
          Ptr<MobilityBuildingInfo> stasBuildingInfo = position->GetObject<MobilityBuildingInfo> ();

          Vector pos = position->GetPosition ();
          std::cout << "\t- Sta-" << n << ": x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z << std::endl;
          std::cout << "\t\t-floorNum: " << stasBuildingInfo->GetFloorNumber () << ", xRoom-#: " << stasBuildingInfo->GetRoomNumberX () << ", yRoom-#: " 
            << stasBuildingInfo->GetRoomNumberY () << std::endl;
        }

      /* Building */
      if ((i + 1) % columnsOfRooms == 0)
        {
          //resetting x coordinate every columnsOfRooms nWifis
          wifiX = config.wifiX; 
          roomX = 1;

          //swapping y coordinate every columnsOfRooms nWifis
          if (wifiY == 0)
            {
              wifiY = 2 * config.yDistanceFromAp;
              roomY = 2;
            }
          else
            {
              wifiY = 0;
              roomY = 2;
            }

          //Going up a floor
          if ((i + 1) % (2 * columnsOfRooms) == 0)
            {
              wifiZ += 2 * config.zDistanceFromAp;
              roomZ ++;
            }
        }
      else
        {
          wifiX += 2 * config.xDistanceFromAp;
          roomX ++;
        }
    }
}

void
mobilitySetup (struct sim_config &config, std::vector<MobilityHelper> &mobility, 
  NodeContainer &backboneNodes, std::vector<NodeContainer> sta)
{
  NS_ASSERT (config.nWifis == backboneNodes.GetN ());
  NS_ASSERT (config.nWifis == mobility.size ());

  Ptr<ListPositionAllocator> positionAlloc;
  Vector apPos;
  Vector staPos;
  double wifiX = config.wifiX;
  double wifiY = 0;
  double wifiZ = 1.5;

  bool random = false;
  Ptr<UniformRandomVariable> rX = CreateObject<UniformRandomVariable> ();
  Ptr<UniformRandomVariable> rY = CreateObject<UniformRandomVariable> ();
  Ptr<UniformRandomVariable> rZ = CreateObject<UniformRandomVariable> ();

  for (uint32_t i = 0; i < config.nWifis; i++)
    {
      NS_ASSERT (sta.at (i).GetN () == config.nStas);
      switch (config.defaultPositions)
        {
          //STAs and AP are in the same coordinate
          case 1:
            goto setupPositions;
            break;
          //Three linear networks with 4 stas forming a cross centered in the AP
          case 2:
            NS_ASSERT (config.nStas == 4 && config.nWifis == 3);
            goto setupPositions;
            break;
          //AP placement follows 2, but stas are randomly deployed
          case 3:
            random = true;
            goto setupPositions;
            break;
          //Nodes and APs are randomly deployed. Follows TGax Building scenario
          case 4:
            random = true;
            NS_ASSERT (config.nWifis % 50 == 0);
            goto setupPositions;
          break;
          default:
            mobility.at (i).SetPositionAllocator ("ns3::GridPositionAllocator",
                                                 "MinX", DoubleValue (wifiX),
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
                                                 "Bounds", RectangleValue (Rectangle (wifiX, wifiX+5.0,0.0, (config.nStas+1)*5.0)));
              }
            mobility.at (i).Install (sta.at (i));
            goto showPositions;
            break;
        }


      setupPositions:
        rX->SetAttribute ("Min", DoubleValue (wifiX - config.xDistanceFromAp));
        rX->SetAttribute ("Max", DoubleValue (wifiX + config.xDistanceFromAp));
        rY->SetAttribute ("Min", DoubleValue (-1.0 * config.yDistanceFromAp + wifiY));
        rY->SetAttribute ("Max", DoubleValue (config.yDistanceFromAp + wifiY));
        /* Floor and ceiling according to TGax. Not used, Z is fized for every nWiFi */
        rZ->SetAttribute ("Min", DoubleValue (-1.0 * config.zDistanceFromAp + wifiZ));
        rZ->SetAttribute ("Max", DoubleValue (wifiZ + config.zDistanceFromAp));

        positionAlloc = CreateObject<ListPositionAllocator> ();
        /* Placing the AP at 1.5 m, according to TGax */
        if (config.defaultPositions == 4)
          {
            apPos = Vector (rX->GetValue (), rY->GetValue (), wifiZ);
          }
        else
          {
            apPos = Vector (wifiX, 0.0, wifiZ);
          }
        positionAlloc->Add (apPos);

        for (uint32_t j = 0; j < config.nStas; j++)
          { 
            if (!random)
              {
                if (config.defaultPositions == 2)
                  {
                    NS_ASSERT (config.nStas == 4);
                    switch (j)
                      {
                        case 0:
                          staPos = Vector (wifiX + config.xDistanceFromAp, 0.0, wifiZ);
                          break;
                        case 1:
                          staPos = Vector (wifiX, config.yDistanceFromAp, wifiZ);
                          break;
                        case 2:
                          staPos = Vector (wifiX - config.xDistanceFromAp, 0.0, wifiZ);
                          break;
                        case 3:
                          staPos = Vector (wifiX, -1.0 * config.yDistanceFromAp, wifiZ);
                          break;
                      }
                  }
                else
                  {
                    staPos = apPos;
                  }
              }
            else
              {
                staPos = Vector (rX->GetValue (), rY->GetValue (), wifiZ);
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
        std::cout << "-\t Center of cell is at: (" << wifiX << ", " << wifiY << ", " << wifiZ << ")" << std::endl;
        uint32_t n = 0;
        for (NodeContainer::Iterator j = sta.at (i).Begin (); j != sta.at (i).End (); j++, n++)
          {
            Ptr<Node> object = *j;
            Ptr<MobilityModel> position = object->GetObject<MobilityModel> ();
            NS_ASSERT (position != 0);
            Vector pos = position->GetPosition ();
            std::cout << "\t- Sta-" << n << ": x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z << std::endl;
            // std::cout << "\t Max distance from the AP: X: " << config.xDistanceFromAp << ", Y: " << config.yDistanceFromAp <<", Z: " << config.zDistanceFromAp << std::endl;
            // std::cout << "\t Default positions: " << config.defaultPositions << std::endl;

            /* Printing position to a file for plotting */
            // *position_stream->GetStream () << pos.x << " " << pos.y << " " << 1 << std::endl;
          }

      /* Building  */
      if (config.defaultPositions == 4 && ( (i + 1) % (5) == 0))
        {
          //resetting x coordinate every 5 nWifis
          wifiX = config.wifiX; 

          //swapping y coordinate every 5 nWifis
          if (wifiY == 0)
            {
              wifiY = 2 * config.yDistanceFromAp;
            }
          else
            {
              wifiY = 0;
            }

          //Going up a floor
          if ((i + 1) % 10 == 0)
            wifiZ += 2 * config.zDistanceFromAp;
        }
      else
        {
          wifiX += config.deltaWifiX;
        }
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

      /* Navigating through stations for providing Arp entries */
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
          NS_ASSERT (edca);
          Ptr<DcfManager> manager = allNodes.at (i).Get (j)->GetDevice (device)->GetObject<WifiNetDevice> ()
                                    ->GetMac ()->GetObject<RegularWifiMac> ()->GetDcfManager ();
          NS_ASSERT (manager);

          Ptr<WifiPhy> phy = allNodes.at (i).Get (j)->GetDevice (device)->GetObject<WifiNetDevice> ()->GetPhy ();
          NS_ASSERT (phy);

          if (config.eca)
            {
              edca->ResetStats ();
              manager->SetEnvironmentForECA (config.hysteresis, config.bitmap, config.stickiness, config.dynStick);

              if (config.bitmap)
                {
                  if (config.srConservative)
                    edca->SetScheduleConservative (); //Determines gamma. Default is aggressive gamma = 1, actual value is 2 though.
                  if (config.srResetMode)
                    edca->SetScheduleResetMode (); //Halving or reset?
                }

              if (config.fairShare)
                {
                  edca->SetFairShare ();
                  manager->SetAmpduSimulation ();
                }
              else if (config.fairShareAMPDU)
                {
                  phy->SetFairShare ();
                  manager->SetAmpduSimulation ();
                }
            }
          /* Testing reducing the CWmax with CSMA/ECA+Hyst */
          // edca->SetMaxCw (255);
            
          /* Universal variables. For visualization only */
          CwMin = edca->GetMinCw ();
          CwMax = edca->GetMaxCw ();

        }
        std::cout << "-Wlan-" << i << std::endl;
        std::cout << "\t- Saturation: " << config.saturation << std::endl;
        std::cout << "\t- CSMA/ECA: " << config.eca << std::endl;
        std::cout << "\t- Hysteresis: " << config.hysteresis << std::endl;
        std::cout << "\t\t- Stickiness: " << config.stickiness << std::endl;
        std::cout << "\t- FairShare: " << config.fairShare << std::endl;
        std::cout << "\t- FairShare AMPDU: " << config.fairShareAMPDU << std::endl;
        std::cout << "\t- Schedule Reduction: " << config.bitmap << std::endl;
        std::cout << "\t\t- srConservative: " << config.srConservative << std::endl;
        std::cout << "\t\t- srResetMode: " << config.srResetMode << std::endl;

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
  std::vector<double> topologyTxAttempts;

  topologyThroughput.assign (results->nWifis, 0.0);
  topologyFailedTx.assign (results->nWifis, 0.0);
  topologyJFI.assign (results->nWifis, 0.0);
  topologyTxAttempts.assign (results->nWifis, 0.0);


  for (uint32_t i = 0; i < config.nWifis; i++)
    {
      double throughput = 0.0;
      double totalFails = 0.0;
      double col = 0;
      double attempts = 0;
      double sx = 0;

      /* Looking at each Sta's position */
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
          std::cout << "\t-Sta-" << j << ": " << addThroughput << " Mbps" << std::endl;
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
          if (j == 0)
            {
              std::cout << "\n\t-AP-" << j << std::endl;
            }
          else
            {
              std::cout << "\n\t-Sta-" << j << std::endl;
            }
          totalFails += results->failTx.at (i).at (j);
          col += results->colTx.at (i).at (j);
          attempts += results->txAttempts.at (i).at (j);
          sx += results->sxTx.at (i).at (j);

          if (results->sxTx.at (i).at (j) > 0)
            timeBetweenSxTx.at (i).at (j) = MicroSeconds ((results->sumTimeBetweenSxTx.at (i).at (j).GetMicroSeconds ()) /
              results->sxTx.at (i).at (j) );
          if (j > 0)
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
        overallTimeBetweenSxTx.at (i) /= results->nStas;
        std::cout << "-Overall Time between sx tx: " << overallTimeBetweenSxTx.at (i) << " s" << std::endl;
        
        // double index = attempts - (col + totalFails + sx);
        // std::cout << "-Count index: " << index << std::endl;
        
        std::cout << "-Total Tx Attmpts: " << attempts << std::endl;
        std::cout << "-Total Sx Frames: " << sx << std::endl;
        std::cout << "-Total Failures: " << totalFails << std::endl;

        /* Global stats */
        topologyThroughput.at (i) = throughput;
        topologyJFI.at (i) = jfi;
        topologyFailedTx.at (i) = failFrac;
        topologyTxAttempts.at (i) = sx + col + totalFails;

    }
    /* Global stats */
    bool multi = false;

    if (results->nWifis > 1) multi = true;
    if (multi) std::cout << "\n###Complete topology statistics###" << std::endl;
    for (uint32_t i = 0; i < results->nWifis; i++)
      {
        std::cout << "\n- Wlan-" << i << std::endl;
        std::cout << "\t- Throughput (Mbps): " << topologyThroughput.at (i) << std::endl;
        std::cout << "\t- Fraction of failures: " << topologyFailedTx.at (i) << std::endl;
        std::cout << "\t- Total Transmission attempts: " << topologyTxAttempts.at (i) << std::endl;
        std::cout << "\t- JFI: " << topologyJFI.at (i) << std::endl;
        std::cout << "\t- Avg. Time between Sx Tx: " << overallTimeBetweenSxTx.at (i) << std::endl;

        /* Writing the results to file */
        /* 0. WLAN
           1. Nodes
           2. Throughput
           3. FailedTX
           4. JFI
           5. Time bet sx tx
           6. txAttempts
         */
        *results->results_stream->GetStream () << i << " " << results->nStas << " " << topologyThroughput.at (i) << " "
          << topologyFailedTx.at (i) << " " << topologyJFI.at (i) << " " << overallTimeBetweenSxTx.at (i) 
          << " " << topologyTxAttempts.at (i) << std::endl;
      }
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
  uint64_t m_now = Simulator::Now().GetNanoSeconds ();
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos + 2, context.length ());

  *stream->GetStream () << m_now << " " << wlan << " " << node << " " << TX << " " << newValue << std::endl; 
  results->srAttempts.at (std::stoi (wlan)).at (std::stoi (node))++;
}

void
TraceSrReductions(Ptr<OutputStreamWrapper> stream, struct sim_results *results, 
  std::string context, uint32_t oldValue, uint32_t newValue)
{
  uint64_t m_now = Simulator::Now().GetNanoSeconds ();
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos + 2, context.length ());

  *stream->GetStream () << m_now << " " << wlan << " " << node << " " << SXTX << " " << newValue << std::endl; 
  results->srReductions.at (std::stoi (wlan)).at (std::stoi (node))++;
}

void
TraceSrFails(Ptr<OutputStreamWrapper> stream, struct sim_results *results, 
  std::string context, uint32_t oldValue, uint32_t newValue)
{

  uint64_t m_now = Simulator::Now().GetNanoSeconds ();
  std::string token;
  std::string delimeter = "->";
  size_t pos = context.find (delimeter);
  std::string wlan = context.substr (0, pos);
  std::string node = context.substr (pos + 2, context.length ());
 
  *stream->GetStream () << m_now << " " << wlan << " " << node << " " << FAILTX << " " << newValue << std::endl; 
  results->srFails.at (std::stoi (wlan)).at (std::stoi (node))++;
}

void
TraceFsAggregated(Ptr<OutputStreamWrapper> stream, std::string context, uint16_t oldValue, uint16_t newValue)
{
  if (newValue != 0xFFFF)
    {
      uint64_t m_now = Simulator::Now().GetNanoSeconds();
      *stream->GetStream () << m_now << " " << context << " " << FSAGG << " " << newValue << std::endl;
    }
}


int main (int argc, char *argv[])
{
  uint32_t nWifis = 1;
  uint32_t nStas = 2;
  bool sendIp = false;
  uint32_t channelWidth = 20;
  uint16_t channelNumber = 40;
  bool channelAllocation = false;
  double freq = 5.240e9;
  bool writeMobility = false;
  double deltaWifiX = 30.0;
  bool elevenAc = false;
  bool shortGuardInterval = true;
  uint32_t dataRateAc  = 7; // Vht mcs
  uint32_t controlRateAc = 7; // Vht mcs
  std::string dataRate ("HtMcs7");
  std::string controlRate ("HtMcs7");
  uint32_t nMsdus = 1;
  uint32_t maxMsdus = 1024/16; /* Cwmax / Cwmin */
  bool randomWalk = false;
  uint32_t payloadSize = 1470; //bytesq
  bool enableRts = false;
  int32_t seed = -1;
  uint32_t destPort = 1000;
  uint64_t simulationTime = 3; //seconds
  uint32_t txRate = 83;
  Time dataGenerationRate = Seconds ((payloadSize*8) / (txRate * 1e6));
  bool saturation = true;
  bool verbose = false;
  uint32_t defaultPositions = 0;
  bool srResetMode = false;
  bool srConservative = false;

  /* Mobility */
  double xDistanceFromAp = 10; // x component of maxWifiRange calculation
  double maxWifiRange = 0.0;
  bool limitRange = false;
  // double edThesholdM = 100.0; // in meters

  /* Channel and CCA settings */
  double edTheshold = -82; //dBm
  double cca1Threshold = -62; //dBm

  /* Protocol specific */
  bool eca = false;
  bool hysteresis = false;
  bool fairShare = false;
  bool fairShareAMPDU = false;
  bool bitmap = false;
  bool dynStick = false;
  uint32_t stickiness = 0;

  std::string resultsName ("results3.log");
  std::string staResultsName ("staResults3.log");
  std::string txLog ("tx.log");
  std::string backoffLog ("backoff.log");
  std::string srLog ("srLog.log");
  std::string fsLog ("fsLog.log");
  std::string bitmapLog ("bitmapLog.log");


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
  cmd.AddValue ("srResetMode", "By default, schedules will be halved. Set true for Schedule Reset", srResetMode);
  cmd.AddValue ("srConservative", "Adjusts the number of iterations for building Schedule Reset bitmap", srConservative);
  cmd.AddValue ("eca", "Activation of a deterministic backoff after sxTx", eca);
  cmd.AddValue ("hyst", "Hysteresis", hysteresis);
  cmd.AddValue ("stickiness", "Stickiness", stickiness);
  cmd.AddValue ("fairShare", "Fair Share", fairShare);
  cmd.AddValue ("bitmap", "Bitmap activation", bitmap);
  cmd.AddValue ("dynStick", "Dynamic stickiness", dynStick);
  cmd.AddValue ("edTheshold", "Energy detection threshold", edTheshold);
  cmd.AddValue ("cca1Threshold", "CCA threshold", cca1Threshold);
  cmd.AddValue ("fairShareAMPDU", "Fair Share at AMPDU level", fairShareAMPDU);
  cmd.AddValue ("saturation", "Maximum packet generation rate", saturation);
  cmd.AddValue ("channelAllocation", "Separate nWiFis in orthogonal channels", channelAllocation);
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
  Ptr<OutputStreamWrapper> sta_stream = asciiTraceHelper.CreateFileStream (staResultsName,  __gnu_cxx::ios_base::app);
  Ptr<OutputStreamWrapper> sr_stream = asciiTraceHelper.CreateFileStream (srLog);
  Ptr<OutputStreamWrapper> bitmap_stream = asciiTraceHelper.CreateFileStream (bitmapLog);
  Ptr<OutputStreamWrapper> fs_stream = asciiTraceHelper.CreateFileStream (fsLog);

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
  double zDistanceFromAp = 1.5;
  double yDistanceFromAp = xDistanceFromAp;
  config.yDistanceFromAp = yDistanceFromAp;
  config.zDistanceFromAp = zDistanceFromAp;
  double beta = 3; // Amplifies the distance between consecutive APs
  deltaWifiX = beta * xDistanceFromAp;
  config.beta = beta;

  double alpha = 1.0 * 2/3 * deltaWifiX;
  maxWifiRange = std::sqrt (std::pow (alpha, 2) + std::pow (yDistanceFromAp, 2));
  config.maxWifiRange = maxWifiRange;    
  config.limitRange = limitRange;

  config.edTheshold = edTheshold;
  config.cca1Threshold = cca1Threshold;

  config.simulationTime = simulationTime;
  config.nWifis = nWifis;
  config.nStas = nStas;
  config.maxMsdus = maxMsdus;
  config.payloadSize = payloadSize;
  config.channelWidth = channelWidth;
  config.channelNumber = channelNumber;
  config.channelAllocation = channelAllocation;
  config.freq = freq;

  config.randomWalk = randomWalk;
  config.deltaWifiX = deltaWifiX;
  config.defaultPositions = defaultPositions;
  config.xDistanceFromAp = xDistanceFromAp;

  config.eca = eca;
  config.hysteresis = hysteresis;
  config.stickiness = stickiness;
  config.fairShare = fairShare;
  config.fairShareAMPDU = fairShareAMPDU;
  config.bitmap = bitmap;
  config.dynStick = dynStick;
  config.srResetMode = srResetMode;
  config.srConservative = srConservative;

  config.saturation = saturation;

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

  results.srAttempts.assign (nWifis, zeroth);
  results.srReductions.assign (nWifis, zeroth);
  results.srFails.assign (nWifis, zeroth);

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
  // wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 

  //Setting up routing, given that is a type of csma network.
  //Helper creates tables and populates them. Forget about routing.
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();  


  YansWifiChannelHelper wifiChannel;
  /* Channel details extracted from TGax */  
  if (limitRange)
      {
        wifiChannel = YansWifiChannelHelper::Default ();
        wifiChannel.AddPropagationLoss ("ns3::RangePropagationLossModel", "MaxRange", DoubleValue (maxWifiRange));
      }
  else
      {
        if (defaultPositions >= 2)
          {
            NS_ASSERT (xDistanceFromAp == 5);
            wifiChannel.AddPropagationLoss ("ns3::ThreeLogDistancePropagationLossModel", 
                                            "Distance0", DoubleValue (1),
                                            "Distance1", DoubleValue (xDistanceFromAp),
                                            "Distance2", DoubleValue (3 * xDistanceFromAp),
                                            "TGax", BooleanValue (true),
                                            "Frequency", DoubleValue (freq)
                                            );
            if (defaultPositions == 5)
              wifiChannel.AddPropagationLoss ("ns3::HybridBuildingsPropagationLossModel",
                                              "hewScenario", BooleanValue (true)
                                              );
            wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
          }
        else
          {
            wifiChannel = YansWifiChannelHelper::Default ();
          }
        
      }

  wifiPhy.SetChannel (wifiChannel.Create ());

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
          wifiPhy.Set ("ChannelWidth", UintegerValue (channelWidth));
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


      /* Setting the Wifi Ap */
      wifiMac.SetType ("ns3::ApWifiMac",
                       "Ssid", SsidValue (ssid));

      if (fairShare || fairShareAMPDU)
        nMsdus = maxMsdus;

      if (fairShare)
        wifiMac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator",
                                        "MaxAmsduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MSDU aggregation for AC_BE with a maximum aggregated size of nMsdus*(payloadSize+100) bytes, i.e. nMsdus aggregated packets in an A-MSDU
      if (fairShareAMPDU)
        {
          wifiMac.SetMpduAggregatorForAc (AC_BE, "ns3::MpduStandardAggregator",
                                          "MaxAmpduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MPDU aggregation for AC_BE with a maximum aggregated size of nMpdus*(payloadSize+100) bytes, i.e. nMpdus aggregated packets in an A-MPDU
          wifiMac.SetBlockAckThresholdForAc (AC_BE, 2);             //enable Block ACK when A-MPDU is enabled (i.e. nMpdus > 1)
        }

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

      if (fairShare)
        wifiMac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator",
                                        "MaxAmsduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MSDU aggregation for AC_BE with a maximum aggregated size of nMsdus*(payloadSize+100) bytes, i.e. nMsdus aggregated packets in an A-MSDU
      if (fairShareAMPDU)
        {
          wifiMac.SetMpduAggregatorForAc (AC_BE, "ns3::MpduStandardAggregator",
                                          "MaxAmpduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MPDU aggregation for AC_BE with a maximum aggregated size of nMpdus*(payloadSize+100) bytes, i.e. nMpdus aggregated packets in an A-MPDU
          wifiMac.SetBlockAckThresholdForAc (AC_BE, 2);             //enable Block ACK when A-MPDU is enabled (i.e. nMpdus > 1)
        }

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

          if (!saturation)
            {
              dataGenerationRate = Seconds ((payloadSize*8) / (100 * 1e3));
              std::cout << "Non-sat" << std::endl;
            }
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
  
      // wifiPhy.EnablePcap ("wifi-wired-bridging", apDevices[i]);
    }

    /* Mobility */
    if (defaultPositions != 5)
      {
        mobilitySetup (config, allMobility, backboneNodes, staNodes);
      }
    else
      { 
        mobilityUsingBuildings (config, allMobility, backboneNodes, staNodes, staDevices, apDevices);
      }



  /* Logging and Tracing artifacts */
  if (verbose)
    {
      LogComponentEnable ("EdcaTxopN", LOG_LEVEL_DEBUG);
      LogComponentEnable ("DcfManager", LOG_LEVEL_DEBUG);
      // LogComponentEnable ("MsduStandardAggregator", LOG_LEVEL_DEBUG);
      // LogComponentEnable ("MpduStandardAggregator", LOG_LEVEL_DEBUG);
      // LogComponentEnable ("MsduAggregator", LOG_LEVEL_DEBUG);
      // LogComponentEnable ("YansWifiPhy", LOG_LEVEL_DEBUG);
      // LogComponentEnable ("InterferenceHelper", LOG_LEVEL_DEBUG);
      // LogComponentEnable ("PropagationLossModel", LOG_LEVEL_DEBUG);
      // LogComponentEnable ("HybridBuildingsPropagationLossModel", LOG_LEVEL_DEBUG);
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

          edca->TraceConnect ("Bitmap", n.str (), MakeBoundCallback (&TraceEcaBitmap, bitmap_stream));
          edca->TraceConnect ("SrReductionAttempts", n.str (), MakeBoundCallback (&TraceSrAttempts, sr_stream, &results));
          edca->TraceConnect ("SrReductions", n.str (), MakeBoundCallback (&TraceSrReductions, sr_stream, &results));
          edca->TraceConnect ("SrReductionFailed", n.str (), MakeBoundCallback (&TraceSrFails, sr_stream, &results));
          edca->TraceConnect ("FsAggregated", n.str (), MakeBoundCallback (&TraceFsAggregated, fs_stream));
        }
    }

  Simulator::Stop (Seconds (simulationTime + 1));
  Simulator::Schedule (Seconds (0.5), channelSetup, config, staDevices, apDevices);
  Simulator::Schedule (Seconds (0.5), finishSetup, config, staNodes);
  Simulator::Schedule (Seconds (simulationTime + 0.999999), finalResults, config, results_stream, &results, sta_stream, staNodes);

  


  Simulator::Run ();
  Simulator::Destroy ();
}
