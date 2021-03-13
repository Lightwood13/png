#pragma once

#include <istream>
#include <cstdint>

void readChunkHeader(std::istream& in, uint32_t& length, std::string& type);

class IDATStream
{
public:
	IDATStream(std::istream& pIn, uint32_t initialLength);
	void get(char& c);
	void read(char* dest, uint16_t len);
private:
	std::istream& in;
	uint32_t length;
	uint32_t bytesRead = 0;

	void skipToNextChunk();
};


class BitStream
{
public:
	BitStream(IDATStream& pIn);
	int16_t read(size_t numOfBits);
	void finishByte();
	bool readBit();
private:
	IDATStream& in;
	char tempByte = 0;
	size_t remainingBits = 0;
};