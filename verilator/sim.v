/*============================================================================
	Missile Command for MiSTer FPGA - Verilator simulation top

	Copyright (C) 2022 - Jim Gregory - https://github.com/JimmyStones/

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the Free
	Software Foundation; either version 3 of the License, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program. If not, see <http://www.gnu.org/licenses/>.
===========================================================================*/

`timescale 1 ps / 1 ps

module emu(

	input			clk_10 /*verilator public_flat*/,
	input			RESET/*verilator public_flat*/,
	input [11:0]	inputs/*verilator public_flat*/,

	input [15:0]	joystick_analog/*verilator public_flat*/,

	// [24] - toggles with every event
	input [24:0]	ps2_mouse,
	input [15:0]	ps2_mouse_ext, // 15:8 - reserved(additional buttons), 7:0 - wheel movements

	output [7:0]	VGA_R/*verilator public_flat*/,
	output [7:0]	VGA_G/*verilator public_flat*/,
	output [7:0]	VGA_B/*verilator public_flat*/,

	output			VGA_HS,
	output			VGA_VS,
	output			VGA_HB,
	output			VGA_VB,

	input			ioctl_download,
	input			ioctl_upload,
	output			ioctl_upload_req,
	input			ioctl_wr,
	input	[24:0]	ioctl_addr,
	input	 [7:0]	ioctl_dout,
	output	 [7:0]	ioctl_din,
	input	 [7:0]	ioctl_index,
	output reg		ioctl_wait=1'b0

);

// Core inputs/outputs
wire		pause/*verilator public_flat*/;
wire [23:0]	rgb;
wire [7:0]	audio;
reg			self_test/*verilator public_flat*/;

// MAP INPUTS FROM SIM
// -------------------
wire p1_fire_l	= inputs[4];
wire p1_fire_c	= inputs[5];
wire p1_fire_r	= inputs[6];
wire coin		= inputs[7];
wire p1_start	= inputs[8];
wire p2_start	= inputs[9];
wire slam		= inputs[10];

reg ce_5M = 1'b0;
always @(posedge clk_10) ce_5M <= ~ce_5M;

reg			rom_downloaded = 0;
wire		rom_download = ioctl_download && ioctl_index == 8'b0;
wire		reset/*verilator public_flat*/;
assign		reset = (RESET | rom_download | !rom_downloaded); 
always @(posedge clk_10) if(rom_download) rom_downloaded <= 1'b1; // Latch downloaded rom state to release reset

// DIPs
wire		dip_cocktail = 1'b1; // 1= Upright, 0=Cocktail (enable flip)
reg  [1:0]	dip_language /*verilator public_flat*/ = 2'b0;    // English
wire		dip_centrecoin = 1'b0;			// Coin multipliers are unnecessary
wire [1:0]	dip_rightcoin = 2'b00;	// Coin multipliers are unnecessary
reg  [1:0]	dip_coinage /*verilator public_flat*/ = 2'b00;
wire [1:0]	dip_cities = 2'b11;
wire [2:0]	dip_bonuscity = 3'b111;
wire		dip_bonuscredit = 1'b0;
wire 		dip_trackballsize = 1'b0;

wire [7:0]	in2 = { 1'b0, dip_language, dip_centrecoin, dip_rightcoin, dip_coinage };
wire [7:0]	switches = { dip_cocktail, dip_bonuscity, dip_trackballsize, dip_bonuscredit, dip_cities };


// Trackball
// ---------
wire		flip;
wire		vtb_dir1;
wire		vtb_clk1;
wire		htb_dir1;
wire		htb_clk1;
reg [1:0]	mouse_speed /*verilator public_flat*/ = 2'b00;
reg			joystick_sensitivity /*verilator public_flat*/ = 1'b0;

trackball trackball
(
	.clk(clk_10),
	.flip(flip),
	.joystick(inputs[3:0]),
	.joystick_mode(1'b1),
	.joystick_analog(joystick_analog),
	.joystick_sensitivity(joystick_sensitivity),
	.mouse_speed(mouse_speed),
	.ps2_mouse(ps2_mouse),
	.v_dir(vtb_dir1),
	.v_clk(vtb_clk1),
	.h_dir(htb_dir1),
	.h_clk(htb_clk1)
);

missile missile(
	.clk_10M(clk_10),
	.ce_5M(ce_5M),
	.reset(reset),
	.audio_o(),

	.htb_dir1(htb_dir1),
	.htb_clk1(htb_clk1),
	.vtb_dir1(vtb_dir1),
	.vtb_clk1(vtb_clk1),

	.coin(coin),
	.p1_fire_l(p1_fire_l),
	.p1_fire_c(p1_fire_c),
	.p1_fire_r(p1_fire_r),

	.p1_start(p1_start),
	.p2_start(p2_start),

	.in2(in2),
	.switches(switches),

	.self_test(self_test),
	.slam(slam),
	.flip(flip),

	.dn_addr(ioctl_addr[15:0]),
	.dn_data(ioctl_dout),
	.dn_wr(ioctl_wr & rom_download),

	.r(VGA_R),
	.g(VGA_G),
	.b(VGA_B),
	.h_sync(VGA_HS),
	.v_sync(VGA_VS),
	.h_blank(VGA_HB),
	.v_blank(VGA_VB),

	.hs_address(hs_address),
	.hs_data_out(hs_data_out),
	.hs_data_in(hs_data_in),
	.hs_write(hs_write_enable),
	.hs_access(hs_access_read | hs_access_write),

	.pause(pause_cpu)
);

wire		pause_cpu = pause | hs_pause;

// HISCORE SYSTEM
// --------------
wire [13:0]	hs_address;
wire [7:0]	hs_data_in;
wire [7:0]	hs_data_out;
wire		hs_write_enable;
wire		hs_access_read;
wire		hs_access_write;
wire		hs_pause;
wire		hs_configured;

hiscore #(
	.HS_ADDRESSWIDTH(14),
	.CFG_ADDRESSWIDTH(1),
	.CFG_LENGTHWIDTH(2)
) hi (
	.*,
	.clk(clk_10),
	.paused(pause_cpu),
	.autosave(1'b1),
	.OSD_STATUS(1'b1),
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