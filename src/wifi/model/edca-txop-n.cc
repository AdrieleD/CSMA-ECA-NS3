/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2006, 2009 INRIA
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
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Mirko Banchi <mk.banchi@gmail.com>
 */

#include "ns3/log.h"
#include "ns3/assert.h"
#include "ns3/pointer.h"
#include "edca-txop-n.h"
#include "mac-low.h"
#include "dcf-manager.h"
#include "mac-tx-middle.h"
#include "wifi-mac-trailer.h"
#include "wifi-mac.h"
#include "random-stream.h"
#include "wifi-mac-queue.h"
#include "msdu-aggregator.h"
#include "mgt-headers.h"
#include "qos-blocked-destinations.h"

#undef NS_LOG_APPEND_CONTEXT
#define NS_LOG_APPEND_CONTEXT if (m_low != 0) { std::clog << "[mac=" << m_low->GetAddress () << "] "; }
#define MY_DEBUG(x) \
  NS_LOG_DEBUG (Simulator::Now () << " " << this << " " << x)

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("EdcaTxopN");

class EdcaTxopN::Dcf : public DcfState
{
public:
  Dcf (EdcaTxopN * txop)
    : m_txop (txop)
  {
  }
    // virtual void DoDeterministicBackoff (uint32_t cw)
    // {
    //   m_txop->StartBackoffNow (m_txop->deterministicBackoff (cw));
    // }

private:
  virtual void DoNotifyAccessGranted (void)
  {
    m_txop->NotifyAccessGranted ();
  }
  virtual void DoNotifyInternalCollision (void)
  {
    m_txop->NotifyInternalCollision ();
  }
  virtual void DoNotifyCollision (void)
  {
    m_txop->NotifyCollision ();
  }
  virtual void DoNotifyChannelSwitching (void)
  {
    m_txop->NotifyChannelSwitching ();
  }
  virtual void DoNotifySleep (void)
  {
    m_txop->NotifySleep ();
  }
  virtual void DoNotifyWakeUp (void)
  {
    m_txop->NotifyWakeUp ();
  }

  EdcaTxopN *m_txop;
};


class EdcaTxopN::TransmissionListener : public MacLowTransmissionListener
{
public:
  TransmissionListener (EdcaTxopN * txop)
    : MacLowTransmissionListener (),
      m_txop (txop)
  {
  }

  virtual ~TransmissionListener ()
  {
  }

  virtual void GotCts (double snr, WifiMode txMode)
  {
    m_txop->GotCts (snr, txMode);
  }
  virtual void MissedCts (void)
  {
    m_txop->MissedCts ();
  }
  virtual void GotAck (double snr, WifiMode txMode)
  {
    m_txop->GotAck (snr, txMode);
  }
  virtual void MissedAck (void)
  {
    m_txop->MissedAck ();
  }
  virtual void GotBlockAck (const CtrlBAckResponseHeader *blockAck, Mac48Address source, WifiMode txMode)
  {
    m_txop->GotBlockAck (blockAck, source,txMode);
  }
  virtual void MissedBlockAck (void)
  {
    m_txop->MissedBlockAck ();
  }
  virtual void StartNext (void)
  {
    m_txop->StartNext ();
  }
  virtual void Cancel (void)
  {
    m_txop->Cancel ();
  }
  virtual void EndTxNoAck (void)
  {
    m_txop->EndTxNoAck ();
  }
  virtual Ptr<WifiMacQueue> GetQueue (void)
  {
    return m_txop->GetEdcaQueue ();
  }

private:
  EdcaTxopN *m_txop;
};


class EdcaTxopN::AggregationCapableTransmissionListener : public MacLowAggregationCapableTransmissionListener
{
public:
  AggregationCapableTransmissionListener (EdcaTxopN * txop)
    : MacLowAggregationCapableTransmissionListener (),
      m_txop (txop)
  {
  }
  virtual ~AggregationCapableTransmissionListener ()
  {
  }

  virtual void BlockAckInactivityTimeout (Mac48Address address, uint8_t tid)
  {
    m_txop->SendDelbaFrame (address, tid, false);
  }
  virtual Ptr<WifiMacQueue> GetQueue (void)
  {
    return m_txop->GetEdcaQueue ();
  }
  virtual  void CompleteTransfer (Mac48Address recipient, uint8_t tid)
  {
    m_txop->CompleteAmpduTransfer (recipient, tid);
  }
  virtual void SetAmpdu (bool ampdu)
  {
    return m_txop->SetAmpduExist (ampdu);
  }
  virtual void CompleteMpduTx (Ptr<const Packet> packet, WifiMacHeader hdr, Time tstamp)
  {
    m_txop->CompleteMpduTx (packet, hdr, tstamp);
  }
  virtual uint16_t GetNextSequenceNumberfor (WifiMacHeader *hdr)
  {
    return m_txop->GetNextSequenceNumberfor (hdr);
  }
  virtual uint16_t PeekNextSequenceNumberfor (WifiMacHeader *hdr)
  {
    return m_txop->PeekNextSequenceNumberfor (hdr);
  }
  virtual Ptr<const Packet> PeekNextPacketInBaQueue (WifiMacHeader &header, Mac48Address recipient, uint8_t tid, Time *timestamp)
  {
    return m_txop->PeekNextRetransmitPacket (header, recipient, tid, timestamp);
  }
  virtual void RemoveFromBaQueue (uint8_t tid, Mac48Address recipient, uint16_t seqnumber)
  {
    m_txop->RemoveRetransmitPacket (tid, recipient, seqnumber);
  }
  virtual bool GetBlockAckAgreementExists (Mac48Address address, uint8_t tid)
  {
    return m_txop->GetBaAgreementExists (address,tid);
  }
  virtual uint32_t GetNOutstandingPackets (Mac48Address address, uint8_t tid)
  {
    return m_txop->GetNOutstandingPacketsInBa (address, tid);
  }
  virtual uint32_t GetNRetryNeededPackets (Mac48Address recipient, uint8_t tid) const
  {
    return m_txop->GetNRetryNeededPackets (recipient, tid);
  }
  virtual Ptr<MsduAggregator> GetMsduAggregator (void) const
  {
    return m_txop->GetMsduAggregator ();
  }
  virtual Mac48Address GetSrcAddressForAggregation (const WifiMacHeader &hdr)
  {
    return m_txop->MapSrcAddressForAggregation (hdr);
  }
  virtual Mac48Address GetDestAddressForAggregation (const WifiMacHeader &hdr)
  {
    return m_txop->MapDestAddressForAggregation (hdr);
  }

private:
  EdcaTxopN *m_txop;
};

NS_OBJECT_ENSURE_REGISTERED (EdcaTxopN);

TypeId
EdcaTxopN::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::EdcaTxopN")
    .SetParent<ns3::Dcf> ()
    .SetGroupName ("Wifi")
    .AddConstructor<EdcaTxopN> ()
    .AddAttribute ("BlockAckThreshold",
                   "If number of packets in this queue reaches this value, "
                   "block ack mechanism is used. If this value is 0, block ack is never used.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&EdcaTxopN::SetBlockAckThreshold,
                                         &EdcaTxopN::GetBlockAckThreshold),
                   MakeUintegerChecker<uint8_t> (0, 64))
    .AddAttribute ("BlockAckInactivityTimeout",
                   "Represents max time (blocks of 1024 micro seconds) allowed for block ack"
                   "inactivity. If this value isn't equal to 0 a timer start after that a"
                   "block ack setup is completed and will be reset every time that a block"
                   "ack frame is received. If this value is 0, block ack inactivity timeout won't be used.",
                   UintegerValue (0),
                   MakeUintegerAccessor (&EdcaTxopN::SetBlockAckInactivityTimeout),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("Queue",
                   "The WifiMacQueue object",
                   PointerValue (),
                   MakePointerAccessor (&EdcaTxopN::GetEdcaQueue),
                   MakePointerChecker<WifiMacQueue> ())
    .AddTraceSource ("TxFailures", "Incremented for each missed ACK",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_failures),
                    "ns3::Traced::Value::Uint64Callback")
    .AddTraceSource ("TxCollisions", "Incremented for tx while channel busy",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_collisions),
                    "ns3::Traced::Value::Uint64Callback")
    .AddTraceSource ("TxSuccesses", "Incremented for each ACK",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_successes),
                    "ns3::Traced::Value::Uint64Callback")
    .AddTraceSource ("TxAttempts", "Incremented for every access to DCF",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_txAttempts),
                    "ns3::Traced::Value:Uint64Callback")
    .AddTraceSource ("BackoffCounter", "Changes in the assigned backoff",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_boCounter),
                    "ns3::Traced::Value:Uint32Callback")
    .AddTraceSource ("Bitmap", "The bitmap state after every cycle",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_ecaBitmap),
                    "ns3::Traced::Value::TracedEcaBitmap")
    .AddTraceSource ("SrReductionAttempts", "Number of reduction attempts",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_scheduleReductionAttempts),
                    "ns3::Traced::Value::Uint32Callback")
    .AddTraceSource ("SrReductions", "Number of SR reductions",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_scheduleReductions),
                    "ns3::Traced::Value:Uint32Callback")
    .AddTraceSource ("SrReductionFailed", "Times does not comply with SR criteria",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_scheduleReductionFailed),
                    "ns3::Traced::Value:Uint32Callback")
    .AddTraceSource ("FsAggregated", "Number of frames aggregated",
                    MakeTraceSourceAccessor (&EdcaTxopN::m_fsAggregated),
                    "ns3::Traced::Value:Uint16Callback")
  ;
  return tid;
}

