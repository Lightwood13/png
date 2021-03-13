#include <SFML/Graphics.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <array>
#include <vector>
#include <map>
#include <algorithm>

#include "deflate.h"



int16_t abs(int16_t a)
{
	if (a < 0)
		return -a;
	else
		return a;
}


// reads unsigned 32-bit integer, stored with MSB first
uint32_t readU32(std::istream& in)
{
	uint32_t a = in.get() << 24;
	a = a | ((in.get() & 0xFF) << 16);
	a = a | ((in.get() & 0xFF) << 8);
	a = a | (in.get() & 0xFF);
	return a;
}

// reads unsigned 8-bit integer
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

// reads length and type of next critical chunk, skipping
// ancillary chunks
void readNextCriticalChunkHeader(std::istream& in, uint32_t& length, std::string& type)
{
	readChunkHeader(in, length, type);
	// if chunk is ancillary
	while (GET_BIT(type[0], 5) == 1)
	{
		std::clog << "Skipped ancillary chunk: " << type << std::endl;
		in.seekg(length + 4, std::ios_base::cur); // skip chunk data and crc
		readChunkHeader(in, length, type);
	}
}

// reads IHDR chunk. If it's not present, throws an error.
// Checks if all fields have valid values
void readChunkIHDR(std::istream& in, uint32_t& width, uint32_t& height,
	uint8_t& bitDepth, uint8_t& colourType)
{
	uint32_t length;
	std::string type;
	readChunkHeader(in, length, type);
	if (length != 13 || type != "IHDR")
		throw "error reading IHDR";

	width = readU32(in);
	height = readU32(in);
	if (width == 0 || height == 0)
		throw "zero image dimension";
	std::clog << "Dimensions: " << height << " x " << width << std::endl;
	
	bitDepth = readU8(in);
	colourType = readU8(in);
	uint8_t compressionMethod = readU8(in);
	uint8_t filterMethod = readU8(in);
	uint8_t interlaceMethod = readU8(in);

	static constexpr std::array<uint8_t, 5> allowedColourTypes = { 0, 2, 3, 4, 6 };
	if (std::find(allowedColourTypes.begin(), allowedColourTypes.end(), colourType)
		== allowedColourTypes.end())
		throw "invalid colour type";
	static const std::map<uint8_t, std::vector<uint8_t>> allowedBitDepths =
	{
		{0, std::vector<uint8_t>{1, 2, 4, 8, 16}},
		{2, std::vector<uint8_t>{8, 16}},
		{3, std::vector<uint8_t>{1, 2, 4, 8}},
		{4, std::vector<uint8_t>{8, 16}},
		{6, std::vector<uint8_t>{8, 16}}
	};
	if (std::find(allowedBitDepths.at(colourType).begin(), allowedBitDepths.at(colourType).end(), bitDepth)
		== allowedBitDepths.at(colourType).end())
		throw "invalid bit depth";
	if (compressionMethod != 0)
		throw "invalid compression method";
	if (filterMethod != 0)
		throw "invalid filter method";
	if (interlaceMethod > 1)
		throw "invalid interlace method";
	static const std::map<uint8_t, std::string> colourTypesNames =
		{ {0, "greyscale"}, {2, "truecolour"}, {3, "indexed-colour"},
		{4, "greyscale with alpha"}, {6, "truecolour with alpha"} };
	std::clog << "Colour type: " << colourTypesNames.at(colourType)
		<< ", bit depth: " << static_cast<int>(bitDepth)
		<< ", interlace used: " << (interlaceMethod ? "yes" : "no")
		<< std::endl << std::endl;

	in.seekg(4, std::ios_base::cur); // skip crc for now
}

// removes filter from a single byte
inline uint8_t reconstructByte(uint8_t x, uint8_t a, uint8_t b, uint8_t c, const uint8_t filterMethod)
{
	if (filterMethod == 0)
		return x;
	else if (filterMethod == 1)
		return x + a;
	else if (filterMethod == 2)
		return x + b;
	else if (filterMethod == 3)
		return x + (a + b) / 2;
	else
	{
		int16_t p = a + b - c;
		int16_t pa = abs(p - a);
		int16_t pb = abs(p - b);
		int16_t pc = abs(p - c);

		if (pa <= pb && pa <= pc)
			return x + a;
		else if (pb <= pc)
			return x + b;
		else
			return x + c;
	}
}

// removes filter from a single scanline
void reconstructScanline(std::vector<uint8_t>::iterator& filteredData, uint32_t distBetweenCorrBytes,
	std::vector<uint8_t>& byteLine, const std::vector<uint8_t>& prevByteLine)
{
	const uint8_t filterMethod = (*filteredData++);
	if (filterMethod < 0 || filterMethod > 4)
		throw "invalid filter method";
	
	for (uint32_t i = 0; i < prevByteLine.size(); i++)
	{
		uint8_t a = 0;
		uint8_t b = prevByteLine[i];
		uint8_t c = 0;
		if (i >= distBetweenCorrBytes)
		{
			a = byteLine[i - distBetweenCorrBytes];
			c = prevByteLine[i - distBetweenCorrBytes];
		}
		byteLine[i] = reconstructByte((*filteredData++), a, b, c, filterMethod);
	}
}

