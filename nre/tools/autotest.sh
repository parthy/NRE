#!/bin/bash
QEMU=qemu-system-x86_64
QEMU_FLAGS="-display none"
timeout=120

builds="debug release"
targets="x86_32 x86_64"
compilers="gcc clang"

usage() {
    echo "Usage: $0 (run|check-all) [-b <builds>] [-t <targets] [-c <compilers>]" 1>&2
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

if [ -f /proc/cpuinfo ]; then
    parallel=`cat /proc/cpuinfo | grep '^processor[[:space:]]*:' | wc -l`
else
    parallel=1
fi

cmd=$1
shift

while getopts b:t:c o
do
    case "$o" in
        b) builds="$OPTARG" ;;
        t) targets="$OPTARG" ;;
        c) compilers="$OPTARG" ;;
        [?]) usage ;;
    esac
done

wait_for() {
    run=true
    while $run; do
        finfo=`stat $1 | grep 'Size:'`
        waittime=0
        # wait until either the run was successfull or the log file didn't change for $timeout seconds
        while [ $waittime -lt $timeout ]; do
            sleep 1
            waittime=$((waittime + 1))
            if [ "`check_result $1`" = "" ]; then
                run=false
                waittime=$timeout
            fi
        done
        if [ "$finfo" = "`stat $1 | grep 'Size:'`" ]; then
            run=false
        fi
    done
}

check_result() {
    test=`echo $1 | sed -e 's/.*-\(.*\)\.txt$/\1/'`
    if [ "$test" = "test" ]; then
        chk1=`grep "bin/apps/test': Pd terminated with exit code 0" $1`
        chk2=`grep "subtest': Pd terminated with exit code 0" $1`
        chk3=`grep "bin/apps/sub': Pd terminated with exit code 0" $1`
        if [ "$chk1" = "" ] || [ "$chk2" = "" ] || [ "$chk3" = "" ]; then
            echo $1: FAILED
        fi
    elif [ "$test" = "unittests" ]; then
        chk1=`grep "Total failures: 0" $1`
        if [ "$chk1" = "" ]; then
            echo $1: FAILED
        fi
    elif [ "$test" = "disktest_nocheck" ]; then
        chk1=`grep FAILED $1`
        chk2=`grep "bin/apps/disktest no-check': Pd terminated with exit code 0" $1`
        if [ "$chk1" != "" ] || [ "$chk2" = "" ]; then
            echo $1: FAILED
        fi
    else
        chk1=`grep 'Welcome to Escape v0.4, vterm' $1 | wc -l`
        if [ "$chk1" != "6" ]; then
            echo $1: FAILED
        fi
    fi
}

run_in_qemu() {
    echo "Executing boot/$1 in qemu..."
    logfile="build/logs/`date --iso-8601=seconds`-$NRE_TARGET-$NRE_BUILD-$NRE_CC-qemu-$1.txt"
    touch $logfile
    ./boot/$1 --qemu="$QEMU" --qemu-append="$QEMU_FLAGS" \
        --build-dir="$PWD/build/$NRE_TARGET-$NRE_BUILD" --strip-rom 2>/dev/null > $logfile &
    pid=$!
    # notify caller that we started the simulator and thus don't need the build-files anymore
    kill -USR1 $$
    wait_for $logfile
    kill $pid
    check_result $logfile
}

run_in_bochs() {
    echo "Executing boot/$1 in bochs..."
    logfile="build/logs/`date --iso-8601=seconds`-$NRE_TARGET-$NRE_BUILD-$NRE_CC-bochs-$1.txt"
    build=build/$NRE_TARGET-$NRE_BUILD

    mkdir -p $build/bin/boot/grub
    bochstmp=$(mktemp)
    cp bochs.cfg $bochstmp
    cp dist/iso/boot/grub/stage2_eltorito $build/bin/boot/grub
    ./tools/novaboot --build-dir="$PWD/$build" --iso --strip-rom -- boot/$1 >/dev/null 2>/dev/null
    # replace the build path for all ata drives
    path=`echo $build/ | sed -e 's/\//\\\\\//g'`
    sed --in-place -e 's/\(ata.*\?:.*\?path\)=build\/[^\/]*\?\/\(.*\?\),/\1='$path'\2,/g' $bochstmp
    # put the generated iso into ata0-master
    path=`echo $build/$1.iso | sed -e 's/\//\\\\\//g'`
    sed --in-place -e 's/^\(ata0-master:.*\?path\)=\(.*\?\),/\1='$path',/' $bochstmp
    bochslogfile=`echo $logfile | sed -e 's/\//\\\\\//g'`
    sed --in-place -e 's/^com1: \(.*\) dev="log.txt"/com1: \1 dev="'$bochslogfile'"/' $bochstmp
    sed --in-place -e 's/^display_library: x/display_library: nogui/' $bochstmp
    # somehow bochs doesn't like that with nogui
    sed --in-place -e 's/^keyboard:/#keyboard:/' $bochstmp

    touch $logfile
    echo "c" | bochs -f $bochstmp -q >/dev/null 2>/dev/null &
    pid=$!
    # notify caller that we started the simulator and thus don't need the build-files anymore
    kill -USR1 $$
    wait_for $logfile
    kill $pid
    rm $bochstmp
    check_result $logfile
}

sigusr1() {
    is_running=1
}

submit_job() {
    # wait until there are free slots
    while [ `jobs -p | wc -l` -ge $parallel ]; do
        sleep 1 || kill -INT $$
    done

    # start job
    is_running=0
    ( $1 $2 ) &

    # wait until it's started
    while [ $is_running -eq 0 ]; do
        sleep 1 || kill -INT $$
    done
}

test_config() {
    echo "Building..."
    ./b >/dev/null 2>/dev/null || kill -INT $$

    submit_job run_in_qemu unittests
    submit_job run_in_qemu test
    submit_job run_in_qemu disktest_nocheck
    submit_job run_in_qemu escape
    submit_job run_in_bochs unittests
    submit_job run_in_bochs test
    submit_job run_in_bochs disktest_nocheck
}

sigint() {
    echo "Got ^C. Terminating running jobs (`jobs -p | wc -l`)..."
    # kill the whole process group to kill also the childs in an easy and reliable way.
    for pid in `jobs -p`; do
        kill -INT -$pid
    done
    exit 1
}

# enable job control
set -m
# let the spawned jobs signal us
trap sigusr1 USR1
trap sigint INT

if [ "$cmd" = "check-all" ]; then
    for f in build/logs/*; do
        check_result $f
    done
elif [ "$cmd" = "run" ]; then
    mkdir -p build/logs/
    for t in $targets; do
        for b in $builds; do
            for c in $compilers; do
                echo "== target=$t build=$b compiler=$c =="
                export NRE_TARGET=$t NRE_BUILD=$b NRE_CC=$c
                test_config
                echo -e "== done ==\n"
            done
        done
    done
    # wait until all remaining jobs are done
    wait
fi