EdcaTxopN::EdcaTxopN ()
  : m_manager (0),
    m_currentPacket (0),
    m_aggregator (0),
    m_typeOfStation (STA),
    m_blockAckType (COMPRESSED_BLOCK_ACK),
    m_ampduExist (false),
    m_fairShare (false),
    m_fsAggregation (0),
    m_srActivationThreshold (1),
    m_srBeingFilled (false),
    m_srIterations (0),
    m_srReductionFactor (1),
    m_scheduleRecentlyReduced (false),
    m_srPreviousCw (0),
    m_failures (0),
    m_collisions (0),
    m_successes (0),
    m_txAttempts (0),
    m_boCounter (0xFFFFFFFF),
    m_ecaBitmap (false),
    m_scheduleReductions (0),
    m_scheduleReductionAttempts (0),
    m_scheduleReductionFailed (0),
    m_fsAggregated (0xFFFF)
{
  NS_LOG_FUNCTION (this);
  m_transmissionListener = new EdcaTxopN::TransmissionListener (this);
  m_blockAckListener = new EdcaTxopN::AggregationCapableTransmissionListener (this);
  m_dcf = new EdcaTxopN::Dcf (this);
  m_queue = CreateObject<WifiMacQueue> ();
  m_rng = new RealRandomStream ();
  m_qosBlockedDestinations = new QosBlockedDestinations ();
  m_baManager = new BlockAckManager ();
  m_baManager->SetQueue (m_queue);
  m_baManager->SetBlockAckType (m_blockAckType);
  m_baManager->SetBlockDestinationCallback (MakeCallback (&QosBlockedDestinations::Block, m_qosBlockedDestinations));
  m_baManager->SetUnblockDestinationCallback (MakeCallback (&QosBlockedDestinations::Unblock, m_qosBlockedDestinations));
  m_baManager->SetMaxPacketDelay (m_queue->GetMaxDelay ());
  m_baManager->SetTxOkCallback (MakeCallback (&EdcaTxopN::BaTxOk, this));
  m_baManager->SetTxFailedCallback (MakeCallback (&EdcaTxopN::BaTxFailed, this));
}

EdcaTxopN::~EdcaTxopN ()
{
  NS_LOG_FUNCTION (this);
}

void
EdcaTxopN::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_queue = 0;
  m_low = 0;
  m_stationManager = 0;
  delete m_transmissionListener;
  delete m_dcf;
  delete m_rng;
  delete m_qosBlockedDestinations;
  delete m_baManager;
  delete m_blockAckListener;
  m_transmissionListener = 0;
  m_dcf = 0;
  m_rng = 0;
  m_qosBlockedDestinations = 0;
  m_baManager = 0;
  m_blockAckListener = 0;
  m_txMiddle = 0;
  m_aggregator = 0;
}

bool
EdcaTxopN::GetBaAgreementExists (Mac48Address address, uint8_t tid)
{
  return m_baManager->ExistsAgreement (address, tid);
}

uint32_t
EdcaTxopN::GetNOutstandingPacketsInBa (Mac48Address address, uint8_t tid)
{
  return m_baManager->GetNBufferedPackets (address, tid);
}

uint32_t
EdcaTxopN::GetNRetryNeededPackets (Mac48Address recipient, uint8_t tid) const
{
  return m_baManager->GetNRetryNeededPackets (recipient, tid);
}

void
EdcaTxopN::CompleteAmpduTransfer (Mac48Address recipient, uint8_t tid)
{
  m_baManager->CompleteAmpduExchange (recipient, tid);
}

void
EdcaTxopN::SetManager (Ptr<DcfManager> manager)
{
  NS_LOG_FUNCTION (this << manager);
  m_manager = manager;
  m_manager->Add (m_dcf);
}

void
EdcaTxopN::SetTxOkCallback (TxOk callback)
{
  NS_LOG_FUNCTION (this << &callback);
  m_txOkCallback = callback;
}

void
EdcaTxopN::SetTxFailedCallback (TxFailed callback)
{
  NS_LOG_FUNCTION (this << &callback);
  m_txFailedCallback = callback;
}

void
EdcaTxopN::SetWifiRemoteStationManager (Ptr<WifiRemoteStationManager> remoteManager)
{
  NS_LOG_FUNCTION (this << remoteManager);
  m_stationManager = remoteManager;
  m_baManager->SetWifiRemoteStationManager (m_stationManager);
}

void
EdcaTxopN::SetTypeOfStation (enum TypeOfStation type)
{
  NS_LOG_FUNCTION (this << static_cast<uint32_t> (type));
  m_typeOfStation = type;
}

enum TypeOfStation
EdcaTxopN::GetTypeOfStation (void) const
{
  NS_LOG_FUNCTION (this);
  return m_typeOfStation;
}

Ptr<WifiMacQueue >
EdcaTxopN::GetEdcaQueue () const
{
  NS_LOG_FUNCTION (this);
  return m_queue;
}

void
EdcaTxopN::SetMinCw (uint32_t minCw)
{
  NS_LOG_FUNCTION (this << minCw);
  m_dcf->SetCwMin (minCw);
}

void
EdcaTxopN::SetMaxCw (uint32_t maxCw)
{
  NS_LOG_FUNCTION (this << maxCw);
  m_dcf->SetCwMax (maxCw);
}

void
EdcaTxopN::SetAifsn (uint32_t aifsn)
{
  NS_LOG_FUNCTION (this << aifsn);
  m_dcf->SetAifsn (aifsn);
}

uint32_t
EdcaTxopN::GetMinCw (void) const
{
  NS_LOG_FUNCTION (this);
  return m_dcf->GetCwMin ();
}

uint32_t
EdcaTxopN::GetMaxCw (void) const
{
  NS_LOG_FUNCTION (this);
  return m_dcf->GetCwMax ();
}

uint32_t
EdcaTxopN::GetAifsn (void) const
{
  NS_LOG_FUNCTION (this);
  return m_dcf->GetAifsn ();
}

void
EdcaTxopN::SetTxMiddle (MacTxMiddle *txMiddle)
{
  NS_LOG_FUNCTION (this << txMiddle);
  m_txMiddle = txMiddle;
}

Ptr<MacLow>
EdcaTxopN::Low (void)
{
  NS_LOG_FUNCTION (this);
  return m_low;
}

void
EdcaTxopN::SetLow (Ptr<MacLow> low)
{
  NS_LOG_FUNCTION (this << low);
  m_low = low;
}

bool
EdcaTxopN::NeedsAccess (void) const
{
  NS_LOG_FUNCTION (this);
  return !m_queue->IsEmpty () || m_currentPacket != 0 || m_baManager->HasPackets ();
}

uint16_t EdcaTxopN::GetNextSequenceNumberfor (WifiMacHeader *hdr)
{
  return m_txMiddle->GetNextSequenceNumberfor (hdr);
}

