#pragma once

#include <cstdint>
#include <vector>
#include <istream>

#include "streams.h"

namespace Huffman
{
	struct Node
	{
		Node* leftChild = nullptr;
		Node* rightChild = nullptr;
		int16_t value = -1;

		Node() {};
		Node(int16_t v) : value(v) {};
		~Node()
		{
			if (leftChild != nullptr)
				delete leftChild;
			if (rightChild != nullptr)
				delete rightChild;
		}
	};

	void addNode(Node* root, int16_t path, size_t codeLength, int16_t value)
	{
		Node* current = root;
		for (int i = codeLength - 1; i >= 0; i--)
		{
			int16_t direction = (path >> i) & 1;
			if (direction == 0) // 0 - left
			{
				if (current->leftChild == nullptr)
					current->leftChild = new Node();
				current = current->leftChild;
			}
			else // 1 - right
			{
				if (current->rightChild == nullptr)
					current->rightChild = new Node();
				current = current->rightChild;
			}
		}
		current->value = value;
	}

	Node* createTree(const std::vector<size_t>& codeLengths)
	{
		Node* tree = new Node;

		std::vector<size_t> bl_count;
		for (size_t codeLength : codeLengths)
		{
			if (bl_count.size() < codeLength + 1)
				bl_count.insert(bl_count.end(), codeLength - bl_count.size() + 1, 0);
			bl_count[codeLength]++;
		}

		int16_t code = 0;
		bl_count[0] = 0;
		std::vector<int16_t> next_code(1, 0);
		for (size_t bits = 1; bits <= bl_count.size() - 1; bits++)
		{
			code = (code + bl_count[bits - 1]) << 1;
			next_code.push_back(code);
		}

		for (uint16_t n = 0; n < codeLengths.size(); n++)
		{
			size_t len = codeLengths[n];
			if (len != 0)
			{
				addNode(tree, next_code[len], len, n);
				next_code[len]++;
			}
		}

		return tree;
	}

	Node* createStaticTree()
	{
		std::vector<size_t> codeLengths;
		codeLengths.insert(codeLengths.end(), 144, 8); // 0-143
		codeLengths.insert(codeLengths.end(), 112, 9); // 144-255
		codeLengths.insert(codeLengths.end(), 24, 7); // 256-279
		codeLengths.insert(codeLengths.end(), 8, 8); // 280-287
		return createTree(codeLengths);
	}
}

namespace
{
	int16_t readCode(DeflateBitStream& r, Huffman::Node* tree)
	{
		Huffman::Node* current = tree;
		while (current->value == -1)
		{
			if (r.readBit() == false) // 0 - left
				current = current->leftChild;
			else // 1 - right
				current = current->rightChild;
		}
		return current->value;
	}

	int16_t decodeLength(DeflateBitStream& r, int16_t code)
	{
		if (code >= 257 && code <= 264)
			return code - 254;
		if (code >= 265 && code <= 268)
			return 11 + (code - 265) * 2 + r.read(1);
		if (code >= 269 && code <= 272)
			return 19 + (code - 269) * 4 + r.read(2);
		if (code >= 273 && code <= 276)
			return 35 + (code - 273) * 8 + r.read(3);
		if (code >= 277 && code <= 280)
			return 67 + (code - 277) * 16 + r.read(4);
		if (code >= 281 && code <= 284)
			return 131 + (code - 281) * 32 + r.read(5);
		if (code == 285)
			return 258;
	}

	int16_t decodeDistance(DeflateBitStream& r, int16_t code)
	{
		if (code >= 0 && code <= 3)
			return 1 + code;
		if (code >= 4 && code <= 29)
		{
			int16_t extraBits = code / 2 - 1;
			return (1 << extraBits) * (code - extraBits * 2) + 1
				+ r.read(extraBits);
		}
	}

