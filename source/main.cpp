#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <array>
#include <vector>
#include <map>
#include <algorithm>

// read unsigned 32-bit integer, stored with MSB first
uint32_t readU32(std::istream& in)
{
	uint32_t a = in.get() << 24;
	a = a | ((in.get() & 0xFF) << 16);
	a = a | ((in.get() & 0xFF) << 8);
	a = a | (in.get() & 0xFF);
	return a;
}

// read unsigned 8-bit integer
uint8_t readU8(std::istream& in)
{
	return static_cast<uint8_t>(in.get());
}

// reads file signature. If it's corrupted, throws an error
void readSignature(std::istream& in)
{
	std::string s(8, 0);
	std::string signature = { -119, 80, 78, 71, 13, 10, 26, 10 };
	in.read(s.data(), 8);
	if (s != signature)
		throw "file signature is incorrect";
}

// reads length and type of chunk
void readChunkHeader(std::istream& in, uint32_t& length, std::string& type)
{
	length = readU32(in);
	type.resize(4);
	in.read(type.data(), 4);
}

// reads IHDR chunk. If it's not present, throws an error
void readChunkIHDR(std::istream& in)
{
	uint32_t length;
	std::string type;
	readChunkHeader(in, length, type);
	if (length != 13 || type != "IHDR")
		throw "error reading IHDR";

	uint32_t width = readU32(in);
	uint32_t height = readU32(in);
	if (width == 0 || height == 0)
		throw "zero image dimension";
	std::clog << "Dimensions: " << width << " x " << height << std::endl;
	
	uint8_t bitDepth = readU8(in);
	uint8_t colourType = readU8(in);
	uint8_t compressionMethod = readU8(in);
	uint8_t filterMethod = readU8(in);
	uint8_t interlaceMethod = readU8(in);

	constexpr std::array<uint8_t, 5> allowedColourTypes = { 0, 2, 3, 4, 6 };
	if (std::find(allowedColourTypes.begin(), allowedColourTypes.end(), colourType)
		== allowedColourTypes.end())
		throw "invalid colour type";
	const std::array<std::vector<uint8_t>, 5> allowedBitDepths =
	{
		std::vector<uint8_t>{1, 2, 4, 8, 16},
		std::vector<uint8_t>{8, 16},
		std::vector<uint8_t>{1, 2, 4, 8},
		std::vector<uint8_t>{8, 16},
		std::vector<uint8_t>{8, 16}
	};
	if (std::find(allowedBitDepths[colourType].begin(), allowedBitDepths[colourType].end(), bitDepth)
		== allowedBitDepths[colourType].end())
		throw "invalid bit depth";
	if (compressionMethod != 0)
		throw "invalid compression method";
	if (filterMethod != 0)
		throw "invalid filter method";
	if (interlaceMethod > 1)
		throw "invalid interlace method";
	const std::map<uint8_t, std::string> colourTypesNames =
		{ {0, "greyscale"}, {2, "truecolour"}, {3, "indexed-colour"},
		{4, "greyscale with alpha"}, {6, "truecolour with alpha"} };
	std::clog << "Colour type: " << colourTypesNames.at(colourType)
		<< ", bit depth: " << static_cast<int>(bitDepth)
		<< ", interlace used: " << (interlaceMethod ? "yes" : "no")
		<< std::endl;
}

void decodePng(std::istream& in)
{
	readSignature(in);
	readChunkIHDR(in);
}

int main()
{
	std::ifstream in("test.png", std::ios_base::binary);
	decodePng(in);
	return 0;
}