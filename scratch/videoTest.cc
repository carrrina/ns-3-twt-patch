/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 SEBASTIEN DERONNE
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
 * Author: Sebastien Deronne <sebastien.deronne@gmail.com>
 * Modified by Shyam K Venkateswaran <vshyamkrishnan@gmail.com>
 */

#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/enum.h"
#include "ns3/log.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/spectrum-wifi-helper.h"
#include "ns3/fq-codel-queue-disc.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ssid.h"
#include "ns3/mobility-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/udp-client-server-helper.h"
#include "ns3/packet-sink-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/packet-sink.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/wifi-acknowledgment.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/basic-energy-source-helper.h"
#include "ns3/energy-source.h"
#include "ns3/core-module.h"
#include "ns3/wifi-mac.h"
#include "ns3/qos-utils.h"
#include "ns3/wifi-mac-queue.h"
#include <unordered_map>
#include <iomanip>
#include <sys/stat.h>

#define LOG_PATH "logs/"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("videoTest");

uint32_t randSeed = 7000; // seed for Random Number Generator
std::string scenario = "videoTest"; // used for logging and processing. Get from command line argument
Time metricsLoggingStartTime_s = Seconds (10); // the moment to start computing metrics from
double simulationTime {360}; // seconds  - we assume metricsLoggingStartTime_s seconds for setup

// Logging 
uint32_t flowMonitorPacketTimeout_s = 10; // if a packet is not seen for this duration it is considered lost
bool enablePcap {false}; // enable pcap traces if true
bool monitorQueueSizes {false}; // whether to print WifiMacQueue and QueueDisc size evolution to a separate file at each change
bool enableStateLogs = false; // enable state logs if true
bool enableFlowMon = true; // enable flow monitor if true
bool recordApPhyState = true; // if true, AP PHY states are also logged to the stateLog file (as StaId=0)
bool linkStatusLogging = true; // enable link status logging if true
bool realTimeSimulator = false;  // sync simulation time with real time
std::string statsFilename = "stats.txt";

// Network configuration 
bool sendWarmupPacket {true}; // whether to send a warmup packet to establish ARP before starting video traffic
bool enableEdca {true}; 
std::size_t nStations {1}; // number of stations
double roomLength = 10.0; // 2D room - side length in meters
std::string protocol = "udp";
std::string p2pDataRate = "1000Mbps";
std::string p2pDelay = "0ms";
uint32_t pktSizeBytes = 1500; // packet size used for the simulation
uint32_t udpRcvBufferSize = 1000000; // 1MB buffer
uint32_t maxMpdusAp = 8; // max number of MPDUs in A-MPDUs at AP transmission queue (0 to disable MPDU aggregation)
uint32_t maxMpdusSta = 8; // max number of MPDUs in A-MPDUs at STAs transmission queues (0 to disable MPDU aggregation)
int mcs {7}; // HE MCS index. can be in [0, 11], -1 indicates an unset value
int channelWidth {20};  // channel width in MHz (can be 20, 40, 80, or 160 MHz also for 5 or 6 GHz band)
int gi {800}; // guard interval in nanoseconds (can be 800, 1600 or 3200 ns)
double frequency {5}; // whether 2.4, 5 or 6 GHz
std::string wifiMacQueueSize = "1000p", queueDiscSize = "10240p"; 
int wifiMacQueueDelay_ms = 1000; // if a packet stays longer than this delay in the queue, it is dropped. default is 500ms but frames timed out at high load and TCP connection kept dropping
double beaconInterval_ms = 102.4;
double beaconTransmission_ms = 2;
uint32_t maxMissedBeacons = std::numeric_limits<uint32_t>::max(); // To avoid disassociation

// Background video traffic rates
double backgroundStaRate = 400; // packets per s
double ulTrafficRate = 70; // packets per s

// Video traffic parameters
std::string videoTrafficDistribution = "weibull"; // possible values: weibull, poisson, custom_trace
// Weibull params
uint32_t videoQuality = 0; // Video quality, bv1 to bv6
double videoFrameInterval_s = 0.0333; // for 30 fps
double weibullScale = 6950; // Will change for each video quality
// BV1 - 6950, BV2 - 13900, BV3 - 20850, BV4 - 27800, BV5 - 34750, BV6 - 54210
double weibullShape = 0.8099;
// Poisson params
double poissonArrivalRate = 1.53; // packets per s
// Custom trace params
std::string traceFilename = "streaming_2160p_DL.txt";
std::string applicationClassName;
uint32_t videoAppStart_ms = 3000, videoTrafficStart_ms = 10000; // = metricsLoggingStartTime_s

// TWT
bool enableTwt = true;
double t_sp;
double t_rp;
double firstTwtSpOffsetFromBeacon_ms = 2;
double firstTwtSpStart_s = 6; // At what timestamp (s) to start TWT.
double twtWakeIntervalMultiplier = 0.25; // K, where twtWakeInterval = BI * K; K is double
double twtNominalWakeDurationDivider = 16; // K, where twtNomimalWakeDuration = BI / K; K is double
bool twtTriggerBased = false; // Set it to false for contention-based TWT
uint64_t maxMuSta = 1; // Maximum number of STAs the TWT scheduler will assign for a DATA MU exchange. For BSRP, max possible is used

// TI device model
std::unordered_map<std::string, double>TI_currentModel_mA = {
    {"IDLE", 50}, {"CCA_BUSY", 50}, {"RX", 66}, {"TX", 232}, {"SLEEP", 0.12}
};
uint64_t batteryVoltage = 3;
// These are only after metricsLoggingStartTime_s
double *totalTimeElapsedForSta_ns, *awakeTimeElapsedForSta_ns, *sleepTimeElapsedForSta_ns, *txTimeElapsedForSta_ns, *rxTimeElapsedForSta_ns, *idleTimeElapsedForSta_ns, *ccaBusyTimeElapsedForSta_ns;
double *current_mA_TimesTime_ns_ForSta_TI;
double *uplinkTimeoutsForSta, downlinkTimeoutsAllSta, *uplinkExpiredMpduForSta, downlinkExpiredMpduAllSta, *uplinkFailedEnqueueMpduForSta, downlinkFailedEnqueueMpduAllSta;    
double *throughput;

// To schedule starting of STA wifi PHYs - to avoid assoc req sent at same time
// Refer https://groups.google.com/g/ns-3-users/c/bWaK9QvB3OQ/m/uHPB3t9DBAAJ
void RandomWifiStart (Ptr<WifiPhy> phy)
{
    phy->ResumeFromOff();
}

// Parse context strings of the form "/NodeList/x/DeviceList/x/..." to extract the NodeId integer
uint32_t ContextToNodeId (std::string context)
{
    std::string sub = context.substr (10);
    uint32_t pos = sub.find ("/Device");
    return atoi (sub.substr (0, pos).c_str ());
}

/* Given a Histogram object, return the bin end value that first exceeds xth percentile */
double GetPercentileValue (Histogram hist, double percentile)
{
    NS_ASSERT_MSG (percentile >= 0 && percentile <= 100, "Percentile should be between 0 and 100");
    double sum = 0;
    for (uint32_t i = 0; i < hist.GetNBins (); i++)
    {
        sum += hist.GetBinCount (i);
    }
    double target = sum * percentile / 100;
    sum = 0;
    for (uint32_t i = 0; i < hist.GetNBins (); i++)
    {
        sum += hist.GetBinCount (i);
        if (sum >= target)
        {
            return hist.GetBinEnd (i);
        }
    }
    return hist.GetBinEnd (hist.GetNBins () - 1);
}

// PHY state tracing
void PhyStateTrace (std::string context, Time start, Time duration, WifiPhyState state)
{
    std::stringstream ss;
    ss <<LOG_PATH<< scenario <<".statelog";

    static std::fstream f (ss.str ().c_str (), std::ios::out);

    // Do not use spaces. Use '='  and ';'
    if (Simulator::Now ().GetSeconds() > (metricsLoggingStartTime_s.GetSeconds() - 1))
    {
        f << "state=" <<state<< ";startTime_ns="<<start.GetNanoSeconds()<<";duration_ns=" << duration.GetNanoSeconds()<<";nextStateShouldStartAt_ns="<<(start + duration).GetNanoSeconds()<<";staId="<<ContextToNodeId (context)<< std::endl;
    }
}

void PhyStateTrace_inPlace (std::string context, Time start, Time duration, WifiPhyState stateObj)
{
    // Do not use spaces. Use '='  and ';'
    if (Simulator::Now ().GetMicroSeconds() > metricsLoggingStartTime_s.GetMicroSeconds())
    {
        std::ostringstream state;
        state << stateObj;
        uint32_t nodeId = ContextToNodeId (context);
        // std::cout << "state=" <<state<< ";startTime_ns="<<start.GetNanoSeconds()<<";duration_ns=" << duration.GetNanoSeconds()<<";nextStateShouldStartAt_ns="<<(start + duration).GetNanoSeconds()<<";staId="<<ContextToNodeId (context)<< std::endl;
        totalTimeElapsedForSta_ns[nodeId - 1] += duration.GetNanoSeconds();
        if (state.str() != "SLEEP")
        {
          awakeTimeElapsedForSta_ns[nodeId - 1] += duration.GetNanoSeconds();
        }

        if (state.str() == "SLEEP")
        {
          sleepTimeElapsedForSta_ns[nodeId - 1] += duration.GetNanoSeconds();
        } else if (state.str() == "TX")
        {
          txTimeElapsedForSta_ns[nodeId - 1] += duration.GetNanoSeconds();
        }
        else if (state.str() == "RX")
        {
          rxTimeElapsedForSta_ns[nodeId - 1] += duration.GetNanoSeconds();
        } else if (state.str() == "IDLE")
        {
          idleTimeElapsedForSta_ns[nodeId - 1] += duration.GetNanoSeconds();
        } else if (state.str() == "CCA_BUSY")
        {
          ccaBusyTimeElapsedForSta_ns[nodeId - 1] += duration.GetNanoSeconds();
        }

        current_mA_TimesTime_ns_ForSta_TI[nodeId - 1] += TI_currentModel_mA[state.str()] * duration.GetNanoSeconds();
    }
}

void PhyTxStateTraceAll (std::string context, Time start, Time duration, WifiPhyState state)
{
    std::stringstream ss;
    ss <<LOG_PATH<< scenario <<".txlog";

    static std::fstream f (ss.str ().c_str (), std::ios::out);

    // Do not use spaces. Use '='  and ';'
    if (state == WifiPhyState::TX)
    {
        f<<"startTime_ns="<<start.GetNanoSeconds()<<";duration_ns=" << duration.GetNanoSeconds()<< std::endl;
    }
}