// convert byte line to line of RGBA pixels
void byteLineToPixelLine(const std::vector<uint8_t>& byteLine, std::vector<uint8_t>::iterator& dest,
	uint32_t width, uint8_t bitDepth, uint8_t colourType)
{
	std::vector<uint8_t>::const_iterator it = byteLine.begin();
	PngBitStream bytes(it, bitDepth);
	for (uint32_t i = 0; i < width; i++)
	{
		uint8_t r, g, b, a;
		if (colourType == 2) // truecolour
		{
			r = bytes.get(); g = bytes.get(); b = bytes.get();
			a = 255;
		}
		else if (colourType == 6) // truecolour with alpha
		{
			r = bytes.get(); g = bytes.get(); b = bytes.get();
			a = bytes.get();
		}
		else if (colourType == 0) // greyscale
		{
			uint8_t sample = bytes.get();
			r = sample; g = sample; b = sample;
			a = 255;
		}
		else if (colourType == 3) // palette
			throw "indexed-coloured images are not supported for now";
		else // greyscale with alpha
		{
			uint8_t sample = bytes.get();
			r = sample; g = sample; b = sample;
			a = bytes.get();
		}
		*(dest++) = r;
		*(dest++) = g;
		*(dest++) = b;
		*(dest++) = a;
	}
}

std::vector<uint8_t> removeFilter(std::vector<uint8_t>::iterator& filteredData,
	uint32_t width, uint32_t height, uint8_t bitDepth, uint8_t colourType)
{
	uint32_t samplesPerPixel;
	if (colourType == 0 || colourType == 3) // greyscale or palette
		samplesPerPixel = 1;
	else if (colourType == 4) // greyscale with alpha
		samplesPerPixel = 2;
	else if (colourType == 2) // truecolour
		samplesPerPixel = 3;
	else // truecolour with alpha
		samplesPerPixel = 4;
	uint32_t byteLineLength = width * samplesPerPixel * bitDepth / 8;
	if (byteLineLength * 8 != width * bitDepth * samplesPerPixel)
		byteLineLength++;
	// distance between current byte and corresponding byte in previous pixel
	// (1 if bitDepth is less than 8)
	uint32_t distBetweenCorrBytes = 1;
	if (bitDepth >= 8)
		distBetweenCorrBytes = samplesPerPixel * bitDepth / 8;

	// reconstructed byte lines
	std::vector<uint8_t> byteLine1(byteLineLength, 0);
	std::vector<uint8_t> byteLine2(byteLineLength, 0);
	std::vector<uint8_t> res(height * width * 4);
	std::vector<uint8_t>::iterator dest = res.begin();

	for (uint32_t i = 0; i < height; i++)
	{
		if (i % 2 == 0)
		{
			reconstructScanline(filteredData, distBetweenCorrBytes, byteLine1, byteLine2);
			byteLineToPixelLine(byteLine1, dest, width, bitDepth, colourType);
		}
		else
		{
			reconstructScanline(filteredData, distBetweenCorrBytes, byteLine2, byteLine1);
			byteLineToPixelLine(byteLine2, dest, width, bitDepth, colourType);
		}
	}

	return res;
}

std::vector<uint8_t> decodePng(std::istream& in, uint32_t& width, uint32_t& height)
{
	readSignature(in);
	uint8_t bitDepth;
	uint8_t colourType;
	readChunkIHDR(in, width, height, bitDepth, colourType);

	uint32_t length;
	std::string type;
	readNextCriticalChunkHeader(in, length, type);
	if (type == "IEND")
		throw "image data not present";
	else if (type == "PLTE")
		throw "indexed-coloured images are not supported for now";
	else if (type != "IDAT")
		throw "unknown critical chunk";
	IDATStream idat(in, length);
	std::vector<uint8_t> filteredImageData = FlateDecode(idat);
	std::vector<uint8_t>::iterator it = filteredImageData.begin();
	idat.close();

	readNextCriticalChunkHeader(in, length, type);
	if (type != "IEND")
		throw "end chunk not found";

	std::vector<uint8_t> res = removeFilter(it, width, height, bitDepth, colourType);
	std::clog << "Image decoding finished successfully" << std::endl;

	return res;
}

int main(int argc, char** argv)
{
	std::string filename = "test3.png";
	if (argc > 1)
		filename = std::string(argv[1]);

	std::ifstream in(filename, std::ios_base::binary);
	if (!in.is_open())
	{
		std::clog << "File not found" << std::endl;
		return 0;
	}
	uint32_t width, height;
	std::vector<uint8_t> buffer = decodePng(in, width, height);

	sf::RenderWindow window(sf::VideoMode(width, height), filename);

	sf::Texture t;
	t.create(width, height);
	sf::Sprite sprite(t);
	t.update(buffer.data());

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
		}

		window.clear();
		window.draw(sprite);
		window.display();
	}

	return 0;
}