uint16_t EdcaTxopN::PeekNextSequenceNumberfor (WifiMacHeader *hdr)
{
  return m_txMiddle->PeekNextSequenceNumberfor (hdr);
}

Ptr<const Packet>
EdcaTxopN::PeekNextRetransmitPacket (WifiMacHeader &header,Mac48Address recipient, uint8_t tid, Time *timestamp)
{
  return m_baManager->PeekNextPacket (header,recipient,tid, timestamp);
}

void
EdcaTxopN::RemoveRetransmitPacket (uint8_t tid, Mac48Address recipient, uint16_t seqnumber)
{
  m_baManager->RemovePacket (tid, recipient, seqnumber);
}

void
EdcaTxopN::SetFairShare (void)
{
  m_fairShare = true;
  m_fsAggregation = 0;
}

bool
EdcaTxopN::IsFairShare (void)
{
  return m_fairShare;
}

void
EdcaTxopN::NotifyAccessGranted (void)
{
  NS_LOG_FUNCTION (this);
  if (m_currentPacket == 0)
    {
      if (m_queue->IsEmpty () && !m_baManager->HasPackets ())
        {
          NS_LOG_DEBUG ("queue is empty");
          return;
        }
      if (m_baManager->HasBar (m_currentBar))
        {
          SendBlockAckRequest (m_currentBar);
          return;
        }
      /* check if packets need retransmission are stored in BlockAckManager */
      m_currentPacket = m_baManager->GetNextPacket (m_currentHdr);
      if (m_currentPacket == 0)
        {
          if (m_queue->PeekFirstAvailable (&m_currentHdr, m_currentPacketTimestamp, m_qosBlockedDestinations) == 0)
            {
              NS_LOG_DEBUG ("no available packets in the queue");
              return;
            }
          if (m_currentHdr.IsQosData () && !m_currentHdr.GetAddr1 ().IsBroadcast ()
              && m_blockAckThreshold > 0
              && !m_baManager->ExistsAgreement (m_currentHdr.GetAddr1 (), m_currentHdr.GetQosTid ())
              && SetupBlockAckIfNeeded ())
            {
              return;
            }
          m_currentPacket = m_queue->DequeueFirstAvailable (&m_currentHdr, m_currentPacketTimestamp, m_qosBlockedDestinations);
          NS_ASSERT (m_currentPacket != 0);

          uint16_t sequence = m_txMiddle->GetNextSequenceNumberfor (&m_currentHdr);
          m_currentHdr.SetSequenceNumber (sequence);
          m_stationManager->UpdateFragmentationThreshold ();
          m_currentHdr.SetFragmentNumber (0);
          m_currentHdr.SetNoMoreFragments ();
          m_currentHdr.SetNoRetry ();
          m_fragmentNumber = 0;
          NS_LOG_DEBUG ("dequeued size=" << m_currentPacket->GetSize () <<
                        ", to=" << m_currentHdr.GetAddr1 () <<
                        ", seq=" << m_currentHdr.GetSequenceControl ());
          if (m_currentHdr.IsQosData () && !m_currentHdr.GetAddr1 ().IsBroadcast ())
            {
              VerifyBlockAck ();
            }
        }
    }

  MacLowTransmissionParameters params;
  params.DisableOverrideDurationId ();
  if (m_currentHdr.GetAddr1 ().IsGroup ())
    {
      params.DisableRts ();
      params.DisableAck ();
      params.DisableNextData ();
      m_low->StartTransmission (m_currentPacket,
                                &m_currentHdr,
                                params,
                                m_transmissionListener);

      NS_LOG_DEBUG ("tx broadcast");
    }
  else if (m_currentHdr.GetType () == WIFI_MAC_CTL_BACKREQ)
    {
      SendBlockAckRequest (m_currentBar);
    }
  else
    {
      /* Registering access to the channel when data packets are to be sent */
      m_txAttempts++;
      if (m_currentHdr.IsQosData () && m_currentHdr.IsQosBlockAck ())
        {
          params.DisableAck ();
        }
      else
        {
          params.EnableAck ();
        }
      if (NeedFragmentation () && ((m_currentHdr.IsQosData ()
                                    && !m_currentHdr.IsQosAmsdu ())
                                   ||
                                   (m_currentHdr.IsData ()
                                    && !m_currentHdr.IsQosData () && m_currentHdr.IsQosAmsdu ()))
          && (m_blockAckThreshold == 0
              || m_blockAckType == BASIC_BLOCK_ACK))
        {
          //With COMPRESSED_BLOCK_ACK fragmentation must be avoided.
          params.DisableRts ();
          WifiMacHeader hdr;
          Ptr<Packet> fragment = GetFragmentPacket (&hdr);
          if (IsLastFragment ())
            {
              NS_LOG_DEBUG ("fragmenting last fragment size=" << fragment->GetSize ());
              params.DisableNextData ();
            }
          else
            {
              NS_LOG_DEBUG ("fragmenting size=" << fragment->GetSize ());
              params.EnableNextData (GetNextFragmentSize ());
            }
          m_low->StartTransmission (fragment, &hdr, params,
                                    m_transmissionListener);
        }
      else
        {
          WifiMacHeader peekedHdr;
          Time tstamp;
          if (m_currentHdr.IsQosData ()
              && m_queue->PeekByTidAndAddress (&peekedHdr, m_currentHdr.GetQosTid (),
                                               WifiMacHeader::ADDR1, m_currentHdr.GetAddr1 (), &tstamp)
              && !m_currentHdr.GetAddr1 ().IsBroadcast ()
              && m_aggregator != 0 && !m_currentHdr.IsRetry ())
            {
              /* here is performed aggregation */
              Ptr<Packet> currentAggregatedPacket = Create<Packet> ();
              m_aggregator->Aggregate (m_currentPacket, currentAggregatedPacket,
                                       MapSrcAddressForAggregation (peekedHdr),
                                       MapDestAddressForAggregation (peekedHdr));
              bool aggregated = false;
              bool isAmsdu = false;
              Ptr<const Packet> peekedPacket = m_queue->PeekByTidAndAddress (&peekedHdr, m_currentHdr.GetQosTid (),
                                                                             WifiMacHeader::ADDR1,
                                                                             m_currentHdr.GetAddr1 (), &tstamp);
              if (!IsFairShare ())
                {
                  while (peekedPacket != 0)
                    {
                      aggregated = m_aggregator->Aggregate (peekedPacket, currentAggregatedPacket,
                                                            MapSrcAddressForAggregation (peekedHdr),
                                                            MapDestAddressForAggregation (peekedHdr));
                      if (aggregated)
                        {
                          isAmsdu = true;
                          m_queue->Remove (peekedPacket);
                        }
                      else
                        {
                          break;
                        }
                      peekedPacket = m_queue->PeekByTidAndAddress (&peekedHdr, m_currentHdr.GetQosTid (),
                                                                   WifiMacHeader::ADDR1, m_currentHdr.GetAddr1 (), &tstamp);
                    }
                }
              else
                {
                  uint32_t count = 1;
                  SetAggregationWithFairShare ();
                  NS_ASSERT (m_fsAggregation >= 0 && m_fsAggregation <= 6);
                  uint16_t totalFrames = std::pow (2, m_fsAggregation);
                  if (totalFrames == 1)
                    {
                      NS_LOG_DEBUG ("On the zeroth backoff stage. Transmitting unicast");
                      count = totalFrames;
                    }
                    
                  while (count < totalFrames && peekedPacket != 0)
                    {
                      NS_LOG_DEBUG ("Peeked: " << count);
                      NS_LOG_DEBUG ("Aggregating frame " << count + 1 << " of " << totalFrames);
                      aggregated = m_aggregator->Aggregate (peekedPacket, currentAggregatedPacket,
                                                            MapSrcAddressForAggregation (peekedHdr),
                                                            MapDestAddressForAggregation (peekedHdr));
                      if (aggregated)
                        {
                          isAmsdu = false;
                          m_queue->Remove (peekedPacket);
                        }
                      else
                        {
                          NS_LOG_DEBUG ("Not performing aggregation");
                          break;
                        }
                      peekedPacket = m_queue->PeekByTidAndAddress (&peekedHdr, m_currentHdr.GetQosTid (),
                                                                       WifiMacHeader::ADDR1, m_currentHdr.GetAddr1 (), &tstamp);
                      count ++;
                    }
                }

              if (isAmsdu)
                {
                  m_currentHdr.SetQosAmsdu ();
                  m_currentHdr.SetAddr3 (m_low->GetBssid ());
                  m_currentPacket = currentAggregatedPacket;
                  currentAggregatedPacket = 0;
                  NS_LOG_DEBUG ("tx unicast A-MSDU");
                }
            }
          if (NeedRts ())
            {
              params.EnableRts ();
              NS_LOG_DEBUG ("tx unicast rts");
            }
          else
            {
              params.DisableRts ();
              NS_LOG_DEBUG ("tx unicast");
            }
          params.DisableNextData ();
          m_low->StartTransmission (m_currentPacket, &m_currentHdr,
                                    params, m_transmissionListener);
          if (!GetAmpduExist ())
            {
              CompleteTx ();
            }
        }
    }
}

