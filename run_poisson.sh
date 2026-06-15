#!/bin/bash
arrivalRate=1000 # packets/s
ulRate=100 # packets/s
stats_filename=stats_poisson.txt
for wifiMacQueueSize in 5000 100
do
    for i in {1..15}
    do
        SEED=$((1000 + RANDOM % 9000))
        time ./ns3 run "videoTest --randSeed=${SEED} --flowMonitorPacketTimeout_s=1000 --wifiMacQueueDelay_ms=1000000 --wifiMacQueueSize=${wifiMacQueueSize}p --videoTrafficDistribution=poisson --poissonArrivalRate=${arrivalRate} --enableTwt=false --maxMpdusAp=0 --maxMpdusSta=0 --nStations=5 --ulTrafficRate=${ulRate} --simulationTime=100 --statsFilename=${stats_filename} --scenario=poisson" 
    done
    for i in {1..15}
    do
        SEED=$((1000 + RANDOM % 9000))
        time ./ns3 run "videoTest --randSeed=${SEED} --flowMonitorPacketTimeout_s=1000 --wifiMacQueueDelay_ms=1000000 --wifiMacQueueSize=${wifiMacQueueSize}p --videoTrafficDistribution=poisson --poissonArrivalRate=${arrivalRate} --t_sp=98.8 --t_rp=3.6 --maxMpdusAp=0 --maxMpdusSta=0 --nStations=5 --ulTrafficRate=${ulRate} --simulationTime=100 --statsFilename=${stats_filename} --scenario=poisson" 
    done
    for t_rp in $(seq 10 10 100) 
        do
        t_sp=$(echo "102.4 - $t_rp" | bc)
        for i in {1..15}
        do
        SEED=$((1000 + RANDOM % 9000))
            time ./ns3 run "videoTest --randSeed=${SEED} --flowMonitorPacketTimeout_s=1000 --wifiMacQueueDelay_ms=1000000 --wifiMacQueueSize=${wifiMacQueueSize}p --videoTrafficDistribution=poisson --poissonArrivalRate=${arrivalRate} --t_sp=${t_sp} --t_rp=${t_rp} --maxMpdusAp=0 --maxMpdusSta=0 --nStations=5 --ulTrafficRate=${ulRate} --simulationTime=100 --statsFilename=${stats_filename} --scenario=poisson" 
        done
    done
done
