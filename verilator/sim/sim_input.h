#pragma once
#ifndef _MSC_VER
#else
#define WIN32
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#endif
#include <vector>

struct SimInput {
public:

	int inputCount = 0;
	bool inputs[16];
	int mappings[16];

	unsigned char keyState[256];

	void Read();
	int Initialise();
	void CleanUp();
	bool ReadKeyboard();
	void SetMapping(int index, int code);
	SimInput(int count);
	~SimInput();
};
