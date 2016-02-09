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

//Defining log codes for interesting metrics
 #define SXTX 1 //successes
 #define FAILTX 2 //failures
 #define TX 3 //attempts
 #define BO 4 //assigned backoff
 #define txDUR 5 //last Tx duration
 #define FSAGG 6 //# of frames aggregated

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
  bool fairShare;
  bool bitmap;
  bool srConservative;
  uint32_t srActivationThreshold;
  bool srResetMode;
  uint32_t EIFSnoDIFS;
  uint32_t ackTimeout;
  uint32_t maxMsdus;
  double frameMinFer;
};
struct sim_config config;

struct sim_results{
  uint32_t stas;
  std::vector<uint64_t> sxTx;
  std::vector<uint64_t> failTx;
  std::vector<uint64_t> txAttempts;
  std::vector<uint64_t> udpClientSentPackets;
  uint64_t lastCollision;
  uint32_t srAttempts;
  uint32_t srReductions;
  uint32_t srFails;
  std::vector<Time> sumTimeBetweenSxTx;
  std::vector<Time> timeOfPrevSxTx;
  uint64_t errorFrames;
};
struct sim_results results;

double
GetJFI (sim_results *results)
{
  double jfi = 0.0;
  double num = 0.0;
  double denom = 0.0;
  for (uint32_t i = 0; i < results->stas; i++)
    {
      if (results->udpClientSentPackets.at (i) > 0)
        {
          num += results->udpClientSentPackets.at (i);
          denom += results->stas * std::pow (results->udpClientSentPackets.at (i), 2);
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
finishSetup (struct sim_config &config)
{
  NodeContainer allNodes (config.allNodes);
  uint32_t Cwmin, Cwmax;
  Cwmin = 0;
  Cwmax = Cwmin;
  ObjectFactory m_factory;
  Ptr<MsduAggregator> m_msduAggregator;
  Ptr<MpduAggregator> m_mpduAggregator;

  /* For displaying */
  uint32_t SIFS = 0;
  uint32_t aSlot = 0;
  uint32_t EIFSnoDIFS = 0;
  uint32_t ackTimeout = 0;

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
    Ptr<WifiMac> wifiMac = allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()
      ->GetMac ();
    Ptr<MacLow> macLow = dca->Low ();
    Ptr<YansWifiPhy> phy = allNodes.Get(i)->GetDevice (0)->GetObject<WifiNetDevice> ()->
      GetPhy ()->GetObject<YansWifiPhy> ();

    /* Setting the FER */
    if (config.frameMinFer > 0)
      phy->SetFrameMinFer(config.frameMinFer);  

    if (config.eca)
    {
      dca->ResetStats ();
      manager->SetEnvironmentForECA (config.hysteresis, config.bitmap,
        config.stickiness, config.dynStick);
      
      if (config.fairShare)
          dca->SetFairShare ();

      /* Setting the Ack timeout and EIFS no DIFS to be equal to DIFS */
      if (config.EIFSnoDIFS != 1)
        wifiMac->SetEifsNoDifs (MicroSeconds (config.EIFSnoDIFS));
      if (config.ackTimeout != 1)
        wifiMac->SetAckTimeout (MicroSeconds (config.ackTimeout));

      /* Setting all the nasty stuff for Schedule Reset */      
      if (config.bitmap == true)
        {
          manager->SetAmsduSimulation ();
          if (config.srActivationThreshold == 1)
            dca->SetScheduleResetActivationThreshold (config.srActivationThreshold);
          if (config.srConservative)
            dca->SetScheduleConservative ();
          if (config.srResetMode)
            dca->SetScheduleResetMode (); //Halving or reset?
        }
    }
    /* Mainly for displaying */
    Cwmin = dca->GetMinCw ();
    Cwmax = dca->GetMaxCw ();
    aSlot = wifiMac->GetSlot ().GetMicroSeconds ();
    SIFS = wifiMac->GetSifs ().GetMicroSeconds ();
    EIFSnoDIFS = wifiMac->GetEifsNoDifs ().GetMicroSeconds ();
    ackTimeout = wifiMac->GetAckTimeout ().GetMicroSeconds ();
    /* </Mainly for displaying> */

  }
  std::cout << "- nWifi: " << config.nWifi << std::endl;
  std::cout << "- Cwmin: " << Cwmin << std::endl;
  std::cout << "- Cwmax: " << Cwmax << std::endl;
  std::cout << "- CSMA/ECA: " << config.eca << std::endl;
  std::cout << "- Hysteresis: " << config.hysteresis << std::endl;
  std::cout << "\t- Stickiness: " << config.stickiness << std::endl;
  std::cout << "- FairShare: " << config.fairShare << std::endl;
  std::cout << "\t-Max aMSDU: " << config.maxMsdus << std::endl;
  std::cout << "- Schedule Reduction: " << config.bitmap << std::endl;
  std::cout << "- Schedule Halving (0) or Reset (1): " << config.srResetMode << std::endl;
  std::cout << "-frameMinFer: " << config.frameMinFer << std::endl;

  std::cout << "\nMAC-specific parameters:" << std::endl;
  std::cout << "-aSlot: " << aSlot << std::endl;
  std::cout << "-SIFS: " << SIFS << std::endl;
  std::cout << "-DIFS: " << 2 * aSlot + SIFS << std::endl;
  std::cout << "-EIFSnoDIFS: " << EIFSnoDIFS << std::endl;
  std::cout << "-ackTimeout: " << ackTimeout << std::endl;
}

void
finalResults (struct sim_config &config, Ptr<OutputStreamWrapper> stream, struct sim_results *results)
{
  double throughput = 0.0;
  double col = 0;
  double attempts = 0;
  double sx = 0;
  std::vector<Time> timeBetweenSxTx;
  timeBetweenSxTx.assign (results->stas, NanoSeconds(0));
  double overallTimeBetweenSxTx = 0.0;

  NS_ASSERT (config.servers.GetN () == config.nWifi);

  std::cout << "\n- Results for " << config.servers.GetN () << " clients." << std::endl;

  for (uint32_t i = 0; i < config.servers.GetN (); i++)
    {
      uint32_t totalPacketsThrough = DynamicCast<UdpServer> (config.servers.Get (i))->GetReceived ();
      results->udpClientSentPackets.at (i) = totalPacketsThrough;
      throughput += totalPacketsThrough * config.payloadSize * 8 / (config.simulationTime * 1000000.0);
      col += results->failTx.at (i);
      attempts += results->txAttempts.at (i);
      sx += results->sxTx.at (i);
      if (results->sxTx.at (i) > 0)
        timeBetweenSxTx.at (i) = NanoSeconds ((results->sumTimeBetweenSxTx.at (i).GetNanoSeconds ()) / results->sxTx.at (i));
    }
  std::cout << "- Throughput: " << throughput << " Mbit/s" << '\n';
  double colFrac = 0;
  if (col > 0) colFrac = col/(sx + col);
  std::cout << "-Fraction of tx resulting in collisions: " << colFrac << ". " << attempts << " attempts" << std::endl;
  std::cout << "\t-Last collision: " << results->lastCollision * 1e-6 << " secs.\n" << std::endl;
  double jfi = GetJFI (results);
  std::cout << "-JFI: " << jfi << std::endl;
  std::cout << "-Average time between successful transmissions: " << std::endl;
  for (int i = 0; i < config.servers.GetN(); i++)
    {
      std::cout << "\tSta-" << i << ": " << timeBetweenSxTx.at (i).GetSeconds () << " s" << std::endl;
      overallTimeBetweenSxTx += timeBetweenSxTx.at (i).GetSeconds ();
    }

  /* Writing to file */
  *stream->GetStream() << config.nWifi << " " << std::fixed << std::setprecision(6) 
  << throughput << " " << colFrac << " " << overallTimeBetweenSxTx/config.servers.GetN () 
  << " " << jfi << std::endl;
}

void
process (std::string &dataFileName)
{
  std::string prefix("(cd ~/Dropbox/PhD/Research/NS3/ns-allinone-3.24.1/bake/source/ns-3.24/tmp2 && ./process ");
  std::string command = prefix + dataFileName + ")";
  system(command.c_str());
}

/* Trace Callbacks */

void
TraceFailures(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << FAILTX << " " << newValue << std::endl;
  results->failTx.at (std::stoi (context)) ++;
  results->lastCollision = Simulator::Now().GetMicroSeconds();
}

void
TraceSuccesses(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  uint64_t delta = 0;
  *stream->GetStream () << m_now << " " << context << " " << SXTX << " " << newValue << std::endl; 
  results->sxTx.at (std::stoi (context)) ++;

  if (newValue == 1)
    results->timeOfPrevSxTx.at (std::stoi (context)) = NanoSeconds (m_now);
  delta = (m_now - results->timeOfPrevSxTx.at (std::stoi (context)).GetNanoSeconds ());
  results->sumTimeBetweenSxTx.at (std::stoi (context)) += NanoSeconds (delta);
  results->timeOfPrevSxTx.at (std::stoi (context)) = NanoSeconds (m_now);
}

void
TraceTxAttempts(Ptr<OutputStreamWrapper> stream, struct sim_results *results, std::string context, 
  uint64_t oldValue, uint64_t newValue){
  uint64_t m_now = Simulator::Now().GetNanoSeconds();
  *stream->GetStream () << m_now << " " << context << " " << TX << " " << newValue << std::endl; 
  results->txAttempts.at (std::stoi (context)) ++;
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

void
TraceFsAggregated(Ptr<OutputStreamWrapper> stream, std::string context, uint16_t oldValue, uint16_t newValue)
{
  if (newValue != 0xFFFF)
    {
      uint64_t m_now = Simulator::Now().GetNanoSeconds();
      *stream->GetStream () << m_now << " " << context << " " << FSAGG << " " << newValue << std::endl;
    }
}

void
TraceErrorFrames(Ptr<OutputStreamWrapper> stream, struct sim_results *results,
  std::string context, uint64_t oldValue, uint64_t newValue)
{
  results->errorFrames += 1;
}

NS_LOG_COMPONENT_DEFINE ("SimpleMsduAggregation");

int main (int argc, char *argv[])
{
  uint32_t nWifi = 2;
  uint32_t payloadSize = 1470; //bytes
  uint64_t simulationTime = 3; //seconds
  bool enableRts = false;
  uint32_t txRate = 73;
  uint32_t destPort = 10000;
  Time dataGenerationRate = Seconds ((payloadSize*8) / (txRate * 1e6));
  std::string dataRate ("HtMcs7");
  std::string controlRate ("HtMcs7");
  std::string ackMode ("ErpOfdmRate54Mbps");
  bool verbose = false;
  bool eca = true;
  bool hysteresis = false;
  uint32_t stickiness = 0;
  bool fairShare = false;
  bool bitmap = false;
  bool dynStick = false;
  bool srConservative = false;
  uint32_t srActivationThreshold = 1;
  bool srResetMode = false;
  uint32_t EIFSnoDIFS = 1;
  uint32_t ackTimeout = 1;
  int32_t seed = -1;
  uint32_t nMsdus = 1;
  uint32_t maxMsdus = 1024/16; /* Cwmax / Cwmin */
  double frameMinFer = 0.0; /* from 0 to 1 */

  std::string resultsName ("results2.log");
  std::string txLog ("tx.log");
  std::string backoffLog ("detBackoff.log");
  std::string bitmapLog ("bitmap.log");
  std::string srLog ("sr.log");
  std::string fsLog ("fs.log");

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
  cmd.AddValue ("srConservative", "Adjusts the number of iterations for building Schedule Reset bitmap", srConservative);
  cmd.AddValue ("srActivationThreshold", "After this many consecutive successfull transmissions, SR is activated", srActivationThreshold);
  cmd.AddValue ("srResetMode", "By default, schedules will be halved. Set true for Schedule Reset", srResetMode);
  cmd.AddValue ("EIFSnoDIFS", "IFS before retransmitting a frame", EIFSnoDIFS);
  cmd.AddValue ("AckTimeout", "Time that will timeout the DATA+ACK exchange", ackTimeout);
  cmd.AddValue ("verbose", "Logging", verbose);
  cmd.AddValue ("seed", "RNG simulation seed", seed);
  cmd.AddValue ("nWifi", "Number of Wifi clients", nWifi);
  cmd.AddValue ("verbose", "Logging", verbose);
  cmd.AddValue ("frameMinFer", "Frame error rate at PHY level", frameMinFer);

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
  config.srConservative = srConservative;
  config.srActivationThreshold = srActivationThreshold;
  config.srResetMode = srResetMode;
  config.EIFSnoDIFS = EIFSnoDIFS;
  config.ackTimeout = ackTimeout;
  config.maxMsdus = maxMsdus;
  config.frameMinFer = frameMinFer;

  results.srAttempts = 0;
  results.srFails = 0;
  results.srReductions = 0;
  results.stas = nWifi;
  results.sxTx.assign (results.stas, 0);
  results.failTx.assign (results.stas, 0);
  results.txAttempts.assign (results.stas, 0);
  results.udpClientSentPackets.assign (results.stas, 0);
  results.sumTimeBetweenSxTx.assign (results.stas, NanoSeconds (0));
  results.timeOfPrevSxTx.assign (results.stas, NanoSeconds (0));
  results.errorFrames = 0;

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
  Ptr<OutputStreamWrapper> bitmap_stream = asciiTraceHelper.CreateFileStream (bitmapLog);
  Ptr<OutputStreamWrapper> sr_stream = asciiTraceHelper.CreateFileStream (srLog);
  Ptr<OutputStreamWrapper> fs_stream = asciiTraceHelper.CreateFileStream (fsLog);

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
   * ErpOfdmRate54Mbps: 20 MHz, 54 Mbps. NS3 does not support Ht mcs for Acks.
  */

  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (dataRate), 
                                                                "ControlMode", StringValue (controlRate),
                                                                "AckMode", StringValue (dataRate));
  HtWifiMacHelper mac = HtWifiMacHelper::Default ();

  Ssid ssid = Ssid ("simple-msdu-aggregation");
  mac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));

  if (fairShare == true)
    nMsdus = maxMsdus;

  mac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator",
                              "MaxAmsduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MSDU aggregation for AC_BE with a maximum aggregated size of nMsdus*(payloadSize+100) bytes, i.e. nMsdus aggregated packets in an A-MSDU

  NetDeviceContainer staDevice;
  staDevice = wifi.Install (phy, mac, wifiStaNode);

  mac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid),
               "BeaconGeneration", BooleanValue (true));

  mac.SetMsduAggregatorForAc (AC_BE, "ns3::MsduStandardAggregator",
                              "MaxAmsduSize", UintegerValue (nMsdus * (payloadSize + 100))); //enable MSDU aggregation for AC_BE with a maximum aggregated size of nMsdus*(payloadSize+100) bytes, i.e. nMsdus aggregated packets in an A-MSDU

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
      std::cout << "-Setting UDP flow " << i << "/" << nWifi << " to AP " << ApInterface.GetAddress (0)
      << ", node: " << wifiStaNode.Get (i)->GetId () << std::endl;
    }

  if (verbose)
    {
      LogComponentEnable ("EdcaTxopN", LOG_LEVEL_DEBUG);
      LogComponentEnable ("DcfManager", LOG_LEVEL_DEBUG);
      LogComponentEnable ("MsduStandardAggregator", LOG_LEVEL_DEBUG);
      LogComponentEnable ("MsduAggregator", LOG_LEVEL_DEBUG);
      LogComponentEnable ("YansWifiPhy", LOG_LEVEL_DEBUG);
    }

  /* Connecting the trace sources to their respective sinks */
  for(uint32_t i = 0; i < config.allNodes.GetN () - 1; i++)
    {
      std::ostringstream n;
      Ptr<EdcaTxopN> dca = config.allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()->GetMac ()->GetObject<RegularWifiMac> ()->GetBEQueue ();
      Ptr<DcfManager> dcfManager = config.allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()->GetMac ()->GetObject<RegularWifiMac> ()->GetDcfManager ();
      Ptr<YansWifiPhy> phy = config.allNodes.Get (i)->GetDevice (0)->GetObject<WifiNetDevice> ()->GetPhy ()->GetObject<YansWifiPhy> ();

      n << i;
      dca->TraceConnect ("TxFailures",n.str (), MakeBoundCallback (&TraceFailures, tx_stream, &results));
      dca->TraceConnect ("TxSuccesses", n.str (), MakeBoundCallback (&TraceSuccesses, tx_stream, &results));
      dca->TraceConnect ("TxAttempts", n.str (), MakeBoundCallback (&TraceTxAttempts, tx_stream, &results));
      dca->TraceConnect ("BackoffCounter", n.str (), MakeBoundCallback (&TraceAssignedBackoff, backoff_stream));  
      dca->TraceConnect ("Bitmap", n.str (), MakeBoundCallback (&TraceEcaBitmap, bitmap_stream));
      dca->TraceConnect ("SrReductionAttempts", n.str (), MakeBoundCallback (&TraceSrAttempts, sr_stream, &results));
      dca->TraceConnect ("SrReductions", n.str (), MakeBoundCallback (&TraceSrReductions, sr_stream, &results));
      dca->TraceConnect ("SrReductionFailed", n.str (), MakeBoundCallback (&TraceSrFails, sr_stream, &results));
      dca->TraceConnect ("FsAggregated", n.str (), MakeBoundCallback (&TraceFsAggregated, fs_stream));

      phy->TraceConnect ("FramesWithErrors", n.str (), MakeBoundCallback (&TraceErrorFrames, tx_stream, &results));

      dcfManager->TraceConnect ("LastTxDuration", n.str (), MakeBoundCallback (&TraceLastTxDuration, tx_stream));
    }

  Simulator::Stop (Seconds (simulationTime + 1));

  Simulator::Schedule (Seconds (0.999999), finishSetup, config);
  Simulator::Schedule (Seconds (simulationTime + 0.999999), finalResults, config, results_stream, &results);
  Simulator::Schedule (Seconds (simulationTime + 0.999999), process, resultsName);

  Simulator::Run ();
  Simulator::Destroy ();

  

  return 0;
}
