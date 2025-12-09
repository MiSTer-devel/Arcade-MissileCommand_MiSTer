//============================================================================
//  Missile Command for MiSTer FPGA
//
//  Copyright (C) 2022 JimmyStones
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//============================================================================

module emu
(
	//Master input clock
	input         CLK_50M,

	//Async reset from top-level module.
	//Can be used as initial reset.
	input         RESET,

	//Must be passed to hps_io module
	inout  [48:0] HPS_BUS,

	//Base video clock. Usually equals to CLK_SYS.
	output        CLK_VIDEO,

	//Multiple resolutions are supported using different CE_PIXEL rates.
	//Must be based on CLK_VIDEO
	output        CE_PIXEL,

	//Video aspect ratio for HDMI. Most retro systems have ratio 4:3.
	//if VIDEO_ARX[12] or VIDEO_ARY[12] is set then [11:0] contains scaled size instead of aspect ratio.
	output [12:0] VIDEO_ARX,
	output [12:0] VIDEO_ARY,

	output  [7:0] VGA_R,
	output  [7:0] VGA_G,
	output  [7:0] VGA_B,
	output        VGA_HS,
	output        VGA_VS,
	output        VGA_DE,    // = ~(VBlank | HBlank)
	output        VGA_F1,
	output [1:0]  VGA_SL,
	output        VGA_SCALER, // Force VGA scaler
	output        VGA_DISABLE, // analog out is off

	input  [11:0] HDMI_WIDTH,
	input  [11:0] HDMI_HEIGHT,
	output        HDMI_FREEZE,
	output        HDMI_BLACKOUT,
	output        HDMI_BOB_DEINT,

`ifdef MISTER_FB
	// Use framebuffer in DDRAM
	// FB_FORMAT:
	//    [2:0] : 011=8bpp(palette) 100=16bpp 101=24bpp 110=32bpp
	//    [3]   : 0=16bits 565 1=16bits 1555
	//    [4]   : 0=RGB  1=BGR (for 16/24/32 modes)
	//
	// FB_STRIDE either 0 (rounded to 256 bytes) or multiple of pixel size (in bytes)
	output        FB_EN,
	output  [4:0] FB_FORMAT,
	output [11:0] FB_WIDTH,
	output [11:0] FB_HEIGHT,
	output [31:0] FB_BASE,
	output [13:0] FB_STRIDE,
	input         FB_VBL,
	input         FB_LL,
	output        FB_FORCE_BLANK,

`ifdef MISTER_FB_PALETTE
	// Palette control for 8bit modes.
	// Ignored for other video modes.
	output        FB_PAL_CLK,
	output  [7:0] FB_PAL_ADDR,
	output [23:0] FB_PAL_DOUT,
	input  [23:0] FB_PAL_DIN,
	output        FB_PAL_WR,
`endif
`endif

	output        LED_USER,  // 1 - ON, 0 - OFF.

	// b[1]: 0 - LED status is system status OR'd with b[0]
	//       1 - LED status is controled solely by b[0]
	// hint: supply 2'b00 to let the system control the LED.
	output  [1:0] LED_POWER,
	output  [1:0] LED_DISK,

	// I/O board button press simulation (active high)
	// b[1]: user button
	// b[0]: osd button
	output  [1:0] BUTTONS,

	input         CLK_AUDIO, // 24.576 MHz
	output [15:0] AUDIO_L,
	output [15:0] AUDIO_R,
	output        AUDIO_S,   // 1 - signed audio samples, 0 - unsigned
	output  [1:0] AUDIO_MIX, // 0 - no mix, 1 - 25%, 2 - 50%, 3 - 100% (mono)

	//ADC
	inout   [3:0] ADC_BUS,

	//SD-SPI
	output        SD_SCK,
	output        SD_MOSI,
	input         SD_MISO,
	output        SD_CS,
	input         SD_CD,

	//High latency DDR3 RAM interface
	//Use for non-critical time purposes
	output        DDRAM_CLK,
	input         DDRAM_BUSY,
	output  [7:0] DDRAM_BURSTCNT,
	output [28:0] DDRAM_ADDR,
	input  [63:0] DDRAM_DOUT,
	input         DDRAM_DOUT_READY,
	output        DDRAM_RD,
	output [63:0] DDRAM_DIN,
	output  [7:0] DDRAM_BE,
	output        DDRAM_WE,

	//SDRAM interface with lower latency
	output        SDRAM_CLK,
	output        SDRAM_CKE,
	output [12:0] SDRAM_A,
	output  [1:0] SDRAM_BA,
	inout  [15:0] SDRAM_DQ,
	output        SDRAM_DQML,
	output        SDRAM_DQMH,
	output        SDRAM_nCS,
	output        SDRAM_nCAS,
	output        SDRAM_nRAS,
	output        SDRAM_nWE,

`ifdef MISTER_DUAL_SDRAM
	//Secondary SDRAM
	//Set all output SDRAM_* signals to Z ASAP if SDRAM2_EN is 0
	input         SDRAM2_EN,
	output        SDRAM2_CLK,
	output [12:0] SDRAM2_A,
	output  [1:0] SDRAM2_BA,
	inout  [15:0] SDRAM2_DQ,
	output        SDRAM2_nCS,
	output        SDRAM2_nCAS,
	output        SDRAM2_nRAS,
	output        SDRAM2_nWE,
`endif

	input         UART_CTS,
	output        UART_RTS,
	input         UART_RXD,
	output        UART_TXD,
	output        UART_DTR,
	input         UART_DSR,

	// Open-drain User port.
	// 0 - D+/RX
	// 1 - D-/TX
	// 2..6 - USR2..USR6
	// Set USER_OUT to 1 to read from USER_IN.
	input   [6:0] USER_IN,
	output  [6:0] USER_OUT,

	input         OSD_STATUS
);