void 
EdcaTxopN::NotifyInternalCollision (void)
{
  NS_LOG_FUNCTION (this);
  NotifyCollision ();
}

void
EdcaTxopN::NotifyCollision (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("Notifying collision. Stickiness " << m_manager->GetStickiness ());
  if (m_manager->GetEnvironmentForECA () && m_manager->GetStickiness () > 0)
    {
      NS_ASSERT (m_manager->GetStickiness () > 0);
      m_manager->ReduceStickiness ();
      m_dcf->StartBackoffNow (deterministicBackoff (m_dcf->GetCw ()));
    }
  else
    {
      m_collisions++;
      if (m_manager->GetScheduleReset ())
        ResetSrMetrics ();
      if (!m_manager->GetHysteresisForECA ())
        m_dcf->ResetCw ();
      m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
    }
  RestartAccessIfNeeded ();
}

void
EdcaTxopN::GotCts (double snr, WifiMode txMode)
{
  NS_LOG_FUNCTION (this << snr << txMode);
  NS_LOG_DEBUG ("got cts");
}

void
EdcaTxopN::MissedCts (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("missed cts");
  if (!NeedRtsRetransmission ())
    {
      NS_LOG_DEBUG ("Cts Fail");
      bool resetCurrentPacket = true;
      m_stationManager->ReportFinalRtsFailed (m_currentHdr.GetAddr1 (), &m_currentHdr);
      if (!m_txFailedCallback.IsNull ())
        {
          m_txFailedCallback (m_currentHdr);
        }
      if (GetAmpduExist ())
        {
          m_low->FlushAggregateQueue ();
          uint8_t tid = 0;
          if (m_currentHdr.IsQosData ())
            {
              tid = m_currentHdr.GetQosTid ();
            }
          else
            {
              NS_FATAL_ERROR ("Current packet is not Qos Data");
            }

          if (GetBaAgreementExists (m_currentHdr.GetAddr1 (), tid))
            {
              NS_LOG_DEBUG ("Transmit Block Ack Request");
              CtrlBAckRequestHeader reqHdr;
              reqHdr.SetType (COMPRESSED_BLOCK_ACK);
              reqHdr.SetStartingSequence (m_txMiddle->PeekNextSequenceNumberfor (&m_currentHdr));
              reqHdr.SetTidInfo (tid);
              reqHdr.SetHtImmediateAck (true);
              Ptr<Packet> bar = Create<Packet> ();
              bar->AddHeader (reqHdr);
              Bar request (bar, m_currentHdr.GetAddr1 (), tid, reqHdr.MustSendHtImmediateAck ());
              m_currentBar = request;
              WifiMacHeader hdr;
              hdr.SetType (WIFI_MAC_CTL_BACKREQ);
              hdr.SetAddr1 (request.recipient);
              hdr.SetAddr2 (m_low->GetAddress ());
              hdr.SetAddr3 (m_low->GetBssid ());
              hdr.SetDsNotTo ();
              hdr.SetDsNotFrom ();
              hdr.SetNoRetry ();
              hdr.SetNoMoreFragments ();
              m_currentPacket = request.bar;
              m_currentHdr = hdr;
              resetCurrentPacket = false;
            }
        }
      //to reset the dcf.
      if (resetCurrentPacket == true)
        {
          m_currentPacket = 0;
        }
      m_dcf->ResetCw ();
    }
  else
    {
      m_dcf->UpdateFailedCw ();
    }
  m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
  RestartAccessIfNeeded ();
}

void
EdcaTxopN::NotifyChannelSwitching (void)
{
  NS_LOG_FUNCTION (this);
  m_queue->Flush ();
  m_currentPacket = 0;
}

void
EdcaTxopN::NotifySleep (void)
{
  NS_LOG_FUNCTION (this);
  if (m_currentPacket != 0)
    {
      m_queue->PushFront (m_currentPacket, m_currentHdr);
      m_currentPacket = 0;
    }
}

void
EdcaTxopN::NotifyWakeUp (void)
{
  NS_LOG_FUNCTION (this);
  RestartAccessIfNeeded ();
}

void
EdcaTxopN::Queue (Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
  NS_LOG_FUNCTION (this << packet << &hdr);
  WifiMacTrailer fcs;
  uint32_t fullPacketSize = hdr.GetSerializedSize () + packet->GetSize () + fcs.GetSerializedSize ();
  m_stationManager->PrepareForQueue (hdr.GetAddr1 (), &hdr,
                                     packet, fullPacketSize);
  m_queue->Enqueue (packet, hdr);
  StartAccessIfNeeded ();
}

void
EdcaTxopN::GotAck (double snr, WifiMode txMode)
{
  m_successes++;
  NS_LOG_FUNCTION (this << snr << txMode);
  if (!NeedFragmentation ()
      || IsLastFragment ()
      || m_currentHdr.IsQosAmsdu ())
    {
      NS_LOG_DEBUG ("got ack. tx done.");
      if (!m_txOkCallback.IsNull ())
        {
          m_txOkCallback (m_currentHdr);
        }

      if (m_currentHdr.IsAction ())
        {
          WifiActionHeader actionHdr;
          Ptr<Packet> p = m_currentPacket->Copy ();
          p->RemoveHeader (actionHdr);
          if (actionHdr.GetCategory () == WifiActionHeader::BLOCK_ACK
              && actionHdr.GetAction ().blockAck == WifiActionHeader::BLOCK_ACK_DELBA)
            {
              MgtDelBaHeader delBa;
              p->PeekHeader (delBa);
              if (delBa.IsByOriginator ())
                {
                  m_baManager->TearDownBlockAck (m_currentHdr.GetAddr1 (), delBa.GetTid ());
                }
              else
                {
                  m_low->DestroyBlockAckAgreement (m_currentHdr.GetAddr1 (), delBa.GetTid ());
                }
            }
        }
      m_currentPacket = 0;

      /* Beginning of CSMA/ECA */
      AddConsecutiveSuccess ();
      m_manager->ResetStickiness ();

      if (m_manager->GetEnvironmentForECA ())
        {
          if (!m_manager->GetHysteresisForECA ())
            m_dcf->ResetCw ();

          if (m_manager->GetScheduleReset ())
            {
              if (m_scheduleRecentlyReduced == true)
                KeepScheduleReductionIfAny ();

              if (m_srActivationThreshold == 0)
                SetScheduleResetActivationThreshold ( (m_dcf->GetCwMax ()+ 1) / (m_dcf->GetCw () + 1) );

              if (GetConsecutiveSuccesses () >= GetScheduleResetActivationThreshold ())
                {
                  if (!m_srBeingFilled)
                    {
                      SetScheduleResetThreshold ();
                      uint32_t size = ((m_dcf->GetCw () + 1) / 2) + 1;
                      m_manager->StartNewEcaBitmap (size);
                      m_srBeingFilled = true;
                      m_manager->SetFillingTheBitmap ();
                      m_srIterations = GetConsecutiveSuccesses ();
                    }
                  else
                    {
                      if ( (GetConsecutiveSuccesses () - m_srIterations) >= GetScheduleResetThreshold ())
                        {
                          NS_LOG_DEBUG ("Checking bitmap for Schedule Reset");
                          if (CanWeReduceTheSchedule ())
                            {
                              ModifyCwAccordingToScheduleReduction ();
                            }
                          else
                            {
                              NS_LOG_DEBUG ("We cannot reduce the schedule");
                            }
                          m_srBeingFilled = false;
                          m_manager->SetNotFillingTheBitmap ();
                          ResetConsecutiveSuccess ();
                          m_srIterations = GetConsecutiveSuccesses ();
                        }
                    }
                }
            }
          m_dcf->StartBackoffNow (deterministicBackoff (m_dcf->GetCw ()));
        }
      else /* End of CSMA/ECA */
        {
          m_dcf->ResetCw ();
          m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
          RestartAccessIfNeeded ();
        }
    }
  else
    {
      NS_LOG_DEBUG ("got ack. tx not done, size=" << m_currentPacket->GetSize ());
    }
}

