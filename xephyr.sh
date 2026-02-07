#!/bin/sh

XEPHYR_TEST_COMMAND="valgrind --leak-check=full --track-origins=yes"
# XEPHYR_TEST_COMMAND="heaptrack"
# XEPHYR_TEST_COMMAND="perf record"

make clean all

Xephyr +xinerama -br -ac -noreset -screen 1920x1080 :1 &
XEPHYR_PID=$!
sleep 1
DISPLAY=:1 alacritty --hold -e $XEPHYR_TEST_COMMAND ./dwm -c ./dwm.conf
kill $XEPHYR_PID
