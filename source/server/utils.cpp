
#include "utils.h"
#include "logger.h"
#include <string>
#include <vector>

#include <stdlib.h>
#include <cstdio>
#include <iostream> 
#include <locale> 


using namespace std;

void tokenize(const std::string& str,
				std::vector<std::string>& tokens,
				const std::string& delimiters)
{
	STACKLOG;
	// Skip delimiters at beginning.
	std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
	// Find first "non-delimiter".
	std::string::size_type pos     = str.find_first_of(delimiters, lastPos);
	while (std::string::npos != pos || std::string::npos != lastPos)
	{
		// Found a token, add it to the vector.
		tokens.push_back(str.substr(lastPos, pos - lastPos));
		// Skip delimiters.  Note the "not_of"
		lastPos = str.find_first_not_of(delimiters, pos);
		// Find next "non-delimiter"
		pos = str.find_first_of(delimiters, lastPos);
	}
}

//! strict_tokenize treats the provided delimiter as the entire delimiter
//! with tokenize, the string "  " would result in an empty vector.
//! with strict_tokenize the same string would result in a vector with
//! a single empty string.
void strict_tokenize(const std::string& str,
				std::vector<std::string>& tokens,
				const std::string& delimiter)
{
	size_t prev_loc = 0, new_loc = str.find( delimiter, prev_loc );
	
	while( new_loc < str.length() && prev_loc < str.length() ) {
		tokens.push_back( str.substr( prev_loc, new_loc - prev_loc ) );
		prev_loc = ( new_loc > str.length() ? new_loc : new_loc + delimiter.length() );
		new_loc = str.find( delimiter, prev_loc ); 
	}
	
	tokens.push_back( str.substr( prev_loc, new_loc - prev_loc ) );
}

std::string trim(const std::string& str )
{
	STACKLOG;
	if(!str.size()) return str;
	return str.substr( str.find_first_not_of(" \t"), str.find_last_not_of(" \t")+1);
}

std::string hexdump(void *pAddressIn, long  lSize)
{
	char szBuf[100];
	long lIndent = 1;
	long lOutLen, lIndex, lIndex2, lOutLen2;
	long lRelPos;
	struct { char *pData; unsigned long lSize; } buf;
	unsigned char *pTmp,ucTmp;
	unsigned char *pAddress = (unsigned char *)pAddressIn;

	buf.pData   = (char *)pAddress;
	buf.lSize   = lSize;

	std::string result = "";
	
	while (buf.lSize > 0)
	{
		pTmp     = (unsigned char *)buf.pData;
		lOutLen  = (int)buf.lSize;
		if (lOutLen > 16)
		  lOutLen = 16;

		// create a 64-character formatted output line:
		sprintf(szBuf, " >                            "
					 "                      "
					 "    %08lX", (long unsigned int)(pTmp-pAddress));
		lOutLen2 = lOutLen;

		for(lIndex = 1+lIndent, lIndex2 = 53-15+lIndent, lRelPos = 0;
		  lOutLen2;
		  lOutLen2--, lIndex += 2, lIndex2++
			)
		{
			ucTmp = *pTmp++;

			sprintf(szBuf + lIndex, "%02X ", (unsigned short)ucTmp);
			if(!isprint(ucTmp))  ucTmp = '.'; // nonprintable char
			szBuf[lIndex2] = ucTmp;

			if (!(++lRelPos & 3))     // extra blank after 4 bytes
			{  lIndex++; szBuf[lIndex+2] = ' '; }
		}

		if (!(lRelPos & 3)) lIndex--;

		szBuf[lIndex  ]   = '<';
		szBuf[lIndex+1]   = ' ';

		result += std::string(szBuf) + "\n";

		buf.pData   += lOutLen;
		buf.lSize   -= lOutLen;
	}
	return result;
}

// Calculates the length of an integer
int intlen(int num)
{
	int length = 0;
	
	if(num<0)
	{
		num = num*(-1);
		length = 1; // set on 1 because of the minus sign
	}

	while(num > 0) {
		length++;
		num = num/10;
	}
	return length;
}

// converts a wstring to a string 
std::string narrow(const std::wstring& wcs) 
{ 
	std::vector<char> mbs(wcs.length()); 
	wcstombs(&mbs[0], wcs.c_str(), wcs.length()); 

	std::string str = std::string(mbs.begin(), mbs.end());
	for (unsigned int i = 0; i<str.size(); i++)
	{
		// replace all non-ASCII characters
		if(str[i] < 32 || str[i] > 125)
			str[i] = '?';
	}
	return str;
}

// converts a string to a wstring 
std::wstring widen(const std::string& mbs) 
{ 
	std::vector<wchar_t> wcs(mbs.length()); 
	mbstowcs(&wcs[0], mbs.c_str(), mbs.length()); 
	return std::wstring(wcs.begin(), wcs.end()); 
}

UTFString tryConvertUTF(const char *buffer)
{
	try
	{
		UTFString s = UTFString(buffer);
		if(s.empty())
			s = UTFString("(UTF conversion error 1)");
		return s;

	} catch(std::exception &e)
	{
		Logger::log(LOG_INFO, UTFString("UTF conversion error: ") + tryConvertUTF(e.what()));
		return UTFString("(UTF conversion error 2)");
	}
	return UTFString("(UTF conversion error 3)");
}

std::string UTF8BuffertoString(const char *buffer)
{
	UTFString u = tryConvertUTF(buffer);
	return UTF8toString(u);
}

std::string UTF8toString(UTFString &u)
{
	return narrow(u.asWStr_c_str());
}