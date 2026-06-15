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
#ifndef POISSON_APPLICATION_H
#define POISSON_APPLICATION_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-callback.h"
#include "ns3/seq-ts-size-header.h"

namespace ns3 {

class Address;
class RandomVariableStream;
class Socket;

class PoissonApplication : public Application
{
public:
    static TypeId GetTypeId (void);
    PoissonApplication ();
    virtual ~PoissonApplication();
    Ptr<Socket> GetSocket (void) const;

protected:
  virtual void DoDispose (void);

private:
  // inherited from Application base class.
  virtual void StartApplication (void);    // Called at time specified by Start
  virtual void StopApplication (void);     // Called at time specified by Stop
  void CancelEvents ();
  void SendPacket ();

  Ptr<Socket>     m_socket;
  Address         m_peer;
  Address         m_local;
  bool            m_connected;
  uint32_t        m_pktSize;
  uint32_t        m_maxPktSize;
  uint64_t        m_totFrames;
  uint64_t        m_totBytesGenerated;
  uint64_t        m_totBytes;     //!< Total bytes sent so far
  EventId         m_frameEvent;     //!< Event id for next start or stop event
  EventId         m_sendEvent;    //!< Event id of pending "send packet" event
  TypeId          m_tid;          //!< Type of the socket used
  uint32_t        m_seq {0};      //!< Sequence
  Ptr<Packet>     m_unsentPacket; //!< Unsent packet cached for future attempt
  bool            m_enableSeqTsSizeHeader {false};
  TracedCallback<Ptr<const Packet> > m_txTrace;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_txTraceWithAddresses;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &, const SeqTsSizeHeader &> m_txTraceWithSeqTsSize;

private:
    Time m_frameInterval_s;
    Ptr<ExponentialRandomVariable> m_arrivalGen = CreateObject<ExponentialRandomVariable>();
    bool sendWarmupPacket;
    uint32_t trafficStartOffset_ms;

    void ScheduleNextTx ();
    void ScheduleFrameGeneration ();
    void ConnectionSucceeded (Ptr<Socket> socket);
    void ConnectionFailed (Ptr<Socket> socket);
};

}

#endif
