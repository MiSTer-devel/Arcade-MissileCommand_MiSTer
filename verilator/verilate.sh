export OPTIMIZE="--x-assign fast --x-initial fast --noassert"
export WARNINGS="-Wno-fatal"
verilator \
-cc --compiler msvc +define+SIMULATION=1 $WARNINGS $OPTIMIZE \
--top-module emu sim.v \
-I../rtl \
-I../rtl/pokey \
-I../rtl/bc6502 \
