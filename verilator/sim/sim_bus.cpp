#include <queue>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include "sim_bus.h"
#include "sim_console.h"
#include "verilated_heavy.h"

#include "inc/rapidxml.hpp"
#include "inc/miniz.h"

#ifndef _MSC_VER
#else
#define WIN32
#endif


static DebugConsole console;
bool ioctl_active = false;
bool ioctl_pending = false;
FILE* ioctl_file = NULL;
int ioctl_next_addr = -1;
int ioctl_last_index = -1;


IData* ioctl_addr = NULL;
CData* ioctl_index = NULL;
CData* ioctl_wait = NULL;
CData* ioctl_download = NULL;
CData* ioctl_upload = NULL;
CData* ioctl_wr = NULL;
CData* ioctl_dout = NULL;
CData* ioctl_din = NULL;

std::queue<SimBus_DownloadChunk> downloadQueue;

void SimBus::QueueDownload(std::string file, int index, long address) {
	SimBus_DownloadChunk chunk = SimBus_DownloadChunk(file, index, address);
	downloadQueue.push(chunk);
}
void SimBus::QueueDownload(std::string file, int index, long address, bool restart) {
	SimBus_DownloadChunk chunk = SimBus_DownloadChunk(file, index, address, restart);
	downloadQueue.push(chunk);
}
bool SimBus::HasQueue() {
	return downloadQueue.size() > 0;
}

#ifdef _WIN32
#include <io.h> 
#define access    _access_s
#else
#include <unistd.h>
#endif

bool FileExists(const std::string& Filename)
{
	return access(Filename.c_str(), 0) == 0;
}

int zip_search_by_crc(mz_zip_archive* zipArchive, uint32_t crc32, uint32_t* size)
{
	for (unsigned int file_index = 0; file_index < zipArchive->m_total_files; file_index++)
	{
		mz_zip_archive_file_stat s;
		if (mz_zip_reader_file_stat(zipArchive, file_index, &s))
		{
			if (s.m_crc32 == crc32)
			{
				*size = s.m_uncomp_size;
				return file_index;
			}
		}
	}

	return -1;
}

