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
 * Adapted from video-application.h for exponential batch inter-arrival times, instead of constant.
 */
#ifndef WEIBULL_EXP_APPLICATION_H
#define WEIBULL_EXP_APPLICATION_H

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

class WeibullExpApplication : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  WeibullExpApplication ();

  virtual ~WeibullExpApplication();

  /**
   * \brief Return a pointer to associated socket.
   * \return pointer to associated socket
   */
  Ptr<Socket> GetSocket (void) const;

protected:
  virtual void DoDispose (void);
private:
  // inherited from Application base class.
  virtual void StartApplication (void);    // Called at time specified by Start
  virtual void StopApplication (void);     // Called at time specified by Stop

  //helpers
  /**
   * \brief Cancel all pending events.
   */
  void CancelEvents ();

  // Event handlers
  /**
   * \brief Send a packet
   */
  void SendPacket ();

  Ptr<Socket>     m_socket;       //!< Associated socket
  Address         m_peer;         //!< Peer address
  Address         m_local;        //!< Local address to bind to
  bool            m_connected;    //!< True if connected
  Time          m_frameInterval_s; //!< Frame interval ==> Example: 0.03333 seconds for 30 FPS
  double          m_weibullScale;    
  double          m_weibullShape;    
  uint32_t        m_RemainingFrameSize;
  uint32_t        m_RemainingPkts;
  uint32_t        m_pktSize;
  uint64_t        m_totFrames;
  uint64_t        m_totBytesGenerated;
  uint64_t        m_totBytes;     //!< Total bytes sent so far
  EventId         m_frameEvent;     //!< Event id for next start or stop event
  EventId         m_sendEvent;    //!< Event id of pending "send packet" event
  TypeId          m_tid;          //!< Type of the socket used
  uint32_t        m_seq {0};      //!< Sequence
  Ptr<Packet>     m_unsentPacket; //!< Unsent packet cached for future attempt
  bool            m_enableSeqTsSizeHeader {false}; //!< Enable or disable the use of SeqTsSizeHeader
  Ptr<WeibullRandomVariable> rand_weibull = CreateObject<WeibullRandomVariable> ();

  /// Traced Callback: transmitted packets.
  TracedCallback<Ptr<const Packet> > m_txTrace;

  /// Callbacks for tracing the packet Tx events, includes source and destination addresses
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_txTraceWithAddresses;

  /// Callback for tracing the packet Tx events, includes source, destination, the packet sent, and header
  TracedCallback<Ptr<const Packet>, const Address &, const Address &, const SeqTsSizeHeader &> m_txTraceWithSeqTsSize;

private:
  const uint32_t maxFrameSize = 1500000;
  const uint32_t maxPacketSize = 1472;
  bool sendWarmupPacket;
  uint32_t trafficStartOffset_ms;
  Ptr<ExponentialRandomVariable> m_arrivalGen = CreateObject<ExponentialRandomVariable>();

  /**
   * \brief Schedule the next packet transmission
   */
  void ScheduleNextTx ();
  /**
   * \brief Schedule the next On period start
   */
  void ScheduleFrameGeneration ();
  /**
   * \brief Handle a Connection Succeed event
   * \param socket the connected socket
   */
  void ConnectionSucceeded (Ptr<Socket> socket);
  /**
   * \brief Handle a Connection Failed event
   * \param socket the not connected socket
   */
  void ConnectionFailed (Ptr<Socket> socket);
};

} // namespace ns3

#endif /* ONOFF_APPLICATION_H */