	// returns number of codes read
	size_t decodeCodeLength(DeflateBitStream& r, int16_t code, std::vector<size_t>& codeLengths)
	{
		if (code >= 0 && code <= 15)
		{
			codeLengths.push_back(code);
			return 1;
		}
		else if (code == 16)
		{
			size_t repeat = 3 + r.read(2);
			size_t copiedValue = codeLengths.back();
			codeLengths.insert(codeLengths.end(), repeat, copiedValue);
			return repeat;
		}
		else if (code == 17)
		{
			size_t repeat = 3 + r.read(3);
			codeLengths.insert(codeLengths.end(), repeat, 0);
			return repeat;
		}
		else if (code == 18)
		{
			size_t repeat = 11 + r.read(7);
			codeLengths.insert(codeLengths.end(), repeat, 0);
			return repeat;
		}
	}

	void readDynamicTrees(DeflateBitStream& r, Huffman::Node*& literalTree, Huffman::Node*& distanceTree)
	{
		size_t HLIT = 257 + r.read(5);
		size_t HDIST = 1 + r.read(5);
		size_t HCLEN = 4 + r.read(4);

		int indices[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
		std::vector<size_t> codeLengths(19, 0);
		for (size_t i = 0; i < HCLEN; i++)
		{
			size_t codeLength = r.read(3);
			codeLengths[indices[i]] = codeLength;
		}

		Huffman::Node* tempTree = Huffman::createTree(codeLengths);

		std::vector<size_t> literalLengths;
		for (size_t i = 0; i < HLIT; i++)
		{
			int16_t code = readCode(r, tempTree);
			size_t codesRead = decodeCodeLength(r, code, literalLengths);
			i += codesRead - 1;
		}

		std::vector<size_t> distanceLengts;
		for (size_t i = 0; i < HDIST; i++)
		{
			int16_t code = readCode(r, tempTree);
			size_t codesRead = decodeCodeLength(r, code, distanceLengts);
			i += codesRead - 1;
		}

		delete tempTree;

		literalTree = Huffman::createTree(literalLengths);
		distanceTree = Huffman::createTree(distanceLengts);
	}
}

std::vector<uint8_t> FlateDecode(PngChunkStream& in)
{
	DeflateBitStream r(in);
	std::vector<uint8_t> res;

	//zlib
	uint8_t c;
	in.get(c); // CMF
	in.get(c); // FLG
	if (GET_BIT(c, 5) == 1)
		throw "zlib preset dictionary not supported";

	static Huffman::Node* staticTree = nullptr;
	bool lastBlock = false;
	while (!lastBlock) // iteration over blocks
	{
		lastBlock = r.readBit(); //BFINAL
		int16_t BTYPE = r.read(2);
		if (BTYPE == 0) // no compression
		{
			r.finishByte();
			uint16_t LEN = r.read(16);
			r.read(16); // NLEN
			res.resize(res.size() + LEN);
			in.read(res.data() + res.size() - LEN, LEN);
		}
		else // compressed
		{
			Huffman::Node* literalTree;
			Huffman::Node* distanceTree;
			if (BTYPE == 1) // static
			{
				if (staticTree == nullptr)
					staticTree = Huffman::createStaticTree();
				literalTree = staticTree;
				distanceTree = nullptr;
			}
			else // dynamic
				readDynamicTrees(r, literalTree, distanceTree);

			while (true) // iteration over codes
			{
				int16_t code = readCode(r, literalTree);
				if (code < 256) // literal byte
					res.push_back((uint8_t)code);
				else if (code == 256) // end of block
					break;
				else // length value
				{
					int16_t length = decodeLength(r, code);
					int16_t distance;
					if (distanceTree == nullptr)
						distance = decodeDistance(r, r.read(5));
					else
						distance = decodeDistance(r, readCode(r, distanceTree));

					res.resize(res.size() + length);
					size_t copyLength = (length < distance) ? length : distance;
					size_t num = length / copyLength;
					const std::vector<uint8_t>::iterator copyStart = res.end() - length - distance;
					std::vector<uint8_t>::iterator it = res.end() - length;
					for (size_t i = 0; i < num; i++)
					{
						if (i != 0)
							it += copyLength;
						std::copy(copyStart, copyStart + copyLength, it);
					}
					if (length % copyLength != 0)
						std::copy(copyStart, copyStart + length % copyLength, it + copyLength);
				}
			}
			if (literalTree != staticTree)
				delete literalTree;
			if (distanceTree != nullptr)
				delete distanceTree;
		}

	}

	// zlib ADLER-32
	uint8_t temp[4];
	in.read(temp, 4);

	return res;
}