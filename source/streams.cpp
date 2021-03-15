#include "streams.h"

PngChunkStream::PngChunkStream(std::istream& pIn) : in(pIn)
{
	computeCrcTable();
}

uint32_t PngChunkStream::_readU32()
{
	uint32_t a = in.get() << 24;
	a = a | ((in.get() & 0xFF) << 16);
	a = a | ((in.get() & 0xFF) << 8);
	a = a | (in.get() & 0xFF);
	return a;
}

uint8_t PngChunkStream::_readU8()
{
	return static_cast<uint8_t>(in.get());
}

uint32_t PngChunkStream::readU32()
{
	uint32_t a = getWithCrc() << 24;
	a = a | ((getWithCrc() & 0xFF) << 16);
	a = a | ((getWithCrc() & 0xFF) << 8);
	a = a | (getWithCrc() & 0xFF);
	return a;
}

uint8_t PngChunkStream::readU8()
{
	return getWithCrc();
}

void PngChunkStream::readChunkHeader(uint32_t& length, std::string& type)
{
	if (insideChunk)
		throw "tried to read next chunk while inside another chunk";
	length = _readU32();
	type.resize(4);
	in.read(type.data(), 4);
	updateCrc(reinterpret_cast<uint8_t*>(type.data()), 4);
	this->length = length;
	this->type = type;
	insideChunk = true;
}

void PngChunkStream::readNextCriticalChunkHeader(uint32_t& length, std::string& type)
{
	readChunkHeader(length, type);
	// if chunk is ancillary
	while (GET_BIT(type[0], 5) == 1)
	{
		std::clog << "Skipped ancillary chunk: " << type << std::endl;
		in.seekg(length + 4, std::ios_base::cur); // skip chunk data and crc
		restartCrc();
		insideChunk = false;
		readChunkHeader(length, type);
	}
}

void PngChunkStream::skipToNextIDATChunk()
{
	do
	{
		finishCrcAndChunk();
		readChunkHeader(length, type);
		if (type != "IDAT")
			throw "unexpected end of image data";
	} while (length == 0);
}

void PngChunkStream::get(uint8_t& c)
{
	if (!insideChunk)
		throw "tried to read byte outside of chunk";
	if (bytesRead == length)
	{
		skipToNextIDATChunk();
		bytesRead = 0;
	}
	bytesRead++;
	c = in.get();
	updateCrc(c);
}

void PngChunkStream::read(uint8_t* dest, uint16_t len)
{
	if (bytesRead == length)
	{
		skipToNextIDATChunk();
		bytesRead = 0;
	}
	while (len > length - bytesRead)
	{
		in.read(reinterpret_cast<char*>(dest), length - bytesRead);
		updateCrc(dest, length - bytesRead);
		dest += length - bytesRead;
		len -= length - bytesRead;
		skipToNextIDATChunk();
		bytesRead = 0;
	}
	if (len != 0)
	{
		in.read(reinterpret_cast<char*>(dest), len);
		updateCrc(dest, len);
	}
}

void PngChunkStream::computeCrcTable()
{
	for (uint32_t i = 0; i < 256; i++)
	{
		uint32_t c = i;
		for (uint32_t k = 0; k < 8; k++)
		{
			if (c & 1)
				c = 0xEDB88320 ^ (c >> 1);
			else
				c = c >> 1;
		}
		crcTable[i] = c;
	}
}

void PngChunkStream::restartCrc()
{
	crc = 0xFFFFFFFF;
}

void PngChunkStream::finishCrcAndChunk()
{
	if (~crc != _readU32())
		throw "crc mismatch";

	std::clog << type << " chunk: crc good" << std::endl;

	crc = 0xFFFFFFFF;

	insideChunk = false;
}


DeflateBitStream::DeflateBitStream(PngChunkStream& pIn) : in(pIn) {};

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


PngBitStream::PngBitStream(std::vector<uint8_t>::const_iterator& pIn, uint8_t pBitDepth, bool pUseScaling)
	: in(pIn), bitDepth(pBitDepth), useScaling(pUseScaling) {};

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
	if (useScaling)
	{
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
	}
	else
	{
		if (bitDepth == 4)
			res = (tempByte >> 4) & 0b1111;
		else if (bitDepth == 2)
			res = (tempByte >> 6) & 0b11;
		else
			res = (tempByte >> 7) & 1;
	}
	tempByte <<= bitDepth;
	return res;
}