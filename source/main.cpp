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
void readChunkIHDR(std::istream& in, uint32_t& width, uint32_t& height)
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
	
	uint8_t bitDepth = readU8(in);
	uint8_t colourType = readU8(in);
	uint8_t compressionMethod = readU8(in);
	uint8_t filterMethod = readU8(in);
	uint8_t interlaceMethod = readU8(in);

	static constexpr std::array<uint8_t, 5> allowedColourTypes = { 0, 2, 3, 4, 6 };
	if (std::find(allowedColourTypes.begin(), allowedColourTypes.end(), colourType)
		== allowedColourTypes.end())
		throw "invalid colour type";
	static const std::array<std::vector<uint8_t>, 5> allowedBitDepths =
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
	static const std::map<uint8_t, std::string> colourTypesNames =
		{ {0, "greyscale"}, {2, "truecolour"}, {3, "indexed-colour"},
		{4, "greyscale with alpha"}, {6, "truecolour with alpha"} };
	std::clog << "Colour type: " << colourTypesNames.at(colourType)
		<< ", bit depth: " << static_cast<int>(bitDepth)
		<< ", interlace used: " << (interlaceMethod ? "yes" : "no")
		<< std::endl << std::endl;

	in.seekg(4, std::ios_base::cur); // skip crc for now
}

std::vector<uint8_t> decodePng(std::istream& in, uint32_t& width, uint32_t& height)
{
	readSignature(in);
	readChunkIHDR(in, width, height);

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
	std::string filteredImageData = FlateDecode(idat);

	std::istringstream imageDataStream(std::move(filteredImageData));
	std::vector<uint8_t> line((width + 1) * 3, 0);
	std::vector<uint8_t> prevLine((width + 1) * 3, 0);
	std::vector<uint8_t> res(height * width * 4);
	size_t resPos = 0;
	for (uint32_t i = 0; i < height; i++)
	{
		char filterMethod = imageDataStream.get();
		if (filterMethod < 0 || filterMethod > 4)
			throw "invalid filter method";
		imageDataStream.read(reinterpret_cast<char*>(line.data()) + 3, width * 3);
		if (filterMethod == 1)
			for (uint32_t j = 3; j < 3 * (width + 1); j++)
				line[j] += line[j - 3];
		else if (filterMethod == 2)
			for (uint32_t j = 3; j < 3 * (width + 1); j++)
				line[j] += prevLine[j];
		else if (filterMethod == 3)
			for (uint32_t j = 3; j < 3 * (width + 1); j++)
				line[j] += (line[j - 3] + prevLine[j]) / 2;
		else if (filterMethod == 4)
			for (uint32_t j = 3; j < 3 * (width + 1); j++)
			{
				unsigned char a = line[j - 3];
				unsigned char b = prevLine[j];
				unsigned char c = prevLine[j - 3];
				int16_t p = a + b - c;
				int16_t pa = abs(p - a);
				int16_t pb = abs(p - b);
				int16_t pc = abs(p - c);

				unsigned char res;
				if (pa <= pb && pa <= pc)
					res = a;
				else if (pb <= pc)
					res = b;
				else
					res = c;
				line[j] += res;
			}
		for (uint32_t i = 0; i < width; i++, resPos += 4)
		{
			res[resPos] = line[(i + 1) * 3];
			res[resPos + 1] = line[(i + 1) * 3 + 1];
			res[resPos + 2] = line[(i + 1) * 3 + 2];
			res[resPos + 3] = 255;
		}
		std::copy(line.begin(), line.end(), prevLine.begin());
	}

	return res;

	/*std::ofstream out("out.txt");
	out << width << " " << height << std::endl;
	for (const auto& line : res)
	{
		for (unsigned char c : line)
			out << (unsigned int)c << " ";
		out << std::endl;
	}*/
}

int main(int argc, char** argv)
{
	std::string filename = "test.png";
	if (argc > 1)
		filename = std::string(argv[1]);

	std::ifstream in("test.png", std::ios_base::binary);
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