/*
 * Handle the psduResponseTimeout event here.
 * For example, you can log the event or update some statistics.
*/
void PsduResponseTimeoutTraceSta (std::string context, uint8_t reason, Ptr<const WifiPsdu> psdu, const WifiTxVector& txVector)
{
    // std::cout << "t = "<<Simulator::Now().As(Time::MS)<<";staId="<<ContextToNodeId (context) <<"; PSDU response timeout. Reason: " << static_cast<int>(reason);
    // // Print the source and destination addresses of the PSDU
    // std::cout << "\n\tSource address: " << psdu->GetAddr2();
    // std::cout << "\n\tDestination address: " << psdu->GetAddr1() << std::endl;
    if (Simulator::Now ().GetMicroSeconds()>metricsLoggingStartTime_s.GetMicroSeconds())
    {
        uplinkTimeoutsForSta[ContextToNodeId (context) - 1]++;
    }
}

/*
 * Handle the psduResponseTimeout event here.
 * For example, you can log the event or update some statistics.
*/
void PsduResponseTimeoutTraceAp (std::string context, uint8_t reason, Ptr<const WifiPsdu> psdu, const WifiTxVector& txVector)
{
    // std::cout << "t = "<<Simulator::Now().As(Time::MS)<<";staId="<<ContextToNodeId (context) <<"; PSDU response timeout. Reason: " << static_cast<int>(reason);
    // // Print the source and destination addresses of the PSDU
    // std::cout << "\n\tSource address: " << psdu->GetAddr2();
    // std::cout << "\n\tDestination address: " << psdu->GetAddr1() << std::endl;
    if (Simulator::Now ().GetMicroSeconds()>metricsLoggingStartTime_s.GetMicroSeconds())
    {
        downlinkTimeoutsAllSta++;
    }
}

void MpduDropped_atSta(std::string context, WifiMacDropReason dropReason,Ptr<const WifiMpdu> mpdu)
{
    if (Simulator::Now ().GetMicroSeconds()>metricsLoggingStartTime_s.GetMicroSeconds())
    {
        if (dropReason == WifiMacDropReason::WIFI_MAC_DROP_EXPIRED_LIFETIME)
        {
        uplinkExpiredMpduForSta[ContextToNodeId (context) - 1]++;
        }
        if (dropReason == WifiMacDropReason::WIFI_MAC_DROP_FAILED_ENQUEUE)
        {
        uplinkFailedEnqueueMpduForSta[ContextToNodeId (context) - 1]++;
        }
    }
    // std::cout << "MPDU expired at STA; t = " <<Simulator::Now().As(Time::MS)<<"; dropReason:"<<+dropReason<<";staId="<< ContextToNodeId (context)-1 <<std::endl;
}

void MpduDropped_atAp(std::string context, WifiMacDropReason dropReason,Ptr<const WifiMpdu> mpdu)
{
    if (Simulator::Now ().GetMicroSeconds()>metricsLoggingStartTime_s.GetMicroSeconds())
    {
        if (dropReason == WifiMacDropReason::WIFI_MAC_DROP_EXPIRED_LIFETIME)
        {
        downlinkExpiredMpduAllSta++;
        }
        if (dropReason == WifiMacDropReason::WIFI_MAC_DROP_FAILED_ENQUEUE)
        {
        downlinkFailedEnqueueMpduAllSta++;
        }
    }
    // std::cout << "MPDU expired at AP; t = "<<Simulator::Now().As(Time::MS)<<"; dropReason:"<<+dropReason<<";staId="<< ContextToNodeId (context)-1 <<std::endl;
}

Time getTimeUnitFromMs(double value_ms)
{
    if (std::floor(value_ms) == (value_ms)) {
        return MilliSeconds(value_ms);
    } 
    
    return MicroSeconds((double)value_ms * 1000);
}

Time getTimeUnitFromS(double value_s)
{
    if (std::floor(value_s) == (value_s)) {
        return Seconds(value_s);
    } 
    
    return getTimeUnitFromMs(value_s * 1000);
}

void initiateTwtAtApAndSta (Ptr<WifiMac> apMac, Ptr<WifiMac> staMac, u_int8_t flowId, Time twtWakeInterval, Time twtNominalWakeDuration, Time nextTwtOffsetFromNextBeacon)
{
    Mac48Address apMacAddress = apMac->GetAddress();
    Mac48Address staMacAddress = staMac->GetAddress();
    apMac -> SetTwtSchedule (flowId, staMacAddress, false, true, true, false, true, 0, twtWakeInterval, twtNominalWakeDuration, nextTwtOffsetFromNextBeacon);
    staMac -> SetTwtSchedule (flowId, apMacAddress, true, true, true, false, true, 0, twtWakeInterval, twtNominalWakeDuration, nextTwtOffsetFromNextBeacon);
}

class QueueOccupancyTracker {
    public:
        QueueOccupancyTracker() 
            : m_lastUpdateTime(metricsLoggingStartTime_s), 
            m_totalAccumulatedPacketsTime(0), 
            m_currentQueueSize(0) {}

        // Call this inside your trace callback
        void UpdateSize(uint32_t newSize) {
            Time now = Simulator::Now();
            if (now < m_lastUpdateTime) { // sometimes we want to analyse from a later time
                return;
            }
            
            // Calculate the duration the queue was at the OLD size
            Time duration = now - m_lastUpdateTime;

            // Area = queue_size * duration (accumulating in nanoSeconds for precision)
            m_totalAccumulatedPacketsTime += m_currentQueueSize * duration.GetNanoSeconds();

            // Update state tracking for the next change
            m_currentQueueSize = newSize;
            m_lastUpdateTime = now;
        }

        // Call this at the very end of the simulation to get the final mean
        double GetMeanOccupancy() {
            Time now = Simulator::Now();
            uint64_t finalAccumulation = m_totalAccumulatedPacketsTime;
            
            // Add any remaining time spent at the final queue size up to the end of the simulation
            Time lastSizeDuration = now - m_lastUpdateTime;
            finalAccumulation+= m_currentQueueSize * lastSizeDuration.GetNanoSeconds();

            Time totalDuration;
            totalDuration = now - metricsLoggingStartTime_s;

            // Prevent division by zero 
            if (totalDuration.GetNanoSeconds() == 0) {
                return 0.0;
            }

            // Mean = Total Area / Total Duration
            return static_cast<double>(finalAccumulation) / totalDuration.GetNanoSeconds();
        }

    private:
        Time m_lastUpdateTime;
        uint64_t m_totalAccumulatedPacketsTime; // units: packets * nanoseconds
        uint32_t m_currentQueueSize;
};

QueueOccupancyTracker apQueueTracker;

void UpdateQueueOccupancyTracker(std::string context, uint32_t oldValue, uint32_t newValue) {
    uint32_t nodeId = ContextToNodeId(context);

    if (nodeId == 0) {
        apQueueTracker.UpdateSize(newValue);
    }
}

void WriteQueueSizeChangeToFile(std::ostream* queueSizeFile, std::string context, uint32_t oldValue, uint32_t newValue) {
    uint32_t nodeId = ContextToNodeId(context);

    if (queueSizeFile && *queueSizeFile) {
        *queueSizeFile
            << "time_ns=" << Simulator::Now().GetNanoSeconds() 
            << ";queue_size_p=" << newValue 
            << ";node_id=" << nodeId
            << std::endl;
    }
}

void DisassociationLog(std::string context, Mac48Address address)
{
    uint32_t nodeId = ContextToNodeId(context);
    NS_LOG_WARN("Disassociation: time=" << Simulator::Now() << " node=" << nodeId);
}

void PrintQueueInfo(std::string label, Ptr<Node> node, Ptr<NetDevice> device) {
    Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer>();
    if (tc) {
        Ptr<QueueDisc> qDisc = tc->GetRootQueueDiscOnDevice(device);
        if (qDisc) {
            std::cout << label << " uses QueueDisc: " 
                      << qDisc->GetInstanceTypeId().GetName() << std::endl;
        } else {
            std::cout << label << " has NO QueueDisc (L2 DropTail only)" << std::endl;
        }
    } else {
        std::cout << label << " has no TrafficControlLayer aggregated." << std::endl;
    }
}

void InspectHandlers(Ptr<Node> node) {
    Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer>();
    
    // Note: This requires making m_handlers public or adding a getter to the class
    std::cout << "--- Protocol Handlers for Node " << node->GetId() << " ---" << std::endl;
    
    for (auto const& entry : tc->m_handlers) {
        std::cout << "Protocol: 0x" << std::hex << entry.protocol << std::dec;
        if (entry.device) {
            std::cout << " on Device Index: " << entry.device->GetIfIndex();
        } else {
            std::cout << " on ALL Devices";
        }
        std::cout << (entry.promiscuous ? " [Promiscuous]" : "") << std::endl;
    }
}

void ConfirmQueues(std::string label, Ptr<Node> node, Ptr<NetDevice> device) {
    std::cout << "\n=== " << label << " (Node " << node->GetId() << ") ===" << std::endl;

    // --- L2.5 Traffic Control ---
    Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer>();
    if (tc) {
        Ptr<QueueDisc> qDisc = tc->GetRootQueueDiscOnDevice(device);
        if (qDisc) {
            std::cout << "[L2.5 Root] " << qDisc->GetInstanceTypeId().GetName();
            if (qDisc->GetInstanceTypeId().GetName() == "ns3::MqQueueDisc") {
                for (uint32_t i = 0; i < qDisc->GetNQueueDiscClasses(); ++i) {
                    Ptr<QueueDisc> child = qDisc->GetQueueDiscClass(i)->GetQueueDisc();
                    std::cout << std::endl
                        << "  |- AC " << i << " QueueDisc: " << child->GetInstanceTypeId().GetName() 
                        << " (Size: " << child->GetMaxSize() << ")";
                }
                std::cout << std::endl;
            } else {
                std::cout << " size: " << qDisc->GetMaxSize() << std::endl;
            }
        }
    }

    // --- L2 P2P Hardware ---
    Ptr<PointToPointNetDevice> p2p_device = DynamicCast<PointToPointNetDevice> (device);
    if (p2p_device) {
        Ptr<Queue<Packet>> queue = p2p_device->GetQueue ();
        std::cout << "[L2 P2P Hardware Queue]" << std::endl;
        std::cout << "  - " << queue->GetInstanceTypeId () << " Max Size: " << queue->GetMaxSize () << std::endl;
    }

    // --- L2 Wifi Hardware ---
    Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(device);
    if (wifiDevice) {
        Ptr<WifiMac> mac = wifiDevice->GetMac();
        std::string acNames[] = {"BE", "BK", "VI", "VO"};
        std::cout << "[L2 Wifi MAC Hardware Queues]" << std::endl;
        for (int i = 0; i < 4; ++i) {
            Ptr<WifiMacQueue> hwQueue = mac->GetTxopQueue(AcIndex(i));
            std::cout << "  - " << acNames[i] << " TXOP Queue Size: " << hwQueue->GetMaxSize() << std::endl;
        }
    }
}

