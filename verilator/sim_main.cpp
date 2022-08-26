#include <iostream>
#include <verilated.h>
#include "Vemu.h"

#include "imgui.h"
#ifndef _MSC_VER
#include <stdio.h>
#include <SDL.h>
#include <SDL_opengl.h>
#else
#define WIN32
#include <dinput.h>
#endif

#define FMT_HEADER_ONLY
#include <fmt/core.h>

#include <sim_console.h>
#include <sim_bus.h>
#include <sim_video.h>
#include <sim_input.h>
#include <sim_clock.h>

#include "../imgui/imgui_memory_editor.h"
#include <fstream>
#include "stdio.h"



// Debug GUI 
// ---------
const char* windowTitle = "Verilator Sim: Arcade-MissileCommand";
bool showDebugWindow = true;
const char* debugWindowTitle = "Virtual Dev Board v1.0";

DebugConsole console;

MemoryEditor memoryEditor_hs;

// MiSTer framework emulation
// ------------
SimBus bus(console);

// Input handling
// --------------
SimInput input(12);
const int input_right = 0;
const int input_left = 1;
const int input_down = 2;
const int input_up = 3;
const int input_fire1 = 4;
const int input_fire2 = 5;
const int input_fire3 = 6;
const int input_coin = 7;
const int input_startp1 = 8;
const int input_startp2 = 9;
const int input_slam = 10;

// Video
// -----
#define VGA_WIDTH 258
#define VGA_HEIGHT 256
#define VGA_ROTATE 0
SimVideo video(VGA_WIDTH, VGA_HEIGHT, VGA_ROTATE);

// Simulation control
// ------------------
int initialReset = 32;
int resetHoldTimer;
bool run_enable = 1;
int batchSize = 150000;
bool single_step = 0;
bool stop_on_log_mismatch = 1;
bool multi_step = 0;
int multi_step_amount = 74000;

bool debug_6502 = 1;
bool debug_cpu = 0;
bool debug_data = 0;
bool debug_enable = 0;

bool self_test = 0;
int dip_language = 0;
int dip_coinage = 0;

int mouse_speed = 2;
int joystick_sensitivity = 0;
bool pause;
bool flip;

unsigned char mouse_clock = 0;
unsigned char mouse_buttons = 0;
signed short mouse_x = 0;
signed short mouse_y = 0;


// Verilog module
// --------------v
Vemu* top = NULL;

vluint64_t main_time = 0;	// Current simulation time.
double sc_time_stamp() {	// Called by $time in Verilog.
	return main_time;
}

int clockSpeed = 10; // This is not used, just a reminder for the dividers below
SimClock clk_sys(1); // 10mhz
SimClock clk_pix(2); // 5mhz

void resetSim() {
	main_time = 0;
	resetHoldTimer = initialReset;
	clk_sys.Reset();
	clk_pix.Reset();
}

int cpu_sync;
int cpu_sync_last;
long cpu_sync_count;
int cpu_clock;
int cpu_clock_last;
const int ins_size = 48;
int ins_index = 0;
int ins_pc[ins_size];
int ins_in[ins_size];
int ins_ma[ins_size];


// MAME debug log
std::vector<std::string> log_mame;
std::vector<std::string> log_cpu;
long log_index;
//long log_breakpoint = 1182;
long log_breakpoint = 0;
long log_debugat = 0;
//long log_debugat = 210000;

bool writeLog(const char* line)
{

	if (debug_6502 && debug_enable) {
		// Compare with MAME log
		bool match = true;

		std::string c_line = std::string(line);
		std::string c = "CPU > " + c_line;
		if (log_index < log_mame.size()) {
			std::string m_line = log_mame.at(log_index);
			std::string m = "MAME > " + m_line;
			int hcnt = top->emu__DOT__missile__DOT__sync_circuit__DOT__hcnt;
			int vcnt = top->emu__DOT__missile__DOT__sync_circuit__DOT__vcnt;
			bool vblank = top->emu__DOT__missile__DOT__sync_circuit__DOT__v_blank;
			//std::string f = fmt::format("{0}: hcnt={1} vcnt={2} vb={3} {4} {5}", log_index, hcnt, vcnt, vblank, m, c);
			//console.AddLog(f.c_str());

			if (stop_on_log_mismatch && m_line != c_line) {
				console.AddLog("DIFF at %d", log_index);
				console.AddLog(m.c_str());
				console.AddLog(c.c_str());
				match = false;
			}
		}
		if (match) {
			console.AddLog(c.c_str());
		}

		if (log_breakpoint > 0 && log_index == log_breakpoint) {
			console.AddLog("BREAK at %d", log_index);
			console.AddLog(c.c_str());
			match = false;
		}

		return match;
	}
	if (log_debugat > 0 && log_index == log_debugat) {
		debug_enable = 1;
		//batchSize /= 100;
	}

	log_index++;
	return true;
}