///////// Default values for ports not used in this core /////////

assign ADC_BUS  = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
assign {SDRAM_DQ, SDRAM_A, SDRAM_BA, SDRAM_CLK, SDRAM_CKE, SDRAM_DQML, SDRAM_DQMH, SDRAM_nWE, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nCS} = 'Z;
assign {DDRAM_CLK, DDRAM_BURSTCNT, DDRAM_ADDR, DDRAM_DIN, DDRAM_BE, DDRAM_RD, DDRAM_WE} = '0;  

assign VGA_F1 = '0;
assign VGA_SCALER = '0;
assign VGA_DISABLE = '0;
assign HDMI_FREEZE = '0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;

assign AUDIO_S = '0;
assign AUDIO_MIX = '0;

assign LED_DISK = '0;
assign LED_POWER = '0;
assign LED_USER = '0;
assign BUTTONS = '0;

//////////////////////////////////////////////////////////////////

wire [1:0] ar = status[9:8];

assign VIDEO_ARX = (!ar) ? 12'd4 : (ar - 1'd1);
assign VIDEO_ARY = (!ar) ? 12'd3 : 12'd0;

`include "build_id.v" 
localparam CONF_STR = {
	"A.MISSILE;;",
	"O35,Scandoubler Fx,None,HQ2x,CRT 25%,CRT 50%,CRT 75%;",  
	"OGJ,Analog Video H-Pos,0,-1,-2,-3,-4,-5,-6,-7,8,7,6,5,4,3,2,1;",
	"OKN,Analog Video V-Pos,0,-1,-2,-3,-4,-5,-6,-7,4,3,2,1;",
	"-;",
	"OEF,Mouse/trackball speed,25%,50%,100%,200%;",
	"OBC,Button order,LMR,LRM,MRL,MLR;",
	"OR,Joystick mode,Digital,Analog;",
	"OD,Joystick speed,Low,High;",
	"-;",
	"DIP;",
	"-;",
	"H2OQ,Autosave Hiscores,Off,On;",
	"P1,Pause options;",
	"P1OO,Pause when OSD is open,On,Off;",
	"P1OP,Dim video after 10s,On,Off;",
	"-;",
	"OA,Test mode,No,Yes;",
	"-;",
	"R0,Reset;",
	"J1,Fire L,Fire C,Fire R,Start 1P,Start 2P,Coin,Pause,Slam;",
	"Jn,A,B,C,Start,Select,R,L,Z;",
	"V,v",`BUILD_DATE
};

////////////////////   CLOCKS   ///////////////////
wire clk_vid;
wire clk_sys;
reg ce_5M = 1'b0;
wire ce_pix = ce_5M;

pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_vid),
	.outclk_1(clk_sys)
);

