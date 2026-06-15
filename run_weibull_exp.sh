#!/bin/bash
weibullScale=43247.57 # ~ 1000 pkts/s
ulRate=100 # packets/s
stats_filename=stats_weibull.txt
for wifiMacQueueSize in 5000 100
do
    for i in {1..15}
    do
        SEED=$((1000 + RANDOM % 9000))
        time ./ns3 run "videoTest --randSeed=${SEED} --flowMonitorPacketTimeout_s=1000 --wifiMacQueueDelay_ms=1000000 --wifiMacQueueSize=${wifiMacQueueSize}p --videoTrafficDistribution=weibull --weibullScale=${weibullScale} --enableTwt=false --maxMpdusAp=0 --maxMpdusSta=0 --nStations=5 --ulTrafficRate=${ulRate} --simulationTime=100 --statsFilename=${stats_filename} --scenario=weibull" 
    done
    for i in {1..15}
    do
        SEED=$((1000 + RANDOM % 9000))
        time ./ns3 run "videoTest --randSeed=${SEED} --flowMonitorPacketTimeout_s=1000 --wifiMacQueueDelay_ms=1000000 --wifiMacQueueSize=${wifiMacQueueSize}p --videoTrafficDistribution=weibull --weibullScale=${weibullScale} --t_sp=98.8 --t_rp=3.6 --maxMpdusAp=0 --maxMpdusSta=0 --nStations=5 --ulTrafficRate=${ulRate} --simulationTime=100 --statsFilename=${stats_filename} --scenario=weibull" 
    done
    for t_rp in $(seq 10 10 100) 
        do
        t_sp=$(echo "102.4 - $t_rp" | bc)
        for i in {1..15}
        do
        SEED=$((1000 + RANDOM % 9000))
            time ./ns3 run "videoTest --randSeed=${SEED} --flowMonitorPacketTimeout_s=1000 --wifiMacQueueDelay_ms=1000000 --wifiMacQueueSize=${wifiMacQueueSize}p --videoTrafficDistribution=weibull --weibullScale=${weibullScale} --t_sp=${t_sp} --t_rp=${t_rp} --maxMpdusAp=0 --maxMpdusSta=0 --nStations=5 --ulTrafficRate=${ulRate} --simulationTime=100 --statsFilename=${stats_filename} --scenario=weibull" 
        done
    done
done