enum instruction_type {
	implied,
	immediate,
	absolute,
	absoluteX,
	absoluteY,
	zeroPage,
	zeroPageX,
	zeroPageY,
	relative,
	accumulator,
	indirect,
	indirectX,
	indirectY
};

void DumpInstruction() {

	if (cpu_sync_count > 1) {

		std::string log = "{0:04X}: ";
		const char* f = "";
		const char* sta;

		instruction_type type = implied;

		int arg1 = 0;
		int arg2 = 0;

		switch (ins_in[0])
		{
		case 0x00: sta = "brk"; break;
		case 0x98: sta = "tya"; break;
		case 0xA8: sta = "tay"; break;
		case 0xAA: sta = "tac"; break;
		case 0x8A: sta = "txa"; break;
		case 0x40: sta = "rti"; break;
		case 0x60: sta = "rts"; break;
		case 0x9A: sta = "txs"; break;
		case 0xBA: sta = "tsx"; break;

		case 0x18: sta = "clc"; break;
		case 0x58: sta = "cli"; break;
		case 0xB8: sta = "clo"; break;
		case 0xD8: sta = "cld"; break;

		case 0xE8: sta = "inx"; break;
		case 0xC8: sta = "iny"; break;

		case 0x80: sta = "nop"; type = immediate; break;

		case 0x38: sta = "sec"; break;
		case 0x78: sta = "sei"; break;
		case 0xF8: sta = "sed"; break;

		case 0x48: sta = "pha"; break;
		case 0x68: sta = "pla"; break;

		case 0x0A: sta = "asl"; type = accumulator; break;
		case 0x06: sta = "asl"; type = zeroPage; break;
		case 0x16: sta = "asl"; type = zeroPageX; break;
		case 0x0E: sta = "asl"; type = absolute; break;
		case 0x1E: sta = "asl"; type = absoluteX; break;

		case 0x09: sta = "ora"; type = immediate; break;
		case 0x05: sta = "ora"; type = zeroPage; break;
		case 0x15: sta = "ora"; type = zeroPageX; break;
		case 0x0D: sta = "ora"; type = absolute; break;
		case 0x1D: sta = "ora"; type = absoluteX; break;
		case 0x19: sta = "ora"; type = absoluteY; break;
		case 0x01: sta = "ora"; type = indirectX; break;
		case 0x11: sta = "ora"; type = indirectY; break;

		case 0x49: sta = "eor"; type = immediate; break;
		case 0x45: sta = "eor"; type = zeroPage; break;
		case 0x55: sta = "eor"; type = zeroPageX; break;
		case 0x5d: sta = "eor"; type = absoluteX; break;
		case 0x59: sta = "eor"; type = absoluteY; break;
		case 0x41: sta = "eor"; type = indirectX; break;
		case 0x51: sta = "eor"; type = indirectY; break;

		case 0x29: sta = "and"; type = immediate; break;
		case 0x25: sta = "and"; type = zeroPage; break;
		case 0x35: sta = "and"; type = zeroPageX; break;
		case 0x2D: sta = "and"; type = absolute; break;
		case 0x3D: sta = "and"; type = absoluteX; break;
		case 0x39: sta = "and"; type = absoluteY; break;


		case 0xE9: sta = "sbc"; type = immediate; break;
		case 0xE5: sta = "sbc"; type = zeroPage; break;
		case 0xF5: sta = "sbc"; type = zeroPageX; break;
		case 0xED: sta = "sbc"; type = absolute; break;
		case 0xFD: sta = "sbc"; type = absoluteX; break;
		case 0xF9: sta = "sbc"; type = absoluteY; break;
		case 0xE1: sta = "sbc"; type = indirectX; break;
		case 0xF1: sta = "sbc"; type = indirectY; break;

		case 0xC9: sta = "cmp"; type = immediate; break;
		case 0xC5: sta = "cmp"; type = zeroPageX; break;
		case 0xDD: sta = "cmp"; type = absoluteX; break;

		case 0xE0: sta = "cpx"; type = immediate; break;
		case 0xE4: sta = "cpx"; type = zeroPage; break;
		case 0xEC: sta = "cpx"; type = absolute; break;

		case 0xC0: sta = "cpy"; type = immediate; break;
		case 0xC4: sta = "cpy"; type = zeroPage; break;
		case 0xCC: sta = "cpy"; type = absolute; break;

		case 0xA2: sta = "ldx"; type = immediate; break;
		case 0xA6: sta = "ldx"; type = zeroPage; break;
		case 0xB6: sta = "ldx"; type = zeroPageY; break;
		case 0xAE: sta = "ldx"; type = absolute; break;
		case 0xBE: sta = "ldx"; type = absoluteY; break;

		case 0xA0: sta = "ldy"; type = immediate; break;
		case 0xA4: sta = "ldy"; type = zeroPage; break;
		case 0xB4: sta = "ldy"; type = zeroPageX; break;
		case 0xAC: sta = "ldy"; type = absolute; break;
		case 0xBC: sta = "ldy"; type = absoluteX; break;

		case 0xA9: sta = "lda"; type = immediate; break;
		case 0xA5: sta = "lda"; type = zeroPage; break;
		case 0xB5: sta = "lda"; type = zeroPageX; break;
		case 0xAD: sta = "lda"; type = absolute; break;
		case 0xBD: sta = "lda"; type = absoluteX; break;
		case 0xB9: sta = "lda"; type = absoluteY; break;
		case 0xA1: sta = "lda"; type = indirectX; break;
		case 0xB1: sta = "lda"; type = indirectY; break;


		case 0x8D: sta = "sta"; type = absolute; break;
		case 0x85: sta = "sta"; type = zeroPage; break;
		case 0x95: sta = "sta"; type = zeroPageX; break;
		case 0x9D: sta = "sta"; type = absoluteX; break;
		case 0x99: sta = "sta"; type = absoluteY; break;
		case 0x81: sta = "sta"; type = indirectX; break;
		case 0x91: sta = "sta"; type = indirectY; break;

		case 0x86: sta = "stx"; type = zeroPage; break;
		case 0x96: sta = "stx"; type = zeroPageY; break;
		case 0x8E: sta = "stx"; type = absolute; break;
		case 0x84: sta = "sty"; type = zeroPage; break;
		case 0x94: sta = "sty"; type = zeroPageX; break;
		case 0x8C: sta = "sty"; type = absolute; break;

		case 0x69: sta = "adc"; type = immediate; break;
		case 0x65: sta = "adc"; type = zeroPage; break;
		case 0x75: sta = "adc"; type = zeroPageX; break;
		case 0x6D: sta = "adc"; type = absolute; break;
		case 0x7D: sta = "adc"; type = absoluteX; break;
		case 0x79: sta = "adc"; type = absoluteY; break;

		case 0xC6: sta = "dec"; type = zeroPage;  break;
		case 0xD6: sta = "dec"; type = zeroPageX;  break;
		case 0xCE: sta = "dec"; type = absolute;  break;
		case 0xDE: sta = "dec"; type = absoluteX;  break;

		case 0xCA: sta = "dex"; break;
		case 0x88: sta = "dey"; break;

		case 0x24: sta = "bit"; type = zeroPage; break;
		case 0x2C: sta = "bit"; type = absolute; break;

		case 0x30: sta = "bmi"; type = relative; break;
		case 0x90: sta = "bcc"; type = relative; break;
		case 0xB0: sta = "bcs"; type = relative; break;
		case 0xD0: sta = "bne"; type = relative; break;
		case 0xF0: sta = "beq"; type = relative; break;
		case 0x50: sta = "bvc"; type = relative; break;
		case 0x10: sta = "bpl"; type = relative; break;

		case 0x2a: sta = "rol"; type = accumulator; break;
		case 0x7e: sta = "ror"; type = absoluteX; break;

		case 0x4A: sta = "lsr"; type = accumulator; break;
		case 0x46: sta = "lsr"; type = zeroPage; break;

		case 0xE6: sta = "inc"; type = zeroPage; break;
		case 0xF6: sta = "inc"; type = zeroPageX; break;
		case 0xEE: sta = "inc"; type = absolute; break;
		case 0xFE: sta = "inc"; type = absoluteX; break;

		case 0x20: sta = "jsr"; type = absolute; break;

		case 0x4C: sta = "jmp"; type = absolute; break;
		case 0x6C: sta = "jmp"; type = indirect; break;

		default: sta = "???"; f = "\t\tPC={0:X} arg1={1:X} arg2={2:X} IN0={3:X} IN1={4:X} IN2={5:X} IN3={6:X} IN4={7:X} MA0={8:X} MA1={9:X} MA2={10:X} MA3={11:X} MA4={12:X}";
		}

		switch (type) {
		case implied: f = ""; break;
		case immediate: arg1 = ins_in[2]; f = " #${1:02x}"; break;
		case absolute: arg1 = ins_in[4]; arg2 = ins_in[2]; f = " ${1:02x}{2:02x}"; break;
		case absoluteX: arg1 = ins_in[4]; arg2 = ins_in[2]; f = " ${1:02x}{2:02x}, x"; break;
		case absoluteY: arg1 = ins_in[4]; arg2 = ins_in[2]; f = " ${1:02x}{2:02x}, y"; break;
		case zeroPage: arg1 = ins_in[2]; f = " ${1:02x}"; break;
		case zeroPageX: arg1 = ins_in[2]; f = " ${1:02x}, x"; break;
		case zeroPageY: arg1 = ins_in[2]; f = " ${1:02x}, y"; break;
		case relative: arg1 = ins_ma[4] + ((signed char)ins_in[2]); f = " ${1:04x}"; break;
		case indirect: arg1 = ins_in[2]; f = " (${1:02x})"; break;
		case indirectX: arg1 = ins_in[2]; f = " (${1:02x}), x"; break;
		case indirectY: arg1 = ins_in[2]; f = " (${1:02x}), y"; break;
		case accumulator: f = " a"; break;
		}

		log.append(sta);
		log.append(f);

		if (sta == "???") {
			log.append("\t\tPC={0:X} arg1={1:X} arg2={2:X} IN0={3:X} IN1={4:X} IN2={5:X} IN3={6:X} IN4={7:X} MA0={8:X} MA1={9:X} MA2={10:X} MA3={11:X} MA4={12:X}");
		}
		log = fmt::format(log, ins_pc[0], arg1, arg2, ins_in[0], ins_in[1], ins_in[2], ins_in[3], ins_in[4], ins_ma[0], ins_ma[1], ins_ma[2], ins_ma[3], ins_ma[4]);

		if (!writeLog(log.c_str())) {
			run_enable = 0;
		}

		if (sta == "???") {
			console.AddLog(log.c_str());
			run_enable = 0;
		}
	}
}

