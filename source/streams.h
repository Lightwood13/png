#pragma once

#include <istream>
#include <cstdint>
#include <vector>

void readChunkHeader(std::istream& in, uint32_t& length, std::string& type);

class IDATStream
{
public:
	IDATStream(std::istream& pIn, uint32_t initialLength);
	void get(uint8_t& c);
	void read(uint8_t* dest, uint16_t len);
	void close();
private:
	std::istream& in;
	uint32_t length;
	uint32_t bytesRead = 0;

	void skipToNextChunk();
};


class DeflateBitStream
{
public:
	DeflateBitStream(IDATStream& pIn);
	int16_t read(size_t numOfBits);
	void finishByte();
	bool readBit();
private:
	IDATStream& in;
	uint8_t tempByte = 0;
	size_t remainingBits = 0;
};


class PngBitStream
{
public:
	PngBitStream(std::vector<uint8_t>::const_iterator& pIn, uint8_t pBitDepth);
	uint8_t get();
private:
	std::vector<uint8_t>::const_iterator& in;
	const uint8_t bitDepth;
	uint8_t tempByte = 0;
	size_t remainingSamples = 0;
};