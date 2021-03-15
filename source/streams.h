#pragma once

#include <iostream>
#include <cstdint>
#include <vector>
#include <array>

#ifndef GET_BIT
#define GET_BIT(VAL, IDX) (((VAL) >> (IDX)) & 1)
#endif // !GET_BIT


class PngChunkStream
{
public:
	PngChunkStream(std::istream& pIn);
	// reads unsigned 32-bit integer, stored with MSB first
	uint32_t readU32();
	// reads unsigned 8-bit integer
	uint8_t readU8();
	// reads length and type of chunk
	void readChunkHeader(uint32_t& length, std::string& type);
	// reads length and type of next critical chunk, skipping
	// ancillary chunks
	void readNextCriticalChunkHeader(uint32_t& length, std::string& type);
	// use only inside IDAT chunk
	void get(uint8_t& c);
	void read(uint8_t* dest, uint16_t len);
	void finishCrcAndChunk();
private:
	std::istream& in;
	bool insideChunk = false;
	uint32_t length;
	std::string type;
	uint32_t bytesRead = 0;

	std::array<uint32_t, 256> crcTable;
	uint32_t crc = 0xFFFFFFFF;

	// the same as readU32 and readU8, but don't use byte in crc
	uint32_t _readU32();
	uint8_t _readU8();

	// gets byte and uses in in crc
	uint8_t getWithCrc();

	void computeCrcTable();
	void updateCrc(uint8_t val);
	void updateCrc(uint8_t* buf, uint32_t len);
	void restartCrc();

	void skipToNextIDATChunk();
};

inline uint8_t PngChunkStream::getWithCrc()
{
	uint8_t c = in.get();
	updateCrc(c);
	return c;
}

inline void PngChunkStream::updateCrc(uint8_t val)
{
	crc = crcTable[(crc ^ val) & 0xFF] ^ (crc >> 8);
}

inline void PngChunkStream::updateCrc(uint8_t* buf, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++)
	{
		crc = crcTable[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
	}
}


class DeflateBitStream
{
public:
	DeflateBitStream(PngChunkStream& pIn);
	int16_t read(size_t numOfBits);
	void finishByte();
	bool readBit();
private:
	PngChunkStream& in;
	uint8_t tempByte = 0;
	size_t remainingBits = 0;
};


class PngBitStream
{
public:
	PngBitStream(std::vector<uint8_t>::const_iterator& pIn, uint8_t pBitDepth, bool pUseScaling);
	uint8_t get();
private:
	std::vector<uint8_t>::const_iterator& in;
	const uint8_t bitDepth;
	const bool useScaling;
	uint8_t tempByte = 0;
	size_t remainingSamples = 0;
};