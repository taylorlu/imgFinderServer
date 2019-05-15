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

using namespace std;

int startsWith(string s, string sub);

int endsWith(string s, string sub);

void readDirectory(const char *directoryName, std::vector<std::string>& filenames, int searchFolder);

bool exist_file(const std::string& name);

long getFileSize(char *filePath);

int loadIndexFile(string filename, /*string ptCount, vector<int> &ptsCounts, */vector<int> &idx2Imgs, vector<unsigned short> &xPts, vector<unsigned short> &yPts, int &maxImageIdx, unordered_map<int, int> &imgKptCounts);

int loadIndexFile(string filename, int &kptCount);

void writeIndexFile(string filename, /*string ptCount, vector<int> ptsCounts, */vector<int> idx2Imgs, vector<unsigned short> xPts, vector<unsigned short> yPts, int maxImageIdx);

int appendIndexFile(string filename, vector<int> idx2Imgs, vector<unsigned short> xPts, vector<unsigned short> yPts, int &targetImgId);

void writeImgPaths(string filename, vector<string> imgPaths);

void loadImgPaths(string filename, vector<string> &imgPaths);

vector<string> splitString(const string &str, const string &pattern);

int base64_decode(std::string const& encoded_string, unsigned char *ret);

char *json_loader(const char *path);

#endif /* utils_h */