void
EdcaTxopN::MissedAck (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("missed ack");
  if (!NeedDataRetransmission ())
    {
      NS_LOG_DEBUG ("Ack Fail");
      m_stationManager->ReportFinalDataFailed (m_currentHdr.GetAddr1 (), &m_currentHdr);
      bool resetCurrentPacket = true;
      if (!m_txFailedCallback.IsNull ())
        {
          m_txFailedCallback (m_currentHdr);
        }
      if (GetAmpduExist ())
        {
          uint8_t tid = 0;
          if (m_currentHdr.IsQosData ())
            {
              tid = m_currentHdr.GetQosTid ();
            }
          else
            {
              NS_FATAL_ERROR ("Current packet is not Qos Data");
            }

          if (GetBaAgreementExists (m_currentHdr.GetAddr1 (), tid))
            {
              //send Block ACK Request in order to shift WinStart at the receiver
              NS_LOG_DEBUG ("Transmit Block Ack Request");
              CtrlBAckRequestHeader reqHdr;
              reqHdr.SetType (COMPRESSED_BLOCK_ACK);
              reqHdr.SetStartingSequence (m_txMiddle->PeekNextSequenceNumberfor (&m_currentHdr));
              reqHdr.SetTidInfo (tid);
              reqHdr.SetHtImmediateAck (true);
              Ptr<Packet> bar = Create<Packet> ();
              bar->AddHeader (reqHdr);
              Bar request (bar, m_currentHdr.GetAddr1 (), tid, reqHdr.MustSendHtImmediateAck ());
              m_currentBar = request;
              WifiMacHeader hdr;
              hdr.SetType (WIFI_MAC_CTL_BACKREQ);
              hdr.SetAddr1 (request.recipient);
              hdr.SetAddr2 (m_low->GetAddress ());
              hdr.SetAddr3 (m_low->GetBssid ());
              hdr.SetDsNotTo ();
              hdr.SetDsNotFrom ();
              hdr.SetNoRetry ();
              hdr.SetNoMoreFragments ();
              m_currentPacket = request.bar;
              m_currentHdr = hdr;
              resetCurrentPacket = false;
            }
        }
      //to reset the dcf.
      if (resetCurrentPacket == true)
        {
          m_currentPacket = 0;
        }
      m_dcf->ResetCw ();
      m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
    }
  else
    {
      NS_LOG_DEBUG ("Retransmit");
      m_currentHdr.SetRetry ();

      /* CSMA/ECA */
      if (m_manager->GetStickiness () == 0)
        {
          m_failures++;
          ResetConsecutiveSuccess ();
          if (m_manager->GetScheduleReset ())
            ResetSrMetrics ();
          m_dcf->UpdateFailedCw ();
          m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
        }
      else
        {
          NS_ASSERT (m_manager->GetStickiness () > 0);
          m_manager->ReduceStickiness ();
          m_dcf->StartBackoffNow (deterministicBackoff (m_dcf->GetCw ()));
        }
    }
  RestartAccessIfNeeded ();
}

void
EdcaTxopN::MissedBlockAck (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("missed block ack");
  if (NeedBarRetransmission ())
    {
      if (!GetAmpduExist ())
        {
          //should i report this to station addressed by ADDR1?
          NS_LOG_DEBUG ("Retransmit block ack request");
          m_currentHdr.SetRetry ();
        }
      else
        {
          //standard says when loosing a BlockAck originator may send a BAR page 139
          NS_LOG_DEBUG ("Transmit Block Ack Request");
          CtrlBAckRequestHeader reqHdr;
          reqHdr.SetType (COMPRESSED_BLOCK_ACK);
          uint8_t tid = 0;
          if (m_currentHdr.IsQosData ())
            {
              tid = m_currentHdr.GetQosTid ();
              reqHdr.SetStartingSequence (m_currentHdr.GetSequenceNumber ());
            }
          else if (m_currentHdr.IsBlockAckReq ())
            {
              CtrlBAckRequestHeader baReqHdr;
              m_currentPacket->PeekHeader (baReqHdr);
              tid = baReqHdr.GetTidInfo ();
              reqHdr.SetStartingSequence (baReqHdr.GetStartingSequence ());
            }
          else if (m_currentHdr.IsBlockAck ())
            {
              CtrlBAckResponseHeader baRespHdr;
              m_currentPacket->PeekHeader (baRespHdr);
              tid = baRespHdr.GetTidInfo ();
              reqHdr.SetStartingSequence (m_currentHdr.GetSequenceNumber ());
            }
          reqHdr.SetTidInfo (tid);
          reqHdr.SetHtImmediateAck (true);
          Ptr<Packet> bar = Create<Packet> ();
          bar->AddHeader (reqHdr);
          Bar request (bar, m_currentHdr.GetAddr1 (), tid, reqHdr.MustSendHtImmediateAck ());
          m_currentBar = request;
          WifiMacHeader hdr;
          hdr.SetType (WIFI_MAC_CTL_BACKREQ);
          hdr.SetAddr1 (request.recipient);
          hdr.SetAddr2 (m_low->GetAddress ());
          hdr.SetAddr3 (m_low->GetBssid ());
          hdr.SetDsNotTo ();
          hdr.SetDsNotFrom ();
          hdr.SetNoRetry ();
          hdr.SetNoMoreFragments ();

          m_currentPacket = request.bar;
          m_currentHdr = hdr;
        }
      m_dcf->UpdateFailedCw ();
    }
  else
    {
      NS_LOG_DEBUG ("Block Ack Request Fail");
      //to reset the dcf.
      m_currentPacket = 0;
      m_dcf->ResetCw ();
    }
  m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
  RestartAccessIfNeeded ();
}

Ptr<MsduAggregator>
EdcaTxopN::GetMsduAggregator (void) const
{
  return m_aggregator;
}

void
EdcaTxopN::RestartAccessIfNeeded (void)
{
  NS_LOG_FUNCTION (this);
  if ((m_currentPacket != 0
       || !m_queue->IsEmpty () || m_baManager->HasPackets ())
      && !m_dcf->IsAccessRequested ())
    {
      m_manager->RequestAccess (m_dcf);
    }
}

void
EdcaTxopN::StartAccessIfNeeded (void)
{
  NS_LOG_FUNCTION (this);
  if (m_currentPacket == 0
      && (!m_queue->IsEmpty () || m_baManager->HasPackets ())
      && !m_dcf->IsAccessRequested ())
    {
      m_manager->RequestAccess (m_dcf);
    }
}

bool
EdcaTxopN::NeedRts (void)
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->NeedRts (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                    m_currentPacket);
}

bool
EdcaTxopN::NeedRtsRetransmission (void)
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->NeedRtsRetransmission (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                                  m_currentPacket);
}

bool
EdcaTxopN::NeedDataRetransmission (void)
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->NeedDataRetransmission (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                                   m_currentPacket);
}

bool
EdcaTxopN::NeedBarRetransmission (void)
{
  uint8_t tid = 0;
  uint16_t seqNumber = 0;
  if (m_currentHdr.IsQosData ())
    {
      tid = m_currentHdr.GetQosTid ();
      seqNumber = m_currentHdr.GetSequenceNumber ();
    }
  else if (m_currentHdr.IsBlockAckReq ())
    {
      CtrlBAckRequestHeader baReqHdr;
      m_currentPacket->PeekHeader (baReqHdr);
      tid = baReqHdr.GetTidInfo ();
      seqNumber = baReqHdr.GetStartingSequence ();
    }
  else if (m_currentHdr.IsBlockAck ())
    {
      CtrlBAckResponseHeader baRespHdr;
      m_currentPacket->PeekHeader (baRespHdr);
      tid = baRespHdr.GetTidInfo ();
      seqNumber = m_currentHdr.GetSequenceNumber ();
    }
  return m_baManager->NeedBarRetransmission (tid, seqNumber, m_currentHdr.GetAddr1 ());
}