int verilate() {

	if (!Verilated::gotFinish()) {

		// Assert reset during startup
		if (main_time < initialReset) { top->RESET = 1; }
		// Deassert reset after startup
		if (main_time == initialReset) { top->RESET = 0; }

		// Clock dividers
		clk_sys.Tick();
		clk_pix.Tick();

		// Set system clock in core
		top->clk_10 = clk_sys.clk;

		// Update console with current cycle for logging
		console.prefix = "(" + std::to_string(main_time) + ") ";

		// Output pixels on rising edge of pixel clock
		if (clk_pix.clk && !clk_pix.old) {
			uint32_t colour = 0xFF000000 | top->VGA_B << 16 | top->VGA_G << 8 | top->VGA_R;
			video.Clock(top->VGA_HB, top->VGA_VB, colour);
		}

		// Simulate both edges of system clock
		if (clk_sys.clk != clk_sys.old) {
			if (clk_sys.clk) { bus.BeforeEval(); }
			top->eval();

			bool irq_any = top->emu__DOT__missile__DOT__mp__DOT__bc6502__DOT__any_int;

			//// Log 6502 instructions
			cpu_clock = top->emu__DOT__missile__DOT__mp__DOT__s_phi_0;
			bool cpu_reset = top->emu__DOT__missile__DOT__mp__DOT__reset;
			if (cpu_clock != cpu_clock_last && cpu_reset == 0) {

				if (cpu_sync_count > 0) {
					ins_pc[ins_index] = top->emu__DOT__missile__DOT__mp__DOT__bc6502__DOT__pc_reg;
					ins_in[ins_index] = top->emu__DOT__missile__DOT__mp__DOT__bc6502__DOT__di;
					ins_ma[ins_index] = top->emu__DOT__missile__DOT__mp__DOT__s_addr;
					ins_index++;
					if (ins_index > ins_size - 1) { ins_index = 0; }
				}
				cpu_sync = top->emu__DOT__missile__DOT__mp__DOT__sync;

				bool cpu_rising = cpu_sync == 1 && cpu_sync_last == 0;
				// If IRQ hit then ignore this instruction
				if (cpu_rising && !irq_any) {
					cpu_sync_count++;
					if (ins_index > 0) {
						DumpInstruction();
					}

					// Clear instruction cache
					ins_index = 0;
					for (int i = 0; i < ins_size; i++) {
						ins_in[i] = 0;
						ins_ma[i] = 0;
					}
				}
				cpu_sync_last = cpu_sync;
			}
			cpu_clock_last = cpu_clock;


			if (clk_sys.clk) { bus.AfterEval(); }
		}

		main_time++;
		return 1;
	}
	// Stop verilating and cleanup
	top->final();
	delete top;
	exit(0);
	return 0;
}

