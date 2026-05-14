#!/bin/bash
# run_experiments_one_mode.sh – прогон экспериментов с таймаутом (4 мин)

DRY_RUN=0
if [[ "$1" == "--dry-run" ]]; then
    DRY_RUN=1
    echo "Режим: только вывод команд (dry-run)"
fi

TIMEOUT_SEC=600          # 4 минуты
BURST_RATES="0 0.001 0.005 0.01 0.02"
NUM_PATHS="3"
SEEDS="1 2 3 4 5"
SIM_TIME=30              # время симуляции 30 секунд

for N in $NUM_PATHS; do
    for ((bad=0; bad<1; bad++)); do
        for rate in $BURST_RATES; do
            for seed in $SEEDS; do
                CMD="./ns3 run \"my-experiment --numPaths=$N --numBadPaths=$bad --lossRate=$rate --seed=$seed --simulationTime=$SIM_TIME\""
                if [ $DRY_RUN -eq 1 ]; then
                    echo "timeout $TIMEOUT_SEC $CMD"
                else
                    echo "Запуск: N=$N bad=$bad burstRate=$rate seed=$seed"
                    timeout $TIMEOUT_SEC bash -c "$CMD" || {
                        rc=$?
                        if [ $rc -eq 124 ]; then
                            echo "  !! Таймаут ($TIMEOUT_SEC сек) – переход к следующему."
                        else
                            echo "  !! Завершилось с ошибкой (код $rc)"
                        fi
                    }
                fi
            done
        done
    done
done

echo "Готово."