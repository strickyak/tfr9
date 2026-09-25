TS=$(date +%Y-%m-%d.%H%M%S)
( set -x
  ./build/annealing.exe \
      --initial="MHZ=250,K1=8,K2=18,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12" \
      --max-trials=1000 --temp=1.9 --cooling=0.99 \
      --distance=5  --neighbor=3 \
      --log=data/sequence-$TS.csv ) 2>&1 | tee data/sequence-$TS.log

#./build/annealing.exe \
#      --initial="MHZ=250,K1=8,K2=18,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12" \
#      --max-trials=100 --temp=0.1 --cooling=0.99 \
#      --log=data/sequence-$(date +%Y-%m-%d.%H%M%S).csv

# ./build/annealing.exe \
#       --initial="MHZ=250,K1=8,K2=18,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12" \
#       --max-trials=100 --temp=1.5 --cooling=0.96 --log=annealing_resume.csv