int main(int argc, char** argv, char** env) {

	// Load MAME debug log
	std::string line;
	std::ifstream fin("dump/missile1.tr");
	while (getline(fin, line)) {
		log_mame.push_back(line);
	}

	// Create core and initialise
	top = new Vemu();
	Verilated::commandArgs(argc, argv);
	// Attach debug console to the verilated code
	Verilated::setDebug(console);
	// Reset sim
	resetSim();

	// Attach bus
	bus.ioctl_addr = &top->ioctl_addr;
	bus.ioctl_index = &top->ioctl_index;
	bus.ioctl_wait = &top->ioctl_wait;
	bus.ioctl_download = &top->ioctl_download;
	bus.ioctl_upload = &top->ioctl_upload;
	bus.ioctl_wr = &top->ioctl_wr;
	bus.ioctl_dout = &top->ioctl_dout;
	bus.ioctl_din = &top->ioctl_din;

	// Set up input module
	input.Initialise();
#ifdef WIN32
	input.SetMapping(input_up, DIK_UP);
	input.SetMapping(input_right, DIK_RIGHT);
	input.SetMapping(input_down, DIK_DOWN);
	input.SetMapping(input_left, DIK_LEFT);
	input.SetMapping(input_fire1, DIK_Z);
	input.SetMapping(input_fire2, DIK_X);
	input.SetMapping(input_fire3, DIK_C);
	input.SetMapping(input_coin, DIK_5);
	input.SetMapping(input_startp1, DIK_1);
	input.SetMapping(input_startp2, DIK_2);
	input.SetMapping(input_slam, DIK_V);
#endif

	// Setup video output
	if (video.Initialise(windowTitle) == 1) { return 1; }

	// Stage roms for this core
	bus.LoadMRA("../releases/Missile Command (rev 1).mra");
	//bus.LoadMRA("../releases/Missile Command (rev 2).mra");
	//bus.LoadMRA("../releases/Missile Command (rev 3).mra");
	//bus.QueueDownload("roms/240/035820-02.h1", 0, 0);
	//bus.QueueDownload("roms/240/035821-02.jk1", 0, 0);
	//bus.QueueDownload("roms/240/035822-03e.kl1", 0, 0);
	////bus.QueueDownload("roms/240/missile2/035822-02.kl1", 0);
	//bus.QueueDownload("roms/240/035823-02.ln1", 0, 0);
	//bus.QueueDownload("roms/240/035824-02.np1", 0, 0);
	//bus.QueueDownload("roms/240/035825-02.r1", 0, 0);
	//bus.QueueDownload("roms/240/035826-01.l6", 0, 0);

#ifdef WIN32
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
#else
	bool done = false;
	while (!done)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT)
				done = true;
		}