////////////////////   HPS   /////////////////////
wire [31:0]	status;
wire  [1:0]	buttons;
wire		forced_scandoubler;
wire		direct_video;
wire		ioctl_download;
wire		ioctl_upload;
wire		ioctl_upload_req;
wire		ioctl_wr;
wire [24:0]	ioctl_addr;
wire  [7:0]	ioctl_dout;
wire  [7:0]	ioctl_din;
wire  [7:0]	ioctl_index;
wire [15:0]	joystick_0, joystick_1;
wire [15:0] joystick_l_analog_0;
wire [15:0] joystick_r_analog_0;
wire [15:0]	joy = joystick_0 | joystick_1;
wire [24:0]	ps2_mouse /* synthesis preserve */;

wire [21:0]	gamma_bus;

hps_io #(.CONF_STR(CONF_STR)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),
	.EXT_BUS(),

	.buttons(buttons),
	.status(status),
	.status_menumask({direct_video}),

	.forced_scandoubler(forced_scandoubler),
	.gamma_bus(gamma_bus),
	.direct_video(direct_video),

	.ioctl_download(ioctl_download),
	.ioctl_upload(ioctl_upload),
	.ioctl_upload_req(ioctl_upload_req),
	.ioctl_wr(ioctl_wr),
	.ioctl_addr(ioctl_addr),
	.ioctl_dout(ioctl_dout),
	.ioctl_din(ioctl_din),
	.ioctl_index(ioctl_index),

	.joystick_0(joystick_0),
	.joystick_1(joystick_1),
	.joystick_l_analog_0(joystick_l_analog_0),
	.joystick_r_analog_0(joystick_r_analog_0),
	
	.ps2_mouse(ps2_mouse)
);



