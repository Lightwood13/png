#include "streams.h"

IDATStream::IDATStream(std::istream& pIn, uint32_t initialLength)
	: in(pIn), length(initialLength) {}

void IDATStream::get(char& c)
{
	if (bytesRead == length)
	{
		skipToNextChunk();
		bytesRead = 0;
	}
	bytesRead++;
	in.get(c);
}

void IDATStream::read(char* dest, uint16_t len)
{
	if (bytesRead == length)
	{
		skipToNextChunk();
		bytesRead = 0;
	}
	while (len > length - bytesRead)
	{
		in.read(dest, length - bytesRead);
		dest += length - bytesRead;
		len -= length - bytesRead;
		skipToNextChunk();
		bytesRead = 0;
	}
	if (len != 0)
		in.read(dest, len);
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


BitStream::BitStream(IDATStream& pIn) : in(pIn) {};

int16_t BitStream::read(size_t numOfBits)
{
	if (remainingBits == 0)
	{
		in.get(tempByte);
		remainingBits = 8;
	}
	int16_t res;
	if (numOfBits <= remainingBits)
	{
		res = ((unsigned char)(tempByte << (remainingBits - numOfBits))) >> (8 - numOfBits);
		remainingBits -= numOfBits;
	}
	else
	{
		res = ((unsigned char)tempByte) >> (8 - remainingBits);
		in.get(tempByte);
		if (numOfBits - remainingBits <= 8)
		{
			res += ((int16_t)(unsigned char)(tempByte << (8 - (numOfBits - remainingBits))))
				>> (8 - (numOfBits - remainingBits)) << remainingBits;
			remainingBits = 8 - (numOfBits - remainingBits);
		}
		else
		{
			res += ((int16_t)(unsigned char)tempByte) << remainingBits;
			in.get(tempByte);
			res += ((int16_t)(unsigned char)(tempByte << (8 - (numOfBits - remainingBits - 8))))
				>> (8 - (numOfBits - remainingBits - 8)) << (remainingBits + 8);
			remainingBits = 8 - (numOfBits - remainingBits - 8);
		}
	}
	return res;
}

void BitStream::finishByte()
{
	remainingBits = 0;
}

bool BitStream::readBit()
{
	if (remainingBits == 0)
	{
		in.get(tempByte);
		remainingBits = 8;
	}
	return (tempByte >> (8 - remainingBits--)) & 1;
}