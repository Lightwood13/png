#include "streams.h"

IDATStream::IDATStream(std::istream& pIn, uint32_t initialLength)
	: in(pIn), length(initialLength) {}

void IDATStream::get(uint8_t& c)
{
	if (bytesRead == length)
	{
		skipToNextChunk();
		bytesRead = 0;
	}
	bytesRead++;
	c = in.get();
}

void IDATStream::read(uint8_t* dest, uint16_t len)
{
	if (bytesRead == length)
	{
		skipToNextChunk();
		bytesRead = 0;
	}
	while (len > length - bytesRead)
	{
		in.read(reinterpret_cast<char*>(dest), length - bytesRead);
		dest += length - bytesRead;
		len -= length - bytesRead;
		skipToNextChunk();
		bytesRead = 0;
	}
	if (len != 0)
		in.read(reinterpret_cast<char*>(dest), len);
}

void IDATStream::close()
{
	char temp[4];
	in.read(temp, 4); // skip crc
}

void IDATStream::skipToNextChunk()
{
	std::string type;
	do
	{
		in.seekg(4, std::ios_base::cur); // skip crc
		readChunkHeader(in, length, type);
		if (type != "IDAT")
			throw "unexpected end of image data";
	} while (length == 0);
}


DeflateBitStream::DeflateBitStream(IDATStream& pIn) : in(pIn) {};

int16_t DeflateBitStream::read(size_t numOfBits)
{
	if (remainingBits == 0)
	{
		in.get(tempByte);
		remainingBits = 8;
	}
	int16_t res;
	if (numOfBits <= remainingBits)
	{
		res = ((uint8_t)(tempByte << (remainingBits - numOfBits))) >> (8 - numOfBits);
		remainingBits -= numOfBits;
	}
	else
	{
		res = ((uint8_t)tempByte) >> (8 - remainingBits);
		in.get(tempByte);
		if (numOfBits - remainingBits <= 8)
		{
			res += ((int16_t)(uint8_t)(tempByte << (8 - (numOfBits - remainingBits))))
				>> (8 - (numOfBits - remainingBits)) << remainingBits;
			remainingBits = 8 - (numOfBits - remainingBits);
		}
		else
		{
			res += ((int16_t)(uint8_t)tempByte) << remainingBits;
			in.get(tempByte);
			res += ((int16_t)(uint8_t)(tempByte << (8 - (numOfBits - remainingBits - 8))))
				>> (8 - (numOfBits - remainingBits - 8)) << (remainingBits + 8);
			remainingBits = 8 - (numOfBits - remainingBits - 8);
		}
	}
	return res;
}

void DeflateBitStream::finishByte()
{
	remainingBits = 0;
}

bool DeflateBitStream::readBit()
{
	if (remainingBits == 0)
	{
		in.get(tempByte);
		remainingBits = 8;
	}
	return (tempByte >> (8 - remainingBits--)) & 1;
}


PngBitStream::PngBitStream(std::vector<uint8_t>::const_iterator& pIn, uint8_t pBitDepth)
	: in(pIn), bitDepth(pBitDepth) {};

uint8_t PngBitStream::get()
{
	if (bitDepth == 8)
		return *(in++);
	else if (bitDepth == 16)
	{
		uint8_t res = *(in++);
		in++;
		return res;
	}
	
	if (remainingSamples == 0)
	{
		tempByte = *(in++);
		remainingSamples = 8 / bitDepth;
	}
	remainingSamples--;
	uint8_t res;
	if (bitDepth == 4)
	{
		res = tempByte & 0b11110000;
		if (res >= 128)
			res |= 0b1111;
	}
	else if (bitDepth == 2)
	{
		res = tempByte & 0b11000000;
		if (res >= 128)
			res |= 0b111111;
	}
	else
	{
		res = tempByte & 0b10000000;
		if (res >= 128)
			res |= 0b1111111;
	}
	tempByte <<= bitDepth;
	return res;
}