int main (int argc, char *argv[])
{
    CommandLine cmd (__FILE__);
    // Simulation metadata
    cmd.AddValue ("randSeed", "Random seed to initialize position and app start times, int, default 7000", randSeed);
    cmd.AddValue ("scenario", "Used for Logging and grouping for batch processing. default: filename.", scenario);
    cmd.AddValue ("simulationTime", "Simulation time in seconds (including 10 seconds for setup), default 360", simulationTime);
    cmd.AddValue ("enableStateLogs", "Whether to generate state logs, default true", enableStateLogs);
    cmd.AddValue ("enablePcap", "Wheteher to generate PCAP files for all devices, default false", enablePcap);
    cmd.AddValue ("monitorQueueSizes", "Wheteher to print WifiMacQueue size to a separate file at each change, default true", monitorQueueSizes);
    cmd.AddValue ("realTimeSimulator", "Whether to enable the real time simulator (syncs ns-3 clock with real time), default false", realTimeSimulator);
    cmd.AddValue ("statsFilename", "string, default: stats.txt", statsFilename);
    // Protocol params
    cmd.AddValue ("protocol", "Transport protocol: tcp or udp, default udp", protocol);
    cmd.AddValue ("nStations", "Number of non-AP HE stations, default 1", nStations);
    cmd.AddValue ("beaconInterval_ms", "Beacon duration expressed in ms, default 102.4", beaconInterval_ms);
    cmd.AddValue ("maxMissedBeacons", "int, default: max integer", maxMissedBeacons);
    cmd.AddValue ("maxMuSta", "Max number of STAs the AP can trigger in one MU_UL with Basic TF, default 1", maxMuSta);
    cmd.AddValue ("maxMpdusAp", "The maximum number of MPDUs in A-MPDUs at AP transmission queue, 0 to disable MPDU aggregation, default 8", maxMpdusAp);
    cmd.AddValue ("maxMpdusSta", "The maximum number of MPDUs in A-MPDUs at STAs transmission queues, 0 to disable MPDU aggregation, default 8", maxMpdusSta);
    cmd.AddValue ("wifiMacQueueSize", "MAC buffer size, string, default 1000p", wifiMacQueueSize);
    cmd.AddValue ("wifiMacQueueDelay_ms", "int, MAC queue timeout, default 1000", wifiMacQueueDelay_ms);
    cmd.AddValue ("flowMonitorPacketTimeout_s", "int, if a packet is not seen for more than this duration it is considered lost, default 10", flowMonitorPacketTimeout_s);
    cmd.AddValue ("queueDiscSize", "queueDisc buffer size, string, default 10240p", queueDiscSize);
    cmd.AddValue ("p2pDataRate", "physical link capacity, string, default 1000Mbps", p2pDataRate);
    cmd.AddValue ("mcs", "HE MCS index. can be in [0, 11], -1 indicates an unset value, default 7", mcs);
    cmd.AddValue ("gi", "guard interval in nanoseconds (can be 800, 1600 or 3200 ns), default 800", gi);
    cmd.AddValue ("channelWidth", "channel width in MHz (can be 20, 40, 80, or 160 MHz also for 5 or 6 GHz band), default 20", channelWidth);
    cmd.AddValue ("enableEdca", "default false", enableEdca);
    // TWT params
    cmd.AddValue ("enableTwt", "Whether to setup TWT (for all devices, AP and STAs), default true", enableTwt);
    cmd.AddValue ("firstTwtSpOffsetFromBeacon_ms", "double, Offset from beacon for first TWT SP, default 2", firstTwtSpOffsetFromBeacon_ms);
    cmd.AddValue ("firstTwtSpStart_s", "double, Start time for the first SP, default 6", firstTwtSpStart_s);
    cmd.AddValue ("t_sp", "double, nominal wake duration in ms", t_sp);
    cmd.AddValue ("t_rp", "double, nominal sleep duration in ms", t_rp);
    // Video traffic params
    cmd.AddValue ("sendWarmupPacket", "Whether to send warmup packet to establish ARP before starting video traffic, default true", sendWarmupPacket);
    cmd.AddValue ("backgroundStaRate", "int: arrival rate (packets per s) of a DL-only background STA, default 400", backgroundStaRate);
    cmd.AddValue ("ulTrafficRate", "int: arrival rate (packets per s) of STA 1 UL, default 70, 0 for no UL", ulTrafficRate);
    // Video traffic distribution
    cmd.AddValue ("videoTrafficDistribution", "string, possible values: weibull, poisson, custom_trace (WIP), default: weibull", videoTrafficDistribution);
    cmd.AddValue ("weibullScale", "[weibull] double, default 6950", weibullScale);
    cmd.AddValue ("weibullShape", "[weibull] double, default 0.8099", weibullShape);
    cmd.AddValue ("videoQuality", "[weibull] Integer between 1 and 6 for bv1 through bv6, default 0 to use scale and shape", videoQuality);
    cmd.AddValue ("videoFrameInterval_s", "[weibull] Double: Frame interval based on frame rate, default 0.03333 for 30 FPS", videoFrameInterval_s);
    cmd.AddValue ("poissonArrivalRate", "[poisson] double: arrival rate (packets per s), default 1.53", poissonArrivalRate);
    cmd.AddValue ("traceFilename", "[custom_trace] string: filename from the Traces/ folder, default: streaming_2160p_DL.txt", traceFilename);
    cmd.Parse (argc, argv);

    if (realTimeSimulator) {
        GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::RealtimeSimulatorImpl"));
    }

    LogComponentEnable ("videoTest", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("PoissonApplication", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("CustomTraceApplication", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("VideoApplication", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WeibullExpApplication", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("StaWifiMac", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("ApWifiMac", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("ArpL3Protocol", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("NetDeviceQueueInterface", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("PointToPointNetDevice", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("Ipv4L3Protocol", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WifiMacQueue", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL)); // queue operations
    // LogComponentEnable ("WifiMacQueueScheduler", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL)); // queue operations
    // LogComponentEnable ("QueueDisc", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL)); // queue operations. drop
    // LogComponentEnable ("FqCoDelQueueDisc", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL)); // queue operations
    // LogComponentEnable ("WifiPhy", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL)); // sleep mode, transmissions, aggreg
    // LogComponentEnable ("MpduAggregator", LOG_LEVEL_ALL); // info about aggregation
    // LogComponentEnable ("WifiMac", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("LlcSnalHeader", LOG_LEVEL_ALL);
    // LogComponentEnable ("InterferenceHelper", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("YansWifiPhy", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("SpectrumWifiPhy", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WifiPhyStateHelper", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WifiHelper", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WifiMpdu", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WifiRemoteStationManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("FrameExchangeManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("QosFrameExchangeManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("VhtFrameExchangeManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("HtFrameExchangeManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("HeFrameExchangeManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("ChannelAccessManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL)); // backoff
    // LogComponentEnable ("QosTxop", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("Txop", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL)); // backoff values
    // LogComponentEnable ("PhyEntity", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WifiDefaultAckManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("WifiAckManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("MultiUserScheduler", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("BlockAckManager", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("BlockAckAgreement", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("OriginatorBlockAckAgreement", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("BlockAckWindow", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("MacTxMiddle", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("BasicEnergySource", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("PacketSink", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("SeqTsHeader", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("UdpServer", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("UdpClient", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("UdpSocket", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("TcpHeader", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("TcpSocketBase", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("TcpSocket", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));
    // LogComponentEnable ("TwtRrMultiUserScheduler", LogLevel (LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_ALL));

    throughput = new double[nStations];
    totalTimeElapsedForSta_ns = new double[nStations];
    awakeTimeElapsedForSta_ns = new double[nStations];
    sleepTimeElapsedForSta_ns = new double[nStations];
    txTimeElapsedForSta_ns = new double[nStations];
    rxTimeElapsedForSta_ns = new double[nStations];
    idleTimeElapsedForSta_ns = new double[nStations];
    ccaBusyTimeElapsedForSta_ns = new double[nStations];
    current_mA_TimesTime_ns_ForSta_TI = new double[nStations];
    uplinkTimeoutsForSta = new double[nStations];
    uplinkExpiredMpduForSta = new double[nStations];
    uplinkFailedEnqueueMpduForSta = new double[nStations];

    for (std::size_t i = 0; i <nStations; i++)
    {
        totalTimeElapsedForSta_ns[i] = 0;
        awakeTimeElapsedForSta_ns[i] = 0;
        sleepTimeElapsedForSta_ns[i] = 0;
        txTimeElapsedForSta_ns[i] = 0;
        rxTimeElapsedForSta_ns[i] = 0;
        idleTimeElapsedForSta_ns[i] = 0;
        ccaBusyTimeElapsedForSta_ns[i] = 0;
        current_mA_TimesTime_ns_ForSta_TI[i] = 0;
        uplinkTimeoutsForSta[i] = 0;
        uplinkExpiredMpduForSta[i] = 0;

    }
    downlinkTimeoutsAllSta = 0;
    downlinkExpiredMpduAllSta = 0;
    downlinkFailedEnqueueMpduAllSta = 0;

    // Check if configuration is valid
    NS_ASSERT_MSG (nStations > 0, "nStations should be > 0");

    if (twtTriggerBased == false && maxMuSta != 1)
    {
        std::cout<<"Error. Non trigger based but maxMuSTA is not 1. Exiting.";
        return 0;
    }

    switch (videoQuality)
    {
        case 0:
            break;
        case 1:
            weibullScale = 6950;
            break;
        case 2:
            weibullScale = 13900;
            break;
        case 3:
            weibullScale = 20850;
            break;
        case 4:
            weibullScale = 27800;
            break;
        case 5:
            weibullScale = 34750;
            break;
        case 6:
            weibullScale = 54210;
            break;
        default:
            std::cout<<"Error. Video quality invalid. Exiting.";
            return 0;
    }

    std::string dlAckSeqType;
    if (enableTwt && twtTriggerBased)
    {
        dlAckSeqType = "AGGR-MU-BAR"; // Use this for MU TWT - only for trigger based
    }
    else
    {
        dlAckSeqType = "NO-OFDMA"; // For PSM and no PS
    }

    if (maxMpdusAp == 0 && maxMpdusSta == 0) {
        // Config::SetDefault("ns3::WifiMac::MaxAmpduSize", UintegerValue(0));
        // Config::SetDefault("ns3::WifiMac::MaxAmsduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::VO_MaxAmpduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::VI_MaxAmpduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::BE_MaxAmpduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::BK_MaxAmpduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::VO_MaxAmsduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::VI_MaxAmsduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::BE_MaxAmsduSize", UintegerValue(0));
        Config::SetDefault("ns3::WifiMac::BK_MaxAmsduSize", UintegerValue(0));
    }

    Config::SetDefault ("ns3::ArpCache::MaxRetries", UintegerValue (20));
    Config::SetDefault ("ns3::ArpCache::WaitReplyTimeout", TimeValue(MilliSeconds(1000)));
    Config::SetDefault ("ns3::ArpCache::AliveTimeout", TimeValue(Seconds(1000))); // default: 120s
    Config::SetDefault ("ns3::WifiMacQueue::MaxDelay", TimeValue (MilliSeconds (wifiMacQueueDelay_ms))); 
    if (queueDiscSize != "10240p") {
        Config::SetDefault ("ns3::DropTailQueue<Packet>::MaxSize", QueueSizeValue (QueueSize (queueDiscSize)));
        Config::SetDefault ("ns3::FifoQueueDisc::MaxSize", QueueSizeValue (QueueSize (queueDiscSize)));
        Config::SetDefault ("ns3::CoDelQueueDisc::MaxSize", QueueSizeValue (QueueSize (queueDiscSize)));
        Config::SetDefault ("ns3::FqCobaltQueueDisc::MaxSize", QueueSizeValue (QueueSize (queueDiscSize)));
        Config::SetDefault ("ns3::FqPieQueueDisc::MaxSize", QueueSizeValue (QueueSize (queueDiscSize)));
        Config::SetDefault ("ns3::FqCoDelQueueDisc::MaxSize", QueueSizeValue (QueueSize (queueDiscSize))); // this one is used
    }

    if (enableEdca) {
        // To set QoS queue size in all QoS data frames by all STAs
        Config::SetDefault ("ns3::QosFrameExchangeManager::SetQueueSize", BooleanValue (true));
    }

    if (dlAckSeqType == "ACK-SU-FORMAT")
    {
        Config::SetDefault ("ns3::WifiDefaultAckManager::DlMuAckSequenceType", EnumValue (WifiAcknowledgment::DL_MU_BAR_BA_SEQUENCE));
    }
    else if (dlAckSeqType == "MU-BAR")
    {
        Config::SetDefault ("ns3::WifiDefaultAckManager::DlMuAckSequenceType", EnumValue (WifiAcknowledgment::DL_MU_TF_MU_BAR));
    }
    else if (dlAckSeqType == "AGGR-MU-BAR")
    {
        Config::SetDefault ("ns3::WifiDefaultAckManager::DlMuAckSequenceType", EnumValue (WifiAcknowledgment::DL_MU_AGGREGATE_TF));
    }
    else if (dlAckSeqType != "NO-OFDMA")
    {
        NS_ABORT_MSG ("Invalid DL ack sequence type (must be NO-OFDMA, ACK-SU-FORMAT, MU-BAR or AGGR-MU-BAR)");
    }

    std::string socketFactoryString;
    if (protocol == "tcp") {
        socketFactoryString = "ns3::TcpSocketFactory";
        Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (pktSizeBytes));
    } else if (protocol == "udp") {
        socketFactoryString = "ns3::UdpSocketFactory";
        Config::SetDefault ("ns3::UdpSocket::RcvBufSize", UintegerValue (udpRcvBufferSize)); 
    } else {
        NS_ABORT_MSG ("Invalid protocol (must be tcp or udp)");
    }

    if (videoTrafficDistribution == "weibull") {
        applicationClassName = "ns3::WeibullExpApplication";
    } else if (videoTrafficDistribution == "poisson") {
        applicationClassName = "ns3::PoissonApplication";
    } else if (videoTrafficDistribution == "custom_trace") {
        applicationClassName = "ns3::CustomTraceApplication";
    } else {
        NS_ABORT_MSG ("Invalid videoTrafficDistribution (must be weibull, poisson or custom_trace)");
    }

    // Creating nodes
    NodeContainer wifiApNodes;
    wifiApNodes.Create (1);
    Ptr<Node> ApNode = wifiApNodes.Get (0);

    NodeContainer wifiStaNodes;
    wifiStaNodes.Create (nStations);

    NodeContainer p2pServerNodes; // only the remote p2p server node - not including AP
    p2pServerNodes.Create (1);
    Ptr<Node> p2pServerNode = p2pServerNodes.Get (0); // the remote P2P node

    NodeContainer p2pConnectedNodes; // remote server and AP node
    p2pConnectedNodes.Add (ApNode);
    p2pConnectedNodes.Add (p2pServerNode);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", StringValue (p2pDataRate));
    pointToPoint.SetChannelAttribute ("Delay", StringValue (p2pDelay));

    NetDeviceContainer p2pdevices;
    p2pdevices = pointToPoint.Install (p2pConnectedNodes);

    // Printing Node IDs
    std::cout<<"Node IDs:\n";
    std::cout<<"\tP2P node0: "<<unsigned(p2pServerNodes.Get(0)->GetId())<<std::endl;
    std::cout<<"\tAP: "<<unsigned(wifiApNodes.Get(0)->GetId()) <<std::endl;
    std::cout<<"\tSTA 1 (TWT) is: "<<unsigned(wifiStaNodes.Get(0)->GetId()) <<std::endl;
    for (uint32_t i=1 ; i < wifiStaNodes.GetN(); i++) {
        std::cout<<"\tSTA "<<i+1<<" is: "<<unsigned(wifiStaNodes.Get(i)->GetId()) <<std::endl;
    }
    

    NetDeviceContainer apDevice, staDevices;
    WifiMacHelper mac;
    WifiHelper wifi;
    auto nonHtRefRateMbps = HePhy::GetNonHtReferenceRate(mcs) / 1e6;
    std::string channelStr ("{0, " + std::to_string (channelWidth) + ", ");
    if (frequency == 6)
    {
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        channelStr += "BAND_6GHZ, 0}";
        Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(48));
    }
    else if (frequency == 5)
    {
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        std::ostringstream ossControlMode;
        ossControlMode << "OfdmRate" << nonHtRefRateMbps << "Mbps";
        channelStr += "BAND_5GHZ, 0}";
    }
    else if (frequency == 2.4)
    {
        wifi.SetStandard(WIFI_STANDARD_80211ax);
        std::ostringstream ossControlMode;
        ossControlMode << "ErpOfdmRate" << nonHtRefRateMbps << "Mbps";
        channelStr += "BAND_2_4GHZ, 0}";
        Config::SetDefault("ns3::LogDistancePropagationLossModel::ReferenceLoss", DoubleValue(40));
    }
    else
    {
        std::cout << "Wrong frequency value!" << std::endl;
        return 0;
    }
    Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue (65535));
    std::ostringstream ossDataMode;
    ossDataMode << "HeMcs" << mcs;
    wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (ossDataMode.str()), "ControlMode", StringValue ("HeMcs0"));
    Ssid ssid = Ssid ("ns3-80211ax");

    /*
    * SingleModelSpectrumChannel cannot be used with 802.11ax because two spectrum models are required: 
    * one with 78.125 kHz bands for HE PPDUs and one with 312.5 kHz bands for, e.g., non-HT PPDUs.
    * For more details, see issue #408 (CLOSED).
    */
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel> ();
    SpectrumWifiPhyHelper phy;
    phy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
    phy.SetChannel (spectrumChannel);

    mac.SetType ("ns3::StaWifiMac",
        "QosSupported", BooleanValue (enableEdca),
        "MaxMissedBeacons", UintegerValue(maxMissedBeacons),  
        "BE_BlockAckThreshold", UintegerValue (1),  // If AMPDU is used, Block Acks will always be used regardless of this value
        "BE_MaxAmpduSize", UintegerValue(maxMpdusSta * (pktSizeBytes + 50)),
        "Ssid", SsidValue (ssid));
    phy.Set ("ChannelSettings", StringValue (channelStr));
    staDevices = wifi.Install (phy, mac, wifiStaNodes);
    if (dlAckSeqType != "NO-OFDMA")   
    {
        mac.SetMultiUserScheduler ("ns3::TwtRrMultiUserScheduler",
            "EnableUlOfdma", BooleanValue (true),
            "EnableBsrp", BooleanValue (twtTriggerBased),
            "NStations", UintegerValue (maxMuSta));
    }
    mac.SetType ("ns3::ApWifiMac",
        "QosSupported", BooleanValue (enableEdca),
        "EnableBeaconJitter", BooleanValue (false),
        "BE_BlockAckThreshold", UintegerValue (1),
        // "BE_MaxAmpduSize", UintegerValue(maxMpdusAp * pktSizeBytes),
        "BE_MaxAmpduSize", UintegerValue(maxMpdusAp * (pktSizeBytes + 50)),
        // "BE_MaxAmsduSize", UintegerValue(11398),
        "BsrLifetime", TimeValue (MilliSeconds (10)),
        "Ssid", SsidValue (ssid));

    apDevice = wifi.Install (phy, mac, wifiApNodes);

    if (enablePcap)
    {
        std::stringstream ssPcap;
        ssPcap<<LOG_PATH<<"pcap_all";
        phy.EnablePcapAll (ssPcap.str());
    }

    RngSeedManager::SetSeed (randSeed);
    RngSeedManager::SetRun (1);
    
    int64_t streamNumber = 100;
    streamNumber += wifi.AssignStreams (apDevice, streamNumber);
    streamNumber += wifi.AssignStreams (staDevices, streamNumber);
    
    Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/HeConfiguration/GuardInterval", TimeValue (NanoSeconds (gi)));

    // Set max missed beacons to avoid dis-association
    // uint32_t staMaxMissedBeacon = 10000; // Set the max missed beacons for STA before attempt for reassociation
    // Time AdvanceWakeupPS = MicroSeconds (10);
    // for (u_int32_t ii = 0; ii < wifiStaNodes.GetN() ; ii++)
    // {
    //   std::stringstream nodeIndexStringTemp, maxBcnStr, advWakeStr, apsdStr;
    //   nodeIndexStringTemp << wifiStaNodes.Get(ii)->GetId();
    //   // maxBcnStr = "/NodeList/" + std::string(ii+1) + "/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/$ns3::StaWifiMac/MaxMissedBeacons";
    //   maxBcnStr << "/NodeList/" << nodeIndexStringTemp.str() << "/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/$ns3::StaWifiMac/MaxMissedBeacons";
    //   // advWakeStr << "/NodeList/" << nodeIndexStringTemp.str() << "/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/$ns3::StaWifiMac/AdvanceWakeupPS";
    //   Config::Set (maxBcnStr.str(), UintegerValue(staMaxMissedBeacon));
    // }

    // Config::Set ("/NodeList/1/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/$ns3::StaWifiMac/MaxMissedBeacons", UintegerValue(staMaxMissedBeacon));
    // Config::Set ("/NodeList/1/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/$ns3::StaWifiMac/AdvanceWakeupPS", TimeValue(AdvanceWakeupPS));

    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/DeAssoc", MakeCallback(&DisassociationLog));

    // Setup TWT schedule
    if (enableTwt)
    {
        Time firstTwtSpOffsetFromBeacon = getTimeUnitFromMs(firstTwtSpOffsetFromBeacon_ms); // Offset from beacon for first TWT SP
        Time firstTwtSpStart = getTimeUnitFromS(firstTwtSpStart_s); 

        for (uint32_t i=0 ; i < wifiStaNodes.GetN(); i++) // all STAs have the same TWT schedule
        {
            Ptr<WifiNetDevice> apDevice = DynamicCast<WifiNetDevice>(ApNode->GetDevice(1));
            Ptr<WifiMac> apMac = apDevice->GetMac();
            Ptr<WifiNetDevice> staDevice = DynamicCast<WifiNetDevice>(wifiStaNodes.Get(i)->GetDevice(0));
            Ptr<WifiMac> staMac = staDevice->GetMac();
            
            // Setting up TWT at AP and STA
            Time nextTwtOffsetFromNextBeacon = firstTwtSpOffsetFromBeacon; // AP should schedule starts from (timeLeftTillNextBeacon + nextTwtOffsetFromNextBeacon)
            Time twtNominalWakeDuration = getTimeUnitFromMs(t_sp);
            Time twtWakeInterval = getTimeUnitFromMs((t_sp+t_rp));
            
            Simulator::Schedule (firstTwtSpStart, &initiateTwtAtApAndSta, apMac, staMac, 0, twtWakeInterval, twtNominalWakeDuration, nextTwtOffsetFromNextBeacon);
            
            // Print TWT schedule information for each STA
            std::cout << "\nTWT agreement for STA: " << i <<"\n\tInitiated at t = " << firstTwtSpStart.As(Time::S) << ";\n\tTWT SP starts at " << nextTwtOffsetFromNextBeacon.As(Time::MS) << " after next beacon;\n\tTWT Wake Interval = "<< twtWakeInterval.As(Time::MS)<<";\n\tTWT Nominal Wake Duration = "<< twtNominalWakeDuration.As(Time::MS)<<";";
        }
    }

    // Enable PHY of wifiStaNodes at random times - to avoid assoc req at same time
    for (uint32_t i=0 ; i < wifiStaNodes.GetN(); i++)
    {
        Ptr<Node> n = wifiStaNodes.Get (i);
        Ptr<WifiNetDevice> wifi_dev = DynamicCast<WifiNetDevice> (n->GetDevice (0)); // assuming only one device
        Ptr<WifiPhy> phy = wifi_dev->GetPhy();
        phy->SetOffMode (); // turn all of them off

        // Schedule random time when they switch their radio ON
        Ptr<UniformRandomVariable> random = CreateObject<UniformRandomVariable> ();
        uint32_t start_time = random->GetInteger (0, 2500);
        Simulator::Schedule (MilliSeconds (start_time), &RandomWifiStart, phy);
    }

    // Mobility
    // Room dimension in meters - creating uniform random number generator
    double minX = -1.0 * roomLength/2;
    double maxX = 1.0 * roomLength/2;
    double minY = -1.0 * roomLength/2;
    double maxY = 1.0 * roomLength/2;

    // STA positions
    double currentX, currentY;
    Ptr<UniformRandomVariable> xCoordinateRand = CreateObject<UniformRandomVariable> ();
    Ptr<UniformRandomVariable> yCoordinateRand = CreateObject<UniformRandomVariable> ();

    xCoordinateRand->SetAttribute ("Min", DoubleValue (minX));
    xCoordinateRand->SetAttribute ("Max", DoubleValue (maxX));
    yCoordinateRand->SetAttribute ("Min", DoubleValue (minY));
    yCoordinateRand->SetAttribute ("Max", DoubleValue (maxY));

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();

    positionAlloc->Add (Vector (0.0, 0.0, 0.0)); // AP node
    // double distance {5.0}; // meters
    // positionAlloc->Add (Vector (distance, 0.0, 0.0)); // STA nodes
    for (uint32_t ii = 0; ii < nStations ; ii++)
    {
        currentX = xCoordinateRand->GetValue ();
        currentY = yCoordinateRand->GetValue ();
        std::cout<<"\n\tPosition of STA "<<ii<<" : [ "<< currentX<<", "<<currentY <<", 0.0 ];\n";
        positionAlloc->Add (Vector (currentX, currentY, 0.0));
    }

    mobility.SetPositionAllocator (positionAlloc);
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (wifiApNodes);
    mobility.Install (wifiStaNodes);

    // Internet stack
    InternetStackHelper stack;
    stack.Install (wifiApNodes);
    stack.Install (wifiStaNodes);
    stack.Install (p2pServerNodes);

    Ipv4AddressHelper address;
    address.SetBase ("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staNodeInterfaces;
    Ipv4InterfaceContainer apNodeInterface;
    Ipv4InterfaceContainer p2pNodeInterfaces;

    apNodeInterface = address.Assign (apDevice);
    staNodeInterfaces = address.Assign (staDevices);
    address.SetBase ("192.168.2.0", "255.255.255.0");
    p2pNodeInterfaces = address.Assign (p2pdevices);

    // TrafficControlHelper tch;
    // // Force a single queue disc (no Mq, no 4 children)
    // tch.SetRootQueueDisc ("ns3::FqCoDelQueueDisc", "MaxSize", QueueSizeValue (QueueSize ("10240p")));
    // tch.Install (apDevice);

    // Create a map of IP addresses to MAC addresses
    std::map<Ipv4Address, Mac48Address> ipToMac;
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
    {
        Ptr<WifiNetDevice> wifi_dev = DynamicCast<WifiNetDevice> (wifiStaNodes.Get (i)->GetDevice (0)); // assuming only one device
        Ptr<WifiMac> wifi_mac = wifi_dev->GetMac();
        Ptr<StaWifiMac> sta_mac = DynamicCast<StaWifiMac> (wifi_mac);
        ipToMac[staNodeInterfaces.GetAddress(i)] = sta_mac->GetAddress();
    }
    // Pretty print ipToMac
    std::cout << "\n\nSTA IP to MAC mapping:\n";
    for (auto it = ipToMac.begin (); it != ipToMac.end (); it++)
    {
        std::cout << it->first << " => " << it->second << '\n';
    }
    std::cout << "P2P/AP IP to MAC mapping:\n";
    Ptr<WifiNetDevice> apWifiDevice = DynamicCast<WifiNetDevice>(ApNode->GetDevice(1));
    Ptr<WifiMac> apMac = apWifiDevice->GetMac();
    std::cout << p2pNodeInterfaces.GetAddress (1) << " => " << apMac->GetAddress() << '\n';

    ApplicationContainer serverApp;
    
    if (ulTrafficRate > 0.0) // we have UL traffic
    {
        uint16_t port = 50000;

        for (std::size_t i = 0; i < 1; i++) // only one STA with UL traffic
        {
            ApplicationContainer tempServerApp;
            // Server = receiver = AP
            if (protocol == "tcp") {
                Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port + i));
                PacketSinkHelper sinkHelper (socketFactoryString, sinkLocalAddress);
                tempServerApp = sinkHelper.Install (p2pServerNode);
            } else {
                UdpServerHelper server(port+i);
                tempServerApp = server.Install(p2pServerNode);
            }
            tempServerApp.Start (Seconds (0.0));
            tempServerApp.Stop (Seconds (simulationTime));
            serverApp.Add (tempServerApp);

            ApplicationHelper videoUL ("ns3::PoissonApplication", socketFactoryString, (InetSocketAddress (p2pNodeInterfaces.GetAddress (1), port+i)));
            videoUL.SetAttribute ("ArrivalInterval", TimeValue (getTimeUnitFromS((double)1/ulTrafficRate)));
            videoUL.SetAttribute ("SendWarmupPacket", BooleanValue(sendWarmupPacket));
            videoUL.SetAttribute ("TrafficStartOffset", UintegerValue(videoTrafficStart_ms - videoAppStart_ms));
            // Client = sender = each STA
            ApplicationContainer clientApp = videoUL.Install (wifiStaNodes.Get (i));
            if (sendWarmupPacket) {
                clientApp.Start (MilliSeconds (videoAppStart_ms));
            } else {
                clientApp.Start (MilliSeconds (videoTrafficStart_ms));
            }
            clientApp.Stop (Seconds (simulationTime));
        }
    }

    if (true) // we have DL traffic
    {
        uint16_t port = 60000;

        for (std::size_t i = 0; i < nStations; i++)
        {
            ApplicationContainer tempServerApp;
            // Server = receiver = each STA
            if (protocol == "tcp") {
                Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port + i));
                PacketSinkHelper sinkHelper (socketFactoryString, sinkLocalAddress);
                tempServerApp = sinkHelper.Install (wifiStaNodes.Get (i));
            } else {
                UdpServerHelper server(port+i);
                tempServerApp = server.Install(wifiStaNodes.Get(i));
            }
            tempServerApp.Start (Seconds (0.0));
            tempServerApp.Stop (Seconds (simulationTime));
            serverApp.Add (tempServerApp); 

            ApplicationContainer clientApp;
            if (i > 0) { // background STAs

                // If STA 1(i=0) has Weibull traffic, STA 3(i=2) and STA 5(i=4) will do Weibull, the rest Poisson
                if (videoTrafficDistribution == "weibull" && i % 2 == 0) {
                    double thisBackgroundStaFrameRate = 10;
                    double thisBackgroundStaWeibullScale = 26210.65; // 1batch ~ 20pkts * 1472 B => ~ 200pkts/s
                    std::cout << "STA "<<i+1<<" has rate: "<<thisBackgroundStaFrameRate<<std::endl;

                    ApplicationHelper videoDL ("ns3::WeibullExpApplication", socketFactoryString, (InetSocketAddress (staNodeInterfaces.GetAddress (i), port+i)));
                    videoDL.SetAttribute ("FrameInterval", TimeValue (getTimeUnitFromS((double)1/thisBackgroundStaFrameRate)));
                    videoDL.SetAttribute ("WeibullScale", DoubleValue (thisBackgroundStaWeibullScale));
                    videoDL.SetAttribute ("WeibullShape", DoubleValue (weibullShape));
                    videoDL.SetAttribute ("SendWarmupPacket", BooleanValue(sendWarmupPacket));
                    videoDL.SetAttribute ("TrafficStartOffset", UintegerValue(videoTrafficStart_ms - videoAppStart_ms));
                    // Client = sender
                    clientApp = videoDL.Install (p2pServerNode);

                } else {
                    ApplicationHelper videoDL ("ns3::PoissonApplication", socketFactoryString, (InetSocketAddress (staNodeInterfaces.GetAddress (i), port+i)));
                    uint32_t thisBackgroundStaRate = 100 * i;
                    if (i % 2 == 1) {
                        thisBackgroundStaRate = 100;
                    } else {
                        thisBackgroundStaRate = 200;
                    }
                    std::cout << "STA "<<i+1<<" has rate: "<<thisBackgroundStaRate<<std::endl;
                    videoDL.SetAttribute ("ArrivalInterval", TimeValue (getTimeUnitFromS((double)1/(thisBackgroundStaRate))));
                    videoDL.SetAttribute ("SendWarmupPacket", BooleanValue(sendWarmupPacket));
                    videoDL.SetAttribute ("TrafficStartOffset", UintegerValue(videoTrafficStart_ms - videoAppStart_ms));
                    // Client = sender
                    clientApp = videoDL.Install (p2pServerNode);
                }

            } else {
                ApplicationHelper videoDL (applicationClassName, socketFactoryString, (InetSocketAddress (staNodeInterfaces.GetAddress (i), port+i)));
                if (videoTrafficDistribution == "poisson") 
                {
                    videoDL.SetAttribute ("ArrivalInterval", TimeValue (getTimeUnitFromS((double)1/poissonArrivalRate)));
                } else if (videoTrafficDistribution == "weibull") 
                {
                    videoDL.SetAttribute ("FrameInterval", TimeValue (getTimeUnitFromS(videoFrameInterval_s)));
                    videoDL.SetAttribute ("WeibullScale", DoubleValue (weibullScale));
                    videoDL.SetAttribute ("WeibullShape", DoubleValue (weibullShape));
                } else {
                    videoDL.SetAttribute ("TraceFilename", StringValue(traceFilename));
                }
                videoDL.SetAttribute ("SendWarmupPacket", BooleanValue(sendWarmupPacket));
                videoDL.SetAttribute ("TrafficStartOffset", UintegerValue(videoTrafficStart_ms - videoAppStart_ms));
                // Client = sender
                clientApp = videoDL.Install (p2pServerNode);
            }
            
            if (sendWarmupPacket) {
                // clientApp.Start (MilliSeconds (videoAppStart_ms+i*100));
                clientApp.Start (MilliSeconds (videoAppStart_ms));
            } else {
                clientApp.Start (MilliSeconds (videoTrafficStart_ms));
            }
            clientApp.Stop (Seconds (simulationTime));
        }
    }

    // For state tracing - to log file
    for (std::size_t ii = 0; ii < wifiStaNodes.GetN() ; ii++)
    {
        std::stringstream nodeIndexStringTemp, phyStateStr;
        nodeIndexStringTemp << wifiStaNodes.Get(ii)->GetId();
        phyStateStr << "/NodeList/" << nodeIndexStringTemp.str() << "/DeviceList/*/Phy/State/State";
        Config::Connect (phyStateStr.str(), MakeCallback (&PhyStateTrace_inPlace));

        if (enableStateLogs)
        {
          Config::Connect (phyStateStr.str(), MakeCallback (&PhyStateTrace));
        }
    }

    // Tracing retries: PSDU
    for (uint32_t i=0 ; i < wifiStaNodes.GetN(); i++)
    {
        std::stringstream nodeIndexStringTemp, macStr;
        nodeIndexStringTemp << wifiStaNodes.Get(i)->GetId();
        macStr << "/NodeList/" << nodeIndexStringTemp.str() << "/DeviceList/*/$ns3::WifiNetDevice/Mac/PsduResponseTimeout";
        Config::Connect (macStr.str(), MakeCallback (&PsduResponseTimeoutTraceSta));
    }

    // PSDU timeout trace for AP - for downlink timeouts
    std::stringstream nodeIndexStringTemp, macStr;
    nodeIndexStringTemp << wifiApNodes.Get(0)->GetId();
    macStr << "/NodeList/" << nodeIndexStringTemp.str() << "/DeviceList/*/$ns3::WifiNetDevice/Mac/PsduResponseTimeout";
    Config::Connect (macStr.str(), MakeCallback (&PsduResponseTimeoutTraceAp));

    // Tracing MPDU drops at STAs
    for (uint32_t i=0 ; i < wifiStaNodes.GetN(); i++)
    {
        std::stringstream nodeIndexStringTemp, macStr;
        nodeIndexStringTemp << wifiStaNodes.Get(i)->GetId();
        macStr << "/NodeList/" << nodeIndexStringTemp.str() << "/DeviceList/*/$ns3::WifiNetDevice/Mac/DroppedMpdu";
        Config::Connect (macStr.str(), MakeCallback (&MpduDropped_atSta));
    }

    // At AP
    std::stringstream nodeIndexStringTemp2, macStr2;
    nodeIndexStringTemp2 << wifiApNodes.Get(0)->GetId();
    macStr2 << "/NodeList/" << nodeIndexStringTemp2.str() << "/DeviceList/*/$ns3::WifiNetDevice/Mac/DroppedMpdu";
    Config::Connect (macStr2.str(), MakeCallback (&MpduDropped_atAp));

    // Config::Set ("/NodeList/0/$ns3::TrafficControlLayer/RootQueueDiscList/1/QueueDiscClassList/*/QueueDisc/MaxSize", QueueSizeValue (QueueSize ("1p")));
    // Config::Set ("/NodeList/0/$ns3::TrafficControlLayer/RootQueueDiscList/1/QueueDiscClassList/*/QueueDisc/$ns3::FqCoDelQueueDisc/MaxSize", QueueSizeValue (QueueSize ("1p")));
    
    // Change L3 queue sizes to 1p 
    Ptr<TrafficControlLayer> tc = ApNode->GetObject<TrafficControlLayer>();
    Ptr<QueueDisc> root = tc->GetRootQueueDiscOnDevice(apDevice.Get(0)); 
    for (uint32_t i = 0; i < root->GetNQueueDiscClasses(); i++) {
        Ptr<QueueDiscClass> qclass = root->GetQueueDiscClass(i);
        Ptr<QueueDisc> child = qclass->GetQueueDisc();
        child->SetAttribute("MaxSize", QueueSizeValue(QueueSize("1p")));
    }
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i) {
        tc = wifiStaNodes.Get(i)->GetObject<TrafficControlLayer>();
        root = tc->GetRootQueueDiscOnDevice(staDevices.Get(i)); 
        for (uint32_t i = 0; i < root->GetNQueueDiscClasses(); i++) {
            Ptr<QueueDiscClass> qclass = root->GetQueueDiscClass(i);
            Ptr<QueueDisc> child = qclass->GetQueueDisc();
            child->SetAttribute("MaxSize", QueueSizeValue(QueueSize("1p")));
        }
    }

    // Customize AP L2 queue size 
    std::vector<AcIndex> acs = {AC_VO, AC_VI, AC_BE, AC_BK};
    for (int i = 0; i < 4; ++i) {
        Ptr<WifiMacQueue> queue = apMac->GetTxopQueue(AcIndex(i));
        queue->SetMaxSize(QueueSize(wifiMacQueueSize));
    }

    if (monitorQueueSizes) {
        static std::ofstream* wifiMacQueueSizeFile = nullptr; 
        static std::ofstream* queueDiscSizeFile = nullptr; 
        static std::ofstream* serverL2QueueSizeFile = nullptr; 
        std::stringstream wifiMacQueueSizeFilename, queueDiscSizeFilename, serverL2QueueSizeFilename;

        wifiMacQueueSizeFilename << LOG_PATH << "wifiMacQueueSize.log";
        queueDiscSizeFilename << LOG_PATH << "queueDiscSize.log";
        serverL2QueueSizeFilename << LOG_PATH << "serverL2queueSize.log";
        wifiMacQueueSizeFile = new std::ofstream(wifiMacQueueSizeFilename.str().c_str());
        queueDiscSizeFile = new std::ofstream(queueDiscSizeFilename.str().c_str());
        serverL2QueueSizeFile = new std::ofstream(serverL2QueueSizeFilename.str().c_str());

        if (wifiMacQueueSizeFile->is_open() && !wifiMacQueueSizeFile->fail()) {
            Config::Connect ("/NodeList/*/$ns3::TrafficControlLayer/RootQueueDiscList/*/$ns3::QueueDisc/PacketsInQueue", MakeBoundCallback(&WriteQueueSizeChangeToFile, queueDiscSizeFile)); 
            Config::Connect ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Queue/PacketsInQueue", MakeBoundCallback(&WriteQueueSizeChangeToFile, wifiMacQueueSizeFile));
            Config::Connect ("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeBoundCallback (&WriteQueueSizeChangeToFile, serverL2QueueSizeFile));
        } else {
            NS_LOG_WARN ("Failed to open the queue size file, falling back to stdout.");
            Config::Connect ("/NodeList/*/$ns3::TrafficControlLayer/RootQueueDiscList/*/$ns3::QueueDisc/PacketsInQueue", MakeBoundCallback(&WriteQueueSizeChangeToFile, &std::cout));
            Config::Connect ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Queue/PacketsInQueue", MakeBoundCallback(&WriteQueueSizeChangeToFile, &std::cout));
            Config::Connect ("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/PacketsInQueue", MakeBoundCallback (&WriteQueueSizeChangeToFile, &std::cout));
        }
    }
    Config::Connect ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/BE_Txop/Queue/PacketsInQueue", MakeBoundCallback(&UpdateQueueOccupancyTracker));

    // State log trace for AP - for channel idle probability
    if (enableStateLogs && recordApPhyState)
    {
        std::stringstream nodeIndexStringTemp, phyStateStr;
        nodeIndexStringTemp << wifiApNodes.Get(0)->GetId();
        phyStateStr << "/NodeList/" << nodeIndexStringTemp.str() << "/DeviceList/*/Phy/State/State";
        Config::Connect (phyStateStr.str(), MakeCallback (&PhyStateTrace));
    }

    Simulator::Schedule (Seconds (0), &Ipv4GlobalRoutingHelper::PopulateRoutingTables);

    // If flowmon is needed
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor;
    if (enableFlowMon)
    {
        flowmon.SetMonitorAttribute("StartTime", TimeValue(metricsLoggingStartTime_s));
        flowmon.SetMonitorAttribute("DelayBinWidth", DoubleValue(0.0001)); // 100 microseconds
        if (flowMonitorPacketTimeout_s != 10) {
            flowmon.SetMonitorAttribute("MaxPerHopDelay", TimeValue(Seconds(flowMonitorPacketTimeout_s))); 
        }
        monitor = flowmon.InstallAll();
    }

    std::cout << "\n\nSimulation started**********************************************************\n\n\n";
    
    Simulator::Stop (Seconds (simulationTime));

    if (monitorQueueSizes) {
        InspectHandlers(p2pServerNode);

        // Check Server (The source)
        // p2pdevices.Get(1) is the device on the Server node
        PrintQueueInfo("AP filaire", p2pServerNode, p2pdevices.Get(0));
        PrintQueueInfo("Server Node filaire", p2pServerNode, p2pdevices.Get(1));

        // Check AP (The bottleneck)
        // apDevice.Get(0) is the Wifi device on the AP
        InspectHandlers(ApNode);
        PrintQueueInfo("AP Node (Wifi)", ApNode, apDevice.Get(0));

        // Check STAs (The receivers)
        for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i) {
            std::string staLabel = "STA Node " + std::to_string(i);
            InspectHandlers(wifiStaNodes.Get(i));
            PrintQueueInfo(staLabel, wifiStaNodes.Get(i), staDevices.Get(i));
        }

        ConfirmQueues("SERVER", p2pServerNode, p2pdevices.Get(1));
        ConfirmQueues("AP (Wifi)", ApNode, apDevice.Get(0));
        for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i) {
            std::string staLabel = "STA " + std::to_string(i);
            ConfirmQueues(staLabel, wifiStaNodes.Get(i), staDevices.Get(i));
        }
    }
    
    // Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    Simulator::Run ();

    // Prepare to open log location
    struct stat st = {0};
    if (stat(LOG_PATH, &st) == -1)
    {
        mkdir(LOG_PATH, 0700);
    }

    std::stringstream logFilename, statsLine;
    logFilename << LOG_PATH << scenario << ".log";
    std::ofstream logFile(logFilename.str().c_str());
    std::ostream* g;

    if (logFile.is_open() && !logFile.fail()) {
        g = &logFile;
    } else {
        NS_ABORT_MSG ("Failed to open the log file, falling back to stdout.");
        g = &std::cout;
    }

    statsLine << "seed="<< randSeed;
    statsLine << ";videoTrafficDistribution="<< videoTrafficDistribution;
    if (videoTrafficDistribution == "weibull") {
        statsLine << ";weibullScale="<< weibullScale;
        statsLine << ";weibullShape="<< weibullShape;
        statsLine << ";videoFrameInterval_s="<< videoFrameInterval_s;
    } else if (videoTrafficDistribution == "poisson") {
        statsLine << ";poissonArrivalRate="<< poissonArrivalRate;
    } else if (videoTrafficDistribution == "custom_trace") {
        statsLine << ";traceFilename="<< traceFilename;
    }
    statsLine << ";t_sp="<< t_sp;
    statsLine << ";t_rp="<< t_rp;
    statsLine << ";wifiMacQueueSize="<< wifiMacQueueSize;
    statsLine << ";nStations="<< nStations;
    statsLine << ";maxMpdusAp="<< maxMpdusAp;
    statsLine << ";maxMpdusSta="<< maxMpdusSta;
    statsLine << ";ulTrafficRate="<< ulTrafficRate;
    statsLine << ";simulationTime="<< simulationTime;
    statsLine << ";meanQueueOccupancy="<< apQueueTracker.GetMeanOccupancy();

    double *avgCurrent_mAForSTA = new double [nStations];
    double *totEnergyConsumedForSta_J = new double [nStations];

    std::map<Mac48Address, double> staMacToCurrent_mA;
    std::map<Mac48Address, double> staMacToTimeElapsed_us;
    std::map<Mac48Address, double> staMacToTimeAwake_us;
    std::map<Mac48Address, double> staMacToTimeAsleep_us;
    std::map<Mac48Address, double> staMacToEnergyConsumed_J;
    std::map<Mac48Address, double> staMacToUplinkTimeoutsThisSta;
    std::map<Mac48Address, double> staMacToDownlinkTimeoutsAllSta;
    std::map<Mac48Address, double> staMacToUplinkMpduExpiredThisSta;
    std::map<Mac48Address, double> staMacToDownlinkMpduExpiredAllSta;
    std::map<Mac48Address, double> staMacToUplinkMpduFailedEnqueueThisSta;
    std::map<Mac48Address, double> staMacToDownlinkMpduFailedEnqueueAllSta;

    // Create a map from STA mac address to the pair - uplink and downlink
    std::map<Mac48Address, std::pair<double, double>> staMacToTotalBitsUplinkDownlink;
    std::map<Mac48Address, std::pair<double, double>> staMacToUplinkDownlinkThroughput_kbps;
    std::map<Mac48Address, std::pair<double, double>> staMacToUplinkDownlinkLatency_usPerPkt;
    std::map<Mac48Address, std::pair<double, double>> staMacToUplinkDownlink90Latency_us; // 90th percentile
    std::map<Mac48Address, std::pair<double, double>> staMacToUplinkDownlink95Latency_us; // 95th percentile
    std::map<Mac48Address, std::pair<double, double>> staMacToUplinkDownlink99Latency_us; // 99th percentile

    // Initialize for all existing STAs with <0,0>
    for (uint32_t i = 0; i < wifiStaNodes.GetN(); i++)
    {
        Ptr<WifiNetDevice> wifi_dev = DynamicCast<WifiNetDevice> (wifiStaNodes.Get (i)->GetDevice (0)); // assuming only one device
        Ptr<WifiMac> wifi_mac = wifi_dev->GetMac ();
        Ptr<StaWifiMac> sta_mac = DynamicCast<StaWifiMac> (wifi_mac);
        staMacToTotalBitsUplinkDownlink[sta_mac->GetAddress ()] = std::make_pair (0, 0);
        staMacToUplinkDownlinkThroughput_kbps[sta_mac->GetAddress ()] = std::make_pair (0, 0);
        staMacToUplinkDownlinkLatency_usPerPkt[sta_mac->GetAddress ()] = std::make_pair (0, 0);
        staMacToUplinkDownlink90Latency_us[sta_mac->GetAddress ()] = std::make_pair (0, 0);
        staMacToUplinkDownlink95Latency_us[sta_mac->GetAddress ()] = std::make_pair (0, 0);
        staMacToUplinkDownlink99Latency_us[sta_mac->GetAddress ()] = std::make_pair (0, 0);
    }

    // In place current calculation
    for (std::size_t i = 0; i < nStations; i++)
    {
        Ptr<WifiNetDevice> wifi_dev = DynamicCast<WifiNetDevice> (wifiStaNodes.Get (i)->GetDevice (0)); // assuming only one device
        Ptr<WifiMac> wifi_mac = wifi_dev->GetMac ();
        Ptr<StaWifiMac> sta_mac = DynamicCast<StaWifiMac> (wifi_mac);
        Mac48Address currentMacAddress = sta_mac->GetAddress ();
        
        avgCurrent_mAForSTA[i] = current_mA_TimesTime_ns_ForSta_TI[i]/totalTimeElapsedForSta_ns[i];
        totEnergyConsumedForSta_J [i] = (avgCurrent_mAForSTA[i]/1000.0) * (totalTimeElapsedForSta_ns[i]/1e9) * batteryVoltage;
        staMacToEnergyConsumed_J[currentMacAddress] = totEnergyConsumedForSta_J [i];
        staMacToCurrent_mA[currentMacAddress] = avgCurrent_mAForSTA[i];
        staMacToTimeElapsed_us[currentMacAddress] = totalTimeElapsedForSta_ns[i]/1000.0;
        staMacToTimeAwake_us[currentMacAddress] = awakeTimeElapsedForSta_ns[i]/1000.0;
        staMacToTimeAsleep_us[currentMacAddress] = sleepTimeElapsedForSta_ns[i]/1000.0;

        staMacToUplinkTimeoutsThisSta[currentMacAddress] = uplinkTimeoutsForSta[i];
        staMacToDownlinkTimeoutsAllSta[currentMacAddress] = downlinkTimeoutsAllSta;
        staMacToUplinkMpduExpiredThisSta[currentMacAddress] = uplinkExpiredMpduForSta[i];
        staMacToDownlinkMpduExpiredAllSta[currentMacAddress] = downlinkExpiredMpduAllSta;
        staMacToUplinkMpduFailedEnqueueThisSta[currentMacAddress] = uplinkFailedEnqueueMpduForSta[i];
        staMacToDownlinkMpduFailedEnqueueAllSta[currentMacAddress] = downlinkFailedEnqueueMpduAllSta;

        *g
        <<"For STA "<< i <<";MacAddress="<<currentMacAddress
        <<"; Elapsed time (ms) = "<<totalTimeElapsedForSta_ns[i]/1e6
        <<"; Awake time (ms) = "<<awakeTimeElapsedForSta_ns[i]/1e6
        <<"; Average current (mA) = "<<avgCurrent_mAForSTA[i]
        <<"; Total energy consumed (J) = "<<staMacToEnergyConsumed_J[currentMacAddress]
        <<"; Uplink timeouts = "<<uplinkTimeoutsForSta[i]
        <<"; Downlink timeouts for all STAs = "<<downlinkTimeoutsAllSta
        <<"; Uplink Expired MPDUs for this STA = "<<uplinkExpiredMpduForSta[i]
        <<"; Downlink Expired MPDUs for all STA = "<<downlinkExpiredMpduAllSta
        <<"; Uplink Failed Enqueue MPDUs for this STA = "<<uplinkFailedEnqueueMpduForSta[i]
        <<"; Downlink Failed Enqueue MPDUs for all STA = "<<downlinkFailedEnqueueMpduAllSta
        <<";\n";

        // statsLine << ";sta_"<<i+1<<"_mac="<< currentMacAddress;
        statsLine << ";sta_"<<i+1<<"_average_current_mA="<< avgCurrent_mAForSTA[i];
        // statsLine << ";total_energy_J="<< staMacToEnergyConsumed_J[currentMacAddress];
        // statsLine << ";time_elapsed_ns="<< totalTimeElapsedForSta_ns[i];
        // statsLine << ";time_awake_ns="<< awakeTimeElapsedForSta_ns[i];
        // statsLine << ";time_tx_ns="<< txTimeElapsedForSta_ns[i];
        // statsLine << ";time_rx_ns="<< rxTimeElapsedForSta_ns[i];
        // statsLine << ";time_idle_ns="<< idleTimeElapsedForSta_ns[i];
        // statsLine << ";time_cca_busy_ns="<< ccaBusyTimeElapsedForSta_ns[i];
        // statsLine << ";time_sleep_ns="<< sleepTimeElapsedForSta_ns[i];
    }

    monitor->CheckForLostPackets ();

    int staWithZeroThroughput = 0;
    if (enableFlowMon)
    {
        *g <<"\n-----------------\n";
        *g <<"Flow Level Stats:\n";
        *g <<"-----------------\n\n";

        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
        std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

        for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
        {
            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);

            double totalBitsRx = i->second.rxBytes * 8.0;
            // double throughputKbps =  i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds())/1000;
            double throughputKbps =  i->second.rxBytes * 8.0 / (simulationTime - metricsLoggingStartTime_s.GetSeconds()) /1000;
            double avgDelayMicroSPerPkt = i->second.delaySum.GetMicroSeconds()/i->second.rxPackets ;
            u_int32_t lostPackets = i->second.lostPackets;
            Histogram delayHist = i->second.delayHistogram;
            double latency90 = GetPercentileValue (delayHist, 90);
            double latency95 = GetPercentileValue (delayHist, 95);
            double latency99 = GetPercentileValue (delayHist, 99);

            *g << std::setw(30)<<std::left << "Flow ID" <<":\t"<<i->first<<"\n";
            *g << std::setw(30)<<std::left << "Source IP and Port" <<":\t"<<t.sourceAddress<<" , "<<t.sourcePort<<"\n";
            std::string trafficLabel;
            if (t.sourceAddress == "192.168.1.2") 
            {
                trafficLabel = "_1UL";
            } else if (t.destinationAddress == "192.168.1.2")
            {
                trafficLabel = "_1DL";
            } else if (t.destinationAddress == "192.168.1.3")
            {
                trafficLabel = "_2DL";
            } else if (t.destinationAddress == "192.168.1.4")
            {
                trafficLabel = "_3DL";
            } else if (t.destinationAddress == "192.168.1.5")
            {
                trafficLabel = "_4DL";
            } else if (t.destinationAddress == "192.168.1.6")
            {
                trafficLabel = "_5DL";
            }
            
            // if sourceAddress is found in ipToMac map, then print the corresponding MAC address
            if (ipToMac.find(t.sourceAddress) != ipToMac.end())
            {
                *g << std::setw(30)<<std::left << "Source STA MAC" <<":\t"<<ipToMac[t.sourceAddress]<<"\n";
                staMacToTotalBitsUplinkDownlink[ipToMac[t.sourceAddress]].first += totalBitsRx;
                staMacToUplinkDownlinkThroughput_kbps[ipToMac[t.sourceAddress]].first = throughputKbps;
                staMacToUplinkDownlinkLatency_usPerPkt[ipToMac[t.sourceAddress]].first = avgDelayMicroSPerPkt;
                staMacToUplinkDownlink90Latency_us[ipToMac[t.sourceAddress]].first = latency90 * 1e6;
                staMacToUplinkDownlink95Latency_us[ipToMac[t.sourceAddress]].first = latency95 * 1e6;
                staMacToUplinkDownlink99Latency_us[ipToMac[t.sourceAddress]].first = latency99 * 1e6;
            }

            *g << std::setw(30)<<std::left << "Destination IP and Port" <<":\t"<<t.destinationAddress<<" , "<<t.destinationPort<<"\n";
            if (ipToMac.find(t.destinationAddress) != ipToMac.end())
            {
                *g << std::setw(30)<<std::left << "Destination STA MAC" <<":\t"<<ipToMac[t.destinationAddress]<<"\n";
                staMacToTotalBitsUplinkDownlink[ipToMac[t.destinationAddress]].second += totalBitsRx;
                staMacToUplinkDownlinkThroughput_kbps[ipToMac[t.destinationAddress]].second = throughputKbps;
                staMacToUplinkDownlinkLatency_usPerPkt[ipToMac[t.destinationAddress]].second = avgDelayMicroSPerPkt;
                staMacToUplinkDownlink90Latency_us[ipToMac[t.destinationAddress]].second = latency90 * 1e6;
                staMacToUplinkDownlink95Latency_us[ipToMac[t.destinationAddress]].second = latency95 * 1e6;
                staMacToUplinkDownlink99Latency_us[ipToMac[t.destinationAddress]].second = latency99 * 1e6;
            }
            if (throughputKbps == 0)
            {
                staWithZeroThroughput++;
            }
            *g << std::setw(30)<<std::left << "Throughput (kbps)" <<":\t"<<throughputKbps<<"\n";
            *g << std::setw(30)<<std::left << "Total bits received" <<":\t"<<totalBitsRx<<"\n";
            *g << std::setw(30)<<std::left << "Avg. Delay (us/pkt)" <<":\t"<< avgDelayMicroSPerPkt << " us/pkt\n";
            *g << std::setw(30)<<std::left << "Latency 90th percentile (us)" <<":\t"<< latency90 * 1e6 << " us\n";
            *g << std::setw(30)<<std::left << "Latency 95th percentile (us)" <<":\t"<< latency95 * 1e6 << " us\n";
            *g << std::setw(30)<<std::left << "Latency 99th percentile (us)" <<":\t"<< latency99 * 1e6 << " us\n";
            *g << std::setw(30)<<std::left << "Lost Packets" <<":\t"<< lostPackets << " pkts\n";
            *g << std::setw(30)<<std::left << "Tx Packets" <<":\t"<< i->second.txPackets << " pkts\n";
            for (uint32_t reasonCode = 0; reasonCode < i->second.packetsDropped.size(); reasonCode++)
            {
                *g << "packetsDropped reasonCode=\"" << reasonCode << "\""
                << " number=\"" << i->second.packetsDropped[reasonCode] << "\"\n";
            }
            for (uint32_t reasonCode = 0; reasonCode < i->second.bytesDropped.size(); reasonCode++)
            {
                *g << "bytesDropped reasonCode=\"" << reasonCode << "\""
                << " bytes=\"" << i->second.bytesDropped[reasonCode] << "\"\n";
            }
            *g <<"-----------------\n\n";

            statsLine << ";lossPercentage"<<trafficLabel<<"="<< std::fixed << std::setprecision(6) << (double)lostPackets / i->second.txPackets * 100;
            statsLine << ";lostPackets"<<trafficLabel<<"="<< lostPackets;
            statsLine << ";txPackets"<<trafficLabel<<"="<< i->second.txPackets;
            statsLine << ";rxPackets"<<trafficLabel<<"="<< i->second.rxPackets;
            statsLine << ";expired"<<trafficLabel<<"="<< downlinkExpiredMpduAllSta;
            statsLine << ";dropQueueDisc"<<trafficLabel<<"="<< (i->second.packetsDropped.size() > 4 ? i->second.packetsDropped[4] : 0);
            statsLine << ";throughput_mbps"<<trafficLabel<<"="<< throughputKbps / 1000.0;

            double latency_min, latency_max, latency_median, latency_mean, latency_q1, latency_q3;
            double latency_sum = i->second.delaySum.GetSeconds();
            double latency_count = i->second.rxPackets;
            latency_mean = (latency_count > 0) ? (latency_sum / latency_count) : 0;

            statsLine << ";latency_mean"<<trafficLabel<<"="<< latency_mean * 1e3;
            
            uint32_t nBins = i->second.delayHistogram.GetNBins();

            // Find Min and Max
            bool foundMin = false;
            for (uint32_t j = 0; j < nBins; ++j) {
                if (i->second.delayHistogram.GetBinCount(j) > 0) {
                    if (!foundMin) {
                        latency_min = i->second.delayHistogram.GetBinStart(j);
                        foundMin = true;
                    }
                    latency_max = i->second.delayHistogram.GetBinEnd(j);
                }
            }

            // Find Quartiles via Linear Interpolation
            std::vector<double> targets = {0.25 * latency_count, 0.50 * latency_count, 0.75 * latency_count};
            std::vector<double*> results = {&latency_q1, &latency_median, &latency_q3};
            for (int j = 0; j < 3; ++j) {
                double targetRank = targets[j];
                double cumulative = 0;
                
                for (uint32_t index = 0; index < nBins; ++index) {
                    uint32_t count = i->second.delayHistogram.GetBinCount(index);
                    if (cumulative + count >= targetRank) {
                        double L = i->second.delayHistogram.GetBinStart(index);
                        double W = i->second.delayHistogram.GetBinWidth(index);
                        // Q = L + ((Rank - CumulativeBefore) / CountInBin) * Width
                        *results[j] = L + ((targetRank - cumulative) / (double)count) * W;
                        break;
                    }
                    cumulative += count;
                }
            }

            // statsLine << ";latency_min="<< latency_min * 1e3;
            // statsLine << ";latency_max="<< latency_max * 1e3;
            // statsLine << ";latency_median="<< latency_median * 1e3;
            // statsLine << ";latency_q1="<< latency_q1 * 1e3;
            // statsLine << ";latency_q3="<< latency_q3 * 1e3;
            // statsLine << ";latency_ms_90th="<< latency90 * 1e3;
            // statsLine << ";latency_ms_95th="<< latency95 * 1e3;
            // statsLine << ";latency_ms_99th="<< latency99 * 1e3;
        }
    }

    for (auto it = staMacToTotalBitsUplinkDownlink.begin (); it != staMacToTotalBitsUplinkDownlink.end (); it++)
    {
        double totalBits = it->second.first  + it->second.second ;
        *g
        <<"scenario="<<scenario
        <<";nSTA="<<nStations
        <<";simulationTime="<<simulationTime
        <<";randSeed="<<+randSeed
        <<";StaMacAddress="<< it->first
        <<";UL_bits=" << it->second.first
        <<";DL_bits=" << it->second.second
        <<";total_bits=" << totalBits
        <<";energy_J="<< staMacToEnergyConsumed_J[it->first]
        <<";energyPerTotBit_JPerBit="<< staMacToEnergyConsumed_J[it->first]/totalBits
        <<";current_mA="<<staMacToCurrent_mA[it->first]
        <<";TimeElapsedForThisSTA_us="<<staMacToTimeElapsed_us[it->first]
        <<";TimeAwakeForThisSTA_us="<<staMacToTimeAwake_us[it->first]
        <<";TimeAsleepForThisSTA_us="<<staMacToTimeAsleep_us[it->first]
        <<";UL_throughput_kbps="<<staMacToUplinkDownlinkThroughput_kbps[it->first].first
        <<";DL_throughput_kbps="<<staMacToUplinkDownlinkThroughput_kbps[it->first].second
        <<";UL_latency_usPerPkt="<<staMacToUplinkDownlinkLatency_usPerPkt[it->first].first
        <<";DL_latency_usPerPkt="<<staMacToUplinkDownlinkLatency_usPerPkt[it->first].second
        <<";UL_latency_90th_us="<<staMacToUplinkDownlink90Latency_us[it->first].first
        <<";DL_latency_90th_us="<<staMacToUplinkDownlink90Latency_us[it->first].second
        <<";UL_latency_95th_us="<<staMacToUplinkDownlink95Latency_us[it->first].first
        <<";DL_latency_95th_us="<<staMacToUplinkDownlink95Latency_us[it->first].second
        <<";UL_latency_99th_us="<<staMacToUplinkDownlink99Latency_us[it->first].first
        <<";DL_latency_99th_us="<<staMacToUplinkDownlink99Latency_us[it->first].second
        <<";UplinkRetriesThisSta="<<staMacToUplinkTimeoutsThisSta[it->first]
        <<";DownlinkRetriesAllSta="<<staMacToDownlinkTimeoutsAllSta[it->first]
        <<";UplinkExpiredMpduThisSta="<<staMacToUplinkMpduExpiredThisSta[it->first]
        <<";DownlinkExpiredMpduAllSta="<<staMacToDownlinkMpduExpiredAllSta[it->first]
        <<";UplinkFailedEnqueueMpduForSta="<<staMacToUplinkMpduFailedEnqueueThisSta[it->first]
        <<";DownlinkFailedEnqueueMpduAllSta="<<staMacToDownlinkMpduFailedEnqueueAllSta[it->first]
        <<";staWithZeroThroughput="<<staWithZeroThroughput
        <<'\n';

        statsLine 
        << ";energyPerTotBit_JPerBit=" << staMacToEnergyConsumed_J[it->first]/totalBits
        << ";UL_bytes=" << it->second.first / 8.0
        << ";DL_bytes=" << it->second.second / 8.0
        << ";total_bytes=" << totalBits / 8.0;
    }

    std::fstream statsFile(statsFilename, std::ios::app | std::ios::out);
    if (!statsFile.is_open() || statsFile.fail())
    {
        NS_ABORT_MSG ("Failed to open statsFilename: "<<statsFilename);
    }
    else {
        statsFile << statsLine.str().c_str() << "\n";
    }

    Simulator::Destroy ();

    std::cout<<std::setw(30) << std::right << "STAs With Zero Throughput" << ":\t" << staWithZeroThroughput << std::endl;
    std::cout<<std::setw(30) << std::right << "scenario" << ":\t" << scenario << std::endl;
    std::cout<<std::setw(30) << std::right << "randSeed" << ":\t" << randSeed << std::endl;
    std::cout<<std::setw(30) << std::right << "#STAs" << ":\t" << nStations << std::endl;
    std::cout<<std::setw(30) << std::right << "simulationTime" << ":\t" << simulationTime << std::endl;

    std::cout<<"\n\nSimulation completed**********************************************************";
    std::cout<<"\n\n";

    if (g == &logFile) {
        if ((*g).fail())
        {
            NS_ABORT_MSG ("Failed to write to the log file");
        }
        logFile.close();
    }

    return 0;
}