///////////////////   DIPS   ////////////////////
reg [7:0] sw[8];
always @(posedge clk_sys)
begin
	if (ioctl_wr && (ioctl_index==8'd254) && !ioctl_addr[24:3]) sw[ioctl_addr[2:0]] <= ioctl_dout;
end

wire		dip_cocktail = 1'b1;		// 1= Upright, 0=Cocktail (enable flip)
wire [1:0]	dip_language = sw[0][1:0];
wire		dip_centrecoin = 1'b0;		// Coin multipliers are unnecessary
wire [1:0]	dip_rightcoin = 2'b00;		// Coin multipliers are unnecessary
wire [1:0]	dip_coinage = sw[0][3:2];
wire [1:0]	dip_cities = sw[0][5:4];
wire		dip_bonuscredit = 1'b1;		// Not useful
wire [2:0]	dip_bonuscity = {sw[1][0], sw[0][7:6]};
wire		dip_trackballspeed = sw[1][1];

wire [7:0]	in2 = { 1'b0, dip_language, dip_centrecoin, dip_rightcoin, dip_coinage };
wire [7:0]	switches = { dip_cocktail, dip_bonuscity, dip_trackballspeed, dip_bonuscredit, dip_cities };

///////////////////   CONTROLS   ////////////////////

reg mouse_left = 1'b0;
reg mouse_center = 1'b0;
reg mouse_right = 1'b0;

wire [15:0] joystick_analog = joystick_l_analog_0 | joystick_r_analog_0;

always @(posedge clk_sys)
begin
	case(status[12:11])
	2'd0: // LMR
	begin
		mouse_left <= ps2_mouse[0];
		mouse_center <= ps2_mouse[2];
		mouse_right <= ps2_mouse[1];
	end
	2'd1: // LRM
	begin
		mouse_left <= ps2_mouse[0];
		mouse_center <= ps2_mouse[1];
		mouse_right <= ps2_mouse[2];
	end
	2'd2: // MRL
	begin
		mouse_left <= ps2_mouse[2];
		mouse_center <= ps2_mouse[1];
		mouse_right <= ps2_mouse[0];
	end
	2'd3: // MLR
	begin
		mouse_left <= ps2_mouse[2];
		mouse_center <= ps2_mouse[0];
		mouse_right <= ps2_mouse[1];
	end
	endcase
end

wire		m_fire_l = joy[4] | mouse_left;
wire		m_fire_c = joy[5] | mouse_center;
wire		m_fire_r = joy[6] | mouse_right;
wire		m_start1 = joy[7];
wire		m_start2 = joy[8];
wire		m_coin	= joy[9];
wire		m_pause	= joy[10];
wire		m_slam	= joy[11];
wire		m_test	= status[10];

wire		flip;
wire		vtb_dir1;
wire		vtb_clk1;
wire		htb_dir1;
wire		htb_clk1;
trackball trackball
(
	.clk(clk_sys),
	.flip(flip),
	.joystick(joy[3:0]),
	.joystick_mode(status[27]),
	.joystick_analog(joystick_analog),
	.joystick_sensitivity(status[13]),
	.mouse_speed(status[15:14]),
	.ps2_mouse(ps2_mouse),
	.v_dir(vtb_dir1),
	.v_clk(vtb_clk1),
	.h_dir(htb_dir1),
	.h_clk(htb_clk1)
);

// PAUSE SYSTEM
wire		pause_cpu;
wire [23:0]	rgb_out;
pause #(8,8,8,10) pause (
	.*,
	.user_button(m_pause),
	.pause_request(hs_pause),
	.options(~status[25:24])
);


///////////////////   CLOCK DIVIDER   ////////////////////
always @(posedge clk_sys) ce_5M <= !ce_5M; 

///////////////////   VIDEO   ////////////////////
wire [7:0] r;
wire [7:0] g;
wire [7:0] b;
wire hblank, vblank, hs, vs, hs_original, vs_original;
arcade_video 
#(
	.WIDTH(320),
	.DW(24),
	.GAMMA(1)
)
arcade_video
(
	.*,
	.clk_video(clk_vid),
	.RGB_in(rgb_out),
	.HBlank(hblank),
	.VBlank(vblank),
	.HSync(hs),
	.VSync(vs),
	.fx(status[5:3])
);

// H/V offset
wire [3:0]	hoffset = status[19:16];
wire [3:0]	voffset = status[23:20];
jtframe_resync jtframe_resync
(
	.clk(clk_vid),
	.pxl_cen(ce_pix),
	.hs_in(hs_original),
	.vs_in(vs_original),
	.LVBL(~vblank),
	.LHBL(~hblank),
	.hoffset(hoffset),
	.voffset(voffset),
	.hs_out(hs),
	.vs_out(vs)
);


///////////////////   AUDIO   ////////////////////
wire [7:0] audio;
assign AUDIO_L = pause_cpu ? 16'b0 : {audio,audio};
assign AUDIO_R = AUDIO_L;
assign AUDIO_S = 0;

///////////////////   GAME   ////////////////////
wire reset = (RESET | status[0] | buttons[1] | rom_download);
wire rom_download = ioctl_download && (ioctl_index == 8'b0);

missile missile(
	.clk_10M(clk_sys),
	.ce_5M(ce_5M),
	.reset(reset),
	
	.audio_o(audio),

	.htb_dir1(htb_dir1),
	.htb_clk1(htb_clk1),
	.vtb_dir1(vtb_dir1),
	.vtb_clk1(vtb_clk1),

	.coin(m_coin),
	.p1_start(m_start1),
	.p2_start(m_start2),
	
	.p1_fire_l(m_fire_l),
	.p1_fire_c(m_fire_c),
	.p1_fire_r(m_fire_r),

	.p2_fire_l(m_fire_l),
	.p2_fire_c(m_fire_c),
	.p2_fire_r(m_fire_r),

	.in2(in2),
	.switches(switches),

	.self_test(m_test),
	.slam(m_slam),

	.flip(flip),

	.dn_addr(ioctl_addr[15:0]),
	.dn_data(ioctl_dout),
	.dn_wr(ioctl_wr & rom_download),

	.r(r),
	.g(g),
	.b(b),
	.h_sync(hs_original),
	.v_sync(vs_original),
	.h_blank(hblank),
	.v_blank(vblank),

	.hs_address(hs_address),
	.hs_data_out(hs_data_out),
	.hs_data_in(hs_data_in),
	.hs_write(hs_write_enable),
	.hs_access(hs_access_read | hs_access_write),

	.pause(pause_cpu)
);

// HISCORE SYSTEM
// --------------
wire [13:0]hs_address;
wire [7:0] hs_data_in;
wire [7:0] hs_data_out;
wire hs_write_enable;
wire hs_access_read;
wire hs_access_write;
wire hs_pause;
wire hs_configured;

hiscore #(
	.HS_ADDRESSWIDTH(14),
	.CFG_ADDRESSWIDTH(1),
	.CFG_LENGTHWIDTH(2)
) hi (
	.*,
	.clk(clk_sys),
	.paused(pause_cpu),
	.autosave(status[26]),
	.ram_address(hs_address),
	.data_from_ram(hs_data_out),
	.data_to_ram(hs_data_in),
	.data_from_hps(ioctl_dout),
	.data_to_hps(ioctl_din),
	.ram_write(hs_write_enable),
	.ram_intent_read(hs_access_read),
	.ram_intent_write(hs_access_write),
	.pause_cpu(hs_pause),
	.configured(hs_configured)
);
endmodule