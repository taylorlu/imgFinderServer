//
//  utils.c
//  imgFinderServer
//
//  Created by LuDong on 2018/8/20.
//  Copyright © 2018年 LuDong. All rights reserved.
//

#include "utils.h"

int startsWith(string s, string sub) {
	return s.find(sub) == 0 ? 1 : 0;
}

int endsWith(string s, string sub) {
	return s.rfind(sub) == (s.length() - sub.length()) ? 1 : 0;
}

void readDirectory(const char *directoryName, std::vector<std::string>& filenames, int searchFolder) {

	filenames.clear();
	struct dirent *dirp;
	DIR* dir = opendir(directoryName);

	while ((dirp = readdir(dir)) != nullptr) {
		if (dirp->d_name[0] != '.') {
			if (dirp->d_type == DT_REG && !searchFolder) {
				// 文件
				std::string directoryStr(directoryName);
				std::string nameStr(dirp->d_name);
				filenames.push_back(directoryStr + "/" + nameStr);
				//printf("file: %s\n", dirp->d_name);
			}
			else if (dirp->d_type == DT_DIR && searchFolder) {
				// 文件夹
				std::string directoryStr(directoryName);
				std::string nameStr(dirp->d_name);
				filenames.push_back(directoryStr + "/" + nameStr);
				//printf("folder: %s\n", dirp->d_name);
			}
		}
	}
	std::sort(filenames.begin(), filenames.end());
	closedir(dir);
}

bool exist_file(const std::string& name) {
	ifstream f(name.c_str());
	return f.good();
}

long getFileSize(char *filePath) {

	FILE *pFile = fopen(filePath, "rb");;
	fseek(pFile, 0, SEEK_END);
	long n = ftell(pFile);
	fclose(pFile);
	return n;
}

void writeIndexFile(string filename, vector<int> idx2Imgs, vector<unsigned short> xPts, vector<unsigned short> yPts, int maxImageIdx) {

	ofstream out(filename, std::ios::binary);
	unsigned count = unsigned(idx2Imgs.size());
	out.write((char *)&count, sizeof(unsigned));
	out.write((char *)&maxImageIdx, sizeof(int));
	for (int i = 0; i < count; ++i) {
		int target = idx2Imgs[i];
		out.write((char *)&target, sizeof(int));
		short ptx = xPts[i];
		out.write((char *)&ptx, sizeof(short));
		short pty = yPts[i];
		out.write((char *)&pty, sizeof(short));
	}
	out.close();

}

int appendIndexFile(string filename, vector<int> idx2Imgs, vector<unsigned short> xPts, vector<unsigned short> yPts, int &targetImgId) {

	if (!exist_file(filename)) {
		return 1;
	}
	fstream inout(filename, std::ios::binary | std::ios::in | std::ios::out);
	unsigned count;
	int maxImageIdx;
	inout.read((char *)&count, sizeof(unsigned));
	inout.read((char *)&maxImageIdx, sizeof(int));
	count += idx2Imgs.size();

	maxImageIdx += 1;
	targetImgId = maxImageIdx;
	inout.seekp(0, ios::beg);
	inout.write((char *)&count, sizeof(unsigned));
	inout.write((char *)&maxImageIdx, sizeof(int));

	inout.seekp(0, ios::end);
	for (int i = 0; i < idx2Imgs.size(); ++i) {
		int target = maxImageIdx;
		inout.write((char *)&target, sizeof(int));
		short ptx = xPts[i];
		inout.write((char *)&ptx, sizeof(short));
		short pty = yPts[i];
		inout.write((char *)&pty, sizeof(short));
	}
	inout.close();
	return 0;
}

int loadIndexFile(string filename, vector<int> &idx2Imgs, vector<unsigned short> &xPts, vector<unsigned short> &yPts, int &maxImageIdx, unordered_map<int, int> &imgKptCounts) {

	if (!exist_file(filename)) {
		return 1;
	}
	std::ifstream in(filename, std::ios::binary);
	unsigned count;
	in.read((char *)&count, sizeof(unsigned));
	in.read((char *)&maxImageIdx, sizeof(int));

	for (int i = 0; i < count; ++i) {
		int target;
		in.read((char *)&target, sizeof(int));
		imgKptCounts[target]++;

		idx2Imgs.push_back(target);
		short ptx;
		in.read((char *)&ptx, sizeof(short));
		xPts.push_back(ptx);
		short pty;
		in.read((char *)&pty, sizeof(short));
		yPts.push_back(pty);
	}
	in.close();
	return 0;
}

int loadIndexFile(string filename, int &kptCount) {

	if (!exist_file(filename)) {
		return 1;
	}
	std::ifstream in(filename, std::ios::binary);
	in.read((char *)&kptCount, sizeof(unsigned));
	in.close();
	return 0;
}

vector<string> splitString(const std::string& str, const std::string& delimiter = " ") {

	size_t pos = 0;
	std::string token;
	vector<string> resultVec;
	std::string s = str;
	while ((pos = s.find(delimiter)) != std::string::npos) {
		token = s.substr(0, pos);
		resultVec.push_back(token);
		s.erase(0, pos + delimiter.length());
	}
	if (s.length()>0) {
		resultVec.push_back(s);
	}
	return resultVec;
}

int base64_decode(std::string const& encoded_string, unsigned char *ret) {
	int in_len = encoded_string.size();
	int i = 0;
	int j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	int cc = 0;

	while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
		char_array_4[i++] = encoded_string[in_]; in_++;
		if (i == 4) {
			for (i = 0; i <4; i++)
				char_array_4[i] = base64_chars.find(char_array_4[i]);

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3); i++) {
				*(ret + cc) = char_array_3[i];
				cc++;
			}

			i = 0;
		}
	}

	if (i) {
		for (j = i; j <4; j++)
			char_array_4[j] = 0;

		for (j = 0; j <4; j++)
			char_array_4[j] = base64_chars.find(char_array_4[j]);

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1); j++) {
			*(ret + cc) = char_array_3[j];
			cc++;
		}
	}

	return cc;
}

char *json_loader(const char *path) {

	FILE *f;
	long len;
	char *content;
	f = fopen(path, "rb");
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	content = (char*)malloc(len + 1);
	fread(content, 1, len, f);
	fclose(f);
	return content;
}