void
EdcaTxopN::NextFragment (void)
{
  NS_LOG_FUNCTION (this);
  m_fragmentNumber++;
}

void
EdcaTxopN::StartNext (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("start next packet fragment");
  /* this callback is used only for fragments. */
  NextFragment ();
  WifiMacHeader hdr;
  Ptr<Packet> fragment = GetFragmentPacket (&hdr);
  MacLowTransmissionParameters params;
  params.EnableAck ();
  params.DisableRts ();
  params.DisableOverrideDurationId ();
  if (IsLastFragment ())
    {
      params.DisableNextData ();
    }
  else
    {
      params.EnableNextData (GetNextFragmentSize ());
    }
  Low ()->StartTransmission (fragment, &hdr, params, m_transmissionListener);
}

void
EdcaTxopN::Cancel (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("transmission cancelled");
}

void
EdcaTxopN::EndTxNoAck (void)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG ("a transmission that did not require an ACK just finished");
  m_currentPacket = 0;

  if (!m_manager->GetHysteresisForECA ())
    m_dcf->ResetCw ();
  
  if (m_manager->GetEnvironmentForECA ())
    {
      m_dcf->StartBackoffNow (deterministicBackoff (m_dcf->GetCw ()));
    }
  else
    {
      m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
    }
  
  StartAccessIfNeeded ();
}

bool
EdcaTxopN::NeedFragmentation (void) const
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->NeedFragmentation (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                              m_currentPacket);
}

uint32_t
EdcaTxopN::GetFragmentSize (void)
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->GetFragmentSize (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                            m_currentPacket, m_fragmentNumber);
}

uint32_t
EdcaTxopN::GetNextFragmentSize (void)
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->GetFragmentSize (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                            m_currentPacket, m_fragmentNumber + 1);
}

uint32_t
EdcaTxopN::GetFragmentOffset (void)
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->GetFragmentOffset (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                              m_currentPacket, m_fragmentNumber);
}


bool
EdcaTxopN::IsLastFragment (void) const
{
  NS_LOG_FUNCTION (this);
  return m_stationManager->IsLastFragment (m_currentHdr.GetAddr1 (), &m_currentHdr,
                                           m_currentPacket, m_fragmentNumber);
}

Ptr<Packet>
EdcaTxopN::GetFragmentPacket (WifiMacHeader *hdr)
{
  NS_LOG_FUNCTION (this << hdr);
  *hdr = m_currentHdr;
  hdr->SetFragmentNumber (m_fragmentNumber);
  uint32_t startOffset = GetFragmentOffset ();
  Ptr<Packet> fragment;
  if (IsLastFragment ())
    {
      hdr->SetNoMoreFragments ();
    }
  else
    {
      hdr->SetMoreFragments ();
    }
  fragment = m_currentPacket->CreateFragment (startOffset,
                                              GetFragmentSize ());
  return fragment;
}

void
EdcaTxopN::SetAccessCategory (enum AcIndex ac)
{
  NS_LOG_FUNCTION (this << static_cast<uint32_t> (ac));
  m_ac = ac;
}

Mac48Address
EdcaTxopN::MapSrcAddressForAggregation (const WifiMacHeader &hdr)
{
  NS_LOG_FUNCTION (this << &hdr);
  Mac48Address retval;
  if (m_typeOfStation == STA || m_typeOfStation == ADHOC_STA)
    {
      retval = hdr.GetAddr2 ();
    }
  else
    {
      retval = hdr.GetAddr3 ();
    }
  return retval;
}

Mac48Address
EdcaTxopN::MapDestAddressForAggregation (const WifiMacHeader &hdr)
{
  NS_LOG_FUNCTION (this << &hdr);
  Mac48Address retval;
  if (m_typeOfStation == AP || m_typeOfStation == ADHOC_STA)
    {
      retval = hdr.GetAddr1 ();
    }
  else
    {
      retval = hdr.GetAddr3 ();
    }
  return retval;
}

void
EdcaTxopN::SetMsduAggregator (Ptr<MsduAggregator> aggr)
{
  NS_LOG_FUNCTION (this << aggr);
  NS_LOG_DEBUG ("Setting new aggregator");
  m_aggregator = aggr;
}

void
EdcaTxopN::PushFront (Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
  NS_LOG_FUNCTION (this << packet << &hdr);
  WifiMacTrailer fcs;
  uint32_t fullPacketSize = hdr.GetSerializedSize () + packet->GetSize () + fcs.GetSerializedSize ();
  m_stationManager->PrepareForQueue (hdr.GetAddr1 (), &hdr,
                                     packet, fullPacketSize);
  m_queue->PushFront (packet, hdr);
  StartAccessIfNeeded ();
}

void
EdcaTxopN::GotAddBaResponse (const MgtAddBaResponseHeader *respHdr, Mac48Address recipient)
{
  NS_LOG_FUNCTION (this << respHdr << recipient);
  NS_LOG_DEBUG ("received ADDBA response from " << recipient);
  uint8_t tid = respHdr->GetTid ();
  if (m_baManager->ExistsAgreementInState (recipient, tid, OriginatorBlockAckAgreement::PENDING))
    {
      if (respHdr->GetStatusCode ().IsSuccess ())
        {
          NS_LOG_DEBUG ("block ack agreement established with " << recipient);
          m_baManager->UpdateAgreement (respHdr, recipient);
        }
      else
        {
          NS_LOG_DEBUG ("discard ADDBA response" << recipient);
          m_baManager->NotifyAgreementUnsuccessful (recipient, tid);
        }
    }
  RestartAccessIfNeeded ();
}

void
EdcaTxopN::GotDelBaFrame (const MgtDelBaHeader *delBaHdr, Mac48Address recipient)
{
  NS_LOG_FUNCTION (this << delBaHdr << recipient);
  NS_LOG_DEBUG ("received DELBA frame from=" << recipient);
  m_baManager->TearDownBlockAck (recipient, delBaHdr->GetTid ());
}

void
EdcaTxopN::GotBlockAck (const CtrlBAckResponseHeader *blockAck, Mac48Address recipient, WifiMode txMode)
{
  NS_LOG_FUNCTION (this << blockAck << recipient);
  NS_LOG_DEBUG ("got block ack from=" << recipient);
  m_baManager->NotifyGotBlockAck (blockAck, recipient, txMode);
  if (!m_txOkCallback.IsNull ())
    {
      m_txOkCallback (m_currentHdr);
    }
  m_currentPacket = 0;
  m_dcf->ResetCw ();
  m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
  RestartAccessIfNeeded ();
}

void
EdcaTxopN::VerifyBlockAck (void)
{
  NS_LOG_FUNCTION (this);
  uint8_t tid = m_currentHdr.GetQosTid ();
  Mac48Address recipient = m_currentHdr.GetAddr1 ();
  uint16_t sequence = m_currentHdr.GetSequenceNumber ();
  if (m_baManager->ExistsAgreementInState (recipient, tid, OriginatorBlockAckAgreement::INACTIVE))
    {
      m_baManager->SwitchToBlockAckIfNeeded (recipient, tid, sequence);
    }
  if ((m_baManager->ExistsAgreementInState (recipient, tid, OriginatorBlockAckAgreement::ESTABLISHED)) && (m_low->GetMpduAggregator () == 0))
    {
      m_currentHdr.SetQosAckPolicy (WifiMacHeader::BLOCK_ACK);
    }
}

bool EdcaTxopN::GetAmpduExist (void)
{
  return m_ampduExist;
}

void EdcaTxopN::SetAmpduExist (bool ampdu)
{
  m_ampduExist = ampdu;
}

void
EdcaTxopN::CompleteTx (void)
{
  NS_LOG_FUNCTION (this);
  if (m_currentHdr.IsQosData () && m_currentHdr.IsQosBlockAck ())
    {
      if (!m_currentHdr.IsRetry ())
        {
          m_baManager->StorePacket (m_currentPacket, m_currentHdr, m_currentPacketTimestamp);
        }
      m_baManager->NotifyMpduTransmission (m_currentHdr.GetAddr1 (), m_currentHdr.GetQosTid (),
                                           m_txMiddle->GetNextSeqNumberByTidAndAddress (m_currentHdr.GetQosTid (),
                                                                                        m_currentHdr.GetAddr1 ()), WifiMacHeader::BLOCK_ACK);
    }
}

void
EdcaTxopN::CompleteMpduTx (Ptr<const Packet> packet, WifiMacHeader hdr, Time tstamp)
{
  NS_ASSERT (hdr.IsQosData ());
  m_baManager->StorePacket (packet, hdr, tstamp);
  m_baManager->NotifyMpduTransmission (hdr.GetAddr1 (), hdr.GetQosTid (),
                                       m_txMiddle->GetNextSeqNumberByTidAndAddress (hdr.GetQosTid (),
                                                                                    hdr.GetAddr1 ()), WifiMacHeader::NORMAL_ACK);
}

bool
EdcaTxopN::SetupBlockAckIfNeeded ()
{
  NS_LOG_FUNCTION (this);
  uint8_t tid = m_currentHdr.GetQosTid ();
  Mac48Address recipient = m_currentHdr.GetAddr1 ();

  uint32_t packets = m_queue->GetNPacketsByTidAndAddress (tid, WifiMacHeader::ADDR1, recipient);

  if (packets >= m_blockAckThreshold)
    {
      /* Block ack setup */
      uint16_t startingSequence = m_txMiddle->GetNextSeqNumberByTidAndAddress (tid, recipient);
      SendAddBaRequest (recipient, tid, startingSequence, m_blockAckInactivityTimeout, true);
      return true;
    }
  return false;
}

void
EdcaTxopN::SendBlockAckRequest (const struct Bar &bar)
{
  NS_LOG_FUNCTION (this << &bar);
  WifiMacHeader hdr;
  hdr.SetType (WIFI_MAC_CTL_BACKREQ);
  hdr.SetAddr1 (bar.recipient);
  hdr.SetAddr2 (m_low->GetAddress ());
  hdr.SetAddr3 (m_low->GetBssid ());
  hdr.SetDsNotTo ();
  hdr.SetDsNotFrom ();
  hdr.SetNoRetry ();
  hdr.SetNoMoreFragments ();

  m_currentPacket = bar.bar;
  m_currentHdr = hdr;

  MacLowTransmissionParameters params;
  params.DisableRts ();
  params.DisableNextData ();
  params.DisableOverrideDurationId ();
  if (bar.immediate)
    {
      if (m_blockAckType == BASIC_BLOCK_ACK)
        {
          params.EnableBasicBlockAck ();
        }
      else if (m_blockAckType == COMPRESSED_BLOCK_ACK)
        {
          params.EnableCompressedBlockAck ();
        }
      else if (m_blockAckType == MULTI_TID_BLOCK_ACK)
        {
          NS_FATAL_ERROR ("Multi-tid block ack is not supported");
        }
    }
  else
    {
      //Delayed block ack
      params.EnableAck ();
    }
  m_low->StartTransmission (m_currentPacket, &m_currentHdr, params, m_transmissionListener);
}

void
EdcaTxopN::CompleteConfig (void)
{
  NS_LOG_FUNCTION (this);
  m_baManager->SetTxMiddle (m_txMiddle);
  m_low->RegisterBlockAckListenerForAc (m_ac, m_blockAckListener);
  m_baManager->SetBlockAckInactivityCallback (MakeCallback (&EdcaTxopN::SendDelbaFrame, this));
}

void
EdcaTxopN::SetBlockAckThreshold (uint8_t threshold)
{
  NS_LOG_FUNCTION (this << static_cast<uint32_t> (threshold));
  m_blockAckThreshold = threshold;
  m_baManager->SetBlockAckThreshold (threshold);
}

void
EdcaTxopN::SetBlockAckInactivityTimeout (uint16_t timeout)
{
  NS_LOG_FUNCTION (this << timeout);
  m_blockAckInactivityTimeout = timeout;
}

uint8_t
EdcaTxopN::GetBlockAckThreshold (void) const
{
  NS_LOG_FUNCTION (this);
  return m_blockAckThreshold;
}

void
EdcaTxopN::SendAddBaRequest (Mac48Address dest, uint8_t tid, uint16_t startSeq,
                             uint16_t timeout, bool immediateBAck)
{
  NS_LOG_FUNCTION (this << dest << static_cast<uint32_t> (tid) << startSeq << timeout << immediateBAck);
  NS_LOG_DEBUG ("sent ADDBA request to " << dest);
  WifiMacHeader hdr;
  hdr.SetAction ();
  hdr.SetAddr1 (dest);
  hdr.SetAddr2 (m_low->GetAddress ());
  hdr.SetAddr3 (m_low->GetAddress ());
  hdr.SetDsNotTo ();
  hdr.SetDsNotFrom ();

  WifiActionHeader actionHdr;
  WifiActionHeader::ActionValue action;
  action.blockAck = WifiActionHeader::BLOCK_ACK_ADDBA_REQUEST;
  actionHdr.SetAction (WifiActionHeader::BLOCK_ACK, action);

  Ptr<Packet> packet = Create<Packet> ();
  /*Setting ADDBARequest header*/
  MgtAddBaRequestHeader reqHdr;
  reqHdr.SetAmsduSupport (true);
  if (immediateBAck)
    {
      reqHdr.SetImmediateBlockAck ();
    }
  else
    {
      reqHdr.SetDelayedBlockAck ();
    }
  reqHdr.SetTid (tid);
  /* For now we don't use buffer size field in the ADDBA request frame. The recipient
   * will choose how many packets it can receive under block ack.
   */
  reqHdr.SetBufferSize (0);
  reqHdr.SetTimeout (timeout);
  reqHdr.SetStartingSequence (startSeq);

  m_baManager->CreateAgreement (&reqHdr, dest);

  packet->AddHeader (reqHdr);
  packet->AddHeader (actionHdr);

  m_currentPacket = packet;
  m_currentHdr = hdr;

  uint16_t sequence = m_txMiddle->GetNextSequenceNumberfor (&m_currentHdr);
  m_currentHdr.SetSequenceNumber (sequence);
  m_currentHdr.SetFragmentNumber (0);
  m_currentHdr.SetNoMoreFragments ();
  m_currentHdr.SetNoRetry ();

  MacLowTransmissionParameters params;
  params.EnableAck ();
  params.DisableRts ();
  params.DisableNextData ();
  params.DisableOverrideDurationId ();

  m_low->StartTransmission (m_currentPacket, &m_currentHdr, params,
                            m_transmissionListener);
}

void
EdcaTxopN::SendDelbaFrame (Mac48Address addr, uint8_t tid, bool byOriginator)
{
  NS_LOG_FUNCTION (this << addr << static_cast<uint32_t> (tid) << byOriginator);
  WifiMacHeader hdr;
  hdr.SetAction ();
  hdr.SetAddr1 (addr);
  hdr.SetAddr2 (m_low->GetAddress ());
  hdr.SetAddr3 (m_low->GetAddress ());
  hdr.SetDsNotTo ();
  hdr.SetDsNotFrom ();

  MgtDelBaHeader delbaHdr;
  delbaHdr.SetTid (tid);
  if (byOriginator)
    {
      delbaHdr.SetByOriginator ();
    }
  else
    {
      delbaHdr.SetByRecipient ();
    }

  WifiActionHeader actionHdr;
  WifiActionHeader::ActionValue action;
  action.blockAck = WifiActionHeader::BLOCK_ACK_DELBA;
  actionHdr.SetAction (WifiActionHeader::BLOCK_ACK, action);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (delbaHdr);
  packet->AddHeader (actionHdr);

  PushFront (packet, hdr);
}

int64_t
EdcaTxopN::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_rng->AssignStreams (stream);
  return 1;
}

void
EdcaTxopN::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
  m_dcf->ResetCw ();
  m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
  ns3::Dcf::DoInitialize ();
}

void
EdcaTxopN::BaTxOk (const WifiMacHeader &hdr)
{
  NS_LOG_FUNCTION (this << hdr);
  if (!m_txOkCallback.IsNull ())
    {
      m_txOkCallback (m_currentHdr);
    }
}

void
EdcaTxopN::BaTxFailed (const WifiMacHeader &hdr)
{
  NS_LOG_FUNCTION (this << hdr);
  if (!m_txFailedCallback.IsNull ())
    {
      m_txFailedCallback (m_currentHdr);
    }
}

/* CSMA/ECA */

uint32_t
EdcaTxopN::GetAssignedBackoff (void)
{
  return m_boCounter;
}

uint32_t
EdcaTxopN::GetConsecutiveSuccesses (void)
{
  return m_consecutiveSuccess;
}

void
EdcaTxopN::AddConsecutiveSuccess (void)
{
  m_consecutiveSuccess++;
}

void
EdcaTxopN::ResetConsecutiveSuccess (void)
{
  m_consecutiveSuccess = 0;
}

void
EdcaTxopN::ResetStats (void)
{
  NS_LOG_DEBUG ("Resetting stats");

  m_fsAggregation = 0;
  m_fsAggregated = 0xFFFF;
  m_fairShare = false;
  m_failures = 0;
  m_collisions = 0;
  m_successes = 0;
  m_txAttempts = 0;
  m_boCounter = 0xFFFFFFFF;
  m_ecaBitmap = false;
  m_consecutiveSuccess = 0;
  m_settingThreshold = false;
  m_scheduleResetThreshold = 2;
  m_scheduleResetMode = false;
  m_scheduleResetConservative = false;
  m_dcf->StartBackoffNow (m_rng->GetNext (0, m_dcf->GetCw ()));
}

uint32_t
EdcaTxopN::deterministicBackoff (uint32_t cw)
{
  uint32_t tmp = ceil(cw / 2) + 1;
  if(!m_settingThreshold)
    {
      m_boCounter = 0xFFFFFFFF;
      m_boCounter = tmp;
    }
  m_settingThreshold = false;
  return tmp;
}
bool
EdcaTxopN::GetScheduleResetMode (void)
{
  return m_scheduleResetMode;
}

void
EdcaTxopN::SetScheduleResetMode (void)
{
  m_scheduleResetMode = true;
}

bool
EdcaTxopN::CanWeReduceTheSchedule (void)
{
  bool canI = false;
  std::vector<bool> *bitmap = m_manager->GetBitmap ();
  NS_LOG_DEBUG ("Got the bitmap from DcfManager " << bitmap->size ());
  
  /* Updating the traced value */
  m_ecaBitmap = (std::vector<bool>*) 0;
  m_ecaBitmap = bitmap;

  /* Checking the possibility of a schedule reduction */
  uint32_t currentSize = bitmap->size ();
  if (!GetScheduleResetMode ())
    {
      /* That is, a Schedule Halving */
      /* Debugging the bitmap */
      NS_LOG_DEBUG ("Checking for Schedule Halving");
      uint32_t slot = 0; 
      for (std::vector<bool>::iterator i = bitmap->begin (); i != bitmap->end (); i++, slot++)
        {
          NS_LOG_DEBUG ("Bitmap position " << slot << " value: " << *i);
        }
      /* End of debug */

      uint32_t midpoint = (currentSize - 1) / 2;
      if (bitmap->at (midpoint) == 0)
        {
          NS_LOG_DEBUG ("A schedule halving is possible. Size: " << currentSize );
          canI = true;
          m_srReductionFactor = 2;
        }
    }
  else
    {
      /* Debugging the bitmap */
      NS_LOG_DEBUG ("Checking for Schedule Reset");
      uint32_t slot = 0; 
      for (std::vector<bool>::iterator i = bitmap->begin (); i != bitmap->end (); i++, slot++)
        {
          NS_LOG_DEBUG ("Bitmap position " << slot << " value: " << *i);
        }
      /* End of debug */
      

      uint32_t maxStage =  log2 ((m_dcf->GetCw () + 1) / (m_dcf->GetCwMin () + 1));
      if(maxStage > 1)
        {
          for (uint32_t i = 0; i <= maxStage; i++)
            {
              uint32_t position = pow (2,i) * std::ceil( (m_dcf->GetCwMin () + 1) / 2);
              NS_ASSERT (position < currentSize);
              // std::cout << "checking for stage : " << i << ", maxStage: " << maxStage << std::endl;

              if (bitmap->at (position) == 0)
              {
                NS_LOG_DEBUG ("A schedule reset is possible. Position: " << position );
                canI = true;
                m_srReductionFactor = (m_dcf->GetCw () + 1) / (pow (2,i) * (m_dcf->GetCwMin () + 1) );
                // std::cout << "\tgetting out, m_srReductionFactor: " << m_srReductionFactor << ", pos: " << position << std::endl;
                break;
              }
            }
        }
        else if (maxStage == 1)
        {
          canI = true;
          m_srReductionFactor = 1;
        }
    }

  /* Updating traced values */
  m_scheduleReductionAttempts++;
  if (canI == true)
    {
      m_scheduleReductions++;
    }
  else
    {
      m_scheduleReductionFailed++;
    }
  /* Done updating */
  return canI;
}

uint32_t
EdcaTxopN::GetScheduleResetThreshold (void)
{
  return m_scheduleResetThreshold;
}

void
EdcaTxopN::SetScheduleResetThreshold (void)
{
  m_settingThreshold = true;

  if(m_scheduleResetConservative)
    {
      m_scheduleResetThreshold = ceil (((m_dcf->GetCwMax () + 1) / 2) / 
        ( deterministicBackoff (m_dcf->GetCw ())));
    }
  else
    {
      m_scheduleResetThreshold = 2;
    }
}

void
EdcaTxopN::SetScheduleConservative (void)
{
  m_scheduleResetConservative = true;
}

void 
EdcaTxopN::SetScheduleResetActivationThreshold (uint32_t thresh)
{
  m_srActivationThreshold = thresh;
}

uint32_t
EdcaTxopN::GetScheduleResetActivationThreshold (void)
{
  return m_srActivationThreshold;
}

void
EdcaTxopN::ModifyCwAccordingToScheduleReduction (void)
{
  m_srPreviousCw =  m_dcf->GetCw ();
  uint32_t factor = GetScheduleReductionFactor ();
  NS_ASSERT (factor > 0);

  uint32_t reduced = std::max ( ((m_dcf->GetCw () + 1) / factor) - 1, m_dcf->GetCwMin () );
  
  NS_ASSERT (reduced % 2 != 0);
  m_dcf->SetCw (reduced);

  NS_LOG_DEBUG ("Schedule Reset modified Cw from: " << m_srPreviousCw << " to: " << m_dcf->GetCw ());

  if (m_manager->UseDynamicStickiness ())
    m_manager->IncreaseStickiness ();
  m_scheduleRecentlyReduced = true;
}

uint32_t
EdcaTxopN::GetScheduleReductionFactor (void)
{
  return m_srReductionFactor;
}

void
EdcaTxopN::ResetSrMetrics (void)
{
  m_srBeingFilled = false;
  m_manager->SetNotFillingTheBitmap ();
  if (m_scheduleRecentlyReduced == true)
    {
      NS_LOG_DEBUG ("Resetting the chances made by Schedule Reset. Cw back to: " << m_srPreviousCw);
      m_dcf->SetCw (m_srPreviousCw);
      // std::cout << "Reversing to: " << m_srPreviousCw << std::endl;
    }
  m_scheduleRecentlyReduced = false;
}

void
EdcaTxopN::KeepScheduleReductionIfAny (void)
{
  m_scheduleRecentlyReduced = false;
}

void
EdcaTxopN::SetAggregationWithFairShare (void)
{
  m_fsAggregation = 0;
  if (GetConsecutiveSuccesses () > 0)
    {
      if (m_dcf->GetCw () > m_dcf->GetCwMin ())
        {
          m_fsAggregation =  log2 ((m_dcf->GetCw () + 1) / (m_dcf->GetCwMin () + 1));
           // Updating traced value 
          m_fsAggregated = 0xFFFF;
          m_fsAggregated = m_fsAggregation;
          NS_LOG_DEBUG (m_fsAggregation);
        }
    }
}

} //namespace ns3