#endif
		video.StartFrame();

		input.Read();

		// Draw GUI
		// --------
		ImGui::NewFrame();

		console.Draw("Debug Log", &showDebugWindow);
		ImGui::Begin(debugWindowTitle);

		if (ImGui::Button("RESET")) { resetSim(); } ImGui::SameLine();
		if (ImGui::Button("START")) { run_enable = 1; } ImGui::SameLine();
		if (ImGui::Button("STOP")) { run_enable = 0; } ImGui::SameLine();
		ImGui::Checkbox("RUN", &run_enable);
		ImGui::Checkbox("STOP @ LOG MISMATCH", &stop_on_log_mismatch);
		ImGui::Checkbox("Debug 6502", &debug_6502);
		ImGui::Checkbox("Debug CPU", &debug_cpu);
		ImGui::Checkbox("Debug DATA", &debug_data);
		ImGui::Checkbox("Self Test", &self_test);
		ImGui::Checkbox("FLIP MODE", &flip);

		ImGui::Checkbox("Pause CPU", &pause);
		top->emu__DOT__pause = pause;

		if (self_test) {
			top->emu__DOT__self_test = 1;
		}
		else {
			top->emu__DOT__self_test = 0;
		}

		ImGui::SliderInt("Batch size", &batchSize, 1, 100000);

		if (single_step == 1) { single_step = 0; }
		if (ImGui::Button("Single Step")) { run_enable = 0; single_step = 1; }
		ImGui::SameLine();
		if (multi_step == 1) { multi_step = 0; }
		if (ImGui::Button("Multi Step")) { run_enable = 0; multi_step = 1; }
		ImGui::SameLine();
		ImGui::SliderInt("Step amount", &multi_step_amount, 8, 1024);

		ImGui::SliderInt("Language", &dip_language, 0, 3);
		top->emu__DOT__dip_language = dip_language;

		ImGui::SliderInt("Coins per play", &dip_coinage, 0, 3);
		top->emu__DOT__dip_coinage = dip_coinage;

		ImGui::SliderInt("Mouse speed", &mouse_speed, 0, 3);
		top->emu__DOT__mouse_speed = mouse_speed;

		ImGui::SliderInt("Joystick sensitivity", &joystick_sensitivity, 0, 1);
		top->emu__DOT__joystick_sensitivity = joystick_sensitivity;

		ImGui::SliderInt("Rotate", &video.output_rotate, -1, 1); ImGui::SameLine();
		ImGui::Checkbox("Flip V", &video.output_vflip);

		ImGui::Text("mouse_x: %d  mouse_y: %d", mouse_x, mouse_y);
		/*ImGui::Text("mouse_mag_x: %d  mouse_mag_y: %d", top->emu__DOT__trackball__DOT__mouse_mag_x, top->emu__DOT__trackball__DOT__mouse_mag_y);*/
		ImGui::Text("main_time: %d frame_count: %d sim FPS: %f", main_time, video.count_frame, video.stats_fps);
		//ImGui::Text("hblank: %x vblank: %x hsync: %x vsync: %x", top->emu__DOT__missile__DOT__h_blank, top->emu__DOT__missile__DOT__v_blank, top->emu__DOT__missile__DOT__h_sync, top->emu__DOT__missile__DOT__v_sync);
		ImGui::Text("hcnt: %d  vx: %d", top->emu__DOT__missile__DOT__sync_circuit__DOT__hcnt, video.count_pixel);
		ImGui::Text("vcnt: %d  vy: %d", top->emu__DOT__missile__DOT__sync_circuit__DOT__vcnt, video.count_line);

		// Draw VGA output
		float m = 2.0;
		ImGui::Image(video.texture_id, ImVec2(video.output_width * m, video.output_height * m));
		ImGui::End();

		//ImGui::Begin("PG-ROM0");
		//memoryEditor_hs.DrawContents(&top->emu__DOT__missile__DOT__pgrom0__DOT__mem, 4096, 0);
		//ImGui::End();
		//ImGui::Begin("PG-ROM1");
		//memoryEditor_hs.DrawContents(&top->emu__DOT__missile__DOT__pgrom1__DOT__mem, 4096, 0);
		//ImGui::End();
		//ImGui::Begin("PG-ROM2");
		//memoryEditor_hs.DrawContents(&top->emu__DOT__missile__DOT__pgrom2__DOT__mem, 4096, 0);
		//ImGui::End();
		//ImGui::Begin("DRAM");
		//memoryEditor_hs.DrawContents(&top->emu__DOT__missile__DOT__ram__DOT__mem, 16384, 0);
		//ImGui::End();
		//ImGui::Begin("CRAM");
		//memoryEditor_hs.DrawContents(&top->emu__DOT__missile__DOT__L7__DOT__mem, 8, 0);
		//ImGui::End();
		//ImGui::Begin("L6-ROM");
		//memoryEditor_hs.DrawContents(&top->emu__DOT__missile__DOT__L6__DOT__mem, 32, 0);
		//ImGui::End();
		//ImGui::Begin("hiscore_data");
		//memoryEditor_hs.DrawContents(&top->emu__DOT__hi__DOT__hiscore_data__DOT__ram, 48, 0);
		//ImGui::End(); 

		video.UpdateTexture();

		// Pass inputs to sim
		top->inputs = 0;
		for (int i = 0; i < input.inputCount; i++)
		{
			if (input.inputs[i]) { top->inputs |= (1 << i); }
		}

		mouse_buttons = 0 | (input.inputs[4]);

		//if (input.keyState[DIK_LSHIFT]) {


		int acc = 16;
		int dec = 1;
		int fric = 2;

		if (input.inputs[input_left]) { mouse_x -= acc; }
		else if (mouse_x < 0) { mouse_x += (dec + (-mouse_x / fric)); }

		if (input.inputs[input_right]) { mouse_x += acc; }
		else if (mouse_x > 0) { mouse_x -= (dec + (mouse_x / fric)); }

		if (input.inputs[input_up]) { mouse_y += acc; }
		else if (mouse_y > 0) { mouse_y -= (dec + (mouse_y / fric)); }

		if (input.inputs[input_down]) { mouse_y -= acc; }
		else if (mouse_y < 0) { mouse_y += (dec + (-mouse_y / fric)); }

		int lim = 127;
		if (mouse_x > lim) { mouse_x = lim; }
		if (mouse_x < -lim) { mouse_x = -lim; }
		if (mouse_y > lim) { mouse_y = lim; }
		if (mouse_y < -lim) { mouse_y = -lim; }

		signed char joy_x = mouse_x / 2;
		signed char joy_y = mouse_y / 2;

		unsigned short joy = ((unsigned char)-joy_y) << 8;
		joy |= (unsigned char)joy_x;

		top->joystick_analog = joy;

		//}
		//else {
			//int mspeed = 64;
			//mouse_x = 0;
			//mouse_y = 0; 
			//if (input.inputs[input_left]) { mouse_x = -mspeed; }
			//if (input.inputs[input_right]) { mouse_x = +mspeed; }
			//if (input.inputs[input_up]) { mouse_y = +mspeed; }
			//if (input.inputs[input_down]) { mouse_y = -mspeed; }
		//}


		//unsigned char ps2_mouse1;
		//unsigned char ps2_mouse2;
		//int x = mouse_x;
		//mouse_buttons |= (x < 0) ? 0x10 : 0x00;
		//if (x < -255)
		//{
		//	// min possible value + overflow flag
		//	mouse_buttons |= 0x40;
		//	ps2_mouse1 = 1; // -255
		//}
		//else if (x > 255)
		//{
		//	// max possible value + overflow flag
		//	mouse_buttons |= 0x40;
		//	ps2_mouse1 = 255;
		//}
		//else
		//{
		//	ps2_mouse1 = (char)x;
		//}

		//// ------ Y axis -----------
		//// store sign bit in first byte
		//int y = mouse_y;
		//mouse_buttons |= (y < 0) ? 0x20 : 0x00;
		//if (y < -255)
		//{
		//	// min possible value + overflow flag
		//	mouse_buttons |= 0x80;
		//	ps2_mouse2 = 1; // -255;
		//}
		//else if (y > 255)
		//{
		//	// max possible value + overflow flag
		//	mouse_buttons |= 0x80;
		//	ps2_mouse2 = 255;
		//}
		//else
		//{
		//	ps2_mouse2 = (char)y;
		//}

		//unsigned long mouse_temp = mouse_buttons;
		//mouse_temp += (((unsigned char)ps2_mouse1) << 8);
		//mouse_temp += (((unsigned char)ps2_mouse2) << 16);
		//if (mouse_clock) { mouse_temp |= (1UL << 24); }

		//mouse_clock = !mouse_clock;

		//top->ps2_mouse = mouse_temp;
		//top->ps2_mouse_ext = mouse_x + (mouse_buttons << 8);

		// Run simulation
		top->emu__DOT__missile__DOT__mp__DOT__bc6502__DOT__debug_cpu = debug_cpu & debug_enable;
		top->emu__DOT__missile__DOT__debug_data = debug_data & debug_enable;
		if (run_enable) {
			for (int step = 0; step < batchSize; step++) { verilate(); if (!run_enable) { break; } }
		}
		else {
			if (single_step) { verilate(); }
			if (multi_step) {
				for (int step = 0; step < multi_step_amount; step++) {
					verilate(); if (!multi_step) { break; }
				}
			}
		}
	}

	// Clean up before exit
	// --------------------

	video.CleanUp();
	input.CleanUp();

	return 0;
}