inline bool ends_with(std::string const& value, std::string const& ending)
{
	if (ending.size() > value.size()) return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void SimBus::LoadMRA(std::string file) {

	rapidxml::xml_document<> doc;
	rapidxml::xml_node<>* root_node = NULL;

	// Read the sample.xml file
	std::ifstream fileStream(file);
	std::vector<char> buffer((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
	buffer.push_back('\0');

	// Parse the buffer
	doc.parse<0>(&buffer[0]);

	// Find the root node
	root_node = doc.first_node("misterromdescription");

	int lastIndex = -1;

	// Iterate over the <rom> nodes
	for (rapidxml::xml_node<>* rom_node = root_node->first_node("rom"); rom_node; rom_node = rom_node->next_sibling())
	{
		rapidxml::xml_attribute<>* index = rom_node->first_attribute("index");
		if (index != NULL) {
			int indexValue = std::stoi(index->value());

			std::vector<std::string> rom_names;
			std::vector<std::string> zip_names;
			rapidxml::xml_attribute<>* rom_zip_name_att = rom_node->first_attribute("zip");
			if (rom_zip_name_att != NULL) {

				std::istringstream zips;
				zips.str(rom_zip_name_att->value());
				std::string segment;
				char splitChar = '|';
				while (std::getline(zips, segment, splitChar))
				{
					zip_names.push_back(segment);
					if (ends_with(segment, ".zip")) {
						rom_names.push_back(segment.substr(0, segment.length() - 4));
					}
					else {
						rom_names.push_back(segment);
					}

				}
			}

			long address = 0;
			rapidxml::xml_attribute<>* address_part = rom_node->first_attribute("address");
			if (address_part != NULL) {
				std::string str_address = address_part->value();
				char* p;
				str_address = str_address.substr(2, str_address.length() - 2);
				address = strtol(str_address.c_str(), &p, 16);
				console.AddLog("%d", address);
			}

			for (rapidxml::xml_node<>* part_node = rom_node->first_node("part"); part_node; part_node = part_node->next_sibling())
			{

				std::string part_crc = "";
				rapidxml::xml_attribute<>* part_crc_att = part_node->first_attribute("crc");
				if (part_crc_att != NULL) {
					part_crc = part_crc_att->value();
				}

				std::string part_name = "";
				rapidxml::xml_attribute<>* part_name_att = part_node->first_attribute("name");
				if (part_name_att != NULL) {
					part_name = part_name_att->value();
				}
				rapidxml::xml_attribute<>* part_zip_name_att = part_node->first_attribute("zip");
				if (part_zip_name_att != NULL) {

					std::string part_zip_name = part_zip_name_att->value();
					part_zip_name = part_zip_name.substr(0, part_zip_name.length() - 4);

					bool zipFound = false;
					for (int p = 0; p < rom_names.size(); p++) {
						if (rom_names[p] == part_zip_name) { zipFound = true; break; }
					}
					if (!zipFound) { rom_names.push_back(part_zip_name); }
				}

				int part_length = 0;
				rapidxml::xml_attribute<>* part_length_att = part_node->first_attribute("length");
				if (part_length_att != NULL) {
					part_length = std::stoi(part_length_att->value());
				}

				int part_offset = 0;
				rapidxml::xml_attribute<>* part_offset_att = part_node->first_attribute("offset");
				if (part_offset_att != NULL) {
					part_offset = std::stoi(part_offset_att->value());
				}

				bool partFound = false;

				if (part_name.length() > 0 || part_crc.length() > 0) {

					int s = strcmp(part_crc.c_str(), "none");
					if (part_crc.length() > 0 && s != 0) {
						// Find part in zip by crc
						uint32_t crc32 = strtoul(part_crc.c_str(), NULL, 16);
						for (int p = 0; p < zip_names.size(); p++) {
							std::string zip_path = "roms\\" + zip_names[p];
							mz_zip_archive* archive = new mz_zip_archive;
							memset(archive, 0, sizeof(mz_zip_archive));
							if (mz_zip_reader_init_file(archive, zip_path.c_str(), 0))
							{
								//console.AddLog("Searching ROM part from file %s by CRC (%s)", zip_path.c_str(), part_crc.c_str());
								uint32_t size;
								int fileIndex = zip_search_by_crc(archive, crc32, &size);
								if (fileIndex >= 0) {
									//console.AddLog("Loading ROM part from file %s by CRC (%s)", zip_path.c_str(), part_crc.c_str());

									char* buffer = new char[size_t(size)];
									mz_zip_reader_extract_to_mem(archive, fileIndex, buffer, size, 0);

									std::string label = part_name;
									label.append(" (");
									label.append(part_crc);
									label.append(")");
									if (part_length > 0) {
										size = part_length;
									}
									SimBus_DownloadChunk chunk = SimBus_DownloadChunk(indexValue, address, label);

									for (int b = part_offset; b < size + part_offset; b++) {
										chunk.contentQueue.push(buffer[b]);
									}
									downloadQueue.push(chunk);

									partFound = true;
									break;
								}
							}
							mz_zip_reader_end(archive);
						}
					}

					// Find rom part
					if (!partFound) {
						for (int p = 0; p < rom_names.size(); p++) {
							std::string part_path = "roms/" + rom_names[p] + "/" + part_name;
							if (FileExists(part_path)) {
								//console.AddLog("Loading ROM part from file: %s", part_path.c_str());
								QueueDownload(part_path, indexValue, address, lastIndex != indexValue);
								partFound = true;
								break;
							}
						}
					}

					if (!partFound && rom_names.size() > 1) {
						// Try primary rom folder with other subfolders

						for (int p = 1; p < rom_names.size(); p++) {
							std::string part_path = "roms/" + rom_names[0] + "/" + rom_names[p] + "/" + part_name;
							//console.AddLog("Looking for ROM part from file: %s", part_path.c_str());
							if (FileExists(part_path)) {
								//console.AddLog("Loading ROM part from file: %s", part_path.c_str());
								QueueDownload(part_path, indexValue, address, lastIndex != indexValue);
								partFound = true;
								break;
							}
						}
					}

					if (!partFound) {
						console.AddLog("Could not load ROM part in any known file: %s", part_name.c_str());
					}
				}
				else {

					SimBus_DownloadChunk chunk = SimBus_DownloadChunk(indexValue, address, "explicit");

					int part_repeat = 1;
					rapidxml::xml_attribute<>* part_repeat_att = part_node->first_attribute("repeat");
					if (part_repeat_att != NULL) {
						part_repeat = std::stoi(part_repeat_att->value());
					}
					std::string hex_chars = part_node->value();
					std::istringstream hex_chars_stream(hex_chars);
					std::vector<unsigned char> bytes;
					unsigned int c;
					while (hex_chars_stream >> std::hex >> c)
					{
						bytes.push_back(c);
					}

					for (int r = 0; r < part_repeat; r++) {
						for (int b = 0; b < bytes.size(); b++) {
							chunk.contentQueue.push(bytes[b]);
						}
					}

					//console.AddLog("Creating ROM part repeat=%d  len=%d", part_repeat, chunk.contentQueue.size());
					downloadQueue.push(chunk);
				}


				lastIndex = indexValue;
			}
		}
	}

}

int nextchar = 0;
void SimBus::BeforeEval()
{
	if (!ioctl_active && downloadQueue.size() == 0) {
		// No queue, nothing to do
		return;
	}
	// If no download is active and there is a download queued
	if (!ioctl_active && downloadQueue.size() > 0) {

		// Get chunk from queue
		currentDownload = downloadQueue.front();
		downloadQueue.pop();

		// If last index differs from this one then reset the addresses
		if (currentDownload.index != *ioctl_index) {
			ioctl_next_addr = -1;
			if (currentDownload.address != 0) {
				ioctl_next_addr = currentDownload.address;
			}
		}
		// Set address and index
		*ioctl_index = currentDownload.index;

		// Open file
		if (!currentDownload.isQueue) {
			ioctl_file = fopen(currentDownload.file.c_str(), "rb");
			if (!ioctl_file) {
				console.AddLog("Cannot open file for download %s\n", currentDownload.file.c_str());
			}
			else {
				ioctl_active = true;
				console.AddLog("Starting download: %s %d index=%d", currentDownload.file.c_str(), *ioctl_addr, currentDownload.index);
			}
		}
		else {
			console.AddLog("Starting download: %s %d index=%d", currentDownload.label.c_str(), *ioctl_addr, currentDownload.index);
			ioctl_active = true;
			nextchar = currentDownload.contentQueue.front();
		}
	}
	else
	{
		bool complete = false;
		if (!currentDownload.isQueue) {

			if (ioctl_file) {
				//console.AddLog("ioctl_download addr %x  ioctl_wait %x", *ioctl_addr, *ioctl_wait);

				// Otherwise drip-feed it through HPS
//					if (*ioctl_wait == 0) {
				*ioctl_download = 1;
				*ioctl_wr = 1;
				if (feof(ioctl_file)) {
					fclose(ioctl_file);
					ioctl_file = NULL;
					ioctl_active = false;
					*ioctl_download = 0;
					*ioctl_wr = 0;
					//console.AddLog("ioctl_download complete %d", ioctl_next_addr);
				}
				if (ioctl_file) {
					int curchar = fgetc(ioctl_file);
					if (feof(ioctl_file) == 0) {
						nextchar = curchar;
						ioctl_next_addr++;
					}
				}
				//				}
			}
			else {
				complete = true;
			}

		}
		else {
			// Do a queue
			//console.AddLog("ioctl: active=%d download=%d wr=%d next_addr=%d", ioctl_active, *ioctl_download, *ioctl_wr, *ioctl_addr);
			if (currentDownload.contentQueue.empty()) {
				complete = true;
				ioctl_pending = false;
			}
			else {
				*ioctl_download = 1;
				*ioctl_wr = 1;
				if (!ioctl_pending) {
					nextchar = currentDownload.contentQueue.front();
					currentDownload.contentQueue.pop();
					ioctl_next_addr++;
					ioctl_pending = true;
				}
				else {
					ioctl_pending = false;
				}
			}
		}

		*ioctl_addr = ioctl_next_addr;
		*ioctl_dout = (unsigned char)nextchar;

		if (complete) {
			ioctl_active = false;
			*ioctl_download = 0;
			*ioctl_wr = 0;
		}
	}
}

void SimBus::AfterEval()
{

}


SimBus::SimBus(DebugConsole c) {
	console = c;
	ioctl_addr = NULL;
	ioctl_index = NULL;
	ioctl_wait = NULL;
	ioctl_download = NULL;
	ioctl_upload = NULL;
	ioctl_wr = NULL;
	ioctl_dout = NULL;
	ioctl_din = NULL;
}

SimBus::~SimBus() {

}
