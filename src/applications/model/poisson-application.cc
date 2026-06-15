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
 * Video application that generates individual packets at exponential inter-arrival times.
 * Addapted from video-application.c.
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
#include "ns3/tcp-socket-factory.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "poisson-application.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PoissonApplication");

NS_OBJECT_ENSURE_REGISTERED (PoissonApplication);

TypeId
PoissonApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PoissonApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<PoissonApplication> ()
    .AddAttribute ("SendWarmupPacket", "Whether to send a small warmup packet at the beginning to establish ARP.",
        BooleanValue (true),
        MakeBooleanAccessor (&PoissonApplication::sendWarmupPacket),
        MakeBooleanChecker ())
    .AddAttribute ("TrafficStartOffset", "The time offset in ms when to start the frame generation.",
        UintegerValue (6000),
        MakeUintegerAccessor (&PoissonApplication::trafficStartOffset_ms),
        MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("ArrivalInterval", "The mean arrivals interval.",
        TimeValue (Seconds (0.65)),
        MakeTimeAccessor (&PoissonApplication::m_frameInterval_s),
        MakeTimeChecker ())
    .AddAttribute ("PacketSize", "Packet size in bytes.",
        UintegerValue (1472),
        MakeUintegerAccessor (&PoissonApplication::m_maxPktSize),
        MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Remote", "The address of the destination",
        AddressValue (),
        MakeAddressAccessor (&PoissonApplication::m_peer),
        MakeAddressChecker ())
    .AddAttribute ("Local",
        "The Address on which to bind the socket. If not set, it is generated automatically.",
        AddressValue (),
        MakeAddressAccessor (&PoissonApplication::m_local),
        MakeAddressChecker ())
    .AddAttribute ("Protocol", "The type of protocol to use. This should be "
        "a subclass of ns3::SocketFactory",
        TypeIdValue (UdpSocketFactory::GetTypeId ()),
        MakeTypeIdAccessor (&PoissonApplication::m_tid),
        MakeTypeIdChecker ())
    .AddAttribute ("EnableSeqTsSizeHeader",
        "Enable use of SeqTsSizeHeader for sequence number and timestamp",
        BooleanValue (false),
        MakeBooleanAccessor (&PoissonApplication::m_enableSeqTsSizeHeader),
        MakeBooleanChecker ())
    .AddTraceSource ("Tx", "A new packet is created and is sent",
        MakeTraceSourceAccessor (&PoissonApplication::m_txTrace),
        "ns3::Packet::TracedCallback")
    .AddTraceSource ("TxWithAddresses", "A new packet is created and is sent",
        MakeTraceSourceAccessor (&PoissonApplication::m_txTraceWithAddresses),
        "ns3::Packet::TwoAddressTracedCallback")
    .AddTraceSource ("TxWithSeqTsSize", "A new packet is created with SeqTsSizeHeader",
        MakeTraceSourceAccessor (&PoissonApplication::m_txTraceWithSeqTsSize),
        "ns3::PacketSink::SeqTsSizeCallback")
    ;
    return tid;
}

PoissonApplication::PoissonApplication ()
  : m_socket (0),
    m_connected (false),
    m_totFrames (0),
    m_totBytesGenerated (0),
    m_totBytes (0),
    m_unsentPacket (0)
{
}

PoissonApplication::~PoissonApplication()
{
}


Ptr<Socket>
PoissonApplication::GetSocket (void) const
{
    return m_socket;
}

void
PoissonApplication::DoDispose (void)
{
    CancelEvents ();
    m_socket = 0;
    m_unsentPacket = 0;
    // chain up
    Application::DoDispose ();
}

// Application Methods
void PoissonApplication::StartApplication () // Called at time specified by Start
{
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
            else if (InetSocketAddress::IsMatchingType (m_peer) || PacketSocketAddress::IsMatchingType (m_peer))
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
            MakeCallback (&PoissonApplication::ConnectionSucceeded, this),
            MakeCallback (&PoissonApplication::ConnectionFailed, this)
        );
    }

    // Ensure no pending event
    CancelEvents ();
    // If we are not yet connected, there is nothing to do here
    // The ConnectionComplete upcall will start timers at that time
    //if (!m_connected) return;

    NS_LOG_INFO("Mean: "<< m_frameInterval_s.GetNanoSeconds() << " ns.");
    m_arrivalGen->SetAttribute("Mean", DoubleValue(m_frameInterval_s.GetSeconds()));

    if (sendWarmupPacket) {
        m_pktSize = 30;
        m_sendEvent = Simulator::Schedule (Seconds(0), &PoissonApplication::SendPacket, this);
        Simulator::Schedule (MilliSeconds(trafficStartOffset_ms), &PoissonApplication::ScheduleFrameGeneration, this);
    } else {
        ScheduleFrameGeneration ();
    }
}

void PoissonApplication::StopApplication () // Called at time specified by Stop
{
    CancelEvents ();

    if (m_socket != nullptr)
    {
        m_socket->Close ();
    }
    else
    {
        NS_LOG_WARN ("PoissonApplication found null socket to close in StopApplication");
    }
}

void PoissonApplication::CancelEvents ()
{
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


void PoissonApplication::ScheduleFrameGeneration()
{
    m_pktSize = m_maxPktSize;
    NS_LOG_DEBUG ("Generating packet of size: " << m_pktSize);
    m_totBytesGenerated += m_pktSize;
    m_totFrames++;
    m_sendEvent = Simulator::Schedule (Seconds(0), &PoissonApplication::SendPacket, this);

    Time nextArrival = Seconds(m_arrivalGen->GetValue());
    NS_LOG_DEBUG("Next packet scheduled after " << nextArrival.GetNanoSeconds() << " ns.");
    m_frameEvent = Simulator::Schedule(nextArrival, &PoissonApplication::ScheduleFrameGeneration, this);
}

void PoissonApplication::SendPacket ()
{
    NS_ASSERT (m_sendEvent.IsExpired ());

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
            m_txTraceWithAddresses (packet, localAddress, InetSocketAddress::ConvertFrom (m_peer));
        }
        else if (Inet6SocketAddress::IsMatchingType (m_peer))
        {
            m_txTraceWithAddresses (packet, localAddress, Inet6SocketAddress::ConvertFrom(m_peer));
        }
    }
    else
    {
        NS_LOG_DEBUG ("Unable to send packet; actual " << actual << " size " << m_pktSize << "; caching for later attempt");
        m_unsentPacket = packet;
    }
}

void PoissonApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
    NS_LOG_INFO ("Connection Succeeded at " << Simulator::Now ().GetSeconds () << "s");
    m_connected = true;
}

void PoissonApplication::ConnectionFailed (Ptr<Socket> socket)
{
    NS_FATAL_ERROR ("Can't connect");
}
}
