//
//  utils.h
//  imgFinderServer
//
//  Created by LuDong on 2018/8/20.
//  Copyright © 2018年 LuDong. All rights reserved.
//

#ifndef utils_h
#define utils_h
#include <dirent.h>
#include <list>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <stdio.h>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <sys/time.h>
#include "cJSON.h"

using namespace std;

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

static inline bool is_base64(unsigned char c) {
	return (isalnum(c) || (c == '+') || (c == '/'));
}

class timer
{
public:
	timer() {gettimeofday(&time, NULL); };
	~timer() {};
	/**
	* Restart the timer.
	*/
	void restart()
	{
		gettimeofday(&time, NULL);
	}
	/**
	* Measures elapsed time.
	*
	* @return The elapsed time
	*/
	unsigned long elapsed_us()
	{
		struct timeval end;
		gettimeofday(&end, NULL);
		unsigned long diff = 1000000 * (end.tv_sec - time.tv_sec) + end.tv_usec - time.tv_usec;
		return diff;
	}
	double elapsed_s()
	{
		struct timeval end;
		gettimeofday(&end, NULL);
		double diff = (1000000 * (end.tv_sec - time.tv_sec) + end.tv_usec - time.tv_usec)/1000000.0;
		return diff;
	}
private:
	struct timeval time;
};

int startsWith(string s, string sub);

int endsWith(string s, string sub);

void readDirectory(const char *directoryName, std::vector<std::string>& filenames, int searchFolder);

bool exist_file(const std::string& name);

long getFileSize(char *filePath);

int loadIndexFile(string filename, /*string ptCount, vector<int> &ptsCounts, */vector<int> &idx2Imgs, vector<unsigned short> &xPts, vector<unsigned short> &yPts, int &maxImageIdx, unordered_map<int, int> &imgKptCounts);

int loadIndexFile(string filename, int &kptCount);

void writeIndexFile(string filename, /*string ptCount, vector<int> ptsCounts, */vector<int> idx2Imgs, vector<unsigned short> xPts, vector<unsigned short> yPts, int maxImageIdx);

int appendIndexFile(string filename, vector<int> idx2Imgs, vector<unsigned short> xPts, vector<unsigned short> yPts, int &targetImgId);

vector<string> splitString(const string &str, const string &pattern);

int base64_decode(std::string const& encoded_string, unsigned char *ret);

char *json_loader(const char *path);

#endif /* utils_h */
