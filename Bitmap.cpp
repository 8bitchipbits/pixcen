/*
   Pixcen - A windows platform low level pixel editor for C64
   Copyright (C) 2013  John Hammarberg (crt@nospam.binarybone.com)
   
    This file is part of Pixcen.

    Pixcen is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Pixcen is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Pixcen.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "StdAfx.h"
#include "C64Interface.h"

Bitmap::Bitmap(int x, int y, int backbuffers)
{
	mode = BITMAP;

	if(x%8!=0)throw _T("X must be divisible by 8");
	if(y%8!=0)throw _T("Y must be divisible by 8");

	xsize=x;
	ysize=y;

	xcell=8;
	ycell=8;

	Create(y * (x/8), 0, (y/8) * (x/8), backbuffers);

	infouse[0].use = InfoUse::INFO_INDEX_LOW;
	infouse[0].pp = &screen;

	infouse[1].use = InfoUse::INFO_INDEX_HIGH;
	infouse[1].pp = &screen;

	DuplicateGlobalsToBackBuffers();
}

Bitmap::~Bitmap()
{
}

void Bitmap::GetSaveFormats(narray<autoptr<SaveFormat>,int> &fmt)
{
	__super::GetSaveFormats(fmt);

	if(xsize==320 && ysize == 200)
	{
		fmt.add(new SaveFormat(_T("Art Studio"),_T("art"),true));
		fmt.add(new SaveFormat(_T("Doodle"),_T("dd"),true));
		fmt.add(new SaveFormat(_T("Doodle compressed"),_T("jj"),true));
		fmt.add(new SaveFormat(_T("C64 Exe compressed"),_T("prg"),false));
		fmt.add(new SaveFormat(_T("C64 Exe"),_T("p00"),false));
		fmt.add(new SaveFormat(_T("Multipaint"), _T("bin"), true));
	}
}

void Bitmap::GetLoadFormats(narray<autoptr<SaveFormat>,int> &fmt)
{
	fmt.add(new SaveFormat(_T("Art Studio"),_T("art"),true,320,200,BITMAP));
	fmt.add(new SaveFormat(_T("Doodle"),_T("dd;ddl;jj"),true,320,200,BITMAP));
	fmt.add(new SaveFormat(_T("Multipaint"), _T("bin;binhi"), true, 320, 200, BITMAP));
}

nstr Bitmap::IdentifyFile(nmemfile &file)
{
	nstr ex;

	if (file.len() == 88000)
	{
		BYTE *ptr = file;
		static const BYTE cmp[] = { 0xFF,0x00,0x00,0x0F,0x28,0x00,0x19 };
		if (!memcmp(ptr + 1, cmp, 7))
		{
			ex = "binhi";
			return ex;
		}
	}

	unsigned short addr;

	file >> addr;

	if((file.len() == 9002 || file.len() == 9009)  && addr == 0x2000)
	{
		ex = _T("art");
	}
	else if(file.len() == 9218 && (addr == 0x2000 || addr == 0x1c00))
	{
		ex = _T("dd");
	}
	else if(addr == 0x2000)
	{
		BYTE buffer[9216];
		//Compressed doodle uses same RLE as compressed Koala
		if(MCBitmap::DecompressKoalaStream(((BYTE *)file)+2, int(file.len()-2), buffer, 9216) == 9216)
		{
			ex = _T("jj");
		}
	}

	return ex;
}


void Bitmap::Load(nmemfile &file, LPCTSTR type, int version)
{
	__super::Load(file,type,version);

	if(lstrcmpi(_T("art"),type)==0)
	{
		if(file.len() != 9002 && file.len() != 9009)
		{
			throw _T("Invalid Art Studio file size");
		}

		unsigned short addr;
		file >> addr;

		file.read(map, 8000);
		file.read(screen, 1000);

		*border = GuessBorderColor();
	}
	else if(lstrcmpi(_T("dd"),type)==0)
	{
		if(file.len() != 9218)
		{
			throw _T("Invalid Doodle file size");
		}

		unsigned short addr;
		file >> addr;

		file.read(screen, 1000);
		file.read(24);
		file.read(map, 8000);

		*border = GuessBorderColor();
	}
	else if(lstrcmpi(_T("jj"),type)==0)
	{
		BYTE buffer[9216];	//Decompress will skip PRG header
		if(MCBitmap::DecompressKoalaStream(((BYTE *)file)+2, int(file.len()-2), buffer, 9216)!=9216)
		{
			throw _T("Invalid Doodle stream");
		}

		memcpy(screen, buffer, 1000);
		memcpy(map, buffer+1024, 8000);

		*border = GuessBorderColor();
	}
	else if (lstrcmpi(_T("binhi"), type) == 0)
	{
		if (file.len() != 88000)
		{
			throw _T("Invalid Multipaint file size");
		}

		file.read(border, 1);

		// Skip to the bitmap
		file.setpos(0x0400);

		BYTE b;

		for (int y = 0; y < 200; y++)
		{
			for (int x = 0; x < 40; x++)
			{
				int mapindex = (y / 8) * 320 + y % 8 + x * 8;
				BYTE tmp = 0;
				for (int bit = 7; bit >= 0; bit--)
				{
					file >> b;
					tmp |= b << bit;
				}
				map[mapindex] = tmp;
			}
		}

		// Skip to the screen and color data
		file.setpos(0x10000);

		for (int y = 0; y < 25; y++)
		{
			for (int x = 0; x < 40; x++)
			{
				file >> b;
				screen[y*40+x] = b << 4;
			}

			//Skip 7 lines of duplicate data (space for FLI?)
			file.read(40 * 7);
		}

		for (int y = 0; y < 25; y++)
		{
			for (int x = 0; x < 40; x++)
			{
				file >> b;
				screen[y * 40 + x] |= b;
			}

			//Skip 7 lines of duplicate data (space for FLI?)
			file.read(40 * 7);
		}

	}
	else if(lstrcmpi(_T("raw"),type)==0)
	{
		size_t len = file.len();

		if(len % 8 != 0)
		{
			//Throw PRG header
			unsigned short tmp;
			file >> tmp;
			len -= 2;
		}

		xsize = 40*8;
		ysize = int(((len+319)/320)*8);

		Destroy();
		Create(ysize * (xsize/8), 0, (ysize/8) * (xsize/8));

		memset(screen,0xbc,(xsize/8) * (ysize/8));

		file.read(map, len);
	}

}


void Bitmap::Save(nmemfile &file, LPCTSTR type)
{
	__super::Save(file, type);

	if (lstrcmpi(_T("art"), type) == 0)
	{
		if (xsize != 320 || ysize != 200)
			throw _T("Buffers are not in standard hires format");

		file << unsigned short(0x2000);

		file.write(map, 8000);
		file.write(screen, 1000);
	}
	else if (lstrcmpi(_T("dd"), type) == 0)
	{
		if (xsize != 320 || ysize != 200)
			throw _T("Buffers are not in standard hires format");

		file << unsigned short(0x5c00);

		file.write(screen, 1000);
		for (int r = 0; r < 24; r++)file << BYTE(0);
		file.write(map, 8000);
		for (int r = 0; r < 192; r++)file << BYTE(0);
	}
	else if (lstrcmpi(_T("jj"), type) == 0)
	{
		if (xsize != 320 || ysize != 200)
			throw _T("Buffers are not in standard hires format");

		BYTE src[9216];
		ZeroMemory(src, sizeof(src));

		memcpy(src, screen, 1000);
		memcpy(src + 1024, map, 8000);

		BYTE buffer[9216 * 3];
		int size = MCBitmap::CompressKoalaStream(src, 9216, buffer, 9216 * 3);

		if (size == -1)
		{
			throw _T("Failed compressing Doodle");
		}

		file << unsigned short(0x5c00);
		file.write(buffer, size);
	}
	else if (lstrcmpi(_T("prg"), type) == 0)
	{
		if (xsize != 320 || ysize != 200)
			throw _T("Buffers are not in standard hires format");

		static unsigned char viewer[] = {
			0x00,0x1f,0x78,0xa9,0x3b,0x8d,0x11,0xd0,0xa9,0x08,0x8d,0x16,0xd0,0xa9,0x18,0x8d,
			0x18,0xd0,0xa2,0x00,0xbd,0x40,0x3f,0x9d,0x00,0x04,0xbd,0x40,0x40,0x9d,0x00,0x05,
			0xbd,0x40,0x41,0x9d,0x00,0x06,0xbd,0x40,0x42,0x9d,0x00,0x07,0xe8,0xd0,0xe5,0xad,
			0x28,0x43,0x8d,0x20,0xd0,0x4c,0x33,0x1f };

		file.write(viewer, sizeof(viewer));

		int pad = sizeof(viewer) - 2;	//minus prg header
		while (pad < 256)
		{
			file << BYTE(0);
			pad++;
		}

		file.write(map, 8000);
		file.write(screen, 1000);
		file << *border;

		B2File bbin, bbout;
		bbin.size = file.len();
		bbin.data = (byte *)file.detach();

		B2Crunch(&bbin, &bbout, 0x1f00);
		B2FreeFile(&bbin);

		file.attach(bbout.data, bbout.size, true);
	}
	else if (lstrcmpi(_T("p00"), type) == 0)
	{
		if (xsize != 320 || ysize != 200)
			throw _T("Buffers are not in standard hires format");

		static unsigned char viewer[] = {
			0x00,0x1f,0x78,0xa9,0x3b,0x8d,0x11,0xd0,0xa9,0x08,0x8d,0x16,0xd0,0xa9,0x18,0x8d,
			0x18,0xd0,0xa2,0x00,0xbd,0x40,0x3f,0x9d,0x00,0x04,0xbd,0x40,0x40,0x9d,0x00,0x05,
			0xbd,0x40,0x41,0x9d,0x00,0x06,0xbd,0x40,0x42,0x9d,0x00,0x07,0xe8,0xd0,0xe5,0xad,
			0x28,0x43,0x8d,0x20,0xd0,0x4c,0x33,0x1f };

		file.write(viewer, sizeof(viewer));

		int pad = sizeof(viewer) - 2;	//minus prg header
		while (pad < 256)
		{
			file << BYTE(0);
			pad++;
		}

		file.write(map, 8000);
		file.write(screen, 1000);
		file << *border;

		B2File bbin, bbout;
		bbin.size = file.len();
		bbin.data = (byte *)file.detach();

		file.attach(bbout.data, bbout.size, true);
	}
	else if (lstrcmpi(_T("bin"), type) == 0)
	{
		if (xsize != 320 || ysize != 200)
			throw _T("Buffers are not in standard hires format");

		file << *border;

		//Just to not confuse Multipaint in any way, write all unidentified metadata (some are settings) produced by Multipaint
		static const BYTE head[] = { 0xFF,0x00,0x00,0x0F,0x28,0x00,0x19 };
		file.write(head, sizeof(head));

		for (int r = 0; r < 4; r++)
			file << BYTE(0);

		file << BYTE(3);

		while (file.getpos() < 0x103)
			file << BYTE(0);

		static const BYTE meta[] = { 0xFF,0xFF,0xFF,0x68,0x37,0x2B,0x70,0xA4,0xB2,0x6F,0x3D,0x86,0x58,0x8D,0x43,0x35,0x28,0x79,0xB8,0xC7,0x6F,0x6F,0x4F,0x25,0x43,0x39,0x00,0x9A,0x67,0x59,0x44,0x44,0x44,0x6C,0x6C,0x6C,0x9A,0xD2,0x84,0x6C,0x5E,0xB5,0x95,0x95,0x95 };

		file.write(meta, sizeof(meta));

		while (file.getpos() < 0x400)
			file << BYTE(0);

		//Write bitmap
		for (int y = 0; y < 200; y++)
		{
			for (int x = 0; x < 40; x++)
			{
				int mapindex = (y / 8) * 320 + y % 8 + x * 8;
				BYTE tmp = map[mapindex];
				for (int bit = 7; bit >= 0; bit--)
				{
					file << BYTE((tmp >> bit) & 1);
				}
			}
		}

		// Fill up to screen and color data
		while (file.getpos() < 0x10000)
			file << BYTE(0);

		for (int y = 0; y < 25; y++)
		{
			for (int l = 0; l < 8; l++)
			{
				for (int x = 0; x < 40; x++)
				{
					file << BYTE(screen[y * 40 + x] >> 4);
				}
			}
		}

		for (int y = 0; y < 25; y++)
		{
			for (int l = 0; l < 8; l++)
			{
				for (int x = 0; x < 40; x++)
				{
					file << BYTE(screen[y * 40 + x] & 0xf);
				}
			}
		}

		// Fill up to end
		while (file.getpos() < 88000)
			file << BYTE(0);
	}
}

BYTE Bitmap::GetPixelInternal(int x, int y)
{
	ASSERT(x>=0 && x<xsize && y>=0 && y<ysize);

	int cx = x/8;
	int cy = y/8;

	int d=(map[cy * xsize + cx * 8 + (y%8)] >> (1*(7-(x%8)))) & 1;

	BYTE b;
	switch(d)
	{
	case 0:
		b = screen[cy * (xsize/8) + cx]  & 0x0f;
		break;
	case 1:
		b = screen[cy * (xsize/8) + cx] >> 4;
		break;
	}

	return b;
}

BYTE Bitmap::GetPixel(int x, int y)
{
	return GetPixelInternal(x, y);
}

void Bitmap::SetPixel(int x, int y, BYTE col)
{
	ASSERT(x>=0 && x<xsize && y>=0 && y<ysize);

	int cx = x/8;
	int cy = y/8;
	int ci = cy * (xsize/8) + cx;

	int mask = ResolveMask1(ci, col, GetMask(x,y));
	if(mask == -1)
		return;

	SetColor(ci, mask, col);

	int mi = cy * xsize + cx * 8 + (y%8);

	int filtermask = ~(1 << (1*(7-(x%8))));
	mask = mask << (1*(7-(x%8)));
	map[mi] = (map[mi] & filtermask) | mask;
}


void Bitmap::Import(CImage &img)
{
	ClearBackBuffer();

	{
		ImportHelper help(this, img, false);

		help.ReduceColors(2, NULL, 0);
	}

	*border = GuessBorderColor();
}

int Bitmap::GetMask(int x, int y)
{
	int cx = x/8;
	int cy = y/8;

	return (map[cy * xsize + cx * 8 + (y%8)] >> (1*(7-(x%8)))) & 1;
}

void Bitmap::SwapCellColours(int cx, int cy)
{
	int mi = GetMapIndexFromCell(cx, cy), t;
	for (t = 0; t < sizecell; t++)
		map[mi + t] = map[mi + t] ^ 0xFF; // invert it

	CellInfo info;
	GetCellInfo(cx, cy, 1, 1, &info);

	int oldColour0 = info.col[0];
	int oldColour1 = info.col[1];
	int ci = cy * (xsize / 8) + cx;
	SetColor(ci, 0, oldColour1);
	SetColor(ci, 1, oldColour0);
}
