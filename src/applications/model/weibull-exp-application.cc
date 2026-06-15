/*
 * Copyright (c) 2026 Andreea-Carina Deaconu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * Author: Andreea-Carina Deaconu <carina99br@gmail.com>
 *
 * Adapted from video-application.cc for exponential batch inter-arrival times, instead of constant.
 */
#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/packet-socket-address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "weibull-exp-application.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/boolean.h"
#include "ns3/double.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("WeibullExpApplication");

NS_OBJECT_ENSURE_REGISTERED (WeibullExpApplication);

TypeId
WeibullExpApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::WeibullExpApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<WeibullExpApplication> ()
                  .AddAttribute ("SendWarmupPacket", "Whether to send a small warmup packet at the beginning to establish ARP.",
                  BooleanValue (true),
                  MakeBooleanAccessor (&WeibullExpApplication::sendWarmupPacket),
                  MakeBooleanChecker ())
    .AddAttribute ("TrafficStartOffset", "The time offset in ms when to start the frame generation.",
                  UintegerValue (6000),
                  MakeUintegerAccessor (&WeibullExpApplication::trafficStartOffset_ms),
                  MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("FrameInterval", "The frame interval.",
                   TimeValue (Seconds (0.03333)),  // For 30 fps
                   MakeTimeAccessor (&WeibullExpApplication::m_frameInterval_s),
                   MakeTimeChecker ())
    .AddAttribute ("WeibullScale", "Weibull scale parameter.",
                   DoubleValue (6950),
                   MakeDoubleAccessor (&WeibullExpApplication::m_weibullScale),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("WeibullShape", "Weibull shape parameter.",
                   DoubleValue (0.8099),
                   MakeDoubleAccessor (&WeibullExpApplication::m_weibullShape),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Remote", "The address of the destination",
                   AddressValue (),
                   MakeAddressAccessor (&WeibullExpApplication::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("Local",
                   "The Address on which to bind the socket. If not set, it is generated automatically.",
                   AddressValue (),
                   MakeAddressAccessor (&WeibullExpApplication::m_local),
                   MakeAddressChecker ())
    .AddAttribute ("Protocol", "The type of protocol to use. This should be "
                   "a subclass of ns3::SocketFactory",
                   TypeIdValue (UdpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&WeibullExpApplication::m_tid),
                   // This should check for SocketFactory as a parent
                   MakeTypeIdChecker ())
    .AddAttribute ("EnableSeqTsSizeHeader",
                   "Enable use of SeqTsSizeHeader for sequence number and timestamp",
                   BooleanValue (false),
                   MakeBooleanAccessor (&WeibullExpApplication::m_enableSeqTsSizeHeader),
                   MakeBooleanChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&WeibullExpApplication::m_txTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("TxWithAddresses", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&WeibullExpApplication::m_txTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
    .AddTraceSource ("TxWithSeqTsSize", "A new packet is created with SeqTsSizeHeader",
                     MakeTraceSourceAccessor (&WeibullExpApplication::m_txTraceWithSeqTsSize),
                     "ns3::PacketSink::SeqTsSizeCallback")
  ;
  return tid;
}


WeibullExpApplication::WeibullExpApplication ()
  : m_socket (0),
    m_connected (false),
    m_totFrames (0),
    m_totBytesGenerated (0),
    m_totBytes (0),
    m_unsentPacket (0)
{
  // NS_LOG_FUNCTION (this);
}

WeibullExpApplication::~WeibullExpApplication()
{
  // NS_LOG_FUNCTION (this);
}


Ptr<Socket>
WeibullExpApplication::GetSocket (void) const
{
  // NS_LOG_FUNCTION (this);
  return m_socket;
}

void
WeibullExpApplication::DoDispose (void)
{
  // NS_LOG_FUNCTION (this);

  CancelEvents ();
  m_socket = 0;
  m_unsentPacket = 0;
  // chain up
  Application::DoDispose ();
}

// Application Methods
void WeibullExpApplication::StartApplication () // Called at time specified by Start
{
  // NS_LOG_FUNCTION (this);

  // Create the socket if not already
  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), m_tid);
      int ret = -1;

      if (! m_local.IsInvalid())
        {
          NS_ABORT_MSG_IF ((Inet6SocketAddress::IsMatchingType (m_peer) && InetSocketAddress::IsMatchingType (m_local)) ||
                           (InetSocketAddress::IsMatchingType (m_peer) && Inet6SocketAddress::IsMatchingType (m_local)),
                           "Incompatible peer and local address IP version");
          ret = m_socket->Bind (m_local);
        }
      else
        {
          if (Inet6SocketAddress::IsMatchingType (m_peer))
            {
              ret = m_socket->Bind6 ();
            }
          else if (InetSocketAddress::IsMatchingType (m_peer) ||
                   PacketSocketAddress::IsMatchingType (m_peer))
            {
              ret = m_socket->Bind ();
            }
        }

      if (ret == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }

      m_socket->Connect (m_peer);
      m_socket->SetAllowBroadcast (true);
      m_socket->ShutdownRecv ();

      m_socket->SetConnectCallback (
        MakeCallback (&WeibullExpApplication::ConnectionSucceeded, this),
        MakeCallback (&WeibullExpApplication::ConnectionFailed, this));
    }

  // Ensure no pending event
  CancelEvents ();
  // If we are not yet connected, there is nothing to do here
  // The ConnectionComplete upcall will start timers at that time
  //if (!m_connected) return;

  // GenerateNextState ();
  rand_weibull->SetAttribute ("Scale", DoubleValue(m_weibullScale));
  NS_LOG_INFO ("Weibull scale: " << m_weibullScale <<" shape: " <<m_weibullShape);
  rand_weibull->SetAttribute ("Shape", DoubleValue(m_weibullShape));
  NS_LOG_INFO ("Arrival mean: "<< m_frameInterval_s.GetNanoSeconds() << " ns.");
  m_arrivalGen->SetAttribute("Mean", DoubleValue(m_frameInterval_s.GetSeconds()));

  if (sendWarmupPacket) {
    m_RemainingPkts = 1;
    m_RemainingFrameSize = 30;
    m_sendEvent = Simulator::Schedule (Seconds(0), &WeibullExpApplication::SendPacket, this);
    Simulator::Schedule (MilliSeconds(trafficStartOffset_ms), &WeibullExpApplication::ScheduleFrameGeneration, this);
  } else {
    ScheduleFrameGeneration ();
  }
}

void WeibullExpApplication::StopApplication () // Called at time specified by Stop
{
  // NS_LOG_FUNCTION (this);
  CancelEvents ();

  if (m_socket != nullptr)
    {
      m_socket->Close ();
    }
  else
    {
      NS_LOG_WARN ("WeibullExpApplication found null socket to close in StopApplication");
    }
}

void WeibullExpApplication::CancelEvents ()
{
  // NS_LOG_FUNCTION (this);

  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_frameEvent);
  // Canceling events may cause discontinuity in sequence number if the
  // SeqTsSizeHeader is header, and m_unsentPacket is true
  if (m_unsentPacket)
    {
      NS_LOG_DEBUG ("Discarding cached packet upon CancelEvents ()");
    }
  m_unsentPacket = 0;
}

// Private helpers
void WeibullExpApplication::ScheduleNextTx ()
{
  // NS_LOG_FUNCTION (this);
  Time nextTime = Seconds(0);
//   NS_LOG_INFO ("Next packet scheduled after " << nextTime << " seconds.");
  m_sendEvent = Simulator::Schedule (nextTime, &WeibullExpApplication::SendPacket, this);
}

void WeibullExpApplication::ScheduleFrameGeneration()
{
  while (true) {
    m_RemainingFrameSize = floor(rand_weibull->GetValue());
    if (m_RemainingFrameSize < maxFrameSize && m_RemainingFrameSize % maxPacketSize >= 12) break;
  }
  m_RemainingPkts = ceil(double(m_RemainingFrameSize)/maxPacketSize);

  NS_LOG_INFO ("Frame size: " << m_RemainingFrameSize << ", Number of packets: " << m_RemainingPkts);
  NS_LOG_INFO ("Average frame size to port "<< InetSocketAddress::ConvertFrom (m_peer).GetPort () << " is " << double(m_totBytesGenerated)/m_totFrames);
  m_totBytesGenerated += m_RemainingFrameSize;
  m_totFrames++;

  ScheduleNextTx();
  Time nextArrival = Seconds(m_arrivalGen->GetValue());
  NS_LOG_INFO ("Next frame scheduled after " << nextArrival.GetNanoSeconds()<<" ns");
  m_frameEvent = Simulator::Schedule (nextArrival, &WeibullExpApplication::ScheduleFrameGeneration, this);
}

void WeibullExpApplication::SendPacket ()
{
  // NS_LOG_FUNCTION (this);

  NS_ASSERT (m_sendEvent.IsExpired ());

  if (m_RemainingPkts > 1) {
    m_pktSize = maxPacketSize;
    ScheduleNextTx ();
  }
  else {
    m_pktSize = m_RemainingFrameSize;
    // m_frameEvent = Simulator::Schedule (Seconds(m_frameInterval_s), &WeibullExpApplication::ScheduleFrameGeneration, this);
  }

  Ptr<Packet> packet;
  if (m_unsentPacket)
    {
      packet = m_unsentPacket;
    }
  else if (m_enableSeqTsSizeHeader)
    {
      Address from, to;
      m_socket->GetSockName (from);
      m_socket->GetPeerName (to);
      SeqTsSizeHeader header;
      header.SetSeq (m_seq++);
      header.SetSize (m_pktSize);
      NS_ABORT_IF (m_pktSize < header.GetSerializedSize ());
      packet = Create<Packet> (m_pktSize - header.GetSerializedSize ());
      // Trace before adding header, for consistency with PacketSink
      m_txTraceWithSeqTsSize (packet, from, to, header);
      packet->AddHeader (header);
    }
  else
    {
      packet = Create<Packet> (m_pktSize);
    }

  int actual = m_socket->Send (packet);
  if ((unsigned) actual == m_pktSize)
    {
      m_txTrace (packet);
      m_totBytes += m_pktSize;
      m_unsentPacket = 0;
      Address localAddress;
      m_socket->GetSockName (localAddress);
      if (InetSocketAddress::IsMatchingType (m_peer))
        {
          // NS_LOG_INFO ("At time " << Simulator::Now ().As (Time::S)
          //              << " Video application sent "
          //              <<  packet->GetSize () << " bytes to "
          //              << InetSocketAddress::ConvertFrom(m_peer).GetIpv4 ()
          //              << " port " << InetSocketAddress::ConvertFrom (m_peer).GetPort ()
          //              << " total Tx " << m_totBytes << " bytes");
          m_txTraceWithAddresses (packet, localAddress, InetSocketAddress::ConvertFrom (m_peer));
        }
      else if (Inet6SocketAddress::IsMatchingType (m_peer))
        {
          // NS_LOG_INFO ("At time " << Simulator::Now ().As (Time::S)
          //              << " Video application sent "
          //              <<  packet->GetSize () << " bytes to "
          //              << Inet6SocketAddress::ConvertFrom(m_peer).GetIpv6 ()
          //              << " port " << Inet6SocketAddress::ConvertFrom (m_peer).GetPort ()
          //              << " total Tx " << m_totBytes << " bytes");
          m_txTraceWithAddresses (packet, localAddress, Inet6SocketAddress::ConvertFrom(m_peer));
        }
    }
  else
    {
      NS_LOG_DEBUG ("Unable to send packet; actual " << actual << " size " << m_pktSize << "; caching for later attempt");
      m_unsentPacket = packet;
    }

  m_RemainingPkts--;
  m_RemainingFrameSize -= m_pktSize;
  // NS_LOG_INFO ("Number of packets remaining in this state: " << m_RemainingPkts);
  // if (m_RemainingPkts > 0) ScheduleNextTx ();
}


void WeibullExpApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  // NS_LOG_FUNCTION (this << socket);
  m_connected = true;
}

void WeibullExpApplication::ConnectionFailed (Ptr<Socket> socket)
{
  // NS_LOG_FUNCTION (this << socket);
  NS_FATAL_ERROR ("Can't connect");
}
} // Namespace